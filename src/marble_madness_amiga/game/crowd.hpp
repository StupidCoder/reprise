#pragma once
#include "marble_madness_amiga/data/game_files.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

// The crowd race (not the original game): seven more marbles race the
// player, steered by the computer. Nothing here is read by the original
// rules; the marbles themselves race by them (race.cpp, marble_rules.cpp).
//
// The computer finds its way from the course's own data:
// - The Track's zone lines (header +8, read by $1130BE) split the course
//   into zones and say which zone lies on each side. Counting lines back
//   from the goal (zone -1) gives every zone its way out towards the goal.
// - The height map gives each 8 x 8 cell its floor and the walls and drops
//   at its sides ($10F390's side analysis). A marble can roll from a cell
//   to a neighbour when no wall stands in the way, the floor does not climb
//   too much and does not drop too far.
// - Per zone, a flood from the cells just past its way out gives every cell
//   a distance to go, dearer near an edge, so the marbles keep to the
//   middle. A marble heads for the cell a few steps down that distance.
namespace marble_madness::game {

class Terrain;

class CrowdMap {
public:
    static constexpr int grid = 160;       // cells per side, 8 x 8 pixels each
    static constexpr std::uint16_t none = 0xFFFF;

    CrowdMap(const data::GameFiles&, data::Course, const Terrain&);

    // The distance to go from a cell for a marble in `zone` (none: no way
    // known from there).
    std::uint16_t to_go(int zone, int cx, int cy) const;
    // The distance to go to the goal itself from a cell (none: no way).
    std::uint16_t to_goal(int cx, int cy) const {
        return cell(cx, cy) != nullptr ? goal_field_[std::size_t(cy * grid + cx)] : none;
    }
    // Zone lines between a zone and the goal (0 the goal, 99 unknown).
    int rank(int zone) const;
    // Whether a marble can roll from a cell to the neighbour in direction
    // d (0 +x, 1 -x, 2 +y, 3 -y).
    bool can_move(int cx, int cy, int d) const;
    // The floor's height in a cell (z >> 16), 0 where there is none.
    int floor(int cx, int cy) const;
    // How much room there is round a cell: floor at about its height in
    // the 5 x 5 cells round it (1-25; narrow ways have little).
    int room(int cx, int cy) const;
    // Where the crowd's marbles start (course pixels): three beside the
    // first player's start cell, four around the second player's (on
    // Aerial, the two launch platforms).
    const std::array<std::pair<int, int>, 7>& starts() const { return starts_; }
    // A run-up lane (Ultimate's jump over the gap into the funnel), if the
    // cell is in one: its start and a point past its end (course pixels),
    // and how far along it the cell is (cells).
    struct Lane {
        int from_x, from_y, to_x, to_y;
        double along;
    };
    std::optional<Lane> run_up(int cx, int cy) const;
    // If the cell is within 2 of a funnel's way in: its way out (course
    // pixels).
    // and the zone it is in there (-99: the same).
    struct Exit {
        int x, y, zone;
        int push_x, push_y; // the funnel's push out, pixels an update
        int alt_x, alt_y;   // its spot when the exit is taken (-1: none, a bridge)
    };
    std::optional<Exit> funnel_exit(int cx, int cy) const;
    // Whether the cell is next to a funnel's way in (not the bridge's: on
    // the screen the bridge must be crossed).
    bool near_way_in(int cx, int cy) const;
    // Whether the cell is within 3 of a funnel's (or bridge's) way out.
    bool near_exit(int cx, int cy) const;

private:
    struct Cell {
        std::int16_t floor{};
        std::uint8_t moves{}; // bit d: can roll to the neighbour in direction d
        bool flat{};          // no wall or drop at any side
        std::uint8_t room{};
    };
    const Cell* cell(int cx, int cy) const;
    std::vector<std::uint16_t> flood(const std::vector<std::pair<int, int>>& seeds) const;
    void find_starts(const data::GameFiles&, data::Course);

    std::vector<Cell> cells_;
    std::vector<bool> animated_; // moving ground filled in
    bool wave_{};                // the wave's channel is the way to go
    std::vector<std::uint8_t> extra_; // dearer steps (detours made less likely)
    std::map<int, std::vector<std::uint16_t>> fields_; // by zone
    std::map<int, int> ranks_;                         // by zone
    std::vector<std::uint16_t> goal_field_;            // zones without a way out
    std::array<std::pair<int, int>, 7> starts_{};
    // Funnels and lifts: the cell a marble goes in by and the cell it comes
    // out at (the floor does not show that way).
    struct Portal {
        int in_x, in_y, out_x, out_y;
        // A run-up: the lane's first cell (the map leads there); the lane
        // runs straight from it to the way in. None: from_x = -1.
        int from_x{-1}, from_y{-1};
        int zone_out{-99};
        bool bridge{};
        int push_x{}, push_y{};
        int alt_x{-1}, alt_y{-1};
    };
    std::vector<Portal> portals_;
};

// A crowd marble's driver.
struct CrowdDriver {
    std::int32_t top_speed{};     // its own pace (16.16 pixels an update)
    std::uint32_t seed{1};        // its own dice (not the game's generator)
    std::uint16_t best{CrowdMap::none}; // the least distance to go in this zone
    std::int8_t best_zone{};
    int stuck{};                  // updates without getting nearer
    int wiggle{};                 // updates of a push to come loose
    std::int16_t wiggle_forward{}, wiggle_side{};
    // The plan: a push for the next updates, or the usual steering.
    int plan_left{};
    bool plan_steer{true};
    std::int16_t plan_forward{}, plan_side{};
    // While a plan is tried on a copy of the race: the push tried.
    bool running{};               // on a run-up, flat out
    int backing{};                // updates spent going back to its start
    int trial_left{};
    // Getting nowhere for long with the plans (afraid of every way on):
    // the usual steering for a while, risks and all.
    double plan_best{1e18};
    int plan_stall{};
    int bold{};
    std::int16_t trial_forward{}, trial_side{};
};

} // namespace marble_madness::game
