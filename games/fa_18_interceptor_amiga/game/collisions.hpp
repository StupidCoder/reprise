#pragma once
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The objects' effects on each other (`$11B6AA`'s object part, game.md
// "Objects touching"): after a record's move, every other active record
// near its position before the move is marked near (+2 bit 0); one inside
// the smaller hit box (also half a move back) is hit: a moving one is
// crashed (state bit 9; an aircraft spins, +2 bit 12, role 1), and the
// mover learns it hit something (the result $40). The sizes go by the two
// types. Ships' decks are the carrier's (carrier.hpp); scenery boxes are
// not built.
namespace interceptor::game {

// A record taking part: its aircraft data and its group (+$5E: records of
// one group - an aircraft and its missiles - never touch).
// (The list is in record order: a record touches only those after it.)
struct Body {
    Aircraft* aircraft{};
    std::uint8_t group{};
    bool active = true; // +1 bit 6
};

namespace touch {
constexpr int object = 0x40;
}

// The flight log's counts a touch adds to (`$11C0BA`).
struct TouchCount {
    int sidewinder_hits{}, amraam_hits{}, missile_hits_taken{};
};

// `$11B6A2`: the record `self`, which was at `before` (32-bit position)
// before this frame's move. Returns `touch::object` when it hit another
// record, else 0; `near` tells whether any record was within the larger
// box (`$13AB8F`).
int touch_bodies(std::vector<Body>& bodies, std::size_t self, const std::array<std::int32_t, 3>& before,
                 bool& near, TouchCount* count = nullptr);

} // namespace interceptor::game
