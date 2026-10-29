#pragma once
// Street lamps (puzzle type 31, class lt2qLampObject).
// ARM9 overlay 52: ROM 0xbc400 (+0xb1c bytes), loaded at 020f78a0, vtable 020f8684.
//
// How the puzzle plays
// --------------------
// The board shows streets along a 7x7 grid of crossing points. Tapping a
// point puts a street lamp there (or removes it). A lamp lights every street
// that passes through its point. The goal is to light every street using
// exactly the number of lamps the puzzle asks for. Streets may run
// horizontally, vertically or at 45 degrees; some points cannot hold a lamp.
//
// When a lamp is placed, the streets it lights glow briefly (fading out over
// 60 frames). When the player submits, every lit street is shown for 60
// frames before the judgment (virtual function +210).
//
// The parameter script supplies the required lamp count and glow colour
// (command 0x67), the streets (0x68) and blocked points (0x6f).
//
// If the answer was wrong and the player retries, the lamps stay where they
// were (virtual functions +22c/+230 save and restore them).
//
// Grid points are 24 pixels apart; point (0, 0) is at pixel (24, 24).
#include "graphics/a2_ds3d.hpp"
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace picarat {

class LampPuzzle {
public:
    // Sound effects: 0x11d a lamp is placed, 0x11e it is removed.
    EffectQueue sounds;

    // A street between two grid points (10-byte record at +0xaa).
    struct Street {
        bool lit = false;
        std::int8_t fade = 0; // glow frames left after a lamp was placed (60..0)
        std::int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };

    explicit LampPuzzle(std::span<const std::uint8_t> script);

    // Removes all lamps (virtual function +204, 020f79d4).
    void reset();

    // Retry after a wrong answer: a fresh puzzle state, but with the lamps
    // and lit streets restored (the +230 restore, 020f7c58).
    void retry();

    // One frame of input (020f7e94).
    void update(const TouchFilter& touch);

    // Advances the glow and builds this frame's street polygons (020f812c).
    void tick();

    // Virtual function +1fc (020f7a7c).
    bool solved() const;

    // Submit pressed: show all lit streets (virtual function +210, 020f7b10).
    // The puzzle scene runs the 60 frames before judging.
    void showAll() { state_ = kShowAll; }

    // Street polygons to draw on the 3D layer this frame.
    const Ds3dFrame& streets() const { return frame_; }

    // Top-left positions of the "lamp" sprites (020f812c).
    std::vector<std::array<int, 2>> lamps() const;

private:
    static constexpr int kPlaying = 0;
    static constexpr int kShowAll = 2;

    // Grid point contents (+0x74, 7x7): 0 free, 1 lamp, -1 blocked.
    std::array<std::int8_t, 49> cells_{};
    std::vector<Street> streets_;  // at most 32 (count at +0x1ea)
    std::int16_t required_ = 0;    // +0xa6: lamps the answer must use
    std::int16_t count_ = 0;       // +0xa8: lamps placed
    std::uint16_t color_ = 0xffff; // +0x72: glow colour, RGB555
    int state_ = kPlaying;         // +0x6c
    Ds3dFrame frame_;

    // Whether grid point (x, y) lies on the street (between its ends, inclusive).
    static bool on(int x, int y, const Street& s);
};

// Draws a thick line as two triangles (overlay 2, 020711a0), with the
// original's fixed-point arithmetic, hardware square root and divider
// rounding, at the line depth used by 02070fcc. Also used by other puzzles.
void addThickLine(Ds3dFrame& frame, int x0, int y0, int x1, int y1, std::uint16_t color, int width,
                  int alpha);

} // namespace picarat
