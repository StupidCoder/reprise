#pragma once
#include "c64/program.hpp"
#include "c64/text_frame.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// Bruce Lee's graphics and room data, decoded from the installed files the
// way the game's own code builds and draws it (see assets.cpp for the
// routines each part follows). Nothing here is animated, and the colour
// changes the game makes during the frame are not applied.
namespace bruce_lee::data {

using Glyph = c64::Glyph;

// The two installed program files, read by C64 address.
class GameImage {
public:
    GameImage(c64::Program game, c64::Program high);

    std::uint8_t byte(std::uint16_t address) const;
    std::uint16_t word(std::uint16_t lo_address, std::uint16_t hi_address) const {
        return std::uint16_t(byte(lo_address) | byte(hi_address) << 8);
    }
    std::span<const std::uint8_t> bytes(std::uint16_t address, std::size_t count) const;

private:
    c64::Program game_, high_;
};

// One of the four play charsets. The game stores them half height: the init
// ($487F) doubles every row, so character c is rows 0-3 of a stored glyph
// and character c + 128 is its rows 4-7. A room cell shows c above c + 128,
// an 8x16 tile.
using Charset = std::array<Glyph, 256>;

// A colour change down the screen: from about raster `line` on, $D021-$D023
// take `colours` (the interrupt at $498F).
struct ColourBand {
    int line{};
    std::array<std::uint8_t, 3> colours{};
};

// A lantern of a room's list ($335B): where it hangs and the character the
// room shows there once it is taken (the map holds the lantern itself).
struct Lantern {
    int column{}, screen_row{};
    std::uint8_t taken_code{};
    std::uint16_t address{}; // of the record (its first byte is the state), for checks against the original
};

// Where an exit leads: the room and Bruce's place there ($2F2E-$2F7B).
struct Exit {
    std::uint8_t room{}, x{}, y{};
};

// A room as drawn by $0A9F: 40 x 11 map cells from screen row 3 down, each
// cell two screen rows high. Bit 7 of a cell gives it colour $F8
// (multicolour, colour 0) instead of the room's colour.
struct Room {
    static constexpr int columns = 40, rows = 11;
    std::array<std::uint8_t, columns * rows> cells{};
    int spill{};            // cells the last run writes past the map (the drawing does not stop)
    int charset{};          // index into Assets::charsets ($D018 = $38/$3A/$3C)
    std::uint8_t colour{};  // colour RAM below row 3 ($478D + room)
    std::array<std::uint8_t, 20> header{}; // room parameters copied to $0153 (not decoded yet)
    std::uint16_t map_address{}, header_address{};
    // $D021-$D023 from the top split (raster $41) down, then the changes.
    std::array<std::uint8_t, 3> top_colours{};
    std::vector<ColourBand> bands;
    std::vector<Lantern> lanterns;
    std::array<Exit, 4> exits{}; // for exit characters 2-5

    std::uint8_t at(int column, int row) const { return cells[std::size_t(row * columns + column)]; }
};

// The loading picture: a multicolour bitmap shown while the game loads.
struct LoadingPicture {
    std::array<std::uint8_t, 8000> bitmap{};
};

// The text font at $8800-$8BFF (built at $0443 from the C64 character ROM):
// code = ROM screen code + $20 (wrapping in $00-$3F), codes $40-$7F the same
// glyphs inverted, three glyphs patched. Shown with $D018 = $32 (title
// options below raster $82, status line).
using TextCharset = std::array<Glyph, 128>;

// A sprite pose: rows of 8 (narrow) or 16 (wide) pixels from the top, shown
// x-expanded. Bruce's and the Yamo's poses come in pairs, the odd one the
// second colour layer of the even one.
struct Pose {
    std::vector<std::uint8_t> rows; // one byte per row (narrow) or two (wide); empty: no shape
    int row_count{};                // rows - 1, as the tables hold it ($5B61)
    bool wide{};                    // $5CBF
    std::uint8_t colour{};          // $5C33
};

// A pose's collision mask as $513C builds it at the start: 3 columns of 32
// rows, each sprite bit as one multicolour pixel pair (the sprites are
// x-expanded), the odd partner layer ORed into an even pose.
using CollisionMask = std::array<std::uint8_t, 96>;

// A string of the game's text table ($43AB), as text codes; the printer
// ($17FA) draws code c in the upper row and c | $80 in the lower one.
using Text = std::vector<std::uint8_t>;

struct Assets {
    std::array<Charset, 4> charsets{};
    TextCharset text{};
    std::vector<Text> strings;
    std::vector<Pose> poses;
    std::vector<CollisionMask> masks; // per pose (meaningful for even poses)
    // Passes an actor waits after starting each move ($4A99, by move code).
    std::vector<std::uint8_t> move_delays;
    // First pose of each character kind ($59A5): Bruce, Ninja, Yamo, unused.
    std::array<std::uint8_t, 4> pose_base{};
    // Per room ($4CDB, $1373, $1337, $4BBB/$4BCF, $4BE3/$4BF7, $1387,
    // $134B, $4CAB): lanterns, passes before the Yamo and the Ninja come,
    // the start place, the other place, the place choices, and rooms
    // without fights or enemies.
    std::vector<std::uint8_t> lanterns_per_room, yamo_waits, ninja_waits;
    std::vector<std::uint8_t> start_x, start_y, other_x, other_y, yamo_places, ninja_places;
    std::vector<bool> quiet_rooms;
    std::vector<std::uint8_t> ninja_entry_waits, yamo_entry_waits; // $1323, $135F ($0A40)
    std::uint32_t first_extra_life{}; // $4378: the score for the first extra fall
    std::uint32_t extra_life_step{};  // $437B: and the scores between later ones
    // Per character kind ($59A8, $59AB): its sprites' codes (0 = none);
    // code - 3 indexes $59AE, the sprite number the collision bits use.
    std::array<std::pair<std::uint8_t, std::uint8_t>, 4> actor_sprites{};
    std::array<std::uint8_t, 5> sprite_slot_of_code{};
    // Sprite block numbers ($2750): sprites 0-4 from the first five or the
    // last five, the buffers alternating every pass; and each slot's first
    // sprite ($275A).
    std::array<std::uint8_t, 10> sprite_blocks{};
    std::array<std::uint8_t, 3> first_sprite{};
    // The attract mode's recorded input ($F300, read from $0574/$0745 on).
    std::vector<std::uint8_t> demo_input;
    std::uint8_t demo_byte(std::uint16_t position) const {
        return demo_input[position % demo_input.size()];
    }
    std::vector<Room> rooms;
    // The lift glyphs ($2C5D): per glyph $61-$63 eight pattern bytes, read
    // from the lift's phase on (rooms 0-9: $42DE, the others: $42F6); and
    // the passes per lift step by speed ($4CBF; bit 7: a pause).
    std::array<std::array<std::uint8_t, 24>, 2> lift_patterns{};
    std::array<std::uint8_t, 8> lift_speeds{};
    // Room 18: the x positions from which the face looks at Bruce
    // ($4A96), and the row masks of the fireballs' random shapes ($430F).
    std::array<std::uint8_t, 3> face_thresholds{};
    std::array<std::uint8_t, 4> fireball_masks{};
    // The front end: the title ($0584), the player screen ($0E9B) and the
    // game-over screen ($18A6) show the charset at $8000 above raster $82
    // and the text font below.
    Charset title_charset{};                                     // $8000-$87FF
    std::array<std::array<std::uint8_t, 40>, 4> title_rows{};    // $4A1A, $49CA, $49F2, $4A42 (screen rows 2, 5, 6, 9)
    std::array<std::uint8_t, 12> trademark{};                    // $0E34: rows 4-7 of the title's sprite
    std::array<std::array<std::uint8_t, 16>, 2> player_label{};  // $4A6A, $4A7A: "PLAYER n", upper and lower row
    std::array<std::array<std::uint8_t, 4>, 2> player_digit{};   // $4A8A/$4A8E: the digit's two columns, upper then lower, per player
    std::array<std::array<std::uint8_t, 18>, 2> game_over_label{}; // $42BA, $42CC
    std::array<std::uint8_t, 40> version_line{};                 // $46F4: shown on row 0 by the V key
    std::array<std::uint8_t, 4> left_hand_bits{};                // $4A92: the stick turned for left-handed play
    std::array<std::uint8_t, 16> effect_priorities{}; // $255F: by an effect's low nibble
    // $E000-$FFFF: the sound drivers' tables and streams.
    std::vector<std::uint8_t> sound_block;
    std::uint8_t sound_byte(std::uint16_t address) const {
        return address >= 0xE000 ? sound_block[std::size_t(address - 0xE000)] : 0;
    }
    // $D020-$D023 as the game sets them for play ($4788).
    std::array<std::uint8_t, 4> play_colours{};
    LoadingPicture loading;
};

Assets decode_assets(const GameImage&, std::span<const std::uint8_t> loading_bitmap);

// Reads game.prg, high.prg and loading.prg from an installed content folder.
// Throws std::runtime_error when they are missing or not the expected files.
Assets load_assets(const std::filesystem::path& content);
GameImage load_game_image(const std::filesystem::path& content);

} // namespace bruce_lee::data
