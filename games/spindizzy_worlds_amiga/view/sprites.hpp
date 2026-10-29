#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/tile_map.hpp"
#include <array>
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
// The value objects are drawn in order of each frame (+$18, sorted by
// $1440E, farthest first): the cells' x + y plus a quarter of z + $4000,
// as $128CE leaves it in d7.
int sort_value(const WorldPosition&);

struct SpritePlace {
    int x{}, y{};
    std::uint16_t key{};
    // Objects whose mask the original computes in 8-row bands with a key
    // that changes per band (the lift: −$100 per band from `band_top`).
    int key_step{};
    int band_top{};
    // Objects apply a mask made beforehand in 8-row bands, row 0 at a band
    // boundary, to their frame rows without the sprite's own y & 7 (only
    // the explorer's path, $101DA, corrects it): their occlusion is tested
    // this many rows away from the pixel. An original quirk, kept.
    int mask_dy{};
    int mask_dx{}; // likewise across (32-pixel creatures, see objects.cpp)
    // 32-pixel creatures ($1012E) key their first or third map column $200
    // nearer, by the parity of their heading: deltas for columns 0-2 from
    // place.x's column.
    std::array<int, 3> column_key{};

    std::uint16_t key_at(int map_x, int map_y) const {
        int k = key;
        if (key_step != 0) k += key_step * ((map_y >> 3) - (band_top >> 3));
        const int column = (map_x >> 4) - (x >> 4);
        if (column >= 0 && column < 3) k += column_key[std::size_t(column)];
        return std::uint16_t(k);
    }
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

// The frame $1490A draws: the spin step (counter bits 2-3, × $A0); while
// the explorer flashes ($2510(a5) set, e.g. at a level's start unless the
// header says "NO player flash") every other frame uses the set at +$280;
// in the states $24F5/$24F2 test, the set at +$C80.
std::uint32_t explorer_frame(int counter, bool flash_phase, bool special_state);

// The explorer arriving in a level (kind −3, $12B0C/$12B04): an animation
// list in main ($12AF2) over the same frames, step = +$1F.
constexpr std::uint32_t explorer_arrival_list = 0x12AF2;

// Whether a map pixel of a sprite with this key shows ($1281E): every
// layer nearer than the sprite (layer key above the sprite's) must let it
// through its mask; an opaque layer hides it.
bool sprite_visible(const TileMap&, const data::Graphics&, int map_x, int map_y, std::uint16_t key);

// Draws a frame at a place into a canvas that shows the map from (left, top).
void draw_sprite(Canvas&, const data::SpriteFrame&, const SpritePlace&, const TileMap&, const data::Graphics&,
                 int left, int top);

} // namespace spindizzy::view
