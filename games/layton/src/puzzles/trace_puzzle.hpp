#pragma once
// Tracing (puzzle types 6 and 34).
//   type 6:  class lt2qTraceObject, ARM9 overlay 34 (ROM 0xaa200 +0x610, vtable 020f8024)
//   type 34: class lt2qTraceOnlyObject, ARM9 overlay 54 (ROM 0xbd800 +0x72c, vtable 020f81c8)
//
// How the puzzles play
// --------------------
// The player draws one continuous line with the stylus on the left part of
// the lower screen (x < 192). Starting a new line erases the old one. The
// answer is checked when the player presses "Submit".
//
//  * Type 6 (a ring): the whole line must stay inside a ring around pixel
//    (95, 95), between 13 and 53 pixels from the centre, and must pass through
//    all four quarters around the centre.
//  * Type 34 (an enclosed area): the line is closed by joining its end to its
//    start, then the area containing a seed point is flood-filled. Points
//    listed by the script as "inside" must end up on the line or inside the
//    area, and "outside" points must not be enclosed or touched.
//
// The line is drawn with the shared ink services of overlay 2 into the text
// layer, and recorded in a 256x192 byte mask used for the checks.
//
// The first adventure's "Trace" engine is type 34 one version earlier: the
// same mask, points, fill and check (its ARM9 02059698..02059920). Its
// engine (a1_trace.cpp) has its own stylus handling and uses the
// mask through the calls marked below.
#include "puzzles/circle_input.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace picarat {

class TracePuzzle {
public:
    // `type` is 6 (ring) or 34 (enclosed area).
    TracePuzzle(std::span<const std::uint8_t> script, unsigned type);

    // An enclosed-area engine set up by calls (the first adventure).
    TracePuzzle();

    // Script commands (second adventure 0x19 / 0x1a / 0x1b, first 0x3f /
    // 0x40 / 0x41): the fill's seed and up to 10 inside and outside points,
    // all rounded down to even pixels.
    void setSeed(int x, int y);
    void addInside(int x, int y);
    void addOutside(int x, int y);

    // --- For an engine with its own stylus handling (first adventure).
    // Clears the mask (02059748).
    void clearMask();
    // Draws the first point of a line (overlay 2, 02071470; first adventure
    // 02010378).
    void dot(int x, int y);
    // Draws a segment; `maskOnly` records it without visible ink (overlay 2,
    // 020714dc; first adventure 0200fff4).
    void line(int x0, int y0, int x1, int y1, bool maskOnly);
    // Fills the enclosed area and checks the points (first adventure
    // 02059880 + 02059760).
    bool areaCorrect();

    // Erases the line (virtual function +204; ov34 020f7a6c / ov54 020f7c28).
    void reset();

    // One frame of input (ov34 020f7b50 / ov54 020f7d20).
    void update(const TouchFilter& touch);

    // The answer check (virtual function +1fc; ov34 020f7ab4 / ov54 020f7c6c).
    bool solved();

    // Ink drawn this frame, for the text layer (palette entry 0xd).
    std::vector<CircleInkCommand> takeInk() {
        auto out = std::move(ink_);
        ink_.clear();
        return out;
    }

    // True once after the line was erased, so the scene clears the text layer.
    bool clearInk() {
        const bool c = clear_;
        clear_ = false;
        return c;
    }

private:
    // Mask values.
    static constexpr std::uint8_t kEmpty = 0;
    static constexpr std::uint8_t kInk = 1;
    static constexpr std::uint8_t kFilled = 2;

    bool ring_;                      // type 6
    std::vector<std::uint8_t> mask_; // engine +0x00: 256x192, one byte per pixel
    // Script points (commands 0x1a and 0x1b; engine +0x04 / +0x54).
    std::vector<std::array<int, 2>> inside_, outside_;
    int seedX_ = 0, seedY_ = 0;     // +0x6c/+0x70: where the fill starts (command 0x19)
    bool valid_ = true;             // +0x94: false once the ring was left
    bool released_ = false;         // +0x95: a complete line exists
    std::uint32_t quadrants_ = 0;   // +0x98: quarters of the ring visited (bits 0..3)
    int startX_ = -1, startY_ = -1; // +0x84: first point of the line (-1 = not drawing)
    int lastX_ = -1, lastY_ = -1;   // last point drawn
    std::vector<CircleInkCommand> ink_;
    bool clear_ = false;

    // Marks a 2x2 block in the mask (overlay 2, 02070f70).
    void maskStamp(int x, int y);

    // Flood-fills the enclosed area from the seed point (ov54 020f7a88).
    void fill();
    // Outside points untouched, inside points on the line or filled.
    bool pointsCorrect() const;
};

} // namespace picarat
