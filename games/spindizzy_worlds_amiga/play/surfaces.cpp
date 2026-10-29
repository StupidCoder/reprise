#include "spindizzy_worlds_amiga/play/surfaces.hpp"
#include <stdexcept>
#include <string>

namespace spindizzy::play {

namespace {

constexpr std::uint32_t routine_table = 0x12FE6;  // 256 words, offsets from $12F7A
constexpr std::uint32_t routine_origin = 0x12F7A;
constexpr std::uint32_t friction_table = 0x12E0A; // 256 signed bytes ($12F9E)
constexpr std::uint32_t conveyor_table = 0x13570; // 4 longs: x push (high word), y push (low word)

struct Routine {
    std::uint32_t address;
    Surface kind;
    int raise;
};

// The contact routines and what they do (traced in game.md, "Surfaces").
constexpr Routine routines[] = {
    {0x12FE4, Surface::none, 0},         {0x1320E, Surface::lost, 0},
    {0x13228, Surface::switch_pad, 0},   {0x1327E, Surface::ice, 0},
    {0x13298, Surface::flat, 0},         {0x132E2, Surface::flat, 0x40},
    {0x132F2, Surface::flat, 0x80},      {0x13302, Surface::flat, 0xC0},
    {0x132D4, Surface::flat, 0x100},     {0x132B8, Surface::flat, 0x180},
    {0x132C6, Surface::flat, 0x200},     {0x13312, Surface::slope, 0},
    {0x1335E, Surface::steep_slope, 0},  {0x13394, Surface::corner_ramp, 0},
    {0x133E2, Surface::diagonal_ramp, 0}, {0x133F8, Surface::corner_slope, 0},
    {0x13464, Surface::valley, 0},       {0x13486, Surface::ridge, 0},
    {0x1349A, Surface::hot, 0},          {0x13504, Surface::bouncy, 0},
    {0x1354C, Surface::conveyor, 0},     {0x13584, Surface::deadly, 0},
    {0x1359C, Surface::lift_top, 0},
};

// A byte replacing the low byte of `base` (move.b dN,d3).
int with_low(int base, int value) { return (base & ~0xFF) | (value & 0xFF); }

} // namespace

SurfaceTable read_surfaces(const data::Memory& main) {
    SurfaceTable table;
    for (int block = 0; block < 256; ++block) {
        const auto offset = std::int16_t(main.u16(routine_table + std::uint32_t(block) * 2));
        const auto address = std::uint32_t(std::int32_t(routine_origin) + offset);
        bool known = false;
        for (const auto& routine : routines) {
            if (routine.address != address) continue;
            table.kind[std::size_t(block)] = routine.kind;
            table.raise[std::size_t(block)] = std::int16_t(routine.raise);
            known = true;
        }
        if (!known) throw std::runtime_error("unknown surface routine for block " + std::to_string(block));
        table.friction[std::size_t(block)] = std::int8_t(main.u8(friction_table + std::uint32_t(block)));
    }
    for (int turn = 0; turn < 4; ++turn) {
        const auto push = main.u32(conveyor_table + std::uint32_t(turn) * 4);
        table.conveyor_x[std::size_t(turn)] = std::int32_t(push & 0xFFFF0000);
        table.conveyor_y[std::size_t(turn)] = std::int32_t(push << 16);
    }
    return table;
}

int surface_height(const SurfaceTable& table, int block, int base, int u, int v) {
    block &= 0xFF;
    u &= 0xFF;
    v &= 0xFF;
    const int sum = u + v; // add.b: a carry means the far half of the cell
    switch (table.kind[std::size_t(block)]) {
    case Surface::slope: return with_low(base, 255 - u);
    case Surface::steep_slope: return std::int16_t(base + 2 * (255 - u));
    case Surface::corner_ramp: return sum < 256 ? std::int16_t(base + 0x100) : with_low(base, 511 - sum);
    case Surface::diagonal_ramp: return sum < 256 ? with_low(base + 0x100, 255 - sum) : with_low(base, 511 - sum);
    case Surface::corner_slope: return sum < 256 ? with_low(base, 255 - sum) : base;
    case Surface::valley: return with_low(base, 255 - (v >= u ? u : v));
    case Surface::ridge: return with_low(base, 255 - (v < u ? u : v));
    default: return std::int16_t(base + table.raise[std::size_t(block)]);
    }
}

} // namespace spindizzy::play
