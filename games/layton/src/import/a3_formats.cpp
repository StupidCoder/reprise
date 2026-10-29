// Readers for the third adventure's containers. See a3_formats.hpp.
#include "import/a3_formats.hpp"
#include <algorithm>

namespace picarat::import::adventure3 {

namespace {

std::span<const std::uint8_t> region(std::span<const std::uint8_t> data, std::size_t offset,
                                     std::size_t size, const char* what) {
    check(offset <= data.size() && size <= data.size() - offset, what);
    return data.subspan(offset, size);
}

bool tagIs(std::span<const std::uint8_t> data, const char (&tag)[5]) {
    return data.size() >= 4 && std::equal(tag, tag + 4, data.begin());
}

} // namespace

std::vector<Lpc2Member> lpc2Members(std::span<const std::uint8_t> data) {
    check(tagIs(data, "LPC2"), "not an LPC2 archive");
    ByteReader r(data, 4);
    const std::uint32_t count = r.u32(), dataOffset = r.u32(), total = r.u32();
    const std::uint32_t table = r.u32(), names = r.u32(), dataAgain = r.u32();
    check(total == data.size() && table == 0x20 && dataAgain == dataOffset,
          "inconsistent LPC2 header");
    check(count <= (data.size() - table) / 12 && names <= dataOffset && dataOffset <= data.size(),
          "LPC2 tables outside the archive");
    const auto pool = data.subspan(names, dataOffset - names);
    const auto content = data.subspan(dataOffset);
    std::vector<Lpc2Member> members;
    for (ByteReader entry(data, table); members.size() < count;) {
        const std::uint32_t name = entry.u32(), offset = entry.u32(), size = entry.u32();
        check(name < pool.size(), "LPC2 name outside the name pool");
        const auto end = std::find(pool.begin() + name, pool.end(), 0);
        check(end != pool.end(), "unterminated LPC2 member name");
        Lpc2Member member{std::string(pool.begin() + name, end),
                          region(content, offset, size, "LPC2 member outside the archive")};
        check(std::none_of(members.begin(), members.end(),
                           [&](const Lpc2Member& m) { return m.name == member.name; }),
              "repeated LPC2 member name");
        safeName(member.name);
        members.push_back(std::move(member));
    }
    return members;
}

std::string Lscr::text(const LscrOperand& operand) const {
    check(operand.type == 2 && operand.value < strings.size(), "not a string operand");
    const auto begin = strings.begin() + operand.value;
    return std::string(begin, std::find(begin, strings.end(), 0));
}

Lscr parseLscr(std::span<const std::uint8_t> data) {
    check(tagIs(data, "LSCR"), "not an LSCR script");
    ByteReader r(data, 4);
    const std::uint16_t count = r.u16(), headerSize = r.u16();
    const std::uint32_t operandOffset = r.u32(), stringOffset = r.u32();
    check(headerSize == 16 && operandOffset == 16u + 8u * count && operandOffset <= stringOffset &&
              stringOffset <= data.size(),
          "inconsistent LSCR header");
    const std::size_t operandCount = (stringOffset - operandOffset) / 5;
    Lscr script;
    script.commands.reserve(count);
    for (ByteReader command(data, headerSize); script.commands.size() < count;) {
        LscrCommand c;
        c.opcode = command.u16();
        const std::uint16_t arguments = command.u16();
        const std::uint32_t first = command.u32();
        check(first <= operandCount && arguments <= operandCount - first,
              "LSCR operands outside the operand table");
        for (ByteReader operand(data, operandOffset + 5u * first); c.operands.size() < arguments;) {
            LscrOperand o;
            o.type = operand.u8();
            o.value = operand.u32();
            check(o.type <= 4, "unknown LSCR operand type");
            c.operands.push_back(o);
        }
        script.commands.push_back(std::move(c));
    }
    const auto pool = data.subspan(stringOffset);
    script.strings.assign(pool.begin(), pool.end());
    for (const LscrCommand& c : script.commands) {
        for (const LscrOperand& o : c.operands) {
            if (o.type == 2) {
                check(o.value < pool.size() &&
                          std::find(pool.begin() + o.value, pool.end(), 0) != pool.end(),
                      "LSCR string outside the string pool");
            }
        }
    }
    return script;
}

Limg parseLimg(std::span<const std::uint8_t> data) {
    check(tagIs(data, "LIMG") && data.size() >= 0x30, "not an LIMG picture");
    ByteReader r(data, 4);
    const std::uint32_t paletteOffset = r.u32();
    std::uint16_t offsets[4], counts[4];
    for (int i = 0; i < 4; ++i) { // parts, pieces, map, tiles
        offsets[i] = r.u16();
        counts[i] = r.u16();
    }
    Limg image;
    image.format = r.u16();
    const std::uint16_t colours = r.u16();
    image.width = r.u16();
    image.height = r.u16();
    image.areaWidth = r.u16();
    image.areaHeight = r.u16();
    check(r.u32() == 0, "unexpected LIMG header field");
    image.areaShiftX = r.u8();
    image.areaShiftY = r.u8();
    check(image.format <= 2 && colours >= 1 && colours <= (image.format == 0 ? 16 : 256),
          "unknown LIMG format");
    check(image.width >= 1 && image.height >= 1 && image.areaWidth >= image.width &&
              image.areaHeight >= image.height && (8u << image.areaShiftX) >= image.areaWidth &&
              (8u << image.areaShiftY) >= image.areaHeight,
          "inconsistent LIMG size");

    ByteReader palette(region(data, paletteOffset, colours * 2u, "LIMG palette outside data"));
    for (unsigned i = 0; i < colours; ++i) {
        image.palette.push_back(palette.u16() & 0x7fff);
    }
    ByteReader parts(region(data, offsets[0], counts[0] * 8u, "LIMG parts outside data"));
    for (unsigned i = 0; i < counts[0]; ++i) {
        LimgPart p;
        p.x = parts.u8();
        p.y = parts.u8();
        p.width = parts.u8();
        p.height = parts.u8();
        p.firstPiece = parts.u16();
        p.pieceCount = parts.u16();
        check(p.firstPiece <= counts[1] && p.pieceCount <= counts[1] - p.firstPiece,
              "LIMG part refers to missing pieces");
        image.parts.push_back(p);
    }
    ByteReader pieces(region(data, offsets[1], counts[1] * 3u, "LIMG pieces outside data"));
    for (unsigned i = 0; i < counts[1]; ++i) {
        LimgPiece p;
        p.shape = pieces.u8();
        p.x = pieces.u8();
        p.y = pieces.u8();
        check(p.shape < 12, "unknown LIMG piece shape");
        image.pieces.push_back(p);
    }
    const unsigned columns = (image.width + 7) / 8, rows = (image.height + 7) / 8;
    check(counts[2] == columns * rows, "LIMG tile map does not match the picture");
    for (const LimgPart& p : image.parts) {
        check(p.x + p.width <= columns && p.y + p.height <= rows, "LIMG part outside the map");
    }
    ByteReader map(region(data, offsets[2], counts[2] * 2u, "LIMG map outside data"));
    for (unsigned i = 0; i < counts[2]; ++i) {
        const std::uint16_t tile = map.u16();
        check(tile == 0xffff || tile < counts[3], "LIMG map refers to a missing tile");
        image.map.push_back(tile);
    }
    image.tileCount = counts[3];
    const std::size_t tileBytes = image.format == 0 ? 32 : 64;
    image.tiles = region(data, offsets[3], counts[3] * tileBytes, "LIMG tiles outside data");
    check(offsets[3] + counts[3] * tileBytes == data.size(), "LIMG has trailing data");
    // Every colour a tile uses must exist (A3I5 uses the low five bits).
    for (std::uint8_t byte : image.tiles) {
        const unsigned high = image.format == 0 ? byte >> 4 : 0;
        const unsigned low = image.format == 0 ? byte & 15 : image.format == 2 ? byte & 31 : byte;
        check(low < colours && high < colours, "LIMG pixel outside the palette");
    }
    return image;
}

Limg::Pixel Limg::pixel(unsigned x, unsigned y) const {
    check(x < width && y < height, "pixel outside the picture");
    const std::uint16_t tile = map[(y / 8) * ((width + 7) / 8) + x / 8];
    if (tile == 0xffff) {
        return {};
    }
    const unsigned inside = (y % 8) * 8 + x % 8;
    if (format == 0) {
        // Two pixels per byte, the left one in the low nibble.
        const std::uint8_t byte = tiles[tile * 32u + inside / 2];
        const std::uint8_t index = (inside & 1) ? byte >> 4 : byte & 15;
        return {index, std::uint8_t(index ? 31 : 0)};
    }
    const std::uint8_t byte = tiles[tile * 64u + inside];
    if (format == 2) {
        // A3I5: alpha 0..7 scaled to the 3D engine's 0..31 (a * 4 + a / 2).
        const unsigned alpha = byte >> 5;
        return {std::uint8_t(byte & 31), std::uint8_t(alpha * 4 + alpha / 2)};
    }
    return {byte, std::uint8_t(byte ? 31 : 0)};
}

} // namespace picarat::import::adventure3
