#pragma once
#include "cannon_fodder_amiga/data/assets.hpp"
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/world.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Soldiers walking (.analysis/cannon_fodder_amiga/game.md "Game logic").
//
// A soldier of the selected squad follows his squad's route: a list of
// waypoints that clicks on the play area append to. He heads for his next
// waypoint, turns his facing towards it every second tick, steps by his
// speed, reacts to the terrain under his feet, waits behind the soldier
// ahead of him, and takes the next waypoint on arrival. Idle soldiers face
// the mouse pointer. Reproduces the soldier handler $96730's walking path
// ($96960…$96BC6) and the routines it calls, and the fire branch ($968B2:
// bullets; the shot itself is in combat.hpp). The other branches (being hit,
// grenades and rockets, vehicles, squads other than the selected one,
// mission end) come with the next steps and are reported as not handled.
namespace fodder::game {

class Terrain {
public:
    // The map, its two tile sets' attribute files (.hit, 2 bytes per tile;
    // .bht, 8 bytes per tile) and the program's blocking table ($A2340).
    Terrain(const data::MissionMap& map, std::span<const std::uint8_t> base_hit, std::span<const std::uint8_t> sub_hit,
            std::span<const std::uint8_t> base_bht, std::span<const std::uint8_t> sub_bht,
            std::span<const std::uint8_t> program_file);

    struct Probe {
        bool outside{};   // negative coordinates: d0 = -1
        bool blocking{};  // $A2340[attribute] != 0
        int attribute{};  // 0-15
    };
    // $A22CE: the point (x + dx, y + dy).
    Probe probe(int x, int y, int dx, int dy) const;
    // $A1260: whether the tiles between two points block the view (whole
    // tiles of a blocking kind; the first two steps and every other step
    // after them are not looked at, as in the original).
    bool sight_blocked(int x0, int y0, int x1, int y1) const;
    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_{}, height_{}, map_width_tiles_{};
    std::vector<std::uint16_t> cells_;
    std::vector<std::uint16_t> attributes_; // per tile, base then sub
    std::vector<std::uint8_t> masks_;       // 8 bytes per tile
    std::array<std::uint8_t, 16> blocking_{};
};

// One soldier's update for the tick, for slots of kind 0. Returns false when
// the soldier is in a situation the rebuild does not handle yet.
bool update_soldier(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);

// One enemy soldier's update ($9DADE, kind 5): it looks for one of the
// recruits in range and in sight, closes in and fires; without a target it
// stands and looks around. Same return as update_soldier.
bool update_enemy(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);

} // namespace fodder::game
