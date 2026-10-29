#pragma once
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <vector>

// Game Boy Advance video, shared by every GBA game in BlueStreak: what a game
// has set up to be shown (tile art, tile maps, sprites, colours and the
// picture's settings) and the renderer that makes the 240x160 picture from it.
//
// The GBA keeps this in video memory (96 KiB: background tiles and maps in
// the first 64 KiB, sprite tiles in the last 32 KiB), 512 colours, a table of
// 128 sprites and a set of display registers. Here each part has a type of
// its own; the games describe their pictures with it and never read it back
// as game state. Settings may change between lines (games do this from the
// horizontal blank for wavy water, gradients and split screens): the picture
// then carries a list of per-line settings.
//
// Semantics follow GBATEK (Martin Korth), "LCD Video Controller".
namespace gba {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

inline constexpr int screen_width = 240, screen_height = 160;

// One background layer.
struct Background {
    u8 priority{};   // 0 (in front) to 3
    u8 tile_block{}; // where its tiles start, in 16 KiB blocks
    bool mosaic{};
    bool colors_256{};          // 8 bits per pixel (one 256-colour palette) instead of 4
    u8 map_block{};             // where its map starts, in 2 KiB blocks
    bool wrap{};                // rotated layers: the map repeats instead of ending
    u8 size{};                  // 0-3: text 256x256, 512x256, 256x512, 512x512;
                                // rotated 128 << size pixels square
    u16 scroll_x{}, scroll_y{}; // text layers (9 bits are used)
    // Rotated layers (2 and 3 in modes 1 and 2): the picture-to-map matrix
    // (8.8 fixed point) and the map position of the picture's top left
    // corner (20.8 fixed point, 28 bits).
    s16 pa{256}, pb{}, pc{}, pd{256};
    s32 origin_x{}, origin_y{};

    // The control register's packed form (BGxCNT), as games and their data
    // write it.
    u16 control() const;
    void set_control(u16 value);
    bool operator==(const Background&) const = default;
};

// A rectangle of the picture (left/top inclusive, right/bottom exclusive;
// a left above its right wraps around).
struct Window {
    u8 left{}, right{}, top{}, bottom{};
    bool operator==(const Window&) const = default;
};

// Which parts are drawn in a window: bit 0-3 backgrounds, 4 sprites,
// 5 colour effects.
using LayerMask = u8;

// Colour effects (BLDCNT, BLDALPHA, BLDY).
struct Blend {
    u8 mode{};       // 0 none, 1 alpha blending, 2 brighter, 3 darker
    u8 first{};      // first target layers: bits 0-3 backgrounds, 4 sprites, 5 backdrop
    u8 second{};     // second target layers (alpha blending)
    u8 eva{}, evb{}; // alpha coefficients (0-16, larger values act as 16)
    u8 evy{};        // brightness coefficient

    u16 control() const;
    void set_control(u16 value);
    bool operator==(const Blend&) const = default;
};

// Everything the display registers decide.
struct Settings {
    u8 mode{};              // 0-5
    bool second_frame{};    // bitmap modes 4/5: show the second frame
    bool sprite_tiles_1d{}; // sprite tiles are numbered in a row (else a 32-tile wide sheet)
    bool forced_blank{};    // the picture is white
    std::array<bool, 4> bg_shown{};
    bool sprites_shown{};
    bool window0{}, window1{}, sprite_window{};
    std::array<Background, 4> bg{};
    std::array<Window, 2> windows{};
    std::array<LayerMask, 2> inside{}; // inside window 0, 1
    LayerMask outside{}, inside_sprite_window{};
    u8 mosaic_bg_x{}, mosaic_bg_y{}, mosaic_sprite_x{}, mosaic_sprite_y{}; // block sizes minus one
    Blend blend{};

    // DISPCNT's packed form.
    u16 control() const;
    void set_control(u16 value);
    bool operator==(const Settings&) const = default;
};

// One entry of the sprite table (OAM attributes 0-2, decoded).
struct Sprite {
    u8 y{};
    bool rotated{};     // uses rotation/scaling parameters
    bool hidden{};      // not rotated: not drawn
    bool double_size{}; // rotated: the drawing area is twice as large
    u8 mode{};          // 0 normal, 1 semi-transparent, 2 sprite window
    bool mosaic{};
    bool colors_256{};
    u8 shape{};      // 0 square, 1 wide, 2 tall
    u16 x{};         // 9 bits
    u8 parameters{}; // rotated: which of the 32 parameter sets
    bool flip_x{}, flip_y{};
    u8 size{};  // 0-3 (with the shape: 8x8 to 64x64)
    u16 tile{}; // 10 bits, in 32-byte units
    u8 priority{};
    u8 palette{}; // 16-colour sprites: palette 0-15

    std::array<u16, 3> attributes() const;
    void set_attributes(u16 a0, u16 a1, u16 a2);
    // Width and height in pixels.
    int width() const;
    int height() const;
};

// A sprite rotation/scaling parameter set (8.8 fixed point).
struct Rotation {
    s16 pa{256}, pb{}, pc{}, pd{256};
};

struct Display {
    // Video memory: background tiles and maps (0x00000-0x0FFFF; bitmap modes
    // use up to 0x13FFF), sprite tiles (0x10000-0x17FFF).
    static constexpr std::size_t memory_size = 0x18000;
    std::array<u8, memory_size> memory{};
    // 256 background colours, then 256 sprite colours (15-bit BGR).
    std::array<u16, 512> colors{};
    std::array<Sprite, 128> sprites{};
    std::array<Rotation, 32> rotations{};
    Settings settings{}; // in effect from the first line

    // Settings that take effect from a later line (sorted by line), and
    // colours likewise. Games set these from the horizontal blank of the
    // line before.
    struct SettingsChange {
        int line;
        Settings settings;
    };

    std::vector<SettingsChange> setting_changes;

    struct ColorChange {
        int line;
        std::array<u16, 512> colors;
    };

    std::vector<ColorChange> color_changes;

    u8* bytes(u32 offset) { return &memory[offset]; }

    u16 halfword(u32 offset) const { return u16(memory[offset] | (memory[offset + 1] << 8)); }

    void put_halfword(u32 offset, u16 value) {
        memory[offset] = u8(value);
        memory[offset + 1] = u8(value >> 8);
    }

    // The 240x160 picture.
    platform::Image render() const;
};

// A GBA colour (15-bit BGR) as an RGBA pixel.
platform::Color to_rgba(u16 color);

} // namespace gba
