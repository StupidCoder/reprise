#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The race screen's artwork: the cockpit picture around the view, the
// overlay items cut from it and from the program's sprite sheet (wheels,
// engine, crane chains, lamps, damage marks, flames, smoke, tyre treads),
// and the dashboard font (game.md, "Race screen").
namespace stuntcar::data {

// An overlay's pixels are race colour indices; the tyre treads, shown by
// hardware sprites, use 17-19 (see sprite_colours); `clear` is see-through.
struct Overlay {
    static constexpr std::uint8_t clear = 0xFF;
    int width{}, height{};
    std::vector<std::uint8_t> pixels;
    // Where the game draws it unless it moves it (screen pixels).
    int x{}, y{};
};

// The overlay items by the game's own numbers.
enum class Item : int {
    right_wheel = 0x00,   // three steering frames, 0x00-0x02
    left_wheel = 0x03,    // three steering frames, 0x03-0x05
    left_flame = 0x06,    // boost flames: 0x06, 0x07, 0x31
    right_flame = 0x08,   // 0x08, 0x09, 0x32
    engine = 0x0A,
    left_sill = 0x0B,     // below the view's lower corners
    right_sill = 0x0C,
    left_corner = 0x0D,   // the window's rounded top corners
    right_corner = 0x0E,
    flag_lamp_on = 0x0F,
    flag_lamp_off = 0x10,
    right_lamp_on = 0x11,
    right_lamp_off = 0x12,
    chain_left_upper = 0x13,
    chain_left_lower = 0x14,
    chain_right_upper = 0x15,
    chain_right_lower = 0x16,
    damage_mark = 0x17,   // hole, cracked, whole: 0x17/0x19/0x1B (+1: odd x)
    smoke = 0x1D,         // eight puffs, 0x1D-0x24
    tread = 0x25,         // tyre treads: + steering frame (right 0-2,
                          // left 3-5), + 6 for a raised wheel
    flame_third_left = 0x31,
    flame_third_right = 0x32,
    message_box = 0x33,
};

struct CockpitArt {
    int width{}, height{};
    std::vector<std::uint8_t> picture;           // the cockpit, colour indices
    std::array<Colour, 32> palette{};            // race colours + sprite colours
    std::vector<Overlay> items;                  // 0x00-0x33
    std::array<std::array<std::uint8_t, 8>, 96> font{}; // from ' ', one byte a row
    const Overlay& item(Item i, int offset = 0) const { return items.at(std::size_t(int(i) + offset)); }
};

// Throws std::runtime_error when the program does not hold what is expected.
CockpitArt load_cockpit_art(const Program&);

// A message shown in the view ("DROP", "START", "RACE WON"...), laid out as
// the game prints it: glyphs at screen pixels.
struct Message {
    struct Glyph {
        char c{};
        int x{}, y{};
    };
    std::vector<Glyph> glyphs;
};

// The game names a message by where it starts in its message table and how
// many 4-byte groups it has (as its callers do, e.g. 0x4C and 2 for DROP).
Message decode_message(const Program&, int start, int groups);

} // namespace stuntcar::data
