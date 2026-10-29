#pragma once
// Ice skating (puzzle type 27, class lt2qSkateObject).
// ARM9 overlay 49: ROM 0xba000 (+0xb44 bytes), loaded at 020f78a0, vtable 020f8844.
//
// How the puzzle plays
// --------------------
// The protagonist stands at the edge of a sheet of ice divided into 16-pixel
// cells, with walls along some grid lines. Arrows around the protagonist
// show the directions available. Tapping an arrow sends the protagonist
// sliding in that direction, cell after cell, until a wall or the edge of
// the ice stops the slide; there is no stopping halfway. The goal is to
// slide out through the exit cell. Reaching it solves the puzzle immediately; there is
// no "Submit" (virtual function +220 returns 0).
//
// The parameter script gives the ice area, the start and exit cells
// (command 0x63) and the walls (0x64, horizontal or vertical segments on grid
// lines). The start and exit lie just outside the ice.
//
// A tap counts when the stylus is pressed on an arrow and released without
// leaving it. The sprite then glides 2 pixels per frame to the new cell.
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace picarat {

class SkatePuzzle {
public:
    // Sound effects (020f7c84): 3 (push off) and the looping slide sound
    // 0x122 when sliding starts; 0x122 stops on arrival and on reset.
    EffectQueue sounds;

    explicit SkatePuzzle(std::span<const std::uint8_t> script);

    // Back to the start (virtual function +204, 020f7a28).
    void reset();

    // One frame of input and movement (020f7c84).
    void update(const TouchFilter& touch);

    // Slid out through the exit (virtual function +1fc, 020f7b00).
    bool solved() const { return state_ == kOut; }

    // The common puzzle buttons only work while idle (020f7b14).
    bool commonInput() const { return state_ == kIdle; }

    // The protagonist's sprite animation (020f83dc): nullptr selects the idle
    // animation, index 1 of iceskate_gfx.arc (its ROM label is the
    // protagonist's name); otherwise the push-off label of the chosen arrow.
    const char* protagonistAnimation() const { return animation_; }

    // The protagonist's sprite position.
    int x() const { return pixelX_; }

    int y() const { return pixelY_; }

    // An arrow sprite to draw. Arrows are shown while idle and while an
    // arrow is pressed (the pressed one shifted by one pixel).
    struct Arrow {
        const char* animation;
        int x, y;
    };

    std::vector<Arrow> arrows() const;

private:
    // A wall on a grid line (x0 == x1: vertical, y0 == y1: horizontal).
    struct Wall {
        std::int16_t x0, y0, x1, y1;
    };

    // States (+0x78).
    static constexpr int kIdle = 0;
    static constexpr int kPressing = 1; // stylus down on an arrow
    static constexpr int kSliding = 2;  // the sprite glides to the new cell
    static constexpr int kOut = 3;      // left through the exit

    // Directions (+0x84), also indices into the step table.
    static constexpr int kRight = 0;
    static constexpr int kDown = 1;
    static constexpr int kLeft = 2;
    static constexpr int kUp = 3;

    // Arrow flags (+0x90): which directions are open, and "at the exit".
    static constexpr std::uint32_t kCanRight = 1;
    static constexpr std::uint32_t kCanDown = 2;
    static constexpr std::uint32_t kCanLeft = 4;
    static constexpr std::uint32_t kCanUp = 8;
    static constexpr std::uint32_t kAtExit = 0x10;

    std::int16_t originX_ = 0, originY_ = 0; // +0x68: top-left of the ice
    std::int16_t columns_ = 0, rows_ = 0;
    std::int16_t startX_ = 0, startY_ = 0; // +0x70: start cell
    std::int16_t exitX_ = 0, exitY_ = 0;   // exit cell
    std::vector<Wall> walls_;              // +0x94: at most 32 (count at +0x194)
    int state_ = kIdle;
    int direction_ = -1;                   // chosen direction, -1 = none
    std::int16_t cellX_ = 0, cellY_ = 0;   // +0x88: the protagonist's cell
    std::int16_t pixelX_ = 0, pixelY_ = 0; // +0x8c: the sprite position
    std::uint32_t flags_ = 0;              // +0x90
    const char* animation_ = nullptr;      // sprite +0x198 animation (02075dd4)

    // Cell to pixel (020f7b68 / 020f7b74) and pixel to cell (020f7b80 / 020f7ba0).
    int pixel(int cell, int origin) const { return origin + cell * 16; }

    static int cell(int pixel, int origin);
};

} // namespace picarat
