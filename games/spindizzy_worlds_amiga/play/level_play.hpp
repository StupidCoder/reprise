#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/play/explorer.hpp"
#include "spindizzy_worlds_amiga/play/surfaces.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include <cstdint>

// One level being played: the explorer's states as object kinds (the
// explorer $12B7C, lost −2 $12D96, arriving −3 $12AC6) around the movement
// rules of play/explorer. The level's objects do not act yet, and energy
// ($206C: deaths, respawns, the timer) is not kept yet.
namespace spindizzy::play {

enum class ExplorerPhase { arriving, playing, lost };

// An object's animation-list stepper ($10058): +$1E counts the current
// entry's ticks down, +$1F is the entry; a new list starts with the count
// at 1, so its first entry is passed at once.
struct ListStepper {
    int count{1};
    int entry{};
    // One drawn frame (slow mode: one count per frame); the frame offset.
    std::uint32_t advance(const data::Memory& main, std::uint32_t list);
};

class LevelPlay {
public:
    LevelPlay(const data::Assets&, const SurfaceTable&, int world, int level);

    // One rules tick ($11510 in slow mode: one per frame).
    void tick(std::uint8_t joystick);
    // The space bar ($11314): stops dead when allowed.
    void brake();

    const world::Level& level() const { return level_; }
    const world::Switches& switches() const { return switches_; }
    std::uint16_t level_flags() const { return level_flags_; }
    const Explorer& explorer() const { return explorer_; }
    ExplorerPhase phase() const { return phase_; }

    // The explorer's frame address this frame (call once per drawn frame:
    // it steps the animations like the original's frame handlers).
    std::uint32_t explorer_frame(const data::Memory& main);

private:
    void arrive();

    const SurfaceTable& surfaces_;
    world::Switches switches_;
    world::Level level_;
    std::uint16_t level_flags_{};
    Explorer explorer_;
    ExplorerPhase phase_{ExplorerPhase::arriving};
    int phase_ticks_{};  // +$2C of the lost/arriving kinds
    ListStepper stepper_;
    int spin_{};         // +$1E of the explorer kind
    bool flash_{};       // +$21 bit 0
};

} // namespace spindizzy::play
