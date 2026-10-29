#pragma once
#include "sonic_advance_gba/game/scheduler.hpp"
#include "sonic_advance_gba/game/tile_copy.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <array>
#include <vector>

// Pictures loaded in one piece for the menus and cut scenes: tiles, a map
// and colours copied straight from the cartridge (08052878), with their
// copies' records taken from a small pool kept by a process that holds the
// screens' graphics (080535C8 creates it once; its update does nothing).
namespace sonic_advance_gba {

class Game;

// The process (task 080536A0, priority 1800, 0x34C bytes).
class ScreenGraphics final : public Process {
public:
    explicit ScreenGraphics(Game& g);
    void on_destroy() override;

    void update(Game&) override {}

    // A loaded picture's record (+00, 12 bytes each).
    struct Slot {
        u32 first_tile{}; // sprite pictures: the tile number of their video memory
        u32 key{};        // copied from the load request
        std::array<u8, 4> extra{};
    };

    std::array<Slot, 30> slots{};

    // 08053640: all copy records free again.
    void reset_pool() { used_ = 0; }

    // 0805367C: a free copy record, or null.
    TileCopy* take();

private:
    Game& game_;
    // +16C: 16-byte nodes, each a link and a copy record; 15 are linked at
    // a reset, a 16th follows whose link is whatever the memory held.
    std::array<TileCopy, 16> pool_;
    unsigned used_{};
};

// 080535C8: creates the process unless it exists, and frees its pool.
void ensure_screen_graphics(Game& g);

// The load request (the original's 0x30-byte record on the stack).
struct GraphicsLoad {
    u32 key{};                 // +04
    std::array<u8, 4> extra{}; // +08
    u32 tiles_to{};            // +0C sprite pictures: video memory offset of their tiles
    u32 map_from{};            // +10
    u32 tiles_from{};          // +14
    u32 tile_bytes{};          // +18
    u32 map_bytes{};           // +1C
    u32 colors_from{};         // +20
    u32 color_bytes{};         // +24
    u8 palette{};              // +28 the first 16-colour palette
    u8 layer{};                // +29 background pictures: the layer
    u8 parts{};                // +2A load:: bits
    u8 slot{};                 // +2B
    u8 options{};              // +2C bit 7: tiles at the start of the layer's tile block
};

namespace load {
inline constexpr u8 tiles = 0x01, colors = 0x04, sprite = 0x08, map = 0x10;
} // namespace load

// 08052878
void load_graphics(Game& g, const GraphicsLoad& l);
// 08053594: sets a slot's glyph layout (key and extra) without loading.
void set_slot_layout(Game& g, u8 slot, u32 key, std::array<u8, 4> extra);

// A line of sprite text: glyphs of a loaded sprite picture (its slot: the
// first tile, the tiles per glyph in `key`, and in `extra` the entries'
// size and shape and the glyph's width and height in 8-pixel units), one
// sprite table entry per character (code - 20h is the glyph).
struct SpriteText {
    u16 layer{};       // +08 the sprite table layer times 8
    s16 x{};           // +0A
    s16 y{};           // +0C (the low byte is used)
    s16 count{};       // +0E characters
    u16 slot{};        // +10
    u16 palette{};     // +12
    u16 rows = 1;      // +14 repeated rows (when not single_row)
    bool single_row{}; // +16
};

// 08052F44 (semi-transparent) and 08053098 (opaque).
void draw_text(Game& g, const u8* chars, const SpriteText& t, bool semi_transparent);
// 08052E0C: each character as a rotated, double-size entry using rotation
// set `rotation_set`, `spacing` 32nds of a pixel apart.
void draw_text_rotated(Game& g, const u8* chars, const SpriteText& t, u8 rotation_set,
                       s16 spacing = 0);
// 080531E8: opaque, and the sprite table layer moves one step back every
// eight characters (the layer field counts in eighths).
void draw_text_layered(Game& g, const u8* chars, const SpriteText& t);
// 08052C50: one row of half-transparent glyphs, scaled and turned with
// rotation set `rotation_set` (double size) unless the scale is 1 and the
// angle 0.
void draw_text_scaled(Game& g, const u8* chars, const SpriteText& t, s16 scale_x, s16 scale_y,
                      u16 angle, u8 rotation_set);
// 0805333C (the staff roll): as draw_text_layered, but spaces put no entry,
// and lowercase letters (61h on) take glyph code - 11h one row of 8 pixels
// lower.
void draw_credits_text(Game& g, const u8* chars, const SpriteText& t);

} // namespace sonic_advance_gba
