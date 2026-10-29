#pragma once
// Bounds-checked readers for the resource formats of the fourth adventure.
// The game is built on a new engine library (overlay 6, classes named
// gnCFile, gnCFileArchive, ...) with containers of its own; every layout
// here comes from that library's readers (addresses below) and was checked
// against all of the ROM's files:
//
//  * GFSA: the three big archives (.fa) that hold nearly all game data.
//    Directory and file names are looked up by a 16-bit hash; each file is
//    one stream for decompressMethodWord().
//  * GFSP: a small named pack inside those files (.ci pictures, .ca
//    animations, .cse sound sets, .cb handwriting data).
//  * GSS1: an event or menu script (blocks, labels, commands, operands).
//  * GSD1: a command list used for data: texts, tables, animation
//    descriptions.
//  * GTXT: a tiled picture; its parts and pieces are the third adventure's
//    LIMG records. GPLT: its palette, a separate member.
//  * GFNT: a font (glyph table and bitmaps).
#include "import/a3_formats.hpp"
#include "import/rom_image.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace picarat::import::adventure4 {

// CRC-16/X.25 (reflected polynomial 0x1021, initial value and final XOR
// 0xffff): the hash names are indexed by in GFSA and GFSP tables and in
// GSS1 block and label tables (table: ov6 020b3f84).
std::uint16_t nameHash(std::string_view name);

struct ArchiveFile {
    std::string path;                     // directory name (ends in '/') + file name
    std::span<const std::uint8_t> stored; // a decompressMethodWord() stream
};

// GFSA (open: ov6 02089a1c, lookup 020898dc, read 0208a2fc). A 0x28-byte
// header: "GFSA", the offsets of the directory table, file table, a table
// the game fills at run time, the name list and the file data, the
// directory and file counts, and the expanded sizes of the first two and
// the last two tables together. Each table is a stream of its own.
// Directories (8 bytes): name hash, file count, first file. Files (8 bytes,
// sorted by hash within their directory): name hash, then offset / 4 and
// size split over three halfwords. The names (directories first, then
// each directory's files) are only needed to list the archive; every
// name must match its entry's hash. Files come in name-list order.
std::vector<ArchiveFile> gfsaFiles(std::span<const std::uint8_t> data);

struct PackMember {
    std::string name;
    std::span<const std::uint8_t> data; // inside the pack passed in
};

// GFSP (member lookup: ov6 0208955c). "GFSP", member count, then the
// offsets / 4 of the member table, the name list and the data. Members (8
// bytes): name hash, offset / 4 and size, like GFSA files.
std::vector<PackMember> gfspMembers(std::span<const std::uint8_t> data);

// A command operand. The low four bits of `type` are its kind (1 integer;
// 2, 3 and 4 numbers whose roles are not known yet; 8 text, 9 a name); with
// 0x10 set, `value` is an offset into the file (strings).
struct Operand {
    std::uint8_t type = 0;
    std::uint32_t value = 0;

    bool isText() const { return (type & 0x10) != 0; }
};

struct DataCommand {
    std::uint16_t opcode = 0;
    std::uint16_t first = 0, count = 0; // its operands
};

// GSD1 (reader: ARM9 02033cfc). "GSD1", command count, command table / 4,
// operand count, operand table / 4, string pool / 4. Commands are 6 bytes
// {opcode, first operand, operand count}, operands 5 bytes {type, value}.
struct DataList {
    std::vector<DataCommand> commands;
    std::vector<Operand> operands;
    std::span<const std::uint8_t> file;
    std::string text(const Operand& operand) const;
};

DataList parseGsd1(std::span<const std::uint8_t> data);

struct ScriptBlock {
    std::uint32_t name = 0; // string offset
    std::uint16_t hash = 0;
    std::uint16_t firstCommand = 0, endCommand = 0; // its commands, end exclusive
    std::uint16_t firstLabel = 0, labelCount = 0;   // its labels
    std::uint16_t unknown16 = 0;                    // roles not known yet
    std::uint32_t unknown32 = 0;
};

struct ScriptLabel {
    std::uint32_t name = 0; // string offset
    std::uint16_t hash = 0, command = 0;
};

struct ScriptCommand {
    std::uint16_t first = 0;  // first operand
    std::uint8_t count = 0;   // operand count
    std::uint16_t group = 0;  // role not known yet (1000..2001)
    std::uint16_t opcode = 0; // below 1000: the interpreter's own; else a bound handler
};

// GSS1 (loader: ov6 0208b8a0). "GSS1" and ten halfwords: block count and
// table / 4 (20-byte blocks, sorted by name hash; block lookup 0208bb60;
// together they cover the commands without overlap), label table / 4 and
// count (8 bytes), command table / 4 and count (12 bytes), operand table / 4
// and count (5 bytes), the number of run-time values, and the string pool
// / 4. The loader fills the commands' handler words and flags.
struct Script {
    std::vector<ScriptBlock> blocks;
    std::vector<ScriptLabel> labels;
    std::vector<ScriptCommand> commands;
    std::vector<Operand> operands;
    std::uint16_t values = 0;
    std::span<const std::uint8_t> file;
    std::string text(std::uint32_t offset) const;
};

Script parseGss1(std::span<const std::uint8_t> data);

// GPLT: "GPLT", colour count, header size (8), RGB555 colours; padded to
// four bytes.
std::vector<std::uint16_t> parseGplt(std::span<const std::uint8_t> data);

// GTXT (reader near ov6 0208e680; per-format tables at 020b46aa and
// 020b4698). The format numbers, with their colours and bits per pixel:
// 0 direct colour (16), 1 four colours (2), 2 sixteen (4), 3 256 (8),
// 4 eight colours with 5-bit alpha (A5I3, 8), 5 32 colours with 3-bit alpha
// (A3I5, 8). The format byte's high four bits are flags (one picture sets
// 0x10; role not known yet).
struct Picture {
    unsigned format = 0, flags = 0;
    // The second header byte: in 256-colour pictures with a shorter palette
    // it matches the palette's size, else 0 (hypothesis from the files).
    unsigned colours = 0;
    std::uint8_t areaShiftX = 0, areaShiftY = 0;
    unsigned areaWidth = 0, areaHeight = 0; // at most 8 << shift
    unsigned width = 0, height = 0;
    std::vector<adventure3::LimgPart> parts;
    std::vector<adventure3::LimgPiece> pieces;
    std::vector<std::uint16_t> map; // (width/8) x (height/8) tile numbers, 0xffff = empty
    unsigned tileCount = 0;
    std::uint16_t mode = 0; // header halfword 0x22, 0 or 1; role not known yet
    std::span<const std::uint8_t> tiles;

    unsigned bitsPerPixel() const;
    // The raw value of one pixel: a palette index (with alpha bits for
    // A3I5/A5I3) or, in format 0, the RGB555 colour; none for empty cells.
    std::optional<std::uint16_t> pixel(unsigned x, unsigned y) const;
};

Picture parseGtxt(std::span<const std::uint8_t> data);

struct FontGlyph {
    std::uint16_t code = 0; // ASCII or Shift-JIS
    // Four nibbles (glyph drawing, ov6 0209afb8): x offset, advance, bitmap
    // width, bits 16..19 of the bitmap offset.
    std::uint16_t metrics = 0;
    std::uint32_t offset = 0; // into the bitmaps (with the metrics' high bits)
};

// GFNT (loader: ov6 0209b238). "GFNT", cell width and height, then the
// offset and byte size of the glyph table (6-byte glyphs {code, metrics,
// offset}, sorted by code; the game binary-searches it, 0209af04) and of
// the bitmaps: one bit per pixel, row after row of the glyph's width, each
// byte filled from its low bit.
struct GameFont {
    unsigned cellWidth = 0, cellHeight = 0;
    std::vector<FontGlyph> glyphs;
    std::span<const std::uint8_t> bitmaps;
};

GameFont parseGfnt(std::span<const std::uint8_t> data);

} // namespace picarat::import::adventure4
