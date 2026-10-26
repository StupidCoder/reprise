#pragma once
#include <array>
#include <cstdint>

// A marble: a point mass rolling over the course's height map.
//
// The game keeps a $F4-byte record per player ($10024E, $100342). This
// struct holds the parts the rules use, under their own names; the comments
// give the record offsets, which the research tools use to compare a run
// with the original.
namespace marble_madness::game {

struct Marble {
    // Motion, 16.16 world units (x and y: 8 per cell; z: height).
    std::int32_t vx{}, vy{}, vz{};   // +$00, +$04, +$08
    std::int32_t x{}, y{}, z{};      // +$0C, +$10, +$14

    // +$18: 0 not in the race, 1 racing, 2 out of time, 3 finished.
    std::uint8_t status{};
    std::uint8_t player{};           // +$19
    // +$1A: what the marble is doing (Marble::Mode).
    std::uint8_t mode{};
    std::int8_t feature{};           // +$1B: course zone it is in (-1: the goal)
    std::uint8_t sprite_flag{};      // +$1C
    std::int8_t saved_feature{};     // +$1D: zone at the last grounded update
    std::array<std::int32_t, 3> screen{}; // +$1E, +$22, +$26: last screen positions (x << 16 | y)
    std::int32_t fall_start_z{};     // +$2A
    std::int16_t edge_cell_x{}, edge_cell_y{}; // +$2E, +$30: cell where it left the ground
    std::int16_t cell_x{}, cell_y{}; // +$32, +$34: cell at the last grounded update
    // +$36: 0 on the ground, 1 rolling over an edge, 2 falling.
    std::uint8_t air{};
    std::uint8_t edge_sides{};       // +$37
    // +$38: the wall covers for this update's sprite ($11EC42): course
    // wall-cover cells (bank 5) whose pixels are cut out of the sprite, at
    // screen column x (a multiple of 8) and height y. cell -1 ends the list.
    struct Cover {
        std::int32_t cell{-1}; // 5 << 16 | cell number
        std::int16_t x{}, y{};
    };
    std::array<Cover, 5> covers{};
    std::uint8_t bounce{};           // +$60
    std::int8_t delay{};             // +$61 (also the landing impact)
    std::int8_t region_code{};       // +$62: terrain code of the region it touches
    std::uint8_t region_timer{};     // +$63
    std::uint32_t animation{};       // +$64: position in an animation list (marbdat address)
    std::uint32_t dust_animation{};  // +$6C: the dust of a breaking marble
    std::uint8_t dust_tick{};        // +$70
    std::uint8_t dust_shown{};       // +$71
    std::int8_t animation_tick{};    // +$69
    std::int8_t animation_period{};  // +$6A
    std::int16_t time{};             // +$74: seconds left
    std::int8_t tenths{};            // +$76: tenths of the current second (9-0)
    std::int8_t finish_feature{};    // +$77
    std::int8_t timer_frames{};      // +$78: 5, 3, 1 per tenth; -1 = timer stopped
    std::int8_t bonus_wobble{};      // +$72
    std::int8_t bonus_wobble_step{}; // +$73
    std::uint8_t bonus_time{};       // +$7A
    // The shine: eight points on the ball (x, y, z rows, 14-bit fixed
    // point), +$7E, +$8E, +$9E; and the four dot records the sprite gets
    // (+$AE: two dots each, x and y; -1 = drawn already).
    std::array<std::int16_t, 8> shine_x{}, shine_y{}, shine_z{};
    std::array<std::array<std::int16_t, 4>, 4> shine_dots{};
    std::int32_t score{};            // +$CE
    std::int16_t roll_sound{};       // +$D6
    std::uint8_t shine_phase{};      // +$DC: which vector is renormalised next
    std::uint8_t stunned{};          // +$DD
    std::uint32_t dizzy_animation{}; // +$DE
    std::uint8_t dizzy_tick{};       // +$E2
    std::uint8_t dizzy_shown{};      // +$E3
    std::int16_t respawns{};         // +$E4
    std::uint32_t bonus_picture{};   // +$E6: the points shown over the marble
    std::uint8_t bonus_on{};         // +$EA
    // The sparkles of a reappearing marble ($1138D4): marble-library cells
    // $44-$4B at screen positions (heights count upwards). The game keeps
    // them in the shine and cover slots (+$AE, +$38) and sets +$EB so the
    // sprite builder shows them instead of the marble.
    struct Sparkle {
        std::int16_t cell{}, x{}, y{};
    };
    std::array<Sparkle, 8> sparkles{};
    std::uint8_t sparkle_count{};

    enum Mode : std::uint8_t {
        rolling = 0,
        landing = 1,     // bouncing after a fall
        breaking = 2,    // shattered (an animation, then a respawn)
        recoiling = 3,   // thrown back by a region, waiting
        respawning = 4,
        bumped = 5,
        finishing = 6,   // rolling on past the goal
        frozen = 7,
        dizzy = 8,
        appearing = 9,
        captured = 10,   // taken by a region (a trap), then respawned
        settling = 11,
    };
};

} // namespace marble_madness::game
