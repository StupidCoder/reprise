#pragma once
#include "amiga/hunk.hpp"
#include "amiga/iff.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// F/A-18 Interceptor's data, decoded from an installed content folder
// (import/disk_import.hpp). Formats: .analysis/fa_18_interceptor_amiga/formats.md.
//
// The program is laid out as LoadSeg would leave it, from $100000 (our
// research layout, work/F.bin), so its tables, fonts and texts are read by
// the addresses the notes give.
namespace interceptor::data {

constexpr std::uint32_t program_base = 0x100000;

// A 12-bit Amiga colour ($0RGB).
using Colour = std::uint16_t;
using Palette = std::array<Colour, 32>;

struct Picture {
    std::string file;  // "pix/splsh"
    std::string title;
    amiga::IlbmPicture picture;
};

// A raw sample file and the sound slot the loader puts it in ($1099CA,
// $1096BA, $109660 through $1458A6). The slot's period is the one it gets
// when loaded; the game changes some at run time (the engine's pitch).
struct Sound {
    std::string file;   // "text/textegn"
    std::string title;  // what it is, where known
    int slot{};
    int period{};       // Paula period (PAL clock / period = rate)
    std::vector<std::int8_t> samples;
};

// A bitmap font: one byte per row, the leftmost pixel in bit 7. Glyphs for
// the characters $20-$5F, found through a table of word offsets from the
// table's start (the text drawers at $127402 and $127B58 subtract $20).
struct Font {
    int rows{};
    std::array<std::vector<std::uint8_t>, 64> glyphs;
    const std::vector<std::uint8_t>* glyph(char c) const {
        const int i = int(static_cast<unsigned char>(c)) - 0x20;
        return i >= 0 && i < 64 ? &glyphs[std::size_t(i)] : nullptr;
    }
};

// A screen-text block (table $133FFE, typed out by $127920): a start on the
// 320-pixel screen and lines 11 scanlines apart ($13AA2E = $1B8 bytes).
struct TextLine {
    int flags{};   // bit 0 changes how the line is typed (not traced yet)
    int colour{};  // palette index of the line's characters
    int speed{};   // frames per character (hypothesis: $13AACB counts down)
    int delay{};   // pause after the line, in units of 4 frames ($127836)
    std::string text;
};

struct TextBlock {
    int number{};            // 1-based, as the game asks for it
    std::uint32_t address{};
    int row{}, column{};     // scanline and byte column (8 pixels)
    std::vector<TextLine> lines;
};

struct Assets {
    amiga::LoadedImage program;
    std::vector<Picture> pictures;
    std::vector<Sound> sounds;
    std::vector<Palette> palettes;          // the ten 32-colour palettes at $119FCC
    Font small_font;                        // cockpit displays, 5 rows ($1328B8)
    Font text_font;                         // menus and briefings, 7 rows ($132A24)
    // Pixel offset of each character column of a text line from the line's
    // start: the drawer at $127BD4 takes a byte offset and a blitter shift
    // (top nibble) per column from the table at $13635A.
    std::vector<int> text_columns;
    std::vector<TextBlock> texts;
    std::vector<std::string> messages;      // the 54 cockpit message lines ($1321C8)
    std::vector<std::uint8_t> config;
    std::vector<std::uint8_t> log, log_control; // text/textply, text/textctl
};

Assets load_assets(const std::filesystem::path& content);

// Decoders, exposed for the checks.
amiga::LoadedImage load_program(std::span<const std::uint8_t> load_file);
Font read_font(const amiga::LoadedImage& program, std::uint32_t table, int rows);
std::vector<TextBlock> read_text_blocks(const amiga::LoadedImage& program);

} // namespace interceptor::data
