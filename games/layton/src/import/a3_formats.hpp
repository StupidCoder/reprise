#pragma once
// Bounds-checked readers for the resource formats of the third adventure.
// It replaced its predecessor's containers with new ones; all layouts here
// were derived from that ROM's files and checked against all of them:
//
//  * c-prefixed files (.cimg, .cani, .cbin, .cpck, .cdat, .csnd, .cfnt,
//    .cdcm) are one stream for the shared decompress() (expandAsset knows
//    them): a BIOS codec, or type 0 for bytes stored as they are.
//  * LPC2: a named archive (animations, sound pairs, script collections).
//  * LSCR: a list of commands with typed operands. The game uses it for
//    event scripts, data tables, text and animation descriptions alike.
//  * LIMG: a tiled picture with an optional list of parts (sprite cells).
#include "import/rom_image.hpp"
#include <string>
#include <vector>

namespace picarat::import::adventure3 {

struct Lpc2Member {
    std::string name;
    std::span<const std::uint8_t> data; // inside the archive passed in
};

// "LPC2", member count, data offset, total size, then the offsets of the
// member table (12-byte records: name offset, data offset, size), of the
// name pool and again of the data. Names are NUL-terminated ASCII.
std::vector<Lpc2Member> lpc2Members(std::span<const std::uint8_t> data);

struct LscrOperand {
    // 0 integer, 1 float (IEEE single), 2 string (offset into the string
    // pool), 3 and 4 integers whose role is not known yet.
    std::uint8_t type = 0;
    std::uint32_t value = 0;
};

struct LscrCommand {
    std::uint16_t opcode = 0;
    std::vector<LscrOperand> operands;
};

struct Lscr {
    std::vector<LscrCommand> commands;
    Bytes strings; // the string pool
    // A string operand's text, without its terminating NUL.
    std::string text(const LscrOperand& operand) const;
};

// "LSCR", u16 command count, u16 header size (16), the offsets of the
// operand table and the string pool; then 8-byte commands (u16 opcode,
// u16 operand count, u32 index of the first operand) and 5-byte operands
// (u8 type, u32 value).
Lscr parseLscr(std::span<const std::uint8_t> data);

struct LimgPart {
    // A rectangle of the tile map, in 8-pixel tiles, and the run of pieces
    // (hardware-sized sprite cells) that cover it.
    unsigned x = 0, y = 0, width = 0, height = 0;
    unsigned firstPiece = 0, pieceCount = 0;
};

struct LimgPiece {
    // Shape code 0..11 (see kLimgPieceSizes) and position within the part,
    // in tiles.
    unsigned shape = 0, x = 0, y = 0;
};

// Pixel sizes of the piece shape codes 0..11 (derived from the parts they
// cover); they are the twelve DS sprite sizes.
inline constexpr unsigned kLimgPieceSizes[12][2] = {
    {8, 8},   {16, 8},  {8, 16},  {32, 8},  {8, 32},  {16, 16},
    {32, 16}, {16, 32}, {32, 32}, {64, 32}, {32, 64}, {64, 64},
};

struct Limg {
    // 0: 16 colours, 4 bits per pixel; 1: up to 256 colours, 8 bits;
    // 2: 32 colours with 3-bit alpha in each byte (the DS texture format
    // A3I5), used for the character portraits.
    unsigned format = 0;
    unsigned width = 0, height = 0;              // the picture
    unsigned areaWidth = 0, areaHeight = 0;      // the power-of-two area it sits in
    std::uint8_t areaShiftX = 0, areaShiftY = 0; // log2 of that area in tiles
    std::vector<std::uint16_t> palette;          // RGB555
    std::vector<LimgPart> parts;
    std::vector<LimgPiece> pieces;
    std::vector<std::uint16_t> map; // (width/8) x (height/8) tile numbers, 0xffff = empty
    unsigned tileCount = 0;
    std::span<const std::uint8_t> tiles; // inside the data passed in

    // Palette index (low 8 bits) and 5-bit alpha (0 = clear, 31 = opaque)
    // of one pixel. Empty cells and, outside format 2, index 0 are clear.
    struct Pixel {
        std::uint8_t index = 0, alpha = 0;
    };

    Pixel pixel(unsigned x, unsigned y) const;
};

// "LIMG", then the offsets of the palette, parts, pieces, tile map and
// tiles with the counts of the last four, the format, the palette size,
// the picture and area sizes and the area's tile shifts (header 0x30).
Limg parseLimg(std::span<const std::uint8_t> data);

} // namespace picarat::import::adventure3
