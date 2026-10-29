// Ice-skating engine (ARM9 overlay 49). See a2_skate_puzzle.hpp for the rules.
#include "puzzles/a2_skate_puzzle.hpp"
#include "system/script.hpp"

namespace picarat {

namespace {
// Stores a value the way the original stores it into a 16-bit field.
std::int16_t s16(int v) {
    return std::int16_t(std::uint16_t(unsigned(v)));
}

// One cell step per direction: right, down, left, up (table at 020f87c8).
constexpr std::array<std::array<int, 2>, 4> kSteps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

// The push-off animation for each direction.
constexpr std::array<const char*, 4> kPushOff{"move_right", "move_down", "move_left", "move_up"};
} // namespace

int SkatePuzzle::cell(int pixel, int origin) {
    // +0x100 (16 cells) keeps the division positive, so positions left of or
    // above the ice give negative cells.
    return (pixel - origin + 0x100) / 16 - 16;
}

SkatePuzzle::SkatePuzzle(std::span<const std::uint8_t> script) {
    ScriptContext context;
    // SkateBoard(x, y, columns, rows, startX, startY, exitX, exitY): virtual
    // +1d8 (020f7bc0).
    context.bind(0x63, [&](CommandArguments& arguments) {
        originX_ = s16(arguments.integer());
        originY_ = s16(arguments.integer());
        columns_ = s16(arguments.integer());
        rows_ = s16(arguments.integer());
        startX_ = s16(arguments.integer());
        startY_ = s16(arguments.integer());
        exitX_ = s16(arguments.integer());
        exitY_ = s16(arguments.integer());
    });
    // SkateWall(x0, y0, x1, y1): virtual +1dc (020f7bf8). Only horizontal or
    // vertical segments of non-zero length are kept, at most 32.
    context.bind(0x64, [&](CommandArguments& arguments) {
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
        if (walls_.size() < 0x20) {
            walls_.push_back({s16(std::min(x0, x1)), s16(std::min(y0, y1)), s16(std::max(x0, x1)),
                              s16(std::max(y0, y1))});
        }
    });
    ScriptInterpreter(script).run(context);
    reset();
}

void SkatePuzzle::reset() {
    sounds.stop(0x122); // 020f7a28 first stops the slide sound
    state_ = kIdle;
    direction_ = -1;
    flags_ = 0;
    cellX_ = startX_;
    cellY_ = startY_;
    pixelX_ = s16(pixel(cellX_, originX_));
    pixelY_ = s16(pixel(cellY_, originY_));
    // The start lies outside the ice; the only arrow points into it.
    if (startX_ < 0) {
        flags_ |= kCanRight;
    }
    if (startY_ < 0) {
        flags_ |= kCanDown;
    }
    if (columns_ <= startX_) {
        flags_ |= kCanLeft;
    }
    if (rows_ <= startY_) {
        flags_ |= kCanUp;
    }
}

void SkatePuzzle::update(const TouchFilter& t) {
    // TouchFilter states: 1 pressed, 2 held, 3 released.
    switch (state_) {
    case kIdle: {
        if (t.state != 1) {
            animation_ = nullptr;
            return;
        }
        // A press on the cell next to the protagonist chooses that direction,
        // if its arrow is shown.
        state_ = kPressing;
        direction_ = -1;
        const int cx = cell(t.pressX, originX_);
        const int cy = cell(t.pressY, originY_);
        if ((flags_ & kCanLeft) && cx == cellX_ - 1 && cy == cellY_) {
            direction_ = kLeft;
        }
        if ((flags_ & kCanRight) && cx == cellX_ + 1 && cy == cellY_) {
            direction_ = kRight;
        }
        if ((flags_ & kCanUp) && cx == cellX_ && cy == cellY_ - 1) {
            direction_ = kUp;
        }
        if ((flags_ & kCanDown) && cx == cellX_ && cy == cellY_ + 1) {
            direction_ = kDown;
        }
        if (direction_ >= 0) {
            animation_ = kPushOff[std::size_t(direction_)];
        }
        return;
    }

    case kPressing: {
        if (t.state == 1 || t.state == 2) {
            // Moving the stylus off the chosen arrow cancels the choice.
            const int cx = cell(t.x, originX_);
            const int cy = cell(t.y, originY_);
            if (direction_ >= 0) {
                const auto [sx, sy] = kSteps[std::size_t(direction_)];
                if (cx != cellX_ + sx || cy != cellY_ + sy) {
                    direction_ = -1;
                }
            }
            return;
        }
        // Released.
        state_ = kIdle;
        if (direction_ == -1) {
            return;
        }
        state_ = kSliding;
        sounds.play(3);
        sounds.play(0x122);

        // Work out where the slide ends: always at least one cell, then on
        // until the exit, the edge of the ice or a wall.
        const auto [sx, sy] = kSteps[std::size_t(direction_)];
        cellX_ = s16(cellX_ + sx);
        cellY_ = s16(cellY_ + sy);
        flags_ &= ~0xfu;
        for (;;) {
            if (exitX_ == cellX_ + sx && exitY_ == cellY_ + sy) {
                // The next cell is the exit: slide out.
                cellX_ = s16(cellX_ + sx);
                cellY_ = s16(cellY_ + sy);
                flags_ |= kAtExit;
                break;
            }
            const bool atEdge = (sx > 0 && cellX_ >= columns_ - 1) ||
                                (sy > 0 && cellY_ >= rows_ - 1) || (sx < 0 && cellX_ <= 0) ||
                                (sy < 0 && cellY_ <= 0);
            if (atEdge) {
                break;
            }
            // A wall on the grid line ahead stops the slide. Moving
            // horizontally, the line ahead is x + 1 (right) or x (left); the
            // wall must span the cell's row, and likewise vertically.
            bool blocked = false;
            for (const auto& wall : walls_) {
                if (sy == 0) {
                    const int lineAhead = sx > 0 ? cellX_ + 1 : cellX_;
                    if (wall.x0 == wall.x1 && lineAhead == wall.x0 && wall.y0 <= cellY_ &&
                        cellY_ + 1 <= wall.y1) {
                        blocked = true;
                        break;
                    }
                } else {
                    const int lineAhead = sy > 0 ? cellY_ + 1 : cellY_;
                    if (wall.y0 == wall.y1 && lineAhead == wall.y0 && wall.x0 <= cellX_ &&
                        cellX_ + 1 <= wall.x1) {
                        blocked = true;
                        break;
                    }
                }
            }
            if (blocked) {
                break;
            }
            cellX_ = s16(cellX_ + sx);
            cellY_ = s16(cellY_ + sy);
        }
        direction_ = -1;
        if (flags_ & kAtExit) {
            return;
        }

        // Arrows for the next move: every direction that is not at the edge
        // of the ice and not blocked by an adjacent wall.
        if (cellX_ + 1 < columns_) {
            flags_ |= kCanRight;
        }
        if (cellY_ + 1 < rows_) {
            flags_ |= kCanDown;
        }
        if (cellX_ > 0) {
            flags_ |= kCanLeft;
        }
        if (cellY_ > 0) {
            flags_ |= kCanUp;
        }
        for (const auto& wall : walls_) {
            if (wall.x0 == wall.x1) {
                if (wall.y0 <= cellY_ && cellY_ + 1 <= wall.y1) {
                    if (cellX_ == wall.x0) {
                        flags_ &= ~kCanLeft;
                    }
                    if (cellX_ + 1 == wall.x0) {
                        flags_ &= ~kCanRight;
                    }
                }
            } else if (wall.x0 <= cellX_ && cellX_ + 1 <= wall.x1) {
                if (cellY_ == wall.y0) {
                    flags_ &= ~kCanUp;
                }
                if (cellY_ + 1 == wall.y0) {
                    flags_ &= ~kCanDown;
                }
            }
        }
        return;
    }

    case kSliding: {
        // Glide 2 pixels per frame on each axis towards the new cell.
        const int targetX = pixel(cellX_, originX_);
        const int targetY = s16(pixel(cellY_, originY_));
        if (pixelX_ == targetX && pixelY_ == targetY) {
            sounds.stop(0x122);
            state_ = (flags_ & kAtExit) ? kOut : kIdle;
            return;
        }
        if (targetX != pixelX_) {
            pixelX_ = s16(pixelX_ + (targetX - pixelX_ < 0 ? -2 : 2));
        }
        if (targetY != pixelY_) {
            pixelY_ = s16(pixelY_ + (targetY - pixelY_ < 0 ? -2 : 2));
        }
        return;
    }

    default:
        return;
    }
}

std::vector<SkatePuzzle::Arrow> SkatePuzzle::arrows() const {
    std::vector<Arrow> out;
    if (state_ > kPressing) {
        return out;
    }
    // The pressed arrow is drawn one pixel right and down.
    auto pressed = [&](int d) {
        return direction_ == d ? 1 : 0;
    };
    if (flags_ & kCanRight) {
        out.push_back({"right", pixelX_ + 0x10 + pressed(kRight), pixelY_ + pressed(kRight)});
    }
    if (flags_ & kCanDown) {
        out.push_back({"down", pixelX_ + pressed(kDown), pixelY_ + 0x10 + pressed(kDown)});
    }
    if (flags_ & kCanLeft) {
        out.push_back({"left", pixelX_ - 0x10 + pressed(kLeft), pixelY_ + pressed(kLeft)});
    }
    if (flags_ & kCanUp) {
        out.push_back({"up", pixelX_ + pressed(kUp), pixelY_ - 0x10 + pressed(kUp)});
    }
    return out;
}

} // namespace picarat
