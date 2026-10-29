#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include <cstdint>
#include <optional>

// Which shape (animation frame) an object shows: the frame choice of the
// per-frame display update ($BEE4, handlers by type at $BF16). The original
// mixes three things there: choosing the frame, updating counters and
// deciding (turning, exploding, sounds). Only the first is here; it works
// on an object's state as it is after its update for the frame, and says
// "keep the current shape" where the original leaves the shape alone.
//
// Frames are shapes in the main data: base + index x frame size, both from
// the handlers' code; the index tables are the program's ($BD9A-$BED4).
namespace babel::game {

// The record fields the frame choice reads (the original's +$0C heading,
// +$0E state, its low byte +$0F, +$10 progress of a move, +$1A, +$1C, +$1E
// counters).
struct AnimationState {
    int type{};
    int heading{};
    int state{};
    int progress{};
    int counter_1a{}, counter_1c{}, counter_1e{};
    bool ride_reversed{}; // a spider's lift ride turned back this frame ($1218)
};

// Values the handlers read from outside the object.
struct AnimationClock {
    int frame{};          // $FBE: frames since the tower started
    bool running{true};   // $11E4 >= 0 (the game has started)
    int switch_phase{};   // $1212 and $1214: the forcers (types $11, $12) run with these
    int switch_state{};
};

class Animation {
public:
    explicit Animation(const data::Memory& program) : program_(program) {}

    // `now` is the object after this frame's update, `before` as it was
    // before it (the Klondike, Freezer and Wiper test their counters before
    // counting down); `spider` the object's place among the spiders (record
    // slot - 4). Returns the shape address, or nothing to keep the current one.
    std::optional<std::uint32_t> shape(const AnimationState& now, const AnimationState& before,
                                       const AnimationClock&, int spider) const;

    // The Watcher's other frames, which depend on its decisions ($C1FC):
    // turning (`count` frames of the turn left, negative for a right turn)
    // and standing with nowhere to go (by the frame counter).
    std::uint32_t watcher_turn(int state, int count) const;
    std::uint32_t watcher_idle(int state, int frame) const;
    // The rotating objects' turn by the frame counter: a Rotating Zapper
    // fires on multiples of 8, in direction value / 8 ($BE0A).
    int rotation(int frame) const { return table(0xBE0A, frame & 63); }

private:
    int table(std::uint32_t address, int index) const { return std::int8_t(program_.u8(address + std::uint32_t(index))); }
    const data::Memory& program_;
};

} // namespace babel::game
