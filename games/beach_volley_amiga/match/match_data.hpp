#pragma once
#include "beach_volley_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// What a match plays from (game.md "Match"): the sprite and effects block
// (animation1 then animation2, unpacked one after the other), the venue's
// court, and the tables volley.code keeps beside its routines: the object
// table's starting contents, animation frames, motion offsets per frame,
// the camera's easing runs, the ball's arc and the venues' chance tables.
// All are read from the files here; the match code refers to them by role.
namespace beach_volley::match {

// One picture in the sprite block: 4 planes $1F40 bytes apart, rows 40
// bytes apart (sheets 320 pixels wide), `width` a multiple of 16. `at` is
// its offset in the block, so two images are equal when the program would
// see the same picture.
struct Image {
    std::uint32_t at{};
    int width{}, height{};
    bool operator==(const Image&) const = default;
    template <class A> void persist(A& ar) { ar(at, width, height); }
};

// A drawn object: where on the screen and which picture.
struct Sprite {
    int x{}, y{};
    Image image;
    bool operator==(const Sprite&) const = default;
    template <class A> void persist(A& ar) { ar(x, y, image); }
};

// The program's objects, in the order of its object table ($30CC4, 20-byte
// records: x, y, picture, mask buffer, last screen address, width, height).
enum Object : int {
    a1, a2, b1, b2,           // the players; slot 1 of a side is the one under control
    ball, ball_shadow,
    a1_shadow, a2_shadow, b1_shadow, b2_shadow,
    net,                      // the net post and umpire's chair
    setter_mark,              // over the setter
    landing,                  // where the ball will come down
    object_13,
    cursor,                   // the blinking arrow over a human player
    clock,                    // the match clock's ring
    bump_mark,
    clock_dots,
    ball_mark,                // where the ball came down
    banner,                   // the winner's picture
    object_count
};

using Frames = std::vector<Image>; // a run of animation frames
using Offsets = std::vector<int>;  // a run of per-frame offsets

struct MatchData {
    std::vector<std::uint8_t> sprites;        // animation1 + animation2
    data::Court court;                        // the venue's
    int venue{};                              // 1-8
    std::array<Sprite, object_count> objects{}; // the object table as loaded

    // Pictures named one by one in the code.
    std::array<Image, 8> stance{};    // $2B5C6: facing the ball (see players)
    std::array<Frames, 6> walk{};     // $2F5B0...: by direction
    Frames ball_spin;                 // $2AEC0
    Frames serve_swing_a, serve_swing_b; // $2716C, $28014
    Frames jump_a, jump_b;            // $2F498, $2F4CC
    Frames recover_left, recover_right; // $2F53C, $2F500

    Offsets toss;   // $2719A: the ball's rise and fall at a serve
    Offsets jump;   // $30B7A: a jump's height per frame
    Offsets hop;    // $30BBA
    Offsets serve_heights_b; // $27FF4: where side B's server stands
    Offsets serve_toss_b;    // $27FB0: side B's toss (23 steps)
    Offsets setter_targets;  // $2FC12
    Offsets touch_order;     // $2A95A: the phases a rally steps through
    std::vector<int> dive_rise, dig_rise, dive_shift; // $2D21E, $2D232, $2D248 (bytes)
    Offsets fall_x, fall_y;  // $2C5CC, $2C600

    // The camera's easing runs ($28B64...), chosen by a hit's travel
    // through the thresholds at $2AADC.
    std::vector<int> ease_thresholds;
    std::vector<Offsets> ease;
    // The arc: the ball's rise per frame. A flight starts `height` bytes
    // below the table's end ($2AF38) and walks toward it and back; arc[64]
    // is the word at the end, arc[64 - height / 2] the first one used.
    std::vector<int> arc;

    // The scoreboard digits drawn into the court ($26286: 16 x 10, 4 planes).
    std::array<Image, 10> board_digits{};
    // The big scores after a point ($30640): 8 x 8 blocks of the dot
    // picture ($53E04) at (column byte, block row) for scores 0-7.
    std::array<std::vector<std::pair<int, int>>, 8> big_digits{};
    // The match clock's dot positions in the sprite sheet at $6B500 ($31EE2,
    // $31EFC): x, y of each of the eleven 3 x 3 dots.
    std::vector<std::pair<int, int>> clock_positions;

    // Per venue: the values the vertical blank steps through, one each
    // blank ($2869A...; game.md "Randomness").
    std::vector<int> chance;
};

// Where the sprite block starts in the original's memory; the program's
// picture addresses are taken from it.
constexpr std::uint32_t sprite_block_address = 0x53E00;

// Reads the venue's files and volley.code's tables; throws
// std::runtime_error on files that do not hold them.
MatchData load_match_data(const std::filesystem::path& content, int venue);

} // namespace beach_volley::match
