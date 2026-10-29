#include "tower_of_babel_amiga/game/animation.hpp"

namespace babel::game {
namespace {

constexpr std::uint32_t shapes = 0x10200; // the handlers add frame offsets to $10200

// Index tables in the program.
constexpr std::uint32_t walk_odd = 0xBD9A, walk_even = 0xBDBA, turn = 0xBDDA; // spiders
constexpr std::uint32_t sway = 0xBDFA;     // zappers and pushers (16 frames)
constexpr std::uint32_t rotate = 0xBE0A;   // rotating zappers and pushers (64)
constexpr std::uint32_t watcher_walk = 0xBE4A, watcher_turns = 0xBE6A;
constexpr std::uint32_t hop = 0xBE7A;
constexpr std::uint32_t convertor = 0xBEAC;
constexpr std::uint32_t worm = 0xBECC;
// Types are the kind table's first word ($13742): 0 the player's spiders,
// 1 Zapper, 2 Pusher, 3/4 rotating ones, 5 Worm, 6 Watcher, 7 Hopper,
// 8 Proximity Mine, 9 Time Bomb, $A-$C Block, Reflector, Prisms (static),
// $D Lizard, $E Klondike, $F Bonus Flag, $10 Convertor, $11/$12 Forcers,
// $13 Freezer, $14 Wiper, $15 Exchanger (static), $1A particles.
constexpr std::uint32_t wiper = 0xBED4;

// Spider frame sets ($BF4E: base offset and frame size per spider; the
// fourth repeats the third).
constexpr std::uint32_t spider_sets = 0xBF4E;

std::uint32_t frame(std::uint32_t base, int index, int size) { return shapes + base + std::uint32_t(index * size); }

} // namespace

std::optional<std::uint32_t> Animation::shape(const AnimationState& o, const AnimationState& before,
                                              const AnimationClock& clock, int spider) const {
    const int previous_counter_1c = before.counter_1c;
    const int low = o.state & 0xFF; // the original's +$0F
    switch (o.type) {
    case 0: { // the player's spiders ($BF6E)
        const std::uint32_t set = spider_sets + std::uint32_t(spider & 3) * 8;
        const std::uint32_t base = program_.u32(set);
        const int size = int(program_.u32(set + 4));
        if (o.progress == 0 || o.state >= 12 || (o.state >= 4 && o.state < 8)) return shapes + base;
        if (o.state < 4) { // walking
            const int at = (low & 2) ? o.progress : 32 - o.progress;
            return frame(base, table((low & 1) ? walk_odd : walk_even, at), size);
        }
        // riding a lift; a ride that turned back this frame shows a fixed frame
        if (o.ride_reversed) return frame(base, (6 - (o.state >> 1)) & 3, size);
        const int at = (low & 2) ? 32 - o.progress : o.progress;
        return frame(base, table(turn, at), size);
    }
    case 1: // Zapper ($C066): swaying by the frame counter
        if (!clock.running) return std::nullopt;
        return frame(0x3C1A0, o.heading * 5 + table(sway, clock.frame & 15), 0xBA);
    case 2: // Pusher ($C0B2)
        if (!clock.running) return std::nullopt;
        return frame(0x3D028, o.heading * 5 + table(sway, clock.frame & 15), 0xBF);
    case 3: // Rotating Zapper ($C0FE)
        if (!clock.running) return std::nullopt;
        return frame(0x3DF14, table(rotate, clock.frame & 63), 0xBA);
    case 4: // Rotating Pusher ($C14E)
        if (!clock.running) return std::nullopt;
        return frame(0x3F654, table(rotate, clock.frame & 63), 0xBF);
    case 5: // Worm ($C19A)
        if (!clock.running || o.state >= 4) return std::nullopt;
        return frame(0x40E34, table(worm, (o.progress & 3) + ((low & 1) ? 0 : 4)), 0xC1);
    case 6: // Watcher ($C1FC): its walk; its turns are chosen with its decisions
        if (!clock.running || o.state >= 4 || o.progress == 0) return std::nullopt;
        return frame(0x412BA, (o.state & 3) * 5 + table(watcher_walk, (o.progress >> 1) & 31), 0x113);
    case 7: { // Hopper ($C382)
        if (!clock.running) return std::nullopt;
        if (o.progress == 0) return shapes + 0x4351A;
        int at = o.progress & 15;
        if (!(low & 0x10)) {
            at += 16;
            if (!(low & 2)) at = 48 - at;
            if (!(low & 1)) at += 16;
        }
        return frame(0x4351A, table(hop, at), 0x14B);
    }
    case 8: // Proximity Mine ($C466): by the frame counter
        if (!clock.running) return std::nullopt;
        return frame(0x46788, clock.frame & 7, 0x14C);
    case 9: // Time Bomb ($C586): a new frame every 32 counts until 800
        if (!clock.running || o.counter_1c >= 0x320 || (o.counter_1c & 0x1F)) return std::nullopt;
        return frame(0x471E8, o.counter_1c >> 5, 0xC3);
    case 0xD: // Lizard ($C66E)
        if (!clock.running || o.state >= 4) return std::nullopt;
        return frame(0x4894A + ((low & 1) ? 0 : 0x888), o.progress & 7, 0x111);
    case 0xE: // Klondike ($C6F4): while +$1A counts down, frame 8 - (count & 7) before the step
        if (before.counter_1a == 0) return std::nullopt;
        return frame(0x49A5A, 8 - (before.counter_1a & 7), 0xC2);
    case 0xF: // Bonus Flag ($C74E)
        return frame(0x4A06A, clock.frame & 7, 0xF0);
    case 0x10: // Convertor ($C76C)
        return frame(0x4A7EA + ((low & 1) ? 0xA45 : 0), table(convertor, clock.frame & 31), 0xEF);
    case 0x11: // Forcer (Up) ($C7B4)
        if (!clock.running || clock.switch_state != 1) return std::nullopt;
        return frame(0x4BC74, clock.switch_phase & 7, 0x11D);
    case 0x12: // Forcer (Down) ($C7E4)
        if (!clock.running || clock.switch_state != -1) return std::nullopt;
        return frame(0x4C55C, clock.switch_phase & 7, 0x11D);
    case 0x13: { // Freezer ($C814)
        if (previous_counter_1c == 0) return std::nullopt;
        const int c = o.counter_1c;
        int index = 0;
        if (c != 0) {
            index = c % 3 + 1;
            if (c <= 0x40) index += 3;
        }
        return frame(0x4CE44, index, 0xAB);
    }
    case 0x14: // Wiper ($C868)
        if (!clock.running || previous_counter_1c == 0) return std::nullopt;
        return frame(0x4D2F2, table(wiper, o.counter_1c & 15), 0x16A);
    default:
        return std::nullopt; // static shapes ($BF4C) and particles ($1A)
    }
}

std::uint32_t Animation::watcher_turn(int state, int count) const {
    return frame(0x412BA, table(watcher_turns, (count + state * 4) & 15), 0x113);
}

std::uint32_t Animation::watcher_idle(int state, int frame_counter) const {
    return frame(0x412BA, (state & 3) * 5 + table(watcher_walk, (frame_counter >> 1) & 31), 0x113);
}

} // namespace babel::game
