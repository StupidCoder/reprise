// Street-lamp engine (ARM9 overlay 52). See a2_lamp_puzzle.hpp for the rules.
#include "puzzles/a2_lamp_puzzle.hpp"
#include "system/script.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace picarat {

namespace {
// Stores a value the way the original stores it into a 16-bit field.
std::int16_t s16(int v) {
    return std::int16_t(std::uint16_t(unsigned(v)));
}

// Grid point to pixel (020f7d2c / 020f7d3c).
int pixel(int cell) {
    return (cell + 1) * 0x18;
}

// Pixel to the nearest grid point (020f7d4c / 020f7d6c). The +0x174 offset
// (16 cells minus half a cell) rounds to the nearest point and keeps the
// division positive.
int cell(int pixel) {
    return (pixel + 0x174) / 24 - 16;
}

// Float to int as the game's helper (0203aed0) does for these small values.
int f2i(float v) {
    if (std::isnan(v)) {
        return 0;
    }
    return int(v);
}

// Multiplication of two 20.12 fixed-point numbers, rounded.
std::int64_t fxMul(std::int64_t a, std::int64_t b) {
    return (a * b + 0x800) >> 12;
}
} // namespace

LampPuzzle::LampPuzzle(std::span<const std::uint8_t> script) {
    ScriptContext context;
    // LampSettings(requiredLamps, red, green, blue): virtual +1e8 (020f7d8c).
    // The colour components are 5-bit values.
    context.bind(0x67, [&](CommandArguments& arguments) {
        required_ = s16(arguments.integer());
        const int r = arguments.integer();
        const int g = arguments.integer();
        const int b = arguments.integer();
        color_ = std::uint16_t(r | (g << 5) | (b << 10));
    });
    // LampStreet(x0, y0, x1, y1): virtual +1ec (020f7da4). Only horizontal,
    // vertical and 45-degree streets of non-zero length are kept, at most 32.
    context.bind(0x68, [&](CommandArguments& arguments) {
        const int x0 = arguments.integer();
        const int y0 = arguments.integer();
        const int x1 = arguments.integer();
        const int y1 = arguments.integer();
        if (streets_.size() > 0x1f) {
            return;
        }
        if (x0 == x1 && y0 == y1) {
            return;
        }
        if (x0 != x1 && y0 != y1 && std::abs(x0 - x1) != std::abs(y0 - y1)) {
            return;
        }
        streets_.push_back({false, 0, s16(x0), s16(y0), s16(x1), s16(y1)});
    });
    // LampBlocked(x, y, width, height): virtual +1f0 (020f7e1c). Marks a
    // rectangle of grid points where no lamp can stand.
    context.bind(0x6f, [&](CommandArguments& arguments) {
        int x = arguments.integer();
        int y = arguments.integer();
        int right = x + arguments.integer();
        int bottom = y + arguments.integer();
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }
        // Quirk kept from the original: an end beyond the grid clamps the
        // *other* axis' end to 7.
        if (right > 7) {
            bottom = 7;
        }
        if (bottom > 7) {
            right = 7;
        }
        for (int row = y; row < bottom; ++row) {
            for (int column = x; column < right; ++column) {
                cells_[std::size_t(row * 7 + column)] = -1;
            }
        }
    });
    ScriptInterpreter(script).run(context);
    reset();
}

void LampPuzzle::reset() {
    // Remove all lamps (blocked points stay blocked) and turn off all streets.
    state_ = kPlaying;
    count_ = 0;
    for (auto& c : cells_) {
        if (c != -1) {
            c = 0;
        }
    }
    for (auto& street : streets_) {
        street.lit = false;
        street.fade = 0;
    }
}

void LampPuzzle::retry() {
    // Like the original, the puzzle object is rebuilt (reset) and the saved
    // lamps, their count and the lit streets are put back. The glow is not.
    const auto cells = cells_;
    const auto count = count_;
    std::vector<bool> lit;
    for (const auto& street : streets_) {
        lit.push_back(street.lit);
    }
    reset();
    cells_ = cells;
    count_ = count;
    for (std::size_t i = 0; i < streets_.size(); ++i) {
        streets_[i].lit = lit[i];
    }
}

bool LampPuzzle::on(int x, int y, const Street& s) {
    // Inside the street's bounding box (each coordinate between the ends) and
    // on its line (the cross product of the two vectors to the ends is zero).
    const bool betweenX = (x - s.x0) * (x - s.x1) < 1;
    const bool betweenY = (y - s.y0) * (y - s.y1) < 1;
    const bool collinear = (x - s.x0) * (y - s.y1) - (x - s.x1) * (y - s.y0) == 0;
    return betweenX && betweenY && collinear;
}

void LampPuzzle::update(const TouchFilter& t) {
    // Only a new press (TouchFilter state 1) on a free point or a lamp counts;
    // the point is the one nearest the press (0206e288 on the press frame).
    if (state_ != kPlaying || t.state != 1) {
        return;
    }
    const int cx = cell(t.x);
    const int cy = cell(t.y);
    if (cx < 0 || cy < 0 || cx >= 7 || cy >= 7) {
        return;
    }
    auto& c = cells_[std::size_t(cy * 7 + cx)];
    if (c == -1) {
        return;
    }
    if (c == 0) {
        sounds.play(0x11d);
        c = 1;
        count_ = s16(count_ + 1);
    } else {
        sounds.play(0x11e);
        c = 0;
        count_ = s16(count_ - 1);
    }

    // Recompute which streets are lit. The streets through the tapped point
    // (if it now has a lamp) start to glow, even when already lit by another
    // lamp; other lamps only light streets that are still dark.
    for (auto& street : streets_) {
        street.lit = false;
    }
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            if (cells_[std::size_t(y * 7 + x)] != 1) {
                continue;
            }
            const bool tapped = x == cx && y == cy;
            for (auto& street : streets_) {
                if ((tapped || !street.lit) && on(x, y, street)) {
                    street.lit = true;
                    if (tapped) {
                        street.fade = 0x3c; // 60 frames of glow
                    }
                }
            }
        }
    }
}

bool LampPuzzle::solved() const {
    // Exactly the required number of lamps, and every street lit.
    if (count_ != required_) {
        return false;
    }
    for (const auto& street : streets_) {
        if (!street.lit) {
            return false;
        }
    }
    return true;
}

void LampPuzzle::tick() {
    for (auto& street : streets_) {
        if (!street.lit) {
            street.fade = 0;
        } else if (street.fade > 0) {
            --street.fade;
        }
    }
    // Draw the streets as 6-pixel-wide translucent lines. While playing, only
    // glowing streets are drawn, fading from opaque to invisible; after
    // "Submit" every lit street is drawn fully opaque.
    frame_ = Ds3dFrame();
    for (const auto& street : streets_) {
        int alpha;
        if (state_ == kShowAll) {
            if (!street.lit) {
                continue;
            }
            alpha = 0x1f;
        } else {
            if (street.fade <= 0) {
                continue;
            }
            alpha = f2i((31.0f * float(street.fade)) / 60.0f);
        }
        addThickLine(frame_, pixel(street.x0), pixel(street.y0), pixel(street.x1), pixel(street.y1),
                     color_, 6, alpha);
    }
}

std::vector<std::array<int, 2>> LampPuzzle::lamps() const {
    // The lamp sprite is centred on its grid point (sprite origin 8 pixels
    // up and left).
    std::vector<std::array<int, 2>> out;
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            if (cells_[std::size_t(y * 7 + x)] == 1) {
                out.push_back({pixel(x) - 8, pixel(y) - 8});
            }
        }
    }
    return out;
}

void addThickLine(Ds3dFrame& frame, int x0, int y0, int x1, int y1, std::uint16_t color, int width,
                  int alpha) {
    if (alpha <= 0) {
        return;
    }
    // Everything below is 20.12 fixed point, as on the DS.
    const std::int64_t dx = std::int64_t(x1 - x0) << 12;
    const std::int64_t dy = std::int64_t(y1 - y0) << 12;
    const std::int64_t lengthSquared = fxMul(dx, dx) + fxMul(dy, dy);
    const std::int64_t half = ((std::int64_t(width) << 12) + 0x100) >> 1; // half the width
    const std::int64_t X0 = std::int64_t(x0) << 12;
    const std::int64_t Y0 = std::int64_t(y0) << 12;
    const std::int64_t X1 = std::int64_t(x1) << 12;
    const std::int64_t Y1 = std::int64_t(y1) << 12;

    // The four corners of the line's rectangle, in triangle-strip order:
    // A and C at the start, B and D at the end.
    std::int64_t ax, ay, bx, by, cx, cy, ex, ey;
    if (lengthSquared <= 0) {
        // Zero length: a square around the start point.
        ax = X0 - half;
        ay = Y0 - half;
        bx = X0 + half;
        by = Y0 - half;
        cx = X0 - half;
        cy = Y0 + half;
        ex = X0 + half;
        ey = Y0 + half;
    } else {
        // Unit direction (ux, uy) = (dx, dy) / length, computed like the DS
        // math hardware: square root 0201a2dc, (isqrt(x << 32) + 0x200) >> 10,
        // and division 0201a2cc, ((a << 32) / b + 0x80000) >> 20.
        if (lengthSquared >= (std::int64_t(1) << 31)) {
            throw std::runtime_error("thick line outside reviewed fixed-point range");
        }
        // Exact integer square root (the float estimate is corrected).
        const std::uint64_t radicand = std::uint64_t(lengthSquared) << 32;
        std::uint64_t root = std::uint64_t(std::sqrt(static_cast<long double>(radicand)));
        while (root * root > radicand) {
            --root;
        }
        while ((root + 1) * (root + 1) <= radicand) {
            ++root;
        }
        const std::int64_t length = std::int64_t((root + 0x200) >> 10);
        auto divide = [&](std::int64_t a) {
            return ((a * (std::int64_t(1) << 32)) / length + 0x80000) >> 20;
        };
        const std::int64_t ux = divide(dx);
        const std::int64_t uy = divide(dy);
        // Half-width steps along the line (nx, ny) and across it.
        const std::int64_t nx = fxMul(ux, half);
        const std::int64_t ny = fxMul(uy, half);
        const std::int64_t mx = fxMul(-ux, half);
        // The rectangle extends half a width beyond both ends.
        ax = X0 - nx - ny;
        ay = Y0 - ny - mx;
        bx = X1 + nx - ny;
        by = Y1 + ny - mx;
        cx = X0 - nx + ny;
        cy = Y0 - ny + mx;
        ex = X1 + nx + ny;
        ey = Y1 + ny + mx;
    }
    // The vertex command (VTX_16) takes 1/16-pixel coordinates; the
    // rasterizer then works in whole screen pixels.
    auto screen = [](std::int64_t v) {
        const auto sixteenth = std::int16_t(std::uint16_t(std::uint64_t(v + 0x800) >> 8));
        return int(sixteenth) >> 4;
    };
    Ds3dPolygon first{
        {{{screen(ax), screen(ay)}, {screen(bx), screen(by)}, {screen(cx), screen(cy)}, {0, 0}}},
        3,
        color,
        alpha,
        0x7fff0000};
    Ds3dPolygon second{
        {{{screen(cx), screen(cy)}, {screen(bx), screen(by)}, {screen(ex), screen(ey)}, {0, 0}}},
        3,
        color,
        alpha,
        0x7fff0000};
    frame.add(first);
    frame.add(second);
}

} // namespace picarat
