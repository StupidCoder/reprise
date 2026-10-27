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
    std::int8_t clock_flash{-1};     // +$7B: the bar's clock flashing (low time), -1 off
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
    // +$EB: the sparkles show instead of the marble, even when none is left
    // to draw (all in a vacuum's mouth, or off the screen).
    bool sparkles_shown{};

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
        dissolving = 9,  // in an acid puddle
        captured = 10,   // taken by a region (a trap), then respawned
        eaten = 11,      // by a bird (Silly): sparkles in, then a respawn
        // The steelies' modes (Steelie): patrolling their path, chasing a
        // player, heading for the player along the path, going back to the
        // path, knocked about.
        patrolling = 0x20,
        chasing = 0x21,
        hunting = 0x22,
        returning = 0x23,
        knocked = 0x24,
    };
};

// A steelie: a black enemy marble ($100BD6, $86 bytes each, two of them),
// rolled by the same rules as the players' marbles and steered along a
// path of waypoints in the Track (6 bytes each: cell x, y, and up to four
// neighbours by index) towards the nearest player of its zone.
struct Steelie {
    Marble body;
    std::int32_t speed{};       // +$72: the steering's strength (16.16)
    std::int16_t chase_time{};  // +$76: -1 always; else updates of chasing left
    std::uint32_t waypoint{};   // +$78: the waypoint it heads for (Track address)
    std::uint32_t path{};       // +$7C: its path's first waypoint
    std::int16_t band_from{}, band_to{}; // +$80, +$82: the scroll bands it lives in
    std::int16_t target{};      // +$84: the player it goes for
};

// A slinky: a green worm on Beginner, Intermediate and Ultimate ($101548,
// $6A bytes each, four of them) that wanders a path of cells, hops after a
// fast player of its zone, bumps marbles it touches and eats a player it
// catches. Its picture and its moves come from animation lists in the
// Track (header +$1C: a table of lists by state and direction).
struct Slinky {
    enum State : std::uint8_t {
        wandering = 0, // along its path, a cell per animation round
        hopping = 1,   // after a player
        knocked = 2,   // pushed by a marble, sliding
        returning = 3, // back to its path
        landing = 4,   // after a slide
        eating = 5,    // caught a player
        missed = 6,    // gave up the hop
    };
    std::int32_t vx{}, vy{};          // +$00, +$04
    std::int32_t x{}, y{}, z{};       // +$0C, +$10, +$14
    std::uint8_t status{};            // +$18
    std::uint8_t index{};             // +$19
    std::uint8_t state{};             // +$1A
    std::int8_t zone{};               // +$1B
    std::int32_t knock_vx{}, knock_vy{}; // +$1C, +$20: the push that knocked it
    std::uint8_t tick{}, period{};    // +$24, +$25: updates per animation step
    std::int8_t step{};               // +$26: +1 or -1 frames
    std::uint8_t hop{};               // +$27: updates into the hop
    std::int32_t screen{};            // +$28
    std::array<Marble::Cover, 5> covers{}; // +$2C
    std::uint32_t waypoint{}, path{}; // +$54, +$58 (3-byte waypoints: x, y, next)
    std::int16_t band_from{}, band_to{}; // +$5C, +$5E
    std::int16_t target{};            // +$60: the player it goes for
    std::uint32_t frame{}, animation{}; // +$62, +$66: animation list position and start
};

// An acid puddle: green ooze on Intermediate and Ultimate ($1016F0, $42 bytes
// each, five of them) that creeps a cell per round of its animation along
// a path of waypoints and dissolves a player's marble it touches. The Track
// lists them (header +$20: a table whose first entry is a list of 8-byte
// entries {first band, last band, path or 0, zone}; then the animation lists
// by direction and the paths that a path of 0 picks from at random).
struct Acid {
    std::int32_t vx{}, vy{};          // +$00, +$04: a cell (8) per round
    std::int32_t x{}, y{}, z{};       // +$0C, +$10, +$14
    std::uint8_t status{};            // +$18
    std::uint8_t index{};             // +$19
    std::int8_t zone{};               // +$1B
    std::uint8_t tick{}, period{};    // +$24, +$25: updates per animation step
    std::int32_t screen{};            // +$28
    std::uint32_t waypoint{}, path{}; // +$2C, +$30 (3-byte waypoints: x, y, next)
    std::int16_t band_from{}, band_to{}; // +$34, +$36
    std::uint32_t frame{}, animation{}; // +$3A, +$3E: animation list position and start
};

// Aerial's pistons ($1018BE, 16 bytes each, 36 of them): three groups of
// twelve cells that rise three at a time while a player is in zone 4 or 5.
struct Piston {
    std::uint8_t index{};   // +$0
    std::int16_t state{};   // +$2: -1 raised, else the group's wait
    std::int8_t cell_x{}, cell_y{}; // +$4, +$5
    std::int16_t z{};       // +$6: the floor's height at its foot
};

// A group of pistons (Track region block +$23C, $14 bytes each, three):
// the picture of its pistons down, the frame list of the rising animation
// (8-byte entries: picture, -1 ends), a wait (-1 while risen), its place
// on the course picture (x, and a line counted from the scroll's 0), the
// first of the three pistons (column, row) and the pattern (0-3).
struct PistonGroup {
    std::uint32_t picture{};     // +$0
    std::uint32_t frame{};       // +$4
    std::int16_t timer{};        // +$8
    std::int16_t x{}, line{};    // +$A, +$C
    std::int16_t column{}, row{}; // +$E, +$10
    std::int16_t pattern{};      // +$12
};

// Silly's critters ($101AFE, $28 bytes each, nine): three each of kinds 7, 8
// and 9 (also their display types), wandering a pen while the view shows
// it, fleeing a player in the pen; a player who catches one wins three
// seconds and points.
struct Critter {
    enum State : std::uint8_t { wandering = 0, fleeing = 1, caught = 2 };
    std::int32_t vx{}, vy{}, vz{};   // +$00, +$04, +$08
    std::int32_t x{}, y{}, z{};      // +$0C, +$10, +$14
    std::uint8_t status{};           // +$18
    std::uint8_t index{};            // +$19
    std::uint8_t state{};            // +$1A
    std::uint8_t steps{};            // +$1B: kind 7's steps since the last choice
    std::uint32_t animation{};       // +$1C: position in an animation list
    std::int32_t screen{};           // +$20
    std::uint8_t tick{};             // +$24
    std::uint8_t kind{};             // +$25
    std::uint8_t direction{};        // +$26: 0-15 (a sixteenth turn each), $10 standing
};

// Silly's birds ($101C66, $3E bytes each, ten): they drop in at the top of
// five columns and walk down them (towards -y), squashed by a wall, eating
// a player's marble they meet.
struct Bird {
    enum State : std::uint8_t { walking = 0, dropping = 1, gone = 2 };
    std::int32_t vy{};               // +$04
    std::int32_t x{}, y{}, z{};      // +$0C, +$10, +$14
    std::uint8_t status{};           // +$18
    std::uint8_t index{};            // +$19
    std::uint8_t state{};            // +$1A
    std::uint8_t count{};            // +$1B: walks before the pace changes
    std::uint32_t animation{};       // +$1C
    std::int32_t screen{};           // +$20
    std::uint8_t tick{}, period{};   // +$24, +$25
    std::array<Marble::Cover, 5> covers{}; // +$26: three used
};

} // namespace marble_madness::game
