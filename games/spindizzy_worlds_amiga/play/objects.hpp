#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/play/explorer.hpp"
#include "spindizzy_worlds_amiga/view/camera.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include "spindizzy_worlds_amiga/world/object_kind.hpp"
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

// The level objects' rules: each kind's create routine, tick and frame
// handler (the descriptors at $148C2: create, frame, rotate, tick). Objects
// tick in slot order after the explorer's own tick and before the rest of
// it ($11510: $14390 runs slots $258C upward; the explorer has slot 1, the
// level's objects the slots after it in the level's order).
//
// Rebuilt: jewels, lifts, exits, push buttons, binary switches, heads,
// secret passages, timers, messages and clues (every kind the Tutorial
// uses). Pac aliens, pyramids, boinks and spark generators stay as they
// start (drawn, inert); starfields and palettes have no tick.
//
// Evidence: games/spindizzy_worlds_amiga/game.md ("Objects").
namespace spindizzy::play {

// The world-wide state objects read and change ($2524 … $3620(a5)).
struct ObjectWorld {
    Explorer& explorer;
    world::LevelGrid& grid;                 // secret passages open cells
    world::Switches& switches;              // $3616-$3618
    std::uint32_t& jewels;                  // $2566: the level's jewels still to take (bit per jewel)
    const std::uint32_t* level_jewels;      // $2524: the same per level 1-16 (gated exits test them)
    int& jewels_taken;                      // $2565
    std::int32_t& fuel_change;              // $206C, 16.16 (taken into the fuel once a frame)
    std::int16_t& jewel_fuel;               // $2070: jewel fuel for the reserve
    std::uint16_t& timer;                   // $360E: seconds × 256 + fraction
    bool& timer_expired;                    // $2492
    int level{};                            // $251E
    std::uint16_t level_flags{};            // $24B8
    int rotation{};                         // $248B & 3
    bool running{true};                     // $2498: the level loop goes on
};

// Requests from objects to the game around the level.
struct ObjectEvents {
    // An exit taken ($150C2): another level, or a place in this one.
    struct Travel {
        int level;
        std::uint16_t place; // $24C0: bit 15 "use XYZ pos", 14 keep velocity, 13 z on the level, x/y/z cells
        bool reserve_fuel_message; // +$21 set: level flag bit 12 (shown by the exit's frame)
    };
    std::optional<Travel> travel;
    bool back_to_respawn{}; // exit flag bit 3 ($12C3E)
    int clue{-1};           // a clue to show once ($15FC0) or a message ($16028)
    bool clue_always{};     // the message's bit 7: shown again
    bool timer_death{};     // the timer elapsed with "Die if elapse" ($15374)
    std::optional<int> timer_travel; // the timer elapsed: this level (0 = the world ends)
};

// One object slot's run-time state.
struct LevelObject {
    world::ObjectKind kind{};
    std::size_t source{};        // index in the level's object list
    int slot{};                  // 2… (the explorer has slot 1)
    std::int16_t x{}, y{}, z{};  // +8, +$A, +$C (cell origin; levels × 256)
    std::int16_t speed{};        // +$16: a lift's or head's move this tick
    std::uint16_t word26{};      // +$26: condition (lifts, exits, heads), clue word
    std::uint16_t word2c{}, word2e{}, word30{}, word32{}, word34{};
    std::uint8_t byte20{}, byte21{};
    int count{};                 // +$1E: animation countdown (high byte) …
    int step{};                  // … and the entry (+$1F)
    bool alive{true};
};

class LevelObjects {
public:
    // Creates the level's objects ($127CA after the build; jewels the
    // level has lost already are not created, $13910). `first_visit`:
    // the level's jewel long had bit 31 set.
    LevelObjects(const world::Level&, const data::Memory& main, ObjectWorld&, bool first_visit, int& jewels_found);

    // One rules tick of every object in slot order ($14390).
    void tick(ObjectWorld&, ObjectEvents&);
    // The frame handlers' animation steps (once per drawn frame in slow
    // mode), only for sprites on the playfield ($1072A and the handlers'
    // own test across).
    void frame(const data::Memory& main, bool slow, const view::Camera&);
    // The secret passages' cells again after a rebuild ($143DE).
    void reapply(ObjectWorld&) const;

    // Every object in slot order (the explorer's slot is not in this list;
    // deleted objects stay, not alive).
    const std::vector<LevelObject>& objects() const { return objects_; }

private:
    void tick_jewel(LevelObject&, ObjectWorld&);
    void tick_lift(LevelObject&, ObjectWorld&);
    void tick_exit(LevelObject&, ObjectWorld&, ObjectEvents&);
    void tick_button(LevelObject&, ObjectWorld&);
    void tick_switch(LevelObject&, ObjectWorld&);
    void tick_heads(LevelObject&, ObjectWorld&);
    void tick_timer(LevelObject&, ObjectWorld&, ObjectEvents&);
    void tick_message(LevelObject&, ObjectEvents&);

    const data::Memory& main_;
    std::vector<LevelObject> objects_;
};

// Whether a condition word holds ($16266).
bool condition_holds(std::uint16_t condition, const world::Switches&, std::uint32_t jewels);

} // namespace spindizzy::play
