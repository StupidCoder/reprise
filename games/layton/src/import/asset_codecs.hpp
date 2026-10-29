#pragma once
// Bounds-checked decoders for the resource formats the importer packs.
// Game-specific layouts were derived from this ROM;
// the BIOS compression codecs and NFTR fonts follow general DS documentation.
#include "import/rom_image.hpp"
#include <array>

namespace picarat::import {

// BIOS LZ10, RL and Huffman 4/8, selected by the header's type byte, and
// type 0 for bytes stored as they are (third adventure).
// LZ references may overlap their destination; Huffman words are little-endian
// but read MSB first, and 4-bit symbols fill a byte low nibble first.
Bytes decompress(std::span<const std::uint8_t> data, std::size_t limit = 16u << 20);

// The fourth adventure's streams use the same codecs under another header
// word: the method in its low three bits (0 stored, 1 LZ10, 2 Huffman with
// 4-bit and 3 with 8-bit symbols, 4 RL) and the expanded size above them
// (its dispatcher: ov6 02080f10, the size 02080ce0).
Bytes decompressMethodWord(std::span<const std::uint8_t> data, std::size_t limit = 32u << 20);

// Only the wrappers established for named families are undone: .arc/.arj
// carry a 4-byte codec tag before the stream, .plz/.dlz are a bare stream,
// and so are the third adventure's c-prefixed files (lt3/.../*.cimg ...).
Bytes expandAsset(std::span<const std::uint8_t> data, std::string_view path);

struct ArchiveMember {
    std::string name;
    Bytes data;
};

// PCK2: header size, total size, "PCK2", then records of
// {header size, record size, flags, payload size, NUL-terminated name}.
std::vector<ArchiveMember> archiveMembers(std::span<const std::uint8_t> data);

struct Background {
    unsigned width = 0, height = 0;
    std::vector<std::uint16_t> palette;
    std::size_t tileOffset = 0; // first 8x8 tile (one byte per pixel)
    Bytes indices;              // width x height palette indices
};

// Palette, 8bpp tiles and a tile map whose entries may flip each tile.
Background decodeBackground(std::span<const std::uint8_t> data);

struct SpriteChunk {
    int x = 0, y = 0, width = 0, height = 0;
};

struct SpriteFrame {
    unsigned width = 0, height = 0;
    std::vector<SpriteChunk> chunks;
    Bytes indices;
};

struct SpriteAnimation {
    std::string label;
    std::vector<std::uint32_t> next, durations, images;
};

struct Sprite {
    std::vector<SpriteFrame> frames;
    std::vector<std::uint16_t> palette;
    std::vector<SpriteAnimation> animations;
    // Optional extension (marker 0x1234): 16 variable names with 8 planes of
    // 16 values each, then the attachment data (see the packer).
    bool hasVariables = false;
    std::vector<std::string> variables;
    std::array<std::array<std::int16_t, 16>, 8> planes{};
    Bytes tail;
};

// .arc stores linear chunk rows; .arj stores consecutive 8x8 tiles and puts
// the palette size before the frames.
Sprite decodeSprite(std::span<const std::uint8_t> data, bool arj);

struct FontGlyph {
    std::uint32_t code = 0;
    std::int32_t left = 0, advance = 0;
    Bytes alpha;
};

struct Font {
    int cellWidth = 0, cellHeight = 0;
    std::vector<FontGlyph> mapped; // character mappings in table order
};

// Generic Nitro NFTR 1.0: glyph cells (1-4 bpp), width chains and
// character maps (direct, table and scan methods).
Font decodeFont(std::span<const std::uint8_t> data);

struct ScriptToken {
    // 0 opcode, 1 int32, 2 float32, 3 bytes, 12 end. The first adventure's
    // reader (ARM9 02017e7c) knows more: 4 bytes like 3, 6 and 7 an int32
    // (in its scripts always the offset of a token after the size word: a
    // jump target), and 5, 8 to 11 markers without an operand.
    std::uint16_t tag = 0;
    std::uint16_t opcode = 0;
    std::int32_t integer = 0;
    Bytes bytes;
    std::string text() const; // byte string up to the trailing NULs
};

// GDS: a size word, then typed tokens; opcode meanings are not needed here.
std::vector<ScriptToken> scriptTokens(std::span<const std::uint8_t> data);

} // namespace picarat::import
