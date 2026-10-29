#pragma once
// Stepping stones (puzzle type 33, class lt2qBridgeObject).
// ARM9 overlay 53: ROM 0xbd000 (+0x7c8 bytes), loaded at 020f78a0, vtable 020f81c4.
//
// How the puzzle plays
// --------------------
// A path of 18 rows of stones, two side by side (one for each foot), runs
// across the lower screen from right to left; some stones are missing. The
// player walks across by tapping where the next footprint goes. Feet must
// alternate, and each step lands either one or three rows further on, never
// on a missing stone. The stones the player may step on next pulse gently.
// Tapping an earlier footprint goes back to it (later footprints vanish).
// Reaching the last row crosses the path; the puzzle judges itself 90 frames
// later (virtual function +220 returns 0, so there is no "Submit").
//
// Unlike most puzzles the board is fixed in the code (020f79a0): the
// script's 0x14 records go to an empty virtual function and are ignored.
//
// Layout: row r is drawn at x = (17 − r)·12 + 11, so row 0 is on the right;
// the right foot's stones are at y = 0x72, the left foot's 16 pixels lower.
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace picarat {

class BridgePuzzle {
public:
    // Sound effects: 0xf7 a new step, 0xf8 going back to an earlier footprint.
    EffectQueue sounds;

    BridgePuzzle() { reset(); }

    // Back to the start (virtual function +204, 020f79a0).
    void reset();

    // One frame of input (020f7c2c).
    void update(const TouchFilter& touch);

    // Across and 90 more frames have passed (virtual function +1fc, 020f7a30).
    bool complete() const { return state_ == kAcross && timer_ > 0x5a; }

    // The common puzzle buttons stop working once across (020f7a54).
    bool commonInput() const { return state_ != kAcross; }

    // One sprite to draw (020f7d44): a footprint or a pulsing candidate stone.
    struct Draw {
        const char* animation;
        int x, y;
        int alpha; // 31 = opaque
    };

    // The sprites for this frame. Also advances the pulse animation, as the
    // original drawing code does (phase kept in game state +82b0).
    std::vector<Draw> draws();

private:
    // Stone contents.
    static constexpr std::uint8_t kStone = 0;
    static constexpr std::uint8_t kHole = 1;
    static constexpr std::uint8_t kFootprint = 2;
    static constexpr std::uint8_t kNextStep = 3; // a stone the player may step on now

    static constexpr int kWalking = 0;
    static constexpr int kAcross = 1;

    // +0x6e: [row][foot], foot 0 = right foot, 1 = left foot.
    std::array<std::array<std::uint8_t, 2>, 18> cells_{};
    int state_ = kWalking;   // +0x68
    std::int16_t timer_ = 0; // +0x6c: frames since crossing
    std::int16_t row_ = 0;   // +0x92: row of the latest footprint
    float phase_ = 0;        // pulse phase (game state +0x82b0)

    // Recomputes the stones the player can step on next (020f7b20).
    void markNextSteps();
};

} // namespace picarat
