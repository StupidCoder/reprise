#pragma once
// Bounds-checked readers for the resource formats of the fifth and sixth
// adventures (Nintendo 3DS). Both games share one engine and one set of
// formats; only the archive container changed between them. The formats
// descend from the fourth adventure's (a4_formats.hpp): the same
// compression word, name-hashed tables and script design, widened to 32-bit
// hashes and fields. Layouts were derived from the two ROMs' files and
// checked against all of them; see .analysis/adventure56/docs/assets.md.
//
//  * XFSA (fifth) / ARC0 (sixth): the archives (.fa). Tables are
//    decompressMethodWord() streams; files are stored as they are.
//  * XPCK: a named pack (pictures with animations, models, popups, ...);
//    XFSP: its predecessor with 16-bit hashes (fifth adventure only).
//  * IMGC: a tiled picture (8x8 tiles, deduplicated through a tile map).
//  * XQ32: a script (blocks, labels, commands, operands), the successor of
//    GSS1. XSCR: a command list for data, the successor of GSD1.
#include "import/rom_image.hpp"
#include <array>
#include <string>
#include <vector>

namespace picarat::import::adventure56 {

// CRC-32 (zlib's), the hash of names in archives, packs and scripts.
std::uint32_t nameHash(std::string_view name);

struct ArchiveFile {
    std::string path;         // directory name (ends in '/') + file name
    std::uint64_t offset = 0; // from the archive's start
    std::uint32_t size = 0;
};

// Bytes of the archive's header and tables an index needs: everything
// before the file data (the header's fifth word).
std::uint32_t archiveTablesSize(std::span<const std::uint8_t> header);

// The archive's files in name-list order. `tables` is the start of the
// archive up to archiveTablesSize(); `archiveSize` bounds the files.
//
// Common header: magic, the offsets of the directory table, a list of
// directory hashes, the file table, the name list and the file data, then
// the directory and hash-list counts (u16) and the file count. XFSA's header
// is 0x24 bytes; ARC0 adds a time stamp and check words (0x48).
//
// XFSA directory (16 bytes): CRC-32 of the name; file count (bits 0-13) and
// file names' offset (14-31); first file (u16) and a u16 link; sub-directory
// count (0-13) and the name's offset (14-31). File (12 bytes): CRC-32;
// offset / 16 (bits 0-25) and name-offset bits 9-14 (26-31); size (0-22) and
// name-offset bits 0-8 (23-31). Name offsets count from the directory's.
//
// ARC0 directory (20 bytes): CRC-32, two u16 links, first file, file count
// (u16), file names' offset, name offset. File (16 bytes): CRC-32, name
// offset, data offset, size.
std::vector<ArchiveFile> archiveFiles(std::span<const std::uint8_t> tables,
                                      std::uint64_t archiveSize);

struct PackMember {
    std::string name; // non-ASCII bytes as %XX
    std::span<const std::uint8_t> data;
};

// XPCK: "XPCK", member count (12 bits), u16 offsets / 4 of the member table,
// the name list (a method-word stream) and the data. Members (12 bytes):
// CRC-32 of the name, name offset, data offset / 4, size, and a u16 whose
// low byte extends the offset and high byte the size.
// XFSP: the same with 10-byte members whose hash is the fourth adventure's
// CRC-16 (adventure4::nameHash).
std::vector<PackMember> packMembers(std::span<const std::uint8_t> data);

// IMGC formats (header byte 0x0a) and their bits per pixel (byte 0x0d):
// 0 RGBA 32, 1, 2 and 4 sixteen-bit colour, 3 RGB 24, 12 eight-bit,
// 27 ETC1 4, 28 ETC1 with alpha 8.
struct Picture {
    unsigned format = 0, bitsPerPixel = 0, levels = 0; // levels: mipmaps
    unsigned width = 0, height = 0;
    std::vector<std::uint16_t> map; // tile per 8x8 cell, all levels; 0xffff empty
    unsigned tileCount = 0;
    Bytes tiles; // expanded

    // RGBA of a pixel of level 0 for the uncompressed formats 0 and 3
    // (tiles hold 64 pixels in Z order, y on the even bits; channels from
    // the lowest byte: alpha, blue, green, red).
    std::array<std::uint8_t, 4> rgba(unsigned x, unsigned y) const;
};

// IMGC: a 0x48-byte header ("IMGC", "00", format, levels, bits per pixel,
// width and height at 0x10, the two streams' sizes at 0x38 and 0x3c), then
// the tile map and the tiles, each a method-word stream.
Picture parseImgc(std::span<const std::uint8_t> data);

struct Operand {
    std::uint32_t type = 0; // 1 integer, 2 unsigned, 3 float, 4 register; 0x18 text
    std::uint32_t value = 0;
};

struct ScriptCommand {
    std::uint16_t first = 0; // operands
    std::uint8_t count = 0;
    std::uint16_t group = 0; // role not known yet
    std::uint16_t opcode = 0;
};

// A block (function) of a script: its commands [first, end), its labels,
// and how many registers it needs (24 bytes in the file: name offset,
// CRC-32, u16 first and end command, u16 first label and label count, u16
// temporaries and locals, u16 fewest arguments, u16 0).
struct ScriptBlock {
    std::string name;
    std::uint32_t hash = 0;
    std::uint16_t first = 0, end = 0, firstLabel = 0, labelCount = 0;
    std::uint16_t temporaries = 0, locals = 0, arguments = 0;
};

struct ScriptLabel {
    std::string name;
    std::uint32_t hash = 0, command = 0;
};

// XQ32: "XQ32" and GSS1's ten halfwords (block count, table / 4, label
// table / 4 and count, command table / 4 and count, operand table / 4 and
// count, run-time values, string pool / 4); each table is a method-word
// stream. Blocks are 24 bytes, labels 12 (name offset, CRC-32, command),
// commands 12 as GSS1 (the u16 "group" is the destination register),
// operands 8 {type, value}. Blocks are sorted by hash, and each block's
// labels too (the game binary-searches both).
struct Script {
    std::vector<ScriptBlock> blocks;
    std::vector<ScriptLabel> labels;
    std::vector<ScriptCommand> commands;
    std::vector<Operand> operands;
    std::uint16_t values = 0; // registers 4000.. that belong to the file
    Bytes strings;

    std::string text(std::uint32_t offset) const; // a name or text of the pool
};

Script parseXq32(std::span<const std::uint8_t> data);

struct DataCommand {
    std::uint16_t opcode = 0, count = 0, first = 0;
};

// XSCR: "XSCR", u16 command count, u16 command table / 4, u32 operand count,
// operand table / 4, string pool / 4; streams as XQ32. Commands 8 bytes
// {opcode, operand count, first operand, 0}, operands as XQ32.
struct DataList {
    std::vector<DataCommand> commands;
    std::vector<Operand> operands;
    Bytes strings;
};

DataList parseXscr(std::span<const std::uint8_t> data);

} // namespace picarat::import::adventure56
