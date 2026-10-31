#pragma once
#include "fa_18_interceptor_amiga/game/autopilot.hpp"
#include "fa_18_interceptor_amiga/game/collisions.hpp"
#include "fa_18_interceptor_amiga/game/object_step.hpp"
#include "fa_18_interceptor_amiga/game/trigger_areas.hpp"
#include "fa_18_interceptor_amiga/game/weapons.hpp"
#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include <array>
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

// A smoke slot (`$13D474`, 16 of 32 bytes): a copy of the record's first
// 32 bytes when it blew up (`$11AB3E`), its type replaced by the smoke's
// shape (33 in the air, 34 / 35 on the ground) and a timer.
struct Smoke {
    int type{};                         // +2: $11-$13, drawn as object type 16 more (33-35, `$11608C`)
    int timer{};                        // +4: $7F, down a frame (`$116B82`); never freed in a flight
    std::array<std::int32_t, 3> at{};   // the record's place then (cells and fine +6 ... +$10: world units)
};

// The smoke trail behind the last aircraft hit (`$1450BA`: set with +2
// bit 4 when a record is hit - $11AA04, $11B364, $11B87C): its last six
// places (`$1450BC`, a ring: `$1450B9` the next slot, `$1450B8` how many),
// taken after its step every frame (`$11AD0A`, not while an outcome is
// awaited; while fewer than five the place goes in the next slot too).
// Burning (+2 bit 4) and moving, its length (+$3D) is the places less one,
// at most 4; else the length runs down and at 0 the ring starts over.
struct Trail {
    int record = -1;                                   // (none at a flight's start, $10D286)
    std::array<std::array<std::int32_t, 3>, 6> places{};
    int next{}, count{};
    void update(Aircraft& a);
    void reset() { *this = Trail{}; }
};

class ObjectWorld {
public:
    ObjectWorld(const FlightTables& tables, const ObjectLists& lists, const ObjectStep& step, const Autopilot& autopilot)
        : tables_(tables), lists_(lists), step_(step), autopilot_(autopilot), areas_(lists) {}

    // $11CF06 (and $10D37A): the records a flight starts with; the
    // counters, the areas and the smoke ($10D248) start over.
    void spawn(const ObjectChoice& choice);

    std::vector<WorldObject>& objects() { return objects_; }
    const std::vector<WorldObject>& objects() const { return objects_; }
    WorldObject* record(int number);
    const WorldObject* record(int number) const;
    bool active(int number) const;
    const std::optional<WorldObject>& pod() const { return pod_; }

    // $11AB3E: a record blown up leaves smoke in the first free slot (none
    // when all 16 are taken): 33 in the air, on the ground 35 or 34 by the
    // slot's number (even, odd). `at`: its place after the move that blew
    // it up (32-bit units).
    void add_smoke(const std::array<std::int32_t, 3>& at, bool ground);
    // $116B82, at the object step's start unless an outcome is awaited: the
    // slots' timers run down (past 0 too: the smoke is gone, the slot stays
    // taken).
    void age_smoke();
    const std::array<std::optional<Smoke>, 16>& smoke() const { return smoke_; }
    Trail& trail() { return trail_; }
    const Trail& trail() const { return trail_; }
    void set_smoke(const std::array<std::optional<Smoke>, 16>& slots) { smoke_ = slots; }

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
        bool flying = true;       // `$13ABBC` bit 6: clear, nothing moves (`$11A5FC`)
        bool ecm{};               // `$13ABA5` (key J): the enemies' launches twice as often
        bool launches = true;     // (research: off where a recording lacks the missile records)
        // The player's latest flare and chaff (`$13AB7C`, `$13AB7D`: their
        // projectile slots), 32-bit places: the decoys of missiles on the
        // player (Sidewinders the flare, AMRAAMs the chaff).
        std::optional<std::array<std::int32_t, 3>> flare, chaff;
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

    // The records whose explosion ended in the last step (`$11A3F8`: they
    // go without asking for new scene lists).
    const std::vector<int>& ended() const { return ended_; }

    // $1185BE ... the objective of mission 3-8 in a frame (`$13AA88` 0).
    // Missions 4 and 7 turn the view to record 4 on the way: the frame
    // before the outcome (`$13AA9E` at 1) the camera goes outside to it,
    // and with the outcome the view follows it (`$10EC3C`), the texts
    // coming `extra` frames later (`$13ABB0`).
    struct Outcome {
        int result{};          // 1 achieved, -2 failed, -3 Air Force One destroyed by the player, 0 not yet
        bool turn{};           // the camera turns outside to record 4 ...
        int turn_distance{};   // ... at this distance (`$13AB23`: -1 Air Force One, 3 the missile)
        bool view{};           // the view follows record 4 from now on
        int extra{};           // frames more before the texts
    };
    // `confirmation` is the radar's `$13AB5F` (mission 3).
    Outcome objective(int mission, const Aircraft& player, int level, int confirmation);

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
    // $1170A2 / $11767E: the missile record after an enemy aircraft (9,
    // 11, 13 after 8, 10, 12), free, takes a missile from it.
    void launch(WorldObject& missile, const Frame& frame);
    // $117E4A and the record step for a missile record (class 0); its
    // touch. Returns true when the radar's lock went.
    bool step_missile(WorldObject& o, const Frame& frame);
    // The missile records kept free after the enemy aircraft's.
    void add_missile_slots();

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
    std::array<std::optional<Smoke>, 16> smoke_;
    Trail trail_;
    std::vector<int> ended_;
};

} // namespace interceptor::game
