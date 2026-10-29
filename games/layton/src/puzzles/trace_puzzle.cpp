// Tracing engines (ARM9 overlays 34 and 54). See trace_puzzle.hpp for the rules.
#include "puzzles/trace_puzzle.hpp"
#include "system/script.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace picarat {

namespace {
constexpr int kWidth = 0x100;            // mask width (256)
constexpr int kHeight = 0xc0;            // mask height (192)
constexpr int kRingCentre = 0x5f;        // (95, 95)
constexpr int kRingInnerSquared = 0xa9;  // 13²
constexpr int kRingOuterSquared = 0xaf9; // 53²
} // namespace

TracePuzzle::TracePuzzle(std::span<const std::uint8_t> script, unsigned type)
    : ring_(type == 6), mask_(kWidth * kHeight, kEmpty) {
    if (type != 6 && type != 34) {
        throw std::runtime_error("trace engine type");
    }
    ScriptContext context;
    // FillSeed(x, y): virtual function +110.
    context.bind(0x19, [&](CommandArguments& arguments) {
        const int x = arguments.integer();
        setSeed(x, arguments.integer());
    });
    // InsidePoint(x, y): virtual +108 -> engine 020f78e0.
    context.bind(0x1a, [&](CommandArguments& arguments) {
        const int x = arguments.integer();
        addInside(x, arguments.integer());
    });
    // OutsidePoint(x, y): virtual +10c -> engine 020f7918.
    context.bind(0x1b, [&](CommandArguments& arguments) {
        const int x = arguments.integer();
        addOutside(x, arguments.integer());
    });
    ScriptInterpreter(script).run(context);
    // The constructor starts with an empty line without calling virtual +204.
}

TracePuzzle::TracePuzzle() : ring_(false), mask_(kWidth * kHeight, kEmpty) {}

void TracePuzzle::setSeed(int x, int y) {
    // Halving and doubling rounds towards zero, which equals rounding down
    // to even for the non-negative points used (first adventure 02048a20).
    seedX_ = (x / 2) * 2;
    seedY_ = (y / 2) * 2;
}

void TracePuzzle::addInside(int x, int y) {
    // 020f78e0 / 020596d8: points rounded down to even (the fill works on a
    // 2-pixel grid), at most 10.
    if (inside_.size() < 10) {
        inside_.push_back({x & ~1, y & ~1});
    }
}

void TracePuzzle::addOutside(int x, int y) {
    // 020f7918 / 02059710.
    if (outside_.size() < 10) {
        outside_.push_back({x & ~1, y & ~1});
    }
}

void TracePuzzle::clearMask() {
    std::fill(mask_.begin(), mask_.end(), kEmpty);
}

void TracePuzzle::reset() {
    clear_ = true;
    ink_.clear();                                  // 02072a10 clears the text layer
    std::fill(mask_.begin(), mask_.end(), kEmpty); // 020f7950
    if (ring_) {
        quadrants_ = 0;
    }
    valid_ = true;
    released_ = false;
    startX_ = startY_ = lastX_ = lastY_ = -1;
}

void TracePuzzle::maskStamp(int x, int y) {
    // A 2x2 block with (x, y) at its top left. The right column is skipped at
    // x + 1 >= 255 and the bottom row at y + 1 >= 191, as in the original.
    if (x < 0 || y < 0) {
        return;
    }
    const int i = x + y * kWidth;
    mask_[std::size_t(i)] = kInk;
    if (x + 1 < 0xff) {
        mask_[std::size_t(i + 1)] = kInk;
    }
    if (y + 1 < 0xbf) {
        mask_[std::size_t(i + kWidth)] = kInk;
        if (x + 1 < 0xff) {
            mask_[std::size_t(i + kWidth + 1)] = kInk;
        }
    }
}

void TracePuzzle::dot(int x, int y) {
    // Ink is clamped to x in 4..250 (points left of x = 4 draw nothing).
    if (x < 4) {
        return;
    }
    x = std::min(x, 0xfa);
    CircleInkCommand command;
    command.kind = CircleInkCommand::Kind::Dot;
    command.x0 = x;
    command.y0 = y;
    ink_.push_back(command);
    maskStamp(x - 2, y - 2);
}

void TracePuzzle::line(int x0, int y0, int x1, int y1, bool maskOnly) {
    if (!maskOnly) {
        CircleInkCommand command;
        command.kind = CircleInkCommand::Kind::Line;
        command.x0 = x0;
        command.y0 = y0;
        command.x1 = x1;
        command.y1 = y1;
        ink_.push_back(command);
    }
    // Record the segment in the mask with the same Bresenham walk the ink
    // drawing uses. Note the quirk: the deltas are taken before the x
    // coordinates are clamped to 4..250.
    const int dy = std::abs(y1 - y0);
    const int dx = std::abs(x1 - x0);
    const int sy = y1 < y0 ? -1 : 1;
    const int sx = x1 < x0 ? -1 : 1;
    int x = std::clamp(x0, 4, 0xfa);
    int y = y0;
    const int targetX = std::clamp(x1, 4, 0xfa);
    maskStamp(x - 2, y - 2);
    if (2 * dy < 2 * dx) {
        // Mostly horizontal: one step in x per pixel.
        int error = -((2 * dx) >> 1);
        if (x == targetX) {
            return;
        }
        do {
            error += 2 * dy;
            if (error >= 0) {
                error -= 2 * dx;
                y += sy;
            }
            x += sx;
            maskStamp(x - 2, y - 2);
        } while (x != targetX);
    } else {
        // Mostly vertical: one step in y per pixel.
        int error = -((2 * dy) >> 1);
        if (y == y1) {
            return;
        }
        do {
            error += 2 * dx;
            if (error >= 0) {
                error -= 2 * dy;
                x += sx;
            }
            y += sy;
            maskStamp(x - 2, y - 2);
        } while (y != y1);
    }
}

void TracePuzzle::update(const TouchFilter& t) {
    // A new press on the drawing area (x < 192) erases the old line and
    // starts a new one. (TouchFilter states: 1 pressed, 2 held, 3 released.)
    if (startX_ < 0 && startY_ < 0 && t.state == 1 && t.x < 0xc0) {
        reset();
        startX_ = lastX_ = t.x;
        startY_ = lastY_ = t.y;
        dot(startX_, startY_);
    }
    if (startX_ < 0 || startY_ < 0) {
        return;
    }
    if (t.state != 3) {
        const int x = t.x; // position as read by 0206e288
        const int y = t.y;
        if (ring_) {
            // Leaving the ring spoils the answer; otherwise note which
            // quarter around the centre the stylus is in.
            const int dx = x - kRingCentre;
            const int dy = y - kRingCentre;
            const int distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < kRingInnerSquared || distanceSquared > kRingOuterSquared) {
                valid_ = false;
            } else if (dx < 0) {
                quadrants_ |= dy < 0 ? 1 : 2;
            } else {
                quadrants_ |= dy < 0 ? 4 : 8;
            }
        }
        if (lastX_ != x || lastY_ != y) {
            line(lastX_, lastY_, x, y, false);
            lastX_ = x;
            lastY_ = y;
        }
        return;
    }
    // Released: the line is complete. For an enclosed area, the gap between
    // the end and the start is closed in the mask (not drawn).
    if (!ring_) {
        line(lastX_, lastY_, startX_, startY_, true);
    }
    startX_ = startY_ = lastX_ = lastY_ = -1;
    released_ = true;
}

void TracePuzzle::fill() {
    // A simple flood fill on the even pixels (a 2-pixel grid): starting at
    // the seed, empty points next to filled points become filled, sweeping
    // the whole screen until nothing changes. The line (ink) stops it.
    auto at = [&](int x, int y) -> std::uint8_t& {
        return mask_[std::size_t(x + y * kWidth)];
    };
    if (at(seedX_, seedY_) == kEmpty) {
        at(seedX_, seedY_) = kFilled;
    }
    auto filledNeighbour = [&](int x, int y) {
        return (x - 2 >= 0 && at(x - 2, y) == kFilled) ||
               (x + 2 < kWidth && at(x + 2, y) == kFilled) ||
               (y - 2 >= 0 && at(x, y - 2) == kFilled) ||
               (y + 2 < kHeight && at(x, y + 2) == kFilled);
    };
    bool stable;
    do {
        stable = true;
        for (int y = 0; y < kHeight; y += 2) {
            for (int x = 0; x < kWidth; x += 2) {
                if (at(x, y) == kEmpty && filledNeighbour(x, y)) {
                    stable = false;
                    at(x, y) = kFilled;
                }
            }
        }
    } while (!stable);
}

bool TracePuzzle::solved() {
    if (!released_) {
        return false;
    }
    if (ring_) {
        // Never left the ring, and visited all four quarters.
        return valid_ && quadrants_ == 0xf;
    }
    fill();
    if (!valid_) {
        return false;
    }
    return pointsCorrect();
}

bool TracePuzzle::areaCorrect() {
    fill();
    return pointsCorrect();
}

bool TracePuzzle::pointsCorrect() const {
    // Outside points must be untouched; inside points on the line or filled.
    for (const auto& p : outside_) {
        if (mask_[std::size_t(p[0] + p[1] * kWidth)] != kEmpty) {
            return false;
        }
    }
    for (const auto& p : inside_) {
        const auto v = mask_[std::size_t(p[0] + p[1] * kWidth)];
        if (v != kInk && v != kFilled) {
            return false;
        }
    }
    return true;
}

} // namespace picarat
