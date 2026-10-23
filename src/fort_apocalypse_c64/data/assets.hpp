#pragma once
#include "c64/program.hpp"
#include "c64/text_frame.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// Fort Apocalypse's graphics data, decoded from the installed game file the
// way the game's own setup code builds it (see assets.cpp for the routines
// each part follows). Everything here is data from the user's tape; nothing
// is drawn or animated yet. Runtime changes the game makes on top (soft
// characters animated by the interrupt, random rock texture, the radar
// blips, the level-2 changes) belong to the game code and are not applied.
namespace fort_apocalypse::data {

// One 8x8 character. The game shows all characters in multicolour mode:
// each byte is four 2-bit pixel pairs (00 background, 01 and 10 the two
// shared colours, 11 the cell's own colour).
using Glyph = c64::Glyph;
using Charset = std::array<Glyph, 128>;

// A helicopter sprite shape: 16x18 single-colour pixels, bit 15 of each row
// is the leftmost pixel. The game shows it with double-width pixels.
struct SpriteShape {
    static constexpr int width = 16, height = 18;
    std::array<std::uint16_t, height> rows{};

    bool pixel(int x, int y) const { return rows[std::size_t(y)] >> (15 - x) & 1; }
};

// A level's terrain as authored: 40 rows of character codes. Columns 0-214
// are the level; the world wraps around horizontally. Each stored row is 256
// cells: after 215 the stream holds 40 empty cells and a final cell that is
// shown at the wrap seam (see `seam`).
struct TerrainMap {
    static constexpr int columns = 215, rows = 40, stored_columns = 256;
    std::array<std::array<std::uint8_t, stored_columns>, rows> cells{};

    std::uint8_t at(int column, int row) const {
        return cells[std::size_t(row)][std::size_t(column)];
    }
    // The cell stored after the padding (column 255), which the camera shows
    // at the right edge just before it wraps.
    std::uint8_t seam(int row) const { return cells[std::size_t(row)][stored_columns - 1]; }
};

// Placeholder codes in the terrain that the level setup replaces with a
// random rock character ($73 -> $62-$64, $74 -> $65-$67), so every game's
// caves look slightly different. Kept as authored here.
inline constexpr std::uint8_t random_rock_a = 0x73, random_rock_b = 0x74;

// The radar picture of a level: 40 lines (one per terrain row) of 40 bytes,
// shown as 160 multicolour pixel pairs per line. As authored, bytes 0-26
// show the level and bytes 27-39 are not used; the level setup copies
// bytes 0-12 there so the radar window can wrap like the terrain.
struct RadarImage {
    static constexpr int bytes_per_line = 40, lines = 40;
    std::array<std::array<std::uint8_t, bytes_per_line>, lines> bytes{};

    // The colour pair (0-3) at pixel pair x (0-159) of line y.
    int pair(int x, int y) const {
        return bytes[std::size_t(y)][std::size_t(x / 4)] >> (6 - 2 * (x % 4)) & 3;
    }
};

// The end of a compressed stream that does not fit its buffer. The
// decompressor only checks for the end after a whole run, and every stream
// in this game ends with a run that crosses the end: the original writes
// `count` more copies of `value` into the memory after the buffer. Whether
// that is ever visible is for the game code to settle; it is recorded here
// exactly.
struct Spill {
    std::uint8_t value{};
    int count{};
};

struct Level {
    TerrainMap terrain;
    RadarImage radar;
    Spill terrain_spill, radar_spill;
    std::uint8_t multicolour{}; // the level's first shared colour (pairs 01)
};

// The top six screen rows during play (score, fuel, radar frame), as
// character codes of the HUD charset.
struct HudScreen {
    static constexpr int columns = 40, rows = 6;
    std::array<std::array<std::uint8_t, columns>, rows> cells{};
};

// A text for the game's double-width font: HUD-charset codes, each drawn as
// the character and the one $20 above it side by side; $00 is a single
// empty cell. Read up to the $FF end mark (not included).
using Text = std::vector<std::uint8_t>;

// The texts of the title and options screens.
struct FrontEndTexts {
    std::array<Text, 5> title{}; // title, author, port, porter, publisher lines
    Text copyright;              // "COPYRIGHT"
    // The year next to it: eight characters placed as they are, not as text.
    std::array<std::uint8_t, 8> year{};
    Text options, option, select;      // headings
    std::array<Text, 3> setting_names; // gravity skill, pilot skill, robo pilots
    // For each setting its three values, padded with blanks to cover longer ones.
    std::array<std::array<Text, 3>, 3> setting_values{};
};

struct Assets {
    Charset hud_charset{};       // screen rows 0-6
    Charset playfield_charset{}; // the terrain window below
    HudScreen hud{};
    std::array<SpriteShape, 14> helicopter{}; // shared by player and enemy
    // Bank/tilt step (0-17) to helicopter shape (0-13); even and odd steps
    // are the two rotor frames of a pose.
    std::array<std::uint8_t, 18> helicopter_tilt{};
    // The bullet's 3x3-line dot pattern (three sprite rows of 3 bytes).
    std::array<std::uint8_t, 9> bullet_dots{};
    // Levels by the game's level number. Level 2 uses level 0's data again.
    std::array<Level, 3> levels{};
    FrontEndTexts texts;
    // The last byte of the VIC's memory bank ($7FFF, in the game file), which
    // the VIC shows on lines between character rows.
    std::uint8_t idle_pattern{};
};

// Decodes the game file. Throws std::runtime_error when the data is not
// what the decoders expect.
Assets decode_assets(const c64::Program& game_file);

// Loads game.prg from an installed content folder and decodes it.
Assets load_assets(const std::filesystem::path& content_directory);

} // namespace fort_apocalypse::data
