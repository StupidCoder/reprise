// Peg solitaire engine. See a2_peg_puzzle.hpp for the rules and the state machine.
#include "puzzles/a2_peg_puzzle.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace picarat {

PegPuzzle::PegPuzzle(std::span<const std::uint8_t> bytes) {
    // 020f78a0: start from the empty cross. A cell belongs to the board when it
    // lies in the middle three columns or the middle three rows.
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool onCross = std::abs(x - 3) <= 1 || std::abs(y - 3) <= 1;
            initial[cellIndex(x, y)] = onCross ? kEmptyHole : kNoHole;
        }
    }

    // The parameter script fills or empties rectangles of holes. Cells outside
    // the cross stay outside the board whatever the rectangle covers.
    ScriptContext context;
    context.bind(0x65, [&](CommandArguments& arguments) {
        // PegRectangle(x, y, width, height, occupied)
        const int x = arguments.integer();
        const int y = arguments.integer();
        const int width = arguments.integer();
        const int height = arguments.integer();
        const int occupied = arguments.integer();
        // Clip to the grid; the 64-bit sum guards against overflow from a
        // malformed script.
        const int endX = int(std::min<std::int64_t>(kSize, std::int64_t(x) + width));
        const int endY = int(std::min<std::int64_t>(kSize, std::int64_t(y) + height));
        for (int r = std::max(0, y); r < endY; ++r) {
            for (int c = std::max(0, x); c < endX; ++c) {
                if (initial[cellIndex(c, r)] != kNoHole) {
                    initial[cellIndex(c, r)] = occupied ? kBall : kEmptyHole;
                }
            }
        }
    });
    ScriptInterpreter(bytes).run(context);
    reset();
}

void PegPuzzle::reset() {
    // 020f7a18: restore the starting board and forget any ball in motion.
    cells = initial;
    state = kIdle;
    counter = 0;
    touchX = touchY = column = row = targetX = targetY = -1;
    displayX = displayY = 0;
}

void PegPuzzle::update(const Touch& t) {
    switch (state) {
    case kIdle: {
        // A new press on a ball picks it up: the hole becomes empty and the
        // ball is drawn separately, following the stylus.
        if (!t.pressed) {
            return;
        }
        const int x = cellX(t.x);
        const int y = cellY(t.y);
        if (x < 0 || y < 0 || x >= kSize || y >= kSize || cells[cellIndex(x, y)] != kBall) {
            return;
        }
        state = kDragging;
        touchX = std::int16_t(t.x);
        touchY = std::int16_t(t.y);
        column = std::int16_t(x);
        row = std::int16_t(y);
        // The ball sprite is 24x24; its top-left corner sits 12 pixels up and
        // left of the stylus so that the stylus holds it at the centre.
        targetX = touchX - 12;
        targetY = touchY - 12;
        displayX = float(kBoardX + kCellPitch * x);
        displayY = float(kBoardY + kCellPitch * y);
        cells[cellIndex(x, y)] = kEmptyHole;
        sounds.play(0x118);
        return;
    }

    case kDragging: {
        // While the stylus is down the ball keeps following it.
        if (t.held) {
            touchX = std::int16_t(t.x);
            touchY = std::int16_t(t.y);
            targetX = touchX - 12;
            targetY = touchY - 12;
            return;
        }
        // Released: the drop is a legal jump when it lands in an empty hole
        // exactly two cells away in a straight line, over a ball.
        const int x = cellX(touchX);
        const int y = cellY(touchY);
        bool legalJump = false;
        if (x >= 0 && y >= 0 && x < kSize && y < kSize && cells[cellIndex(x, y)] == kEmptyHole) {
            const int dx = x - column;
            const int dy = y - row;
            const bool straight = dx * dy == 0;
            const bool twoCellsAway = std::abs(dx) == 2 || std::abs(dy) == 2;
            if (straight && twoCellsAway) {
                legalJump = cells[cellIndex(column + dx / 2, row + dy / 2)] == kBall;
            }
        }
        // Either way the ball now glides into a hole: the new one for a legal
        // jump, otherwise back to where it was picked up.
        targetX = std::int16_t(kBoardX + kCellPitch * (legalJump ? x : column));
        targetY = std::int16_t(kBoardY + kCellPitch * (legalJump ? y : row));
        state = kSettling;
        touchX = touchY = -1;
        return;
    }

    case kSettling: {
        // Wait until the gliding ball is within half a pixel of its hole.
        if (std::abs(float(targetX) - displayX) >= 0.5f ||
            std::abs(float(targetY) - displayY) >= 0.5f) {
            return;
        }
        const int x = cellX(targetX);
        const int y = cellY(targetY);
        sounds.play(0x119);
        // The cell halfway between the start and the destination holds the
        // jumped ball, which is removed. For a ball returning to its own hole
        // "halfway" is that hole itself (already empty), so nothing is lost.
        const int jumped = cellIndex((column + x) / 2, (row + y) / 2);
        if (cells[jumped] == kBall) {
            sounds.play(0x11a);
        }
        cells[jumped] = kEmptyHole;
        cells[cellIndex(x, y)] = kBall;
        state = kIdle;
        column = row = targetX = targetY = -1;
        if (std::count(cells.begin(), cells.end(), kBall) <= 1) {
            state = kComplete;
            counter = 0;
        }
        return;
    }

    case kComplete:
        // Count frames towards the automatic judgment (see solved()). The
        // counter is a 16-bit field in the original and wraps the same way.
        counter = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(counter) + 1));
        return;
    }
}

void PegPuzzle::animate() {
    // The moving ball covers half of its remaining distance every frame, which
    // gives the original's quick ease-out. The subtraction, halving and
    // addition are kept as separate float steps, as in the game, so rounding
    // matches exactly.
    if (column >= 0 && row >= 0) {
        float dx = displayX - float(targetX);
        float dy = displayY - float(targetY);
        dx *= 0.5f;
        dy *= 0.5f;
        displayX = float(targetX) + dx;
        displayY = float(targetY) + dy;
    }
}

void PegPuzzle::render(Surface& target, const Sprite& sprite, int yOffset) const {
    const auto& frame = sprite.animations.at(sprite.animationNames.at("ball")).at(0);
    auto drawBall = [&](int x, int y) {
        blit(target, sprite.images.at(frame.image), x, yOffset + y);
    };
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            if (cells[cellIndex(x, y)] == kBall) {
                drawBall(kBoardX + kCellPitch * x, kBoardY + kCellPitch * y);
            }
        }
    }
    // The held or gliding ball, rounded to the nearest pixel.
    if (column >= 0 && row >= 0) {
        drawBall(int(displayX + 0.5f), int(displayY + 0.5f));
    }
}

} // namespace picarat
