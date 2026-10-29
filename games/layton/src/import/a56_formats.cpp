// Readers for the fifth and sixth adventures' containers. See
// a56_formats.hpp.
#include "import/a56_formats.hpp"
#include "import/a4_formats.hpp"
#include "import/asset_codecs.hpp"
#include <algorithm>
#include <map>
#include <zlib.h>

namespace picarat::import::adventure56 {

namespace {

bool tagIs(std::span<const std::uint8_t> data, const char (&tag)[5]) {
    return data.size() >= 4 && std::equal(tag, tag + 4, data.begin());
}

std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t at) {
    return ByteReader(d, at).u32();
}

std::uint16_t u16(std::span<const std::uint8_t> d, std::size_t at) {
    return ByteReader(d, at).u16();
}

// The NUL-terminated name at `offset` of a name list.
std::string nameAt(std::span<const std::uint8_t> names, std::size_t offset) {
    check(offset < names.size(), "name outside the name list");
    const auto begin = names.begin() + std::ptrdiff_t(offset);
    const auto end = std::find(begin, names.end(), 0);
    check(end != names.end(), "unterminated name");
    return std::string(begin, end);
}

// Bytes outside printable ASCII spelled %XX, as the cartridge readers do.
std::string spelled(const std::string& raw) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : raw) {
        if (c >= 32 && c < 127 && c != '%') {
            out += char(c);
        } else {
            out += {'%', kHex[c >> 4], kHex[c & 15]};
        }
    }
    return out;
}

// A method-word stream between two offsets of `data`, expanded.
Bytes stream(std::span<const std::uint8_t> data, std::size_t begin, std::size_t end) {
    check(begin <= end && end <= data.size() && end - begin >= 4, "stream outside the file");
    return decompressMethodWord(data.subspan(begin, end - begin));
}

void checkText(const std::vector<Operand>& operands, const Bytes& strings) {
    for (const Operand& o : operands) {
        if (o.type & 0x10) {
            nameAt(strings, o.value);
        }
    }
}

Operand operandAt(const Bytes& table, std::size_t i) {
    ByteReader r(table, i * 8);
    Operand o;
    o.type = r.u32();
    o.value = r.u32();
    check(o.type == 1 || o.type == 2 || o.type == 3 || o.type == 4 || o.type == 0x18,
          "unknown operand type");
    return o;
}

// Bits per pixel of the IMGC formats.
unsigned formatBits(unsigned format) {
    switch (format) {
    case 0:
        return 32;
    case 1:
    case 2:
    case 4:
        return 16;
    case 3:
        return 24;
    case 12:
    case 28:
        return 8;
    case 27:
        return 4;
    default:
        throw FormatError("unknown IMGC format");
    }
}

} // namespace

std::uint32_t nameHash(std::string_view name) {
    return std::uint32_t(crc32(0, reinterpret_cast<const Bytef*>(name.data()), uInt(name.size())));
}

std::uint32_t archiveTablesSize(std::span<const std::uint8_t> header) {
    check((tagIs(header, "XFSA") || tagIs(header, "ARC0")) && header.size() >= 0x24,
          "not an archive");
    return u32(header, 0x14);
}

std::vector<ArchiveFile> archiveFiles(std::span<const std::uint8_t> tables,
                                      std::uint64_t archiveSize) {
    const bool arc0 = tagIs(tables, "ARC0");
    check((arc0 || tagIs(tables, "XFSA")) && tables.size() >= (arc0 ? 0x48u : 0x24u),
          "not an archive");
    ByteReader r(tables, 4);
    std::uint32_t offsets[5]; // directories, hash list, files, names, data
    for (auto& offset : offsets) {
        offset = r.u32();
    }
    const unsigned directoryCount = r.u16(), hashCount = r.u16();
    const std::uint32_t fileCount = r.u32();
    check(offsets[0] == (arc0 ? 0x48u : 0x24u) && std::is_sorted(offsets, offsets + 5) &&
              offsets[4] == tables.size() && offsets[4] <= archiveSize && fileCount <= 0xffff,
          "inconsistent archive header");
    auto table = [&](int i) {
        return stream(tables, offsets[i], offsets[i + 1]);
    };
    const Bytes directories = table(0), hashes = table(1), files = table(2), names = table(3);
    const std::size_t dirSize = arc0 ? 20 : 16, fileSize = arc0 ? 16 : 12;
    check(directories.size() == directoryCount * dirSize && hashes.size() == hashCount * 4u &&
              files.size() == fileCount * fileSize,
          "archive table sizes do not match the header");

    std::vector<ArchiveFile> out;
    std::vector<bool> used(fileCount, false);
    for (unsigned i = 0; i < directoryCount; ++i) {
        ByteReader d(directories, i * dirSize);
        const std::uint32_t hash = d.u32();
        std::uint32_t first, count, fileNames, name;
        if (arc0) {
            d.u16();
            d.u16();
            first = d.u16();
            count = d.u16();
            fileNames = d.u32();
            name = d.u32();
        } else {
            const std::uint32_t w1 = d.u32(), w2 = d.u32(), w3 = d.u32();
            count = w1 & 0x3fff;
            fileNames = w1 >> 14;
            first = w2 & 0xffff;
            name = w3 >> 14;
        }
        const std::string directory = nameAt(names, name);
        // The root directory's entry carries no hash of its empty name.
        check(directory.empty() || nameHash(directory) == hash, "archive directory hash mismatch");
        check(first <= fileCount && count <= fileCount - first,
              "archive directory outside the files");
        for (std::uint32_t f = first; f < first + count; ++f) {
            check(!used[f], "archive file in two directories");
            used[f] = true;
            ByteReader e(files, f * fileSize);
            const std::uint32_t fileHash = e.u32();
            std::uint32_t nameOffset, size;
            std::uint64_t offset;
            if (arc0) {
                nameOffset = e.u32();
                offset = e.u32();
                size = e.u32();
            } else {
                const std::uint32_t w1 = e.u32(), w2 = e.u32();
                offset = std::uint64_t(w1 & 0x3ffffff) * 16;
                size = w2 & 0x7fffff;
                nameOffset = (w2 >> 23) | (w1 >> 26) << 9;
            }
            ArchiveFile file{directory + spelled(nameAt(names, fileNames + nameOffset)),
                             offsets[4] + offset, size};
            check(nameHash(nameAt(names, fileNames + nameOffset)) == fileHash,
                  "archive file hash mismatch");
            check(file.offset <= archiveSize && size <= archiveSize - file.offset,
                  "archive file outside the archive");
            safeName(file.path);
            out.push_back(std::move(file));
        }
    }
    check(std::all_of(used.begin(), used.end(), [](bool u) { return u; }),
          "archive files without a directory");
    return out;
}

std::vector<PackMember> packMembers(std::span<const std::uint8_t> data) {
    const bool xfsp = tagIs(data, "XFSP");
    check((xfsp || tagIs(data, "XPCK")) && data.size() >= 0x14, "not a pack");
    const unsigned count = data[4] | (data[5] & 15u) << 8;
    const std::size_t table = u16(data, 6) * 4u, names = u16(data, 8) * 4u,
                      content = u16(data, 10) * 4u;
    const std::size_t record = xfsp ? 10 : 12;
    check(table >= 0x14 && table + count * record <= names && names <= content &&
              content <= data.size(),
          "inconsistent pack header");
    const Bytes nameList = stream(data, names, content);
    std::vector<PackMember> members;
    for (unsigned i = 0; i < count; ++i) {
        ByteReader e(data, table + i * record);
        const std::uint32_t hash = xfsp ? e.u16() : e.u32();
        const std::string name = nameAt(nameList, e.u16());
        const std::uint32_t offset = e.u16(), size = e.u16(), high = e.u16();
        check(xfsp ? adventure4::nameHash(name) == hash : nameHash(name) == hash,
              "pack member hash mismatch");
        const std::size_t begin = content + (offset | (high & 0xff) << 16) * 4u;
        const std::size_t length = size | (high >> 8) << 16;
        check(begin <= data.size() && length <= data.size() - begin,
              "pack member outside the pack");
        PackMember member{spelled(name), data.subspan(begin, length)};
        safeName(member.name);
        check(std::none_of(members.begin(), members.end(),
                           [&](const PackMember& m) { return m.name == member.name; }),
              "repeated pack member");
        members.push_back(std::move(member));
    }
    return members;
}

Picture parseImgc(std::span<const std::uint8_t> data) {
    check(tagIs(data, "IMGC") && data.size() >= 0x48 && data[4] == '0' && data[5] == '0',
          "not an IMGC picture");
    Picture picture;
    picture.format = data[0x0a];
    picture.levels = data[0x0c];
    picture.bitsPerPixel = data[0x0d];
    picture.width = u16(data, 0x10);
    picture.height = u16(data, 0x12);
    check(formatBits(picture.format) == picture.bitsPerPixel && picture.levels >= 1 &&
              picture.width >= 1 && picture.height >= 1,
          "inconsistent IMGC header");
    const std::size_t mapSize = u32(data, 0x38), tileSize = u32(data, 0x3c);
    check(mapSize <= data.size() - 0x48 && tileSize <= data.size() - 0x48 - mapSize,
          "IMGC streams outside the file");
    const Bytes map = stream(data, 0x48, 0x48 + mapSize);
    picture.tiles = stream(data, 0x48 + mapSize, 0x48 + mapSize + tileSize);
    check(map.size() % 2 == 0, "odd IMGC tile map");
    unsigned highest = 0;
    bool any = false;
    for (std::size_t i = 0; i < map.size(); i += 2) {
        const std::uint16_t tile = std::uint16_t(map[i] | map[i + 1] << 8);
        picture.map.push_back(tile);
        if (tile != 0xffff) {
            highest = std::max<unsigned>(highest, tile);
            any = true;
        }
    }
    picture.tileCount = any ? highest + 1 : 0;
    const std::size_t cells = ((picture.width + 7) / 8) * ((picture.height + 7) / 8);
    check(picture.map.size() >= cells, "IMGC tile map smaller than the picture");
    check(picture.tiles.size() == picture.tileCount * 8u * picture.bitsPerPixel,
          "IMGC tiles do not match the map");
    return picture;
}

std::array<std::uint8_t, 4> Picture::rgba(unsigned x, unsigned y) const {
    check(x < width && y < height && (format == 0 || format == 3), "pixel not decodable here");
    const std::uint16_t tile = map[(y / 8) * ((width + 7) / 8) + x / 8];
    if (tile == 0xffff) {
        return {0, 0, 0, 0};
    }
    // Z order inside the tile: y on bits 0, 2, 4 and x on bits 1, 3, 5.
    unsigned inside = 0;
    for (unsigned bit = 0; bit < 3; ++bit) {
        inside |= ((y % 8 >> bit) & 1) << (2 * bit) | ((x % 8 >> bit) & 1) << (2 * bit + 1);
    }
    const std::size_t bytes = bitsPerPixel / 8, at = (tile * 64u + inside) * bytes;
    if (format == 0) {
        return {tiles[at + 3], tiles[at + 2], tiles[at + 1], tiles[at]};
    }
    return {tiles[at + 2], tiles[at + 1], tiles[at], 255};
}

Script parseXq32(std::span<const std::uint8_t> data) {
    check(tagIs(data, "XQ32") && data.size() >= 0x20, "not an XQ32 script");
    ByteReader r(data, 4);
    std::uint16_t h[10];
    for (auto& value : h) {
        value = r.u16();
    }
    const std::size_t offsets[5] = {h[1] * 4u, h[2] * 4u, h[4] * 4u, h[6] * 4u, h[9] * 4u};
    check(offsets[0] >= 0x20 && std::is_sorted(offsets, offsets + 5) && offsets[4] < data.size(),
          "inconsistent XQ32 header");
    const Bytes blocks = stream(data, offsets[0], offsets[1]),
                labels = stream(data, offsets[1], offsets[2]),
                commands = stream(data, offsets[2], offsets[3]),
                operands = stream(data, offsets[3], offsets[4]);
    Script script;
    script.strings = stream(data, offsets[4], data.size());
    check(blocks.size() == h[0] * 24u && labels.size() == h[3] * 12u &&
              commands.size() == h[5] * 12u && operands.size() == h[7] * 8u,
          "XQ32 table sizes do not match the header");
    script.values = h[8];
    for (unsigned i = 0; i < h[0]; ++i) {
        ScriptBlock block;
        block.name = nameAt(script.strings, u32(blocks, i * 24u));
        block.hash = u32(blocks, i * 24u + 4);
        check(nameHash(block.name) == block.hash, "XQ32 block hash mismatch");
        ByteReader b(blocks, i * 24u + 8);
        block.first = b.u16();
        block.end = b.u16();
        block.firstLabel = b.u16();
        block.labelCount = b.u16();
        block.temporaries = b.u16();
        block.locals = b.u16();
        block.arguments = b.u16();
        check(block.first <= block.end && block.end <= h[5] &&
                  block.firstLabel + block.labelCount <= h[3],
              "XQ32 block outside the tables");
        script.blocks.push_back(std::move(block));
    }
    for (unsigned i = 0; i < h[3]; ++i) {
        ScriptLabel label;
        label.name = nameAt(script.strings, u32(labels, i * 12u));
        label.hash = u32(labels, i * 12u + 4);
        label.command = u32(labels, i * 12u + 8);
        check(nameHash(label.name) == label.hash && label.command <= h[5], "XQ32 label invalid");
        script.labels.push_back(std::move(label));
    }
    for (unsigned i = 0; i < h[5]; ++i) {
        ByteReader c(commands, i * 12u);
        ScriptCommand command;
        command.first = c.u16();
        command.count = c.u8();
        const std::uint8_t loaded = c.u8();
        command.group = c.u16();
        command.opcode = c.u16();
        check(loaded == 0 && c.u32() == 0, "XQ32 command with run-time fields set");
        check(command.first + command.count <= h[7], "XQ32 operands outside the table");
        script.commands.push_back(command);
    }
    for (unsigned i = 0; i < h[7]; ++i) {
        script.operands.push_back(operandAt(operands, i));
    }
    checkText(script.operands, script.strings);
    return script;
}

std::string Script::text(std::uint32_t offset) const {
    return nameAt(strings, offset);
}

DataList parseXscr(std::span<const std::uint8_t> data) {
    check(tagIs(data, "XSCR") && data.size() >= 0x14, "not an XSCR list");
    const unsigned count = u16(data, 4);
    const std::size_t commandsAt = u16(data, 6) * 4u;
    const std::uint32_t operandCount = u32(data, 8);
    const std::size_t operandsAt = std::size_t(u32(data, 12)) * 4,
                      stringsAt = std::size_t(u32(data, 16)) * 4;
    check(commandsAt >= 0x14 && commandsAt <= operandsAt && operandsAt <= stringsAt &&
              stringsAt < data.size(),
          "inconsistent XSCR header");
    const Bytes commands = stream(data, commandsAt, operandsAt),
                operands = stream(data, operandsAt, stringsAt);
    DataList list;
    list.strings = stream(data, stringsAt, data.size());
    check(commands.size() == count * 8u && operands.size() == operandCount * 8ull,
          "XSCR table sizes do not match the header");
    for (unsigned i = 0; i < count; ++i) {
        ByteReader c(commands, i * 8u);
        DataCommand command;
        command.opcode = c.u16();
        command.count = c.u16();
        command.first = c.u16();
        check(c.u16() == 0 && command.first + command.count <= operandCount,
              "XSCR command invalid");
        list.commands.push_back(command);
    }
    for (std::uint32_t i = 0; i < operandCount; ++i) {
        list.operands.push_back(operandAt(operands, i));
    }
    checkText(list.operands, list.strings);
    return list;
}

} // namespace picarat::import::adventure56
