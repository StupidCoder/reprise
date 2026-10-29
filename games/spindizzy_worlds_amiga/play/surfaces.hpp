#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include <array>
#include <cstdint>

// What the top of each block type does to the explorer. The original
// keeps a table of 256 routine offsets ($12FE6, relative to $12F7A): each
// routine has a height entry (4 bytes before it, used by the wall test)
// and a contact entry (landing, sliding, switching, killing). The port
// reads that table and the friction table ($12E0A) from the game's
// program and maps each routine to one of the kinds below; a release
// whose table names an unknown routine is refused.
//
// Heights are words in the explorer's z units (256 per cell). `base` is
// the found cell's level << 8; u and v are the explorer's position inside
// the cell (0-255), turned to the block's orientation (block & 3).
namespace spindizzy::play {

enum class Surface : std::uint8_t {
    none,          // $12FE4: no top (empty cell, $22/$23, $40/$80/$C0)
    lost,          // $1320E: swallows the explorer (kind −2, then respawn), no clue
    switch_pad,    // $13228: sets the push-button state $3616 when stepped on
    slippery,      // $1327E: lands without traction (no steering, no friction)
    flat,          // $13298 and the raised variants $132B8 … $13302
    slope,         // $13312: one cell rise along −u
    steep_slope,   // $1335E: two cells rise along −u
    corner_ramp,   // $13394: flat one cell up while u + v < 256, then falls away diagonally
    diagonal_ramp, // $133E2: falls diagonally over two cells (511 − u − v)
    corner_slope,  // $133F8: falls diagonally while u + v < 256, flat beyond
    valley,        // $13464: 255 − min(u, v)
    ridge,         // $13486: 255 − max(u, v)
    ice,           // $1349A: kills when standing still or after 250 ticks (clues 7 and 10 call it ice)
    bouncy,        // $13504: throws the explorer back up
    conveyor,      // $1354C: pushes 2 units per tick along the block's direction
    deadly,        // $13584: kills on touch
    lift_top,      // $1359C: a lift's platform (3C …): the lift object holds the explorer on it;
                   // touched without that, no landing, and the second tick kills
};

struct SurfaceTable {
    std::array<Surface, 256> kind{};
    std::array<std::int16_t, 256> raise{};   // flat tops above the cell's level ($132AE …)
    std::array<std::int8_t, 256> friction{}; // per block, from $12E0A
    std::array<std::int32_t, 4> conveyor_x{}, conveyor_y{}; // 16.16 pushes per orientation ($13570)
};

SurfaceTable read_surfaces(const data::Memory& main);

// The surface height at (u, v), in the wall test's form (the routines'
// height entries; `base` keeps its high byte where they replace the low one).
int surface_height(const SurfaceTable& table, int block, int base, int u, int v);

} // namespace spindizzy::play
