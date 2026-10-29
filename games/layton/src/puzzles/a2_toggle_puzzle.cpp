// Grid-painting engine (ARM9 overlay 45). See a2_toggle_puzzle.hpp for the rules.
#include "puzzles/a2_toggle_puzzle.hpp"
#include "system/script.hpp"
#include <stdexcept>

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

// Stores a value the way the original stores it into a 16-bit field.
std::int16_t s16(int v) {
    return std::int16_t(std::uint16_t(unsigned(v)));
}
} // namespace

TogglePuzzle::TogglePuzzle(std::span<const std::uint8_t> script) {
    ScriptContext context;
    // ToggleGrid(x, y, columns, rows, cellWidth, cellHeight, red, green, blue,
    // alpha): virtual +180 (020f7bec). All cells start clear.
    context.bind(0x4a, [&](CommandArguments& arguments) {
        originX_ = s16(arguments.integer());
        originY_ = s16(arguments.integer());
        const int columns = arguments.integer();
        const int rows = arguments.integer();
        columns_ = s16(columns);
        rows_ = s16(rows);
        cellWidth_ = s16(arguments.integer());
        cellHeight_ = s16(arguments.integer());
        const int r = arguments.integer();
        const int g = arguments.integer();
        const int b = arguments.integer();
        color_ = std::uint16_t(std::uint16_t(r) | std::uint16_t(g << 5) | std::uint16_t(b << 10));
        alpha_ = std::uint8_t(arguments.integer());
        if (columns > 10 || rows * 10 > 0x64) {
            throw std::runtime_error("toggle grid outside 10x10 storage");
        }
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                at(x, y) = kClear;
            }
        }
    });
    // ToggleAnswer(x, y, width, height): virtual +184 (020f7c7c).
    context.bind(0x4b, [&](CommandArguments& arguments) {
        const std::int16_t x = s16(arguments.integer());
        const std::int16_t y = s16(arguments.integer());
        const std::int16_t width = s16(arguments.integer());
        const std::int16_t height = s16(arguments.integer());
        if (answer_.size() < 16) {
            answer_.push_back({x, y, width, height});
        }
    });
    // ToggleDisabled(x, y, width, height): virtual +188 (020f7cc8).
    context.bind(0x6e, [&](CommandArguments& arguments) {
        int x = arguments.integer();
        int y = arguments.integer();
        const int width = arguments.integer();
        const int height = arguments.integer();
        int right = x + width;
        int bottom = y + height;
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }
        // Quirk kept from the original: an end beyond the grid clamps the
        // *other* axis' end.
        if (right > columns_) {
            bottom = columns_;
        }
        if (bottom > rows_) {
            right = rows_;
        }
        for (int row = y; row < bottom; ++row) {
            for (int column = x; column < right; ++column) {
                grid_[std::size_t(row * 10 + column)] = kDisabled;
            }
        }
    });
    ScriptInterpreter(script).run(context);
    reset(); // 020f78a0 calls virtual +204 after the script.
}

int TogglePuzzle::cellX(int pixel) const {
    // Shifting by 16 cells keeps the division positive, so positions left of
    // the grid map to negative (invalid) cells.
    pixel += cellWidth_ * 16;
    return sdiv(pixel - originX_, cellWidth_) - 16;
}

int TogglePuzzle::cellY(int pixel) const {
    pixel += cellHeight_ * 16;
    return sdiv(pixel - originY_, cellHeight_) - 16;
}

void TogglePuzzle::reset() {
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < columns_; ++x) {
            if (at(x, y) != kDisabled) {
                at(x, y) = kClear;
            }
        }
    }
}

void TogglePuzzle::update(const TouchFilter& t) {
    // The original has an engine state at +0x74, but it is always 0: every
    // new press (TouchFilter state 1) on an enabled cell toggles it.
    if (t.state != 1) {
        return;
    }
    // Comparing as unsigned 16-bit rejects negative cells too.
    const auto cx = std::uint16_t(cellX(t.pressX));
    const auto cy = std::uint16_t(cellY(t.pressY));
    if (cx >= std::uint16_t(columns_) || cy >= std::uint16_t(rows_)) {
        return;
    }
    auto& cell = at(cx, cy);
    if (cell == kDisabled) {
        return;
    }
    cell = cell == kClear ? kFilled : kClear;
    sounds.play(cell == kClear ? 0xf8 : 0xf7);
}

bool TogglePuzzle::solved() const {
    // Every enabled cell must be filled exactly when it lies inside one of
    // the answer rectangles.
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < columns_; ++x) {
            const auto cell = grid_[std::size_t(y * 10 + x)];
            if (cell == kDisabled) {
                continue;
            }
            bool inAnswer = false;
            for (const auto& r : answer_) {
                if (r.x <= x && r.y <= y && x < r.x + r.width && y < r.y + r.height) {
                    inAnswer = true;
                    break;
                }
            }
            if ((cell == kFilled) != inAnswer) {
                return false;
            }
        }
    }
    return true;
}

std::vector<TogglePuzzle::Cell> TogglePuzzle::cells() const {
    // Filled cells are drawn one pixel narrower and shorter than the cell.
    std::vector<Cell> out;
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < columns_; ++x) {
            if (grid_[std::size_t(y * 10 + x)] == kFilled) {
                out.push_back({cellWidth_ * x + originX_, cellHeight_ * y + originY_,
                               cellWidth_ - 1, cellHeight_ - 1});
            }
        }
    }
    return out;
}

} // namespace picarat
