#pragma once
#include <cstdint>
#include <vector>

// The demo's recording played back (menu item 1; `$10DFEE`, game.md "The
// demo"): each frame one control byte from `text/textply` for the player's
// aircraft and two words of key events from `text/textctl`, which go to
// the same key handlers as the keys (`$10D97C`). The key events run one
// record ahead of the control bytes; three $FF control bytes end the
// recording.
namespace interceptor::game {

// The key events' bits, in the order `$10D97C` tests them (the high byte's
// bits 0-7 first, then the low byte's).
namespace replay_key {
// The first word (`$13AC86`): the game's keys.
enum Game {
    gear,          // $10E966 (G)
    hook,          // $10E36A (A)
    fire,          // $10DF70 (space down)
    fire_up,       // $10DF80 (space up)
    weapon,        // $10E8CE (Return)
    shift_e,       // $10DE7A (Shift-E)
    radar_range,   // $10DF1C (R)
    target,        // $10DEF8 (T)
    speed_brake,   // $10E330 (\ or Backspace)
    flare,         // $10EE34 (F)
    chaff,         // $10EEC6 (C)
    ecm,           // $10EF46 (J)
    game_count
};
// The second word (`$13AC88`): the view keys (only played back).
enum View {
    above,         // $10ECE0 (M)
    outside,       // $10E3C0 (keypad Enter)
    tower,         // $10E416 (Shift with keypad Enter)
    nearer,        // $10E544
    pad_8,         // $10E64C
    pad_9,         // $10E4D4
    pad_6,         // $10E6B4
    pad_3,         // $10E4EE
    pad_2,         // $10E668
    pad_1,         // $10E700
    pad_4,         // $10E684
    pad_7,         // $10E6DE
    pad_5,         // $10E50A
    pad_point,     // $10E516
    pad_release,   // $10E8BA
    farther,       // $10E5E4
    view_count
};
} // namespace replay_key

class Replay {
public:
    Replay(const std::vector<std::uint8_t>& controls, const std::vector<std::uint8_t>& keys)
        : controls_(controls), keys_(keys) {}

    bool available() const { return !controls_.empty(); }
    // $10D532: from the first control byte and the second key record.
    void start() {
        control_ = 0;
        key_ = 1;
    }

    struct Frame {
        std::uint8_t control{};
        std::uint16_t game{}, view{}; // bit k: replay_key::Game / View k
        bool ended{};                 // the recording's end ($10EF08)
    };
    // `$10DFEE`: this frame's control byte and key events.
    Frame next();

private:
    std::uint8_t control_at(std::size_t i) const { return i < controls_.size() ? controls_[i] : 0; }
    std::uint16_t key_word(std::size_t record, int word) const;

    const std::vector<std::uint8_t>& controls_;
    const std::vector<std::uint8_t>& keys_;
    std::size_t control_ = 0, key_ = 1; // `$1450A4`, `$1450A0` (in records)
public:
    template <class A> void persist(A& ar) { ar(control_, key_); }
};

} // namespace interceptor::game
