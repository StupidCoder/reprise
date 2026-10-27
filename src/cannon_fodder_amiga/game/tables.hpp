#pragma once
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/world.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Tables the game's rules read from the installed program file (`fodderc`),
// by their addresses in the running program.
namespace fodder::game {

// The facing turn steps ($955C0) and each side's pictures by facing and state
// ($8DBB2: words at +0 walking, +$30 standing, +$40 wading, +$50 throwing,
// +$60 (open), +$70 state 3).
class SoldierTables {
public:
    explicit SoldierTables(std::span<const std::uint8_t> program_file);
    int turn(int index) const { return turn_[std::size_t(index & 15)]; }
    int picture(int side, int offset) const;
    // $9C4F8: walking speed while firing, by facing steps away from the heading.
    int firing_speed(int steps) const { return firing_speed_[std::size_t(steps & 7)]; }

private:
    std::vector<std::uint8_t> program_;
    std::array<std::int8_t, 16> turn_{};
    std::array<int, 8> firing_speed_{};
};

// Per-kind hit data: whether a kind can be hit ($801C2: 1 solid, 2 killable)
// and its box ($8029E width, $8037A height above and $80456 below the position).
class HitTables {
public:
    explicit HitTables(std::span<const std::uint8_t> program_file);
    int hittable(int kind) const { return word(0x801c2, kind); }
    int width(int kind) const { return word(0x8029e, kind); }
    int above(int kind) const { return word(0x8037a, kind); }
    int below(int kind) const { return word(0x80456, kind); }

private:
    int word(std::uint32_t table, int kind) const;
    std::vector<std::uint8_t> program_;
};

// The weapon table by rank ($9E826, row min(rank + 8, 15)) and the squads'
// firing orders by size ($9E748: member indexes, -1 ends).
class FireTables {
public:
    explicit FireTables(std::span<const std::uint8_t> program_file);
    WeaponStats weapons(int rank) const;
    const std::vector<int>& firing_order(int members) const;

private:
    std::array<WeaponStats, 16> weapons_{};
    std::array<std::vector<int>, 8> orders_{};
};

struct ProgramTables {
    explicit ProgramTables(std::span<const std::uint8_t> program_file)
        : geometry(program_file), soldiers(program_file), hits(program_file), fire(program_file) {}
    GeometryTables geometry;
    SoldierTables soldiers;
    HitTables hits;
    FireTables fire;
};

} // namespace fodder::game
