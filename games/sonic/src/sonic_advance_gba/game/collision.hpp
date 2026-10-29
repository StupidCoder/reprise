#pragma once
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/types.hpp"

// The act's solid ground, read from its stage data (084A6DA4[zone]):
// blocks of 12x12 tiles laid out in two layers, and for every tile an 8x8
// height map, an angle and two flag bits. The graphics use the same tiles.
//
// Heights: each of a tile's 8 bytes holds two signed nibbles; the high one
// is the floor height in that column (1-8 pixels solid from the bottom,
// negative: from the top, 8: full), the low one the wall width in that row.
namespace sonic_advance_gba {

struct StageMap {
    u32 heights{};                       // [0] 8 bytes per tile
    u32 angles{};                        // [1] 1 byte per tile
    u32 blocks{};                        // [2] 12x12 map entries (16 bits) per block
    u32 layouts[2]{};                    // [3], [4] block numbers (bytes), one layout per layer
    u32 tile_flags{};                    // [5] 2 bits per tile
    u16 layout_width{}, layout_height{}; // +18, +1A in blocks
    u16 width{}, height{};               // +1C, +1E in pixels

    static StageMap load(const Assets& a, u32 table);
};

// 08040080: the map entry (tile number, flips) at pixel (x, y) of a layer.
u16 map_entry(const Assets& a, const StageMap& m, s32 x, s32 y, unsigned layer);

// 0803FF50: distance information of the floor/ceiling at column x, row y
// (a signed height within the tile: >0 solid from below, <0 from above,
// 8 full, 0 empty). `layer` bit 0 picks the layout, bit 7 makes tiles
// with flag bit 0 passable. Writes the tile's angle.
s32 floor_sensor(const Assets& a, const StageMap& m, s32 y, s32 x, u8 layer, u8& angle);
// 0803FE10: the same for walls at row y, column x.
s32 wall_sensor(const Assets& a, const StageMap& m, s32 x, s32 y, u8 layer, u8& angle);

enum class Sensor { floor, wall };

// 0803FA40: the distance from `pos` to the surface along an axis (`step`
// +8 or -8 pixels per tile; the other coordinate fixed), looking up to
// three tiles; negative inside the surface. Writes the surface's angle.
s32 probe(const Assets& a, const StageMap& m, s32 pos, s32 other, u8 layer, s32 step, u8* angle,
          Sensor s);
// 08040164: the same within one tile.
s32 probe_short(const Assets& a, const StageMap& m, s32 pos, s32 other, u8 layer, s32 step,
                u8* angle, Sensor s);

} // namespace sonic_advance_gba
