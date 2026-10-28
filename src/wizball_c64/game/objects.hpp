#pragma once
#include <array>
#include <cstdint>

// The things that fly about in a place: enemies, the droplets and pearls
// they leave, the timer's filler enemies. Six slots ($0200 + $2A * n). A
// slot is filled from an enemy kind's template (23 bytes) and random fields;
// the template's bytes are the first 23 fields below, in this order, which
// is why the data addresses them by number (field()).
namespace wizball::game {

struct Object {
    // Position and speed, 8.8 (whole byte first). x counts double pixels.
    std::uint8_t x{}, x_fraction{}, y{}, y_fraction{};
    std::uint8_t dx{}, dx_fraction{}, dy{}, dy_fraction{};
    std::uint8_t motion{};        // $08: 0/1 speeding up/down vertically, 2 path, 3 path with momentum
    std::uint8_t acceleration{};  // $09 (also the colour cycle's clock)
    std::uint8_t path_lo{}, path_hi{}; // $0A/$0B: direction table (bits: 1 right, 2 left, 4 down, 8 up)
    std::uint8_t path_period{};   // $0C
    std::uint8_t landscape{};     // $0D: 0 passes through, 1 bounces, 2+ stops
    std::uint8_t fire{};          // $0E: 0 none, 1 aimed, 2 turning, 3 eight ways
    std::uint8_t fire_period{};   // $0F
    std::uint8_t kind{};          // $10: colour (low five bits; $10 cycles), $40 single colour, $80 beeps
    std::uint8_t animation{};     // $11: 0 still, 1 ping-pong, 2+ table
    std::uint8_t anim_a{};        // $12: frame / period / table index
    std::uint8_t anim_b{}, anim_c{}; // $13/$14: table address or blink clock
    std::uint8_t state{};         // $15: 0 enemy, 1 drops paint when hit, 2-3 falling droplet, 4 dying, 5 pearl, 6 life, 7 token, $FF bounces off
    std::uint8_t score{};         // $16: points (tens)
    // Not from the template.
    std::uint8_t mx{}, mx_fraction{}, my{}, my_fraction{}; // $17-$1A: momentum (motion 3)
    std::uint8_t path_index{}, path_clock{};               // $1B/$1C
    std::uint8_t box_left{}, box_right{}, box_top{}, box_bottom{}; // $1D-$20
    std::uint8_t fire_clock{}, fire_angle{}, anim_clock{}; // $21-$23
    std::uint8_t frame{};         // $24: sprite
    std::uint8_t hit{};           // $25
    std::uint8_t spare26{}, spare27{};
    int entry{-1};                // $28/$29: its spawn list entry (to clear the "came" bit when it leaves)
    int entry_slot{};

    bool active() const { return y != 0; }
    std::uint16_t path() const { return std::uint16_t(path_lo | path_hi << 8); }

    // The template's numbering (offsets $00-$1C as the data uses them).
    std::uint8_t& field(int offset);

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(x, x_fraction, y, y_fraction, dx, dx_fraction, dy, dy_fraction, motion, acceleration, path_lo,
           path_hi, path_period, landscape, fire, fire_period, kind, animation, anim_a, anim_b, anim_c, state,
           score, mx, mx_fraction, my, my_fraction, path_index, path_clock, box_left, box_right, box_top,
           box_bottom, fire_clock, fire_angle, anim_clock, frame, hit, spare26, spare27, entry, entry_slot);
    }
};

// A shot drawn as a character into the screen ($0480 + 12 * n for the
// wiz's and the catellite's, $0300 + 16 * n for enemy shots and debris).
struct PlayerShot {
    std::uint8_t column{}, row{};
    std::int8_t dx{}, dy{};
    std::uint8_t code{};          // 0: free
    std::uint8_t colour{};
    std::uint8_t saved{};         // the cell's code before the shot
    std::uint8_t second_code{};   // a double shot's right half (0: none)
    std::uint8_t second_saved{};
    int drawn_column{-1}, drawn_row{}; // where it was drawn ($0487-$0489)
    int drawn_screen{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(column, row, dx, dy, code, colour, saved, second_code, second_saved, drawn_column, drawn_row,
           drawn_screen);
    }
};

struct EnemyShot {
    std::uint8_t column{}, row{};
    std::uint8_t sub_x{}, sub_y{}; // 0-31 within the cell
    std::int8_t dx{}, dy{};
    std::uint8_t saved{};
    std::uint8_t state{};          // 0 free, 2 just fired, 1 flying
    std::uint8_t clock{};
    int drawn_column{-1}, drawn_row{}, drawn_screen{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(column, row, sub_x, sub_y, dx, dy, saved, state, clock, drawn_column, drawn_row, drawn_screen);
    }
};

} // namespace wizball::game
