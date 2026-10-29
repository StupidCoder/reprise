// Sliding-blocks engine (ARM9 overlay 47). See a2_slide_puzzle.hpp for the rules.
//
// Floating-point steps follow the game's resident float helpers (0203af44,
// 0203b2a4, 0203afd4, 0203aa94, 0203a290, 0203aed0), so the sliding paths
// match the original pixel for pixel.
#include "puzzles/a2_slide_puzzle.hpp"
#include "system/script.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace picarat {

namespace {
// The game's signed division helper (0203b744), including its results for
// division by 0 (returns the dividend) and by -1 (wrapping negation).
int sdiv(int a, int b) {
    if (b == 0) {
        return a;
    }
    if (b == -1) {
        return int(0u - unsigned(a));
    }
    return a / b;
}

// Float to int with the helper's saturation (0203aed0).
int f2i(float v) {
    if (std::isnan(v)) {
        return 0;
    }
    if (v >= 2147483648.0f) {
        return std::numeric_limits<int>::max();
    }
    if (v <= -2147483648.0f) {
        return std::numeric_limits<int>::min();
    }
    return int(v);
}

// Stores a value the way the original stores it into a 16-bit field.
std::int16_t s16(int v) {
    return std::int16_t(std::uint16_t(unsigned(v)));
}

// The sliding sound's fade-out time: the float 0.1 exactly as stored.
constexpr std::uint32_t kFadeTenthBits = 0x3dcccccdu;

// 020f8008: whether every rectangle in `rects`, moved by (dx, dy), stays clear
// of `other`. Rectangles are {left, top, right, bottom} with inclusive edges.
bool clear(int dx, int dy, const std::array<int, 4>& other,
           const std::vector<std::array<int, 4>>& rects) {
    for (const auto& r : rects) {
        if (r[0] + dx <= other[2] && r[1] + dy <= other[3] && other[0] <= r[2] + dx &&
            other[1] <= r[3] + dy) {
            return false;
        }
    }
    return true;
}
} // namespace

SlidePuzzle::SlidePuzzle(std::span<const std::uint8_t> script, unsigned internalId) {
    alternateSolved_ = internalId == 0x15 || internalId == 0x76 || internalId == 0x98;
    ScriptContext context;
    // SlideBoard(x, y, columns, rows, cellWidth, cellHeight): virtual +194 (020f7cac).
    context.bind(0x4e, [&](CommandArguments& arguments) {
        originX_ = s16(arguments.integer());
        originY_ = s16(arguments.integer());
        columns_ = s16(arguments.integer());
        rows_ = s16(arguments.integer());
        cellWidth_ = s16(arguments.integer());
        cellHeight_ = s16(arguments.integer());
    });
    // SlideWall(x, y, width, height), in cells: virtual +198 (020f7cd8).
    context.bind(0x4f, [&](CommandArguments& arguments) {
        const std::int16_t x = s16(arguments.integer());
        const std::int16_t y = s16(arguments.integer());
        const std::int16_t width = s16(arguments.integer());
        const std::int16_t height = s16(arguments.integer());
        if (walls_.size() < 16) {
            walls_.push_back({x, y, width, height});
        }
    });
    // SlideGoal(block, x, y): virtual +19c (020f7d20).
    context.bind(0x50, [&](CommandArguments& arguments) {
        const std::int16_t piece = s16(arguments.integer());
        const std::int16_t x = s16(arguments.integer());
        const std::int16_t y = s16(arguments.integer());
        if (goals_.size() < 16) {
            goals_.push_back({piece, x, y});
        }
    });
    // SlideSprite(name): virtual +1a0 (020f7d64).
    context.bind(0x51, [&](CommandArguments& arguments) {
        auto name = arguments.string();
        if (sprites_.size() < 4) {
            sprites_.push_back("nazo/slide/" + name);
        }
    });
    // SlideBlock(sprite, heldAnimation, idleAnimation, x, y): virtual +1a4 (020f7de0).
    context.bind(0x52, [&](CommandArguments& arguments) {
        Piece piece;
        piece.sprite = s16(arguments.integer());
        piece.held = arguments.string();
        piece.idle = arguments.string();
        piece.initialX = s16(arguments.integer());
        piece.initialY = s16(arguments.integer());
        if (pieces_.size() < 16 && std::uint16_t(piece.sprite) < 4) {
            pieces_.push_back(std::move(piece));
        }
    });
    // SlideBlockCells(x, y, width, height), relative to the last block:
    // virtual +1a8 (020f7e5c).
    context.bind(0x53, [&](CommandArguments& arguments) {
        const std::int16_t x = s16(arguments.integer());
        const std::int16_t y = s16(arguments.integer());
        const std::int16_t width = s16(arguments.integer());
        const std::int16_t height = s16(arguments.integer());
        if (!pieces_.empty() && pieces_.back().rects.size() < 16) {
            pieces_.back().rects.push_back({x, y, width, height});
        }
    });
    ScriptInterpreter(script).run(context);
    reset(); // 020f78a0 calls virtual +204 after the script.
}

int SlidePuzzle::pixelX(int cell) const {
    return cellWidth_ * cell + originX_;
}

int SlidePuzzle::pixelY(int cell) const {
    return cellHeight_ * cell + originY_;
}

int SlidePuzzle::cellX(int pixel) const {
    // Shifting by 16 cells keeps the division positive (round down).
    pixel += cellWidth_ * 16;
    return sdiv(pixel - originX_, cellWidth_) - 16;
}

int SlidePuzzle::cellY(int pixel) const {
    pixel += cellHeight_ * 16;
    return sdiv(pixel - originY_, cellHeight_) - 16;
}

void SlidePuzzle::reset() {
    state_ = kIdle;
    timer_ = moves_ = 0;
    touchX_ = touchY_ = -1;
    offsetX_ = offsetY_ = 0;
    for (auto& piece : pieces_) {
        piece.cellX = piece.initialX;
        piece.cellY = piece.initialY;
        piece.pixelX = s16(pixelX(piece.cellX));
        piece.pixelY = s16(pixelY(piece.cellY));
        piece.displayX = float(piece.pixelX);
        piece.displayY = float(piece.pixelY);
    }
    selected_ = -1;
    sliding_ = false;
}

bool SlidePuzzle::complete() {
    if (!(state_ == kSolved && timer_ > 0x2d)) {
        return false;
    }
    if (sliding_) {
        sounds.stop(0x109, std::bit_cast<float>(kFadeTenthBits));
        sliding_ = false;
    }
    return true;
}

bool SlidePuzzle::pick(int x, int y) {
    if (selected_ >= 0) {
        return false;
    }
    // The first block (in script order) with a cell rectangle covering the
    // pressed cell.
    const int cx = cellX(x);
    const int cy = cellY(y);
    for (std::size_t i = 0; i < pieces_.size() && selected_ < 0; ++i) {
        const auto& piece = pieces_[i];
        for (const auto& r : piece.rects) {
            const int left = piece.cellX + r.x;
            const int top = piece.cellY + r.y;
            if (left <= cx && top <= cy && cx < r.width + left && cy < r.height + top) {
                selected_ = s16(int(i));
                break;
            }
        }
    }
    if (selected_ < 0) {
        return false;
    }
    auto& piece = pieces_[std::size_t(selected_)];
    piece.grabX = piece.cellX;
    piece.grabY = piece.cellY;
    offsetX_ = s16(x - piece.pixelX);
    offsetY_ = s16(y - piece.pixelY);
    return true;
}

bool SlidePuzzle::fits(int dx, int dy, const std::vector<std::array<int, 4>>& rects) const {
    // Walls, as pixel rectangles...
    for (const auto& wall : walls_) {
        const std::array<int, 4> pixels{pixelX(wall.x), pixelY(wall.y),
                                        pixelX(wall.x + wall.width) - 1,
                                        pixelY(wall.y + wall.height) - 1};
        if (!clear(dx, dy, pixels, rects)) {
            return false;
        }
    }
    // ...and the other blocks, at their cells (not their drawn positions).
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        if (int(i) == selected_) {
            continue;
        }
        const auto& piece = pieces_[i];
        for (const auto& r : piece.rects) {
            const std::array<int, 4> pixels{pixelX(piece.cellX + r.x), pixelY(piece.cellY + r.y),
                                            pixelX(r.width + piece.cellX + r.x) - 1,
                                            pixelY(r.height + piece.cellY + r.y) - 1};
            if (!clear(dx, dy, pixels, rects)) {
                return false;
            }
        }
    }
    return true;
}

void SlidePuzzle::move(int& dx, int& dy, const std::vector<std::array<int, 4>>& rects) const {
    // 020f8298: walk the straight line towards (dx, dy) one pixel at a time
    // along the larger axis. At each step the smaller axis advances by its
    // rounded share of the line, int(1024.5 + minor·i/major) − 1024. Each
    // axis' step is kept only if the block still fits, so a block pushed
    // diagonally against a wall slides along it.
    if (dx == 0 && dy == 0) {
        return;
    }
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    int x = 0;
    int y = 0;
    if (ax > ay) {
        const int step = sdiv(dx, ax); // +1 or -1
        int previous = 0;
        int along = step;
        for (int i = 1; i <= ax; ++i) {
            const int share = f2i(1024.5f + float(dy) * (float(along) / float(dx))) - 0x400;
            const int oldX = x;
            const int oldY = y;
            x += step;
            if (!fits(x, y, rects)) {
                x = oldX;
            }
            y += share - previous;
            if (y != oldY && !fits(x, y, rects)) {
                y = oldY;
            }
            previous = share;
            along += step;
        }
    } else {
        const int step = sdiv(dy, ay);
        int previous = 0;
        int along = step;
        for (int i = 1; i <= ay; ++i) {
            const int share = f2i(1024.5f + float(dx) * (float(along) / float(dy))) - 0x400;
            const int oldX = x;
            const int oldY = y;
            y += step;
            if (!fits(x, y, rects)) {
                y = oldY;
            }
            x += share - previous;
            if (x != oldX && !fits(x, y, rects)) {
                x = oldX;
            }
            previous = share;
            along += step;
        }
    }
    dx = x;
    dy = y;
}

void SlidePuzzle::drag(int dx, int dy) {
    // 020f8530: the held block's cells as pixel rectangles at its current
    // logical position.
    auto& piece = pieces_[std::size_t(selected_)];
    std::vector<std::array<int, 4>> rects;
    for (const auto& r : piece.rects) {
        rects.push_back({cellWidth_ * r.x + piece.pixelX, cellHeight_ * r.y + piece.pixelY,
                         cellWidth_ * (r.x + r.width) + piece.pixelX - 1,
                         cellHeight_ * (r.y + r.height) + piece.pixelY - 1});
    }
    // Keep the movement inside the board...
    const int left = pixelX(0);
    const int top = pixelY(0);
    const int right = pixelX(columns_) - 1;
    const int bottom = pixelY(rows_) - 1;
    for (const auto& r : rects) {
        if (r[0] + dx < left) {
            dx = left - r[0];
        }
        if (r[1] + dy < top) {
            dy = top - r[1];
        }
        if (r[2] + dx > right) {
            dx = right - r[2];
        }
        if (r[3] + dy > bottom) {
            dy = bottom - r[3];
        }
    }
    // ...then slide as far as walls and blocks allow.
    move(dx, dy, rects);
    piece.pixelX = s16(piece.pixelX + s16(dx));
    piece.pixelY = s16(piece.pixelY + s16(dy));
}

void SlidePuzzle::update(const TouchFilter& t) {
    // TouchFilter states: 1 pressed, 2 held, 3 released.
    if (state_ == kIdle) {
        if (t.state != 1) {
            return;
        }
        if (pick(t.pressX, t.pressY)) {
            touchX_ = s16(t.pressX);
            touchY_ = s16(t.pressY);
            state_ = kDragging;
        }
        return;
    }
    if (state_ == kSolved) {
        timer_ = s16(timer_ + 1);
        return;
    }
    if (state_ != kDragging) {
        return;
    }
    if (t.state == 1 || t.state == 2) {
        // Follow the stylus; the block's cell is the one under its
        // top-left cell's centre.
        auto& piece = pieces_[std::size_t(selected_)];
        drag(t.x - piece.pixelX - offsetX_, t.y - piece.pixelY - offsetY_);
        piece.cellX = s16(cellX(piece.pixelX + (cellWidth_ >> 1)));
        piece.cellY = s16(cellY(piece.pixelY + (cellHeight_ >> 1)));
    } else {
        // Released: snap to the cell; a block that ends in a different cell
        // than it was picked up from counts as one move.
        auto& piece = pieces_[std::size_t(selected_)];
        piece.pixelX = s16(pixelX(piece.cellX));
        piece.pixelY = s16(pixelY(piece.cellY));
        if (piece.grabX != piece.cellX || piece.grabY != piece.cellY) {
            moves_ = s16(moves_ + 1);
            if (moves_ > 9999) {
                moves_ = 9999;
            }
        }
        piece.grabX = piece.cellX;
        piece.grabY = piece.cellY;
        selected_ = -1;
        touchX_ = touchY_ = -1;
        offsetX_ = offsetY_ = 0;
        state_ = kIdle;
    }

    // Solved when every goal block is on its goal cell. This is checked while
    // dragging too: the moment the held block reaches its goal, it snaps
    // there and the move is counted.
    for (const auto& goal : goals_) {
        const auto& piece = pieces_.at(std::size_t(goal.piece));
        if (piece.cellX != goal.x || piece.cellY != goal.y) {
            return;
        }
    }
    if (state_ == kDragging) {
        auto& piece = pieces_[std::size_t(selected_)];
        piece.pixelX = s16(pixelX(piece.cellX));
        piece.pixelY = s16(pixelY(piece.cellY));
        moves_ = s16(moves_ + 1);
        if (moves_ > 9999) {
            moves_ = 9999;
        }
    }
    state_ = kSolved;
    timer_ = 0;
    sounds.play(alternateSolved_ ? 0x10a : 0x10b);
}

void SlidePuzzle::animate() {
    // 020f8a78: drawn positions cover half the remaining distance each frame.
    // While any block is still more than half a pixel away, the sliding
    // sound plays.
    bool sliding = false;
    for (auto& piece : pieces_) {
        const float dx = 0.5f * (piece.displayX - float(piece.pixelX));
        const float dy = 0.5f * (piece.displayY - float(piece.pixelY));
        if ((dx >= 0 ? dx : 0.0f - dx) > 0.5f || (dy >= 0 ? dy : 0.0f - dy) > 0.5f) {
            sliding = true;
        }
        piece.displayX = float(piece.pixelX) + dx;
        piece.displayY = float(piece.pixelY) + dy;
    }
    if (sliding != sliding_) {
        if (sliding) {
            sounds.play(0x109);
        } else {
            sounds.stop(0x109, std::bit_cast<float>(kFadeTenthBits));
        }
    }
    sliding_ = sliding;
}

} // namespace picarat
