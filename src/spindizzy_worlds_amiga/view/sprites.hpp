#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/tile_map.hpp"
#include <cstdint>

// Sprites in the playfield: where a world position appears on the map
// and how the blocks in front of it hide it.
namespace spindizzy::view {

// A world position in 1/256 of a cell (z: 1/256 of a block height), as the
// objects keep it (+8, +$A, +$C of an object record).
struct WorldPosition {
    int x{}, y{}, z{};
};

// Where a sprite is drawn: its top-left corner in map pixels and its depth
// key, compared with the tile map's layers.
struct SpritePlace {
    int x{}, y{};
    std::uint16_t key{};
};

// $128CE: x = $200 + (y − x)/16, y = $48 + (x + y − z)/32 (arithmetic
// shifts on 16-bit words); key = 2·(cell x + cell y) + z rounded down to a
// half block, + $4000, all in 1/256 units.
SpritePlace project(const WorldPosition&);

// The explorer's sprite place ($12DF4): its 16 × 16 frame's corner sits 8
// pixels left of and 16 above the projected point. (Objects use their own
// offsets, set by each kind's code.)
SpritePlace explorer_place(const WorldPosition&);

// The explorer's frames ($1490A): four spin steps of 160 bytes from
// $21240, the step = bits 2-3 of its animation counter.
constexpr std::uint32_t explorer_frames = 0x21240;
constexpr std::uint32_t explorer_frame_size = 0xA0;

// Whether a map pixel of a sprite with this key shows ($1281E): every
// layer nearer than the sprite (layer key above the sprite's) must let it
// through its mask; an opaque layer hides it.
bool sprite_visible(const TileMap&, const data::Graphics&, int map_x, int map_y, std::uint16_t key);

// Draws a frame at a place into a canvas that shows the map from (left, top).
void draw_sprite(Canvas&, const data::SpriteFrame&, const SpritePlace&, const TileMap&, const data::Graphics&,
                 int left, int top);

} // namespace spindizzy::view
