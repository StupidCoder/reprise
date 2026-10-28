#pragma once
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/world.hpp"
#include <algorithm>
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
    // $A399C: the eight cries of a hit soldier.
    int cry(int index) const { return cries_[std::size_t(index & 7)]; }
    // $9C4F8: walking speed while firing, by facing steps away from the heading.
    int firing_speed(int steps) const { return firing_speed_[std::size_t(steps & 7)]; }
    // $955D0: a helicopter's rotor sound, one of eight at random each tick.
    int rotor_sound(int index) const { return rotor_[std::size_t(index & 7)]; }

private:
    std::vector<std::uint8_t> program_;
    std::array<std::int8_t, 16> turn_{};
    std::array<int, 8> firing_speed_{};
    std::array<int, 8> cries_{};
    std::array<int, 8> rotor_{};
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
    // $80532: what a homing missile can be aimed at (a helicopter's pointer).
    int homing(int kind) const { return word(0x80532, kind); }

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

// Explosions: the box's shrink and the explosion's drift per frame ($974AE,
// pairs), the blast points around an explosion ($AAF48, until $8000), and
// per landscape the tiles that only big blasts change ($A4B8C).
class BlastTables {
public:
    explicit BlastTables(std::span<const std::uint8_t> program_file);
    std::array<int, 2> step(int index) const { return steps_[std::size_t(std::clamp(index, 0, 13))]; }
    const std::vector<std::array<int, 2>>& points() const { return points_; }
    bool immune(int landscape, int tile) const;

private:
    std::array<std::array<int, 2>, 14> steps_{};
    std::vector<std::array<int, 2>> points_;
    std::array<std::vector<int>, 5> immune_;
};

// Per phase: the objectives (codes 1-8, $A4D8E), their names ($A4CDA) and
// the enemies' skill range ($A4F70: min, max).
class MissionTables {
public:
    explicit MissionTables(std::span<const std::uint8_t> program_file);
    std::vector<int> objectives(int phase) const;
    std::array<int, 2> skill_range(int phase) const;
    // $A4C1E: the phases of each of the 24 missions; true for a mission's
    // last phase ($80658 = 1 at $947EA).
    bool last_of_mission(int phase) const;
    int mission_phases(int mission) const { return mission >= 0 && mission < 24 ? mission_phases_[std::size_t(mission)] : 1; }
    // $A22A8: terrain kinds a civilian does not choose to walk to.
    bool civilian_avoids(int attribute) const { return avoid_[std::size_t(attribute & 15)]; }
    // $A22B8: terrain kinds that shelter a recruit from enemy vehicles.
    bool sheltered(int attribute) const { return shelter_[std::size_t(attribute & 15)]; }
    static constexpr int phases = 72;

private:
    std::vector<std::uint8_t> program_;
    std::array<bool, 16> avoid_{}, shelter_{};
    std::array<int, 24> mission_phases_{};
};

struct ProgramTables {
    explicit ProgramTables(std::span<const std::uint8_t> program_file)
        : geometry(program_file), soldiers(program_file), hits(program_file), fire(program_file), blasts(program_file),
          mission(program_file) {}
    GeometryTables geometry;
    SoldierTables soldiers;
    HitTables hits;
    FireTables fire;
    BlastTables blasts;
    MissionTables mission;
};

} // namespace fodder::game
