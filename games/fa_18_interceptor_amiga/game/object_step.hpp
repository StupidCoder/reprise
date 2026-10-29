#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include <cstdint>
#include <vector>

// The object step's part for the other aircraft before their autopilot and
// flight step (`$11797E`, game.md "The missions' aircraft"): the timer,
// the speed it aims for (the throttle bits of its control byte), the
// distance to its point, and where the point is - a path's places one
// after the other, a point just behind the record it follows (or the
// player once near enough), its own list entry's place when that record
// is gone - and, now and then, an attack maneuver chosen by the level.
namespace interceptor::game {

// The records the step looks at: the player's (record 0) and the others.
struct Records {
    Aircraft* player{};
    std::int16_t player_distance{0x7FFF}; // the player's +$4A (to the locked record, `$1183E2`)
    std::vector<WorldObject>* objects{};

    Aircraft* craft(int record) const;
    std::int16_t distance(int record) const;
    bool active(int record) const;
};

// The game-wide state it reads.
struct StepWorld {
    std::uint8_t mode{};      // `$13AB96`
    std::int8_t level{};      // `$13AB97`
    int locked{-1};           // `$13ACB0`: the radar's locked record (-1: none)
    bool attack_now{};        // `$13AAA7` (cleared by the step)
    std::uint32_t list{};     // `$13AA2A`: the stage's first list entry
    int turned_hostile{};     // touchable records turned attackers (`$13AB99`, `$13AB9A` + 1 each)
};

class ObjectStep {
public:
    ObjectStep(const amiga::LoadedImage& program, const FlightTables& tables, const ObjectLists& lists);

    // `$11797E` for a record of class $10: true when it then flies
    // (autopilot and flight step); the others' classes are not built.
    bool before_flight(WorldObject& o, Records& records, StepWorld& world) const;
    // `$1183E2`, the player's step: now and then its distance to the
    // locked record (+$4A; the same counter and estimate as the others').
    void measure_player(Aircraft& player, std::int16_t& distance, const Aircraft* locked) const;

private:
    void aim_speed(WorldObject& o, const Records& records) const;        // $117A16
    void measure(WorldObject& o) const;                                  // $118468
    void follow(WorldObject& o, Records& records, StepWorld& world) const; // $117BA6
    void attack(WorldObject& o, const Aircraft& target, std::int16_t target_distance, StepWorld& world) const; // $1180A6

    const amiga::LoadedImage& program_;
    const FlightTables& tables_;
    const ObjectLists& lists_;
};

} // namespace interceptor::game
