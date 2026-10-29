// Readers for the fourth adventure's containers. See a4_formats.hpp.
#include "import/a4_formats.hpp"
#include "import/asset_codecs.hpp"
#include <algorithm>
#include <array>
#include <map>

namespace picarat::import::adventure4 {

namespace {

std::span<const std::uint8_t> region(std::span<const std::uint8_t> data, std::size_t offset,
                                     std::size_t size, const char* what) {
    check(offset <= data.size() && size <= data.size() - offset, what);
    return data.subspan(offset, size);
}

bool tagIs(std::span<const std::uint8_t> data, const char (&tag)[5]) {
    return data.size() >= 4 && std::equal(tag, tag + 4, data.begin());
}

// The NUL-terminated names of a name list, `count` of them.
std::vector<std::string> nameList(std::span<const std::uint8_t> pool, std::size_t count,
                                  const char* what) {
    std::vector<std::string> names;
    auto at = pool.begin();
    while (names.size() < count) {
        const auto end = std::find(at, pool.end(), 0);
        check(end != pool.end(), what);
        names.emplace_back(at, end);
        at = end + 1;
    }
    return names;
}

// A NUL-terminated string at `offset` of `file`, which must lie at or after
// `poolStart`.
std::string stringAt(std::span<const std::uint8_t> file, std::size_t poolStart,
                     std::uint32_t offset) {
    check(offset >= poolStart && offset < file.size(), "string outside the string pool");
    const auto begin = file.begin() + offset;
    const auto end = std::find(begin, file.end(), 0);
    check(end != file.end(), "unterminated string");
    return std::string(begin, end);
}

// Offset and size of a GFSA file or GFSP member: offset / 4 and size in
// halfwords 1 and 2, their high bits in halfword 3 (GFSA: 12 offset bits,
// then 4 size bits; GFSP: 2 and 2).
struct Placement {
    std::uint32_t offset, size;
};

Placement placement(std::uint16_t offset, std::uint16_t size, std::uint16_t high, bool archive) {
    if (archive) {
        return {std::uint32_t(offset) << 2 | std::uint32_t(high & 0xfff) << 18,
                std::uint32_t(size) | std::uint32_t(high >> 12) << 16};
    }
    return {std::uint32_t(offset) << 2 | std::uint32_t(high & 3) << 18,
            std::uint32_t(size) | std::uint32_t((high & 15) >> 2) << 16};
}

Operand readOperand(ByteReader& r) {
    Operand o;
    o.type = r.u8();
    o.value = r.u32();
    const unsigned kind = o.type & 15;
    check((o.type & 0xe0) == 0 && kind >= 1 && kind <= 9 && (kind < 5 || kind > 7) &&
              o.isText() == (kind >= 8),
          "unknown operand type");
    return o;
}

// The GTXT formats: colours and bits per pixel (ov6 020b46aa, 020b4698).
constexpr unsigned kFormatBits[6] = {16, 2, 4, 8, 8, 8};

} // namespace

std::uint16_t nameHash(std::string_view name) {
    std::uint16_t crc = 0xffff;
    for (const unsigned char c : name) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? std::uint16_t((crc >> 1) ^ 0x8408) : std::uint16_t(crc >> 1);
        }
    }
    return std::uint16_t(crc ^ 0xffff);
}

std::vector<ArchiveFile> gfsaFiles(std::span<const std::uint8_t> data) {
    check(tagIs(data, "GFSA") && data.size() >= 0x28, "not a GFSA archive");
    ByteReader r(data, 4);
    std::uint32_t offsets[5];
    for (auto& offset : offsets) { // directories, files, run-time table, names, data
        offset = r.u32();
    }
    const std::uint32_t directoryCount = r.u32(), fileCount = r.u32();
    const std::uint32_t tableSize = r.u32(), nameSize = r.u32();
    check(offsets[0] == 0x28 && std::is_sorted(std::begin(offsets), std::end(offsets)) &&
              offsets[4] <= data.size(),
          "inconsistent GFSA header");
    check(directoryCount <= 0xffff && fileCount <= 0xffff, "GFSA table too large");
    auto table = [&](int i) {
        return decompressMethodWord(data.subspan(offsets[i], offsets[i + 1] - offsets[i]));
    };
    const Bytes directories = table(0), files = table(1), runtime = table(2), names = table(3);
    // The header's sizes count each table rounded up to four bytes, as the
    // game allocates them.
    auto aligned = [](std::size_t size) {
        return (size + 3) & ~std::size_t(3);
    };
    check(directories.size() == directoryCount * 8u && files.size() == fileCount * 8u &&
              runtime.size() == (directoryCount + fileCount) * 4u &&
              tableSize == aligned(directories.size()) + aligned(files.size()) &&
              nameSize == aligned(runtime.size()) + aligned(names.size()),
          "GFSA table sizes do not match the header");
    const auto content = data.subspan(offsets[4]);
    const auto list = nameList(names, directoryCount + fileCount, "GFSA name list too short");

    std::map<std::uint16_t, std::pair<unsigned, unsigned>> byHash; // count, first file
    for (ByteReader d(directories); d.remaining() > 0;) {
        const std::uint16_t hash = d.u16(), count = d.u16(), first = d.u16();
        d.u16();
        check(first <= fileCount && count <= fileCount - first, "GFSA directory outside the files");
        check(byHash.emplace(hash, std::pair{count, first}).second, "repeated GFSA directory hash");
    }
    std::vector<ArchiveFile> out;
    std::vector<bool> used(fileCount, false);
    std::size_t next = directoryCount; // the current directory's first name
    for (std::size_t i = 0; i < directoryCount; ++i) {
        const std::string& directory = list[i];
        const auto found = byHash.find(nameHash(directory));
        check(found != byHash.end(), "GFSA directory name without an entry");
        const auto [count, first] = found->second;
        check(next + count <= list.size(), "GFSA name list too short");
        std::map<std::uint16_t, Placement> entries;
        for (unsigned f = first; f < first + count; ++f) {
            ByteReader e(files, f * 8u);
            const std::uint16_t hash = e.u16(), offset = e.u16(), size = e.u16(), high = e.u16();
            check(!used[f], "GFSA file in two directories");
            used[f] = true;
            check(entries.emplace(hash, placement(offset, size, high, true)).second,
                  "repeated GFSA file hash");
        }
        for (unsigned k = 0; k < count; ++k) {
            const std::string& name = list[next + k];
            const auto entry = entries.find(nameHash(name));
            check(entry != entries.end(), "GFSA file name without an entry");
            ArchiveFile file{directory + name, region(content, entry->second.offset,
                                                      entry->second.size, "GFSA file outside")};
            check(file.stored.size() >= 4, "GFSA file too short");
            safeName(file.path);
            out.push_back(std::move(file));
            entries.erase(entry);
        }
        next += count;
    }
    check(next == list.size() && std::all_of(used.begin(), used.end(), [](bool u) { return u; }),
          "GFSA files without a directory");
    return out;
}

std::vector<PackMember> gfspMembers(std::span<const std::uint8_t> data) {
    check(tagIs(data, "GFSP") && data.size() >= 0x14, "not a GFSP pack");
    ByteReader r(data, 4);
    const unsigned count = r.u8();
    r.u8();
    const std::size_t table = r.u16() * 4u, names = r.u16() * 4u, content = r.u16() * 4u;
    check(table >= 0x14 && table + count * 8u <= names && names <= content &&
              content <= data.size(),
          "inconsistent GFSP header");
    const auto list =
        nameList(data.subspan(names, content - names), count, "GFSP name list too short");
    std::map<std::uint16_t, Placement> entries;
    for (ByteReader e(data, table); entries.size() < count;) {
        const std::uint16_t hash = e.u16(), offset = e.u16(), size = e.u16(), high = e.u16();
        check(entries.emplace(hash, placement(offset, size, high, false)).second,
              "repeated GFSP member hash");
    }
    std::vector<PackMember> members;
    for (const std::string& name : list) {
        const auto entry = entries.find(nameHash(name));
        check(entry != entries.end(), "GFSP member name without an entry");
        safeName(name);
        members.push_back({name, region(data.subspan(content), entry->second.offset,
                                        entry->second.size, "GFSP member outside the pack")});
        entries.erase(entry);
    }
    return members;
}

std::string DataList::text(const Operand& operand) const {
    check(operand.isText(), "not a text operand");
    return stringAt(file, 0, operand.value);
}

DataList parseGsd1(std::span<const std::uint8_t> data) {
    check(tagIs(data, "GSD1") && data.size() >= 0x10, "not a GSD1 list");
    ByteReader r(data, 4);
    const std::uint16_t commandCount = r.u16();
    const std::size_t commands = r.u16() * 4u;
    const std::uint16_t operandCount = r.u16();
    const std::size_t operands = r.u16() * 4u, strings = r.u16() * 4u;
    check(r.u16() == 0 && commands >= 0x10 && commands + commandCount * 6u <= operands &&
              operands + operandCount * 5u <= strings && strings <= data.size(),
          "inconsistent GSD1 header");
    DataList list;
    list.file = data;
    for (ByteReader c(data, commands); list.commands.size() < commandCount;) {
        DataCommand command{c.u16(), c.u16(), c.u16()};
        check(command.first <= operandCount && command.count <= operandCount - command.first,
              "GSD1 operands outside the operand table");
        list.commands.push_back(command);
    }
    for (ByteReader o(data, operands); list.operands.size() < operandCount;) {
        list.operands.push_back(readOperand(o));
        if (list.operands.back().isText()) {
            stringAt(data, strings, list.operands.back().value);
        }
    }
    return list;
}

std::string Script::text(std::uint32_t offset) const {
    return stringAt(file, 0, offset);
}

Script parseGss1(std::span<const std::uint8_t> data) {
    check(tagIs(data, "GSS1") && data.size() >= 0x18, "not a GSS1 script");
    ByteReader r(data, 4);
    std::uint16_t h[10];
    for (auto& value : h) {
        value = r.u16();
    }
    const std::uint16_t blockCount = h[0], labelCount = h[3], commandCount = h[5],
                        operandCount = h[7];
    const std::size_t blocks = h[1] * 4u, labels = h[2] * 4u, commands = h[4] * 4u,
                      operands = h[6] * 4u, strings = h[9] * 4u;
    check(blocks >= 0x18 && blocks + blockCount * 20u <= labels &&
              labels + labelCount * 8u <= commands && commands + commandCount * 12u <= operands &&
              operands + operandCount * 5u <= strings && strings <= data.size(),
          "inconsistent GSS1 header");
    Script script;
    script.file = data;
    script.values = h[8];
    auto name = [&](std::uint32_t offset, std::uint16_t hash) {
        check(nameHash(stringAt(data, strings, offset)) == hash, "GSS1 name hash mismatch");
    };
    for (ByteReader b(data, blocks); script.blocks.size() < blockCount;) {
        ScriptBlock block;
        block.name = b.u32();
        block.hash = b.u16();
        block.firstCommand = b.u16();
        block.endCommand = b.u16();
        block.firstLabel = b.u16();
        block.labelCount = b.u16();
        block.unknown16 = b.u16();
        block.unknown32 = b.u32();
        name(block.name, block.hash);
        check(block.firstCommand <= block.endCommand && block.endCommand <= commandCount &&
                  block.firstLabel <= labelCount &&
                  block.labelCount <= labelCount - block.firstLabel,
              "GSS1 block outside the tables");
        check(script.blocks.empty() || script.blocks.back().hash < block.hash,
              "GSS1 blocks not sorted by hash");
        script.blocks.push_back(block);
    }
    // The blocks share the command table without overlapping.
    std::vector<std::pair<unsigned, unsigned>> spans;
    for (const ScriptBlock& block : script.blocks) {
        spans.emplace_back(block.firstCommand, block.endCommand);
    }
    std::sort(spans.begin(), spans.end());
    for (std::size_t i = 1; i < spans.size(); ++i) {
        check(spans[i - 1].second <= spans[i].first, "GSS1 blocks overlap");
    }
    for (ByteReader l(data, labels); script.labels.size() < labelCount;) {
        ScriptLabel label;
        label.name = l.u32();
        label.hash = l.u16();
        label.command = l.u16();
        name(label.name, label.hash);
        check(label.command <= commandCount, "GSS1 label outside the commands");
        script.labels.push_back(label);
    }
    for (ByteReader c(data, commands); script.commands.size() < commandCount;) {
        ScriptCommand command;
        command.first = c.u16();
        command.count = c.u8();
        const std::uint8_t loaded = c.u8(); // set by the loader
        command.group = c.u16();
        command.opcode = c.u16();
        const std::uint32_t handler = c.u32(); // set by the loader
        check(loaded == 0 && handler == 0, "GSS1 command with run-time fields set");
        check(command.first <= operandCount && command.count <= operandCount - command.first,
              "GSS1 operands outside the operand table");
        script.commands.push_back(command);
    }
    for (ByteReader o(data, operands); script.operands.size() < operandCount;) {
        script.operands.push_back(readOperand(o));
        if (script.operands.back().isText()) {
            stringAt(data, strings, script.operands.back().value);
        }
    }
    return script;
}

std::vector<std::uint16_t> parseGplt(std::span<const std::uint8_t> data) {
    check(tagIs(data, "GPLT") && data.size() >= 8, "not a GPLT palette");
    ByteReader r(data, 4);
    const std::uint16_t count = r.u16(), header = r.u16();
    check(header == 8 && data.size() == ((8u + 2u * count + 3u) & ~3u),
          "inconsistent GPLT palette");
    std::vector<std::uint16_t> colours;
    for (unsigned i = 0; i < count; ++i) {
        colours.push_back(r.u16());
    }
    return colours;
}

unsigned Picture::bitsPerPixel() const {
    return kFormatBits[format];
}

Picture parseGtxt(std::span<const std::uint8_t> data) {
    check(tagIs(data, "GTXT") && data.size() >= 0x24, "not a GTXT picture");
    ByteReader r(data, 4);
    Picture picture;
    const std::uint8_t format = r.u8();
    picture.format = format & 15;
    picture.flags = format >> 4;
    picture.colours = r.u8();
    picture.areaShiftX = r.u8();
    picture.areaShiftY = r.u8();
    picture.areaWidth = r.u16();
    picture.areaHeight = r.u16();
    picture.width = r.u16();
    picture.height = r.u16();
    std::uint16_t offsets[3], counts[3];
    for (int i = 0; i < 3; ++i) { // parts, pieces, map
        offsets[i] = r.u16();
        counts[i] = r.u16();
    }
    const std::uint32_t tileOffset = r.u32();
    picture.tileCount = r.u16();
    picture.mode = r.u16();
    check(picture.format < 6, "unknown GTXT format");
    check(picture.areaShiftX < 8 && picture.areaShiftY < 8 && picture.width >= 1 &&
              picture.height >= 1 && picture.width <= picture.areaWidth &&
              picture.height <= picture.areaHeight &&
              picture.areaWidth <= (8u << picture.areaShiftX) &&
              picture.areaHeight <= (8u << picture.areaShiftY),
          "inconsistent GTXT size");

    ByteReader parts(region(data, offsets[0], counts[0] * 8u, "GTXT parts outside data"));
    for (unsigned i = 0; i < counts[0]; ++i) {
        adventure3::LimgPart p;
        p.x = parts.u8();
        p.y = parts.u8();
        p.width = parts.u8();
        p.height = parts.u8();
        p.firstPiece = parts.u16();
        p.pieceCount = parts.u16();
        check(p.firstPiece <= counts[1] && p.pieceCount <= counts[1] - p.firstPiece,
              "GTXT part refers to missing pieces");
        picture.parts.push_back(p);
    }
    ByteReader pieces(region(data, offsets[1], counts[1] * 3u, "GTXT pieces outside data"));
    for (unsigned i = 0; i < counts[1]; ++i) {
        adventure3::LimgPiece p;
        p.shape = pieces.u8();
        p.x = pieces.u8();
        p.y = pieces.u8();
        check(p.shape < 12, "unknown GTXT piece shape");
        picture.pieces.push_back(p);
    }
    const unsigned columns = (picture.width + 7) / 8, rows = (picture.height + 7) / 8;
    check(counts[2] == columns * rows, "GTXT tile map does not match the picture");
    for (const auto& p : picture.parts) {
        check(p.x + p.width <= (8u << picture.areaShiftX) / 8 &&
                  p.y + p.height <= (8u << picture.areaShiftY) / 8,
              "GTXT part outside the area");
    }
    ByteReader map(region(data, offsets[2], counts[2] * 2u, "GTXT map outside data"));
    for (unsigned i = 0; i < counts[2]; ++i) {
        const std::uint16_t tile = map.u16();
        check(tile == 0xffff || tile < picture.tileCount, "GTXT map refers to a missing tile");
        picture.map.push_back(tile);
    }
    const std::size_t tileBytes = 8u * picture.bitsPerPixel();
    picture.tiles =
        region(data, tileOffset, picture.tileCount * tileBytes, "GTXT tiles outside data");
    check(tileOffset + picture.tileCount * tileBytes == data.size(), "GTXT has trailing data");
    return picture;
}

std::optional<std::uint16_t> Picture::pixel(unsigned x, unsigned y) const {
    check(x < width && y < height, "pixel outside the picture");
    const std::uint16_t tile = map[(y / 8) * ((width + 7) / 8) + x / 8];
    if (tile == 0xffff) {
        return std::nullopt;
    }
    const unsigned bits = bitsPerPixel(), inside = (y % 8) * 8 + x % 8;
    const std::size_t base = tile * 8u * bits;
    if (bits == 16) {
        return std::uint16_t(tiles[base + inside * 2] | tiles[base + inside * 2 + 1] << 8);
    }
    // Pixels fill each byte from its low bits up.
    const std::uint8_t byte = tiles[base + inside * bits / 8];
    return std::uint16_t((byte >> (inside * bits % 8)) & ((1u << bits) - 1));
}

GameFont parseGfnt(std::span<const std::uint8_t> data) {
    check(tagIs(data, "GFNT") && data.size() >= 0x18, "not a GFNT font");
    ByteReader r(data, 4);
    GameFont font;
    font.cellWidth = r.u8();
    font.cellHeight = r.u8();
    check(r.u16() == 0, "unexpected GFNT header field");
    const std::uint32_t table = r.u32(), tableSize = r.u32();
    const std::uint32_t bitmaps = r.u32(), bitmapSize = r.u32();
    check(table == 0x18 && tableSize % 6 == 0 && bitmaps == table + tableSize &&
              bitmaps <= data.size() && bitmapSize == data.size() - bitmaps,
          "inconsistent GFNT header");
    font.bitmaps = data.subspan(bitmaps);
    for (ByteReader g(data, table); font.glyphs.size() < tableSize / 6;) {
        FontGlyph glyph;
        glyph.code = g.u16();
        glyph.metrics = g.u16();
        // The metrics' top nibble holds bits 16..19 of the bitmap offset
        // (glyph drawing, ov6 0209afb8).
        glyph.offset = g.u16() | std::uint32_t(glyph.metrics >> 12) << 16;
        check(glyph.offset < font.bitmaps.size(), "GFNT glyph outside the bitmaps");
        check(font.glyphs.empty() || font.glyphs.back().code < glyph.code,
              "GFNT glyphs not sorted by code");
        font.glyphs.push_back(glyph);
    }
    return font;
}

} // namespace picarat::import::adventure4
