#pragma once
#include "amiga/iff.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Rock 'n Roll's data, decoded from an installed content folder
// (import/disk_import.hpp). Formats: games/rock_n_roll_amiga/game.md.
namespace rocknroll::data {

// The unpacked program as it runs from $1A578. Its tables, texts and part of
// the game's graphics are read from it by address.
class Program {
public:
    static constexpr std::uint32_t base = 0x1A578;

    Program() = default;
    explicit Program(std::vector<std::uint8_t> bytes);

    bool contains(std::uint32_t address, std::uint32_t length = 1) const {
        return address >= base && address - base + length <= bytes_.size();
    }
    std::uint8_t u8(std::uint32_t address) const;
    std::uint16_t u16(std::uint32_t address) const;
    std::uint32_t u32(std::uint32_t address) const;
    std::span<const std::uint8_t> bytes(std::uint32_t address, std::uint32_t length) const;
    // Text up to (not including) `terminator`, at most `limit` bytes.
    std::string text(std::uint32_t address, std::uint32_t limit, std::uint8_t terminator = 0xFF) const;

private:
    std::vector<std::uint8_t> bytes_;
};

// A world: one of the game's sets of graphics and music. The program's world
// table ($70E94, 16 bytes per world) names four files for each: the graphics
// (an ILBM with more graphics behind it), a 6,144-byte file that repeats the
// graphics file's last 6,144 bytes, the TFMX song data and the TFMX samples.
// Worlds 1-7 are the levels' worlds; world 8 is the title screen.
struct WorldFiles {
    int graphics{}, extra{}, songs{}, samples{};
};

// The picture memory the play view's tile drawer blits from ($19000 in the
// original, 200 bytes a row: 5 bitplanes of 40 bytes, interleaved). Rows 0-199
// are the world's ILBM body, rows 200-336 the bytes that follow it in the
// world's file (animation frames, ball and item pictures), and rows from 344
// on are graphics inside the program itself (panel, name bar, abyss pattern,
// balls), which the world file does not reach.
struct GraphicsBank {
    static constexpr int width = 320;
    static constexpr int row_bytes = 200;
    static constexpr std::uint32_t address = 0x19000; // where row 0 lies
    int rows{};
    std::vector<std::uint8_t> pixels; // colour index (0-31) per pixel

    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y) * width + std::size_t(x)]; }
};

struct World {
    int number{}; // 1-8
    WorldFiles files;
    amiga::IlbmPicture tiles; // 320 x 200, 32 colours: the tile sheet and its palette
    GraphicsBank bank;
};

// A level map. The file starts with a $3E8-byte header (object lists and
// settings, mostly not read yet), rows at +$3C8 and columns at +$3CA (the
// stride the drawer $6FAA0 and the overview map $6B77A step by), then one
// byte per cell, row by row. A cell value v from 1 to $F0 is drawn as tile
// v - 1 of the world's sheet (20 per row; table $72CBE built by $70954),
// except $14 and $21, which the program re-points every frame into the
// scrolling abyss pattern ($6C972). Values from $F1 up are placed at run time
// (none are stored in the files).
struct Level {
    int number{};       // 1-33, the order of the program's level table
    std::string name;   // from the program's name list ($6AEFC)
    int file{};         // the game file holding the map
    int world{};        // 1-7
    int rows{}, columns{};
    std::vector<std::uint8_t> header; // the first $3E8 bytes as stored
    std::vector<std::uint8_t> cells;  // rows x columns
    // Cells the file holds; the rest (level 28 only) are what the game finds
    // behind the file in memory, here the program's start-up image.
    std::size_t stored_cells{};

    std::uint8_t at(int column, int row) const { return cells[std::size_t(row) * std::size_t(columns) + std::size_t(column)]; }
};

struct Picture {
    std::string name;
    amiga::IlbmPicture picture;
};

// A TFMX music file (song data or samples) as installed; the player is not
// rebuilt yet.
struct MusicFile {
    int file{};
    bool song_data{}; // starts with "TFMX " (song data); otherwise a sample bank
    std::size_t size{};
};

// The high-score file (game file $30, loaded at $762B8 by $70E0C): one
// table of 128 bytes per level index (0-31, the bonus level $20 has none)
// and the hall of fame at index 32 ($762B8 + 32 × 128 = $772B8). An
// entry is 16 bytes: a rank character, a 12-byte name and a 24-bit value
// (+$D..+$F; the program reads the long at +$C and masks it). A level's
// value is its time (seconds << 8 | hundredths, five places used); the
// hall of fame's is a score (ten places); its first score is the high
// score the panel shows ($772C4).
struct ScoreTable {
    std::vector<std::uint8_t> bytes;
    static constexpr int file = 0x30;
    static constexpr int hall_of_fame = 32;
    static constexpr int time_places = 5, score_places = 10;
    struct Time {
        int seconds, hundredths;
    };
    struct Entry {
        std::string name;    // 12 characters as stored ('(' is a space)
        std::uint32_t value; // 24 bits
    };
    Time record(int level_index) const;
    int high_score() const;
    Entry entry(int table, int place) const; // place 1-10
    // $6A38A: the first place whose value the new one beats (a time below
    // it, a score above it; a tie does not), the places below it moved
    // down one ($6A55E, rank characters stay) and the name and value
    // written there ($6A694). Returns the place, or 0 for none.
    int insert(int table, std::string_view name, std::uint32_t value);
};

struct Assets {
    Program program;
    ScoreTable scores;
    // Game file $31 (12,288 bytes, raw): the title's name box, 60 rows in
    // the bank's format, copied over the title picture's rows 124-183 when
    // the name is entered ($693D8 copies 12,000 bytes).
    GraphicsBank name_box;
    std::vector<World> worlds;    // 1-8
    std::vector<Level> levels;    // 1-33
    std::vector<Picture> pictures;
    std::vector<MusicFile> music;

    const World& world(int number) const { return worlds.at(std::size_t(number - 1)); }
};

// Cell values the program draws from the abyss pattern instead of the sheet.
bool is_abyss(std::uint8_t cell);
// The abyss pattern as the drawer shows it with the camera on a tile
// boundary: 16 x 16 pixels from bank row 440, column 0 ($2E7C0).
constexpr int abyss_row = 440;

// Loads and decodes everything; throws std::runtime_error when the content
// is incomplete or malformed.
Assets load_assets(const std::filesystem::path& content);

} // namespace rocknroll::data
