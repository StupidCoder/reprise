// Rose-placement engine (ARM9 overlay 46). See a2_rose_puzzle.hpp for the rules.
#include "puzzles/a2_rose_puzzle.hpp"
#include "system/script.hpp"
#include <stdexcept>
#include <vector>

namespace picarat {

namespace {
// Stores a value the way the original stores it into a 16-bit field.
std::int16_t s16(int v) {
    return std::int16_t(std::uint16_t(unsigned(v)));
}
} // namespace

RosePuzzle::Cell& RosePuzzle::at(int x, int y) {
    const int index = y * 8 + x;
    if (index < 0 || index >= 64) {
        throw std::runtime_error("rose cell outside the original 8x8 storage");
    }
    return cells_[std::size_t(index)];
}

int RosePuzzle::cell(int pixel, int origin) {
    // +0x180 (16 cells of 24 pixels) keeps the division positive so positions
    // left of or above the board round down to negative cells.
    return (pixel + 0x180 - origin) / 24 - 16;
}

RosePuzzle::RosePuzzle(std::span<const std::uint8_t> script) {
    // A wall segment from (x0, y0) to (x1, y1), in grid-line coordinates.
    struct Segment {
        std::int16_t x0, y0, x1, y1;
    };

    std::vector<Segment> walls; // the original keeps at most 16 (+0x280)

    ScriptContext context;
    // RoseBoard(x, y, columns, rows): virtual function +18c (020f7f00).
    context.bind(0x4c, [&](CommandArguments& arguments) {
        originX_ = s16(arguments.integer());
        originY_ = s16(arguments.integer());
        columns_ = s16(arguments.integer());
        rows_ = s16(arguments.integer());
    });
    // RoseWall(x0, y0, x1, y1): virtual function +190 (020f7f18). Only
    // horizontal or vertical segments of non-zero length are kept.
    context.bind(0x4d, [&](CommandArguments& arguments) {
        const int x0 = arguments.integer();
        const int y0 = arguments.integer();
        const int x1 = arguments.integer();
        const int y1 = arguments.integer();
        if (x0 == x1 && y0 == y1) {
            return;
        }
        if (x0 != x1 && y0 != y1) {
            return;
        }
        if (walls.size() < 16) {
            walls.push_back({s16(std::min(x0, x1)), s16(std::min(y0, y1)), s16(std::max(x0, x1)),
                             s16(std::max(y0, y1))});
        }
    });
    ScriptInterpreter(script).run(context);
    if (columns_ > 8 || rows_ > 8) {
        throw std::runtime_error("rose board outside 8x8 storage");
    }

    // 020f78a0: the board's outer border is walled...
    for (int x = 0; x < columns_; ++x) {
        at(x, 0).flags |= kWallTop;
        at(x, rows_ - 1).flags |= kWallBottom;
    }
    for (int y = 0; y < rows_; ++y) {
        at(0, y).flags |= kWallLeft;
        at(columns_ - 1, y).flags |= kWallRight;
    }
    // ...then each script wall marks the cells on both of its sides. A
    // vertical wall at grid line x separates columns x - 1 and x; a
    // horizontal one at grid line y separates rows y - 1 and y.
    for (const auto& wall : walls) {
        if (wall.x0 == wall.x1) {
            for (int y = wall.y0; y < wall.y1; ++y) {
                at(wall.x0 - 1, y).flags |= kWallRight;
                at(wall.x0, y).flags |= kWallLeft;
            }
        } else if (wall.x0 < wall.x1) {
            for (int x = wall.x0; x < wall.x1; ++x) {
                at(x, wall.y0 - 1).flags |= kWallBottom;
                at(x, wall.y0).flags |= kWallTop;
            }
        }
    }
    // Finally every cell gets its reach mask.
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < columns_; ++x) {
            std::uint32_t mask = at(x, y).flags;
            flood(x, y, mask, 0, 0, 0);
            at(x, y).flags = mask;
        }
    }
    reset(); // virtual function +204
}

void RosePuzzle::flood(int x, int y, std::uint32_t& mask, int dx, int dy, int depth) {
    // A depth-first walk of at most two steps. Every offset visited within the
    // 5x5 window is added to the mask.
    if (depth > 2) {
        return;
    }
    if (std::uint16_t(dx + 2) < 5 && std::uint16_t(dy + 2) < 5) {
        mask |= reachBit(dx, dy);
    }
    // Walls of the cell currently visited. For the starting cell they are read
    // from the mask being built (its low bits are the walls). As in the
    // original, they are read again before each direction.
    auto walls = [&] {
        return x + dx == x && y + dy == y ? mask : at(x + dx, y + dy).flags;
    };
    if (!(walls() & kWallLeft)) {
        flood(x, y, mask, dx - 1, dy, depth + 1);
    }
    if (!(walls() & kWallRight)) {
        flood(x, y, mask, dx + 1, dy, depth + 1);
    }
    if (!(walls() & kWallTop)) {
        flood(x, y, mask, dx, dy - 1, depth + 1);
    }
    if (!(walls() & kWallBottom)) {
        flood(x, y, mask, dx, dy + 1, depth + 1);
    }
}

void RosePuzzle::reset() {
    // Remove every rose; walls and reach masks stay.
    state_ = kIdle;
    startX_ = startY_ = touchX_ = touchY_ = -1;
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < columns_; ++x) {
            auto& c = at(x, y);
            c.flags &= ~kRose;
            c.count = 0;
        }
    }
}

void RosePuzzle::update(const TouchFilter& t) {
    // TouchFilter states: 1 = just pressed, 2 = held, 3 = just released.
    if (state_ == kIdle) {
        if (t.state != 1) {
            return;
        }
        startX_ = touchX_ = s16(t.pressX);
        startY_ = touchY_ = s16(t.pressY);
        state_ = kTouching;
        return;
    }
    if (state_ != kTouching) {
        return;
    }
    if (t.state != 3) {
        // Still touching: remember the latest position (0206e288).
        if (t.state == 1 || t.state == 2) {
            touchX_ = s16(t.x);
            touchY_ = s16(t.y);
        }
        return;
    }

    // Released. A tap counts when pressed and released on the same cell of
    // the board; it plants a rose there, or removes the one that is there.
    const int cx = cell(startX_, originX_);
    const int cy = cell(startY_, originY_);
    const bool onBoard =
        std::uint16_t(cx) < std::uint16_t(columns_) && std::uint16_t(cy) < std::uint16_t(rows_);
    if (onBoard && cx == cell(touchX_, originX_) && cy == cell(touchY_, originY_)) {
        auto& c = at(cx, cy);
        int delta;
        if (!(c.flags & kRose)) {
            sounds.play(0x10f);
            delta = 1;
            c.flags |= kRose;
        } else {
            sounds.play(0x110);
            delta = -1;
            c.flags &= ~kRose;
        }
        // Update the coverage count of every cell this rose reaches.
        for (int dy = -2; dy < 3; ++dy) {
            for (int dx = -2; dx < 3; ++dx) {
                if (c.flags & reachBit(dx, dy)) {
                    at(cx + dx, cy + dy).count += delta;
                }
            }
        }
    }
    startX_ = startY_ = touchX_ = touchY_ = -1;

    // Solved when every cell is covered exactly once.
    bool everyCellOnce = true;
    for (int y = 0; y < rows_ && everyCellOnce; ++y) {
        for (int x = 0; x < columns_; ++x) {
            if (at(x, y).count != 1) {
                everyCellOnce = false;
                break;
            }
        }
    }
    state_ = everyCellOnce ? kSolved : kIdle;
}

std::array<RosePuzzle::Draw, 2 * 64> RosePuzzle::draws(int& count) const {
    std::array<Draw, 2 * 64> out{};
    count = 0;
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < columns_; ++x) {
            const auto& c = cells_[std::size_t(y * 8 + x)];
            const int px = x * 24 + originX_;
            const int py = y * 24 + originY_;
            if (c.count != 0) {
                out[std::size_t(count++)] = {px, py, c.count == 1 ? "one" : "two"};
            }
            if (c.flags & kRose) {
                out[std::size_t(count++)] = {px, py, "rose"};
            }
        }
    }
    return out;
}

} // namespace picarat
