#pragma once
#include "fa_18_interceptor_amiga/game/autopilot.hpp"
#include "fa_18_interceptor_amiga/game/collisions.hpp"
#include "fa_18_interceptor_amiga/game/object_step.hpp"
#include "fa_18_interceptor_amiga/game/trigger_areas.hpp"
#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace interceptor::data {
struct World;
}

// The world beside the player's aircraft (game.md "The missions' objects",
// "The trigger areas", "The missions' flow"): the object lists' records,
// what counts towards a mission's end (aircraft spawned, allowed,
// destroyed), the trigger areas, mission 6's rescue pod, and each
// mission's objective. The module drives it a frame at a time; the
// research tools build it from a recorded frame.
namespace interceptor::game {

class ObjectWorld {
public:
    ObjectWorld(const FlightTables& tables, const ObjectLists& lists, const ObjectStep& step, const Autopilot& autopilot)
        : tables_(tables), lists_(lists), step_(step), autopilot_(autopilot), areas_(lists) {}

    // $11CF06 (and $10D37A): the records a flight starts with; the
    // counters and the areas start over.
    void spawn(const ObjectChoice& choice);

    std::vector<WorldObject>& objects() { return objects_; }
    const std::vector<WorldObject>& objects() const { return objects_; }
    WorldObject* record(int number);
    bool active(int number) const;
    const std::optional<WorldObject>& pod() const { return pod_; }

    // $11D17A (every 16th frame, not in the demo): the player's cell
    // against the trigger areas; `draw` is a fresh `$13ADE8` for an area's
    // offsets.
    void trigger_areas(const Aircraft& player, int mode, int level, const std::function<int()>& draw);
    // $116BE4: the others' "near" bit lasts the frame of the touch.
    void clear_near();

    // What the object step reads besides the records.
    struct Frame {
        Aircraft* player{};
        std::int16_t player_distance{};
        int locked{-1};           // `$13ACB0`: the radar's locked record
        std::uint8_t mode{};      // `$13AB96`
        std::int8_t level{};      // `$13AB97`
        long counter{};           // `$13ABCA`
        const data::World* world{};
        // The bodies before the records in a touch (the player's first,
        // its missiles, the practice's lead).
        std::vector<Body> bodies;
    };
    // The object step for the lists' records in record order (`$116C4E`
    // ...), skipped while an outcome is awaited (`$13AA9E`); then crashed
    // records start to blow up ($1169D0). Returns true when the radar's
    // locked record was one of them (its lock goes).
    bool step(const Frame& frame);

    // $10EE76 (Shift-F in mission 6): the rescue pod drops from below the
    // player's aircraft (once).
    void drop_pod(const Aircraft& player);
    // $117C82: its fall and drift.
    void pod_step();

    // $1185BE ... the objective of mission 3-8 in a frame (`$13AA88` 0):
    // 1 achieved, -2 failed, -3 Air Force One destroyed by the player,
    // 0 not yet. `confirmation` is the radar's `$13AB5F` (mission 3).
    int objective(int mission, const Aircraft& player, int level, int confirmation);

    // An outcome being awaited (`$13AA9E`): the records stand.
    bool waiting() const { return outcome_wait_ != 0; }

    // The counters as the original keeps them (research tools set them
    // from a recording).
    struct Counters {
        int aircraft{};      // `$13AB99`: aircraft spawned that count towards the limit
        int aircraft_limit{}; // `$13AB9A`
        int destroyed{};     // `$13AB9B`: the list's aircraft destroyed
        int escort{};        // `$13AA72`: mission 5's frames beside the stolen aircraft
        int outcome_wait{};  // `$13AA9E`
        std::uint8_t inside{}; // `$13AA8D`: the trigger areas entered
        std::uint32_t list_start{}; // `$13AA2A`: the stage's first list entry
    };
    Counters counters() const;
    void set(const Counters& c, std::vector<WorldObject> objects, std::optional<WorldObject> pod = std::nullopt);

private:
    bool outcome_wait(int frames);

    const FlightTables& tables_;
    const ObjectLists& lists_;
    const ObjectStep& step_;
    const Autopilot& autopilot_;
    TriggerAreas areas_;
    std::vector<WorldObject> objects_;
    std::uint32_t list_start_ = 0;
    int aircraft_ = 0, aircraft_limit_ = 0, destroyed_ = 0;
    int escort_ = 0;
    int outcome_wait_ = 0;
    std::optional<WorldObject> pod_; // `$13ABB2`
};

} // namespace interceptor::game
