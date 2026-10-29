// Lines-between-points engine (ARM9 overlay 37). See line_puzzle.hpp for the rules.
//
// Grids are 188 columns of 188 bytes (020f78a0), indexed [x][y]. Coverage
// counts the paths through a cell; in lines mode each covered cell also
// records the line that owns it.
//
// Floating-point steps follow the game's resident IEEE helpers (0203af44
// int-to-float, 0203a290 add, 0203b2a4 subtract, 0203afd4 multiply, 0203aa94
// divide, 0203aed0 saturating float-to-int); the comparison helpers
// 0203a95c / 0203a8f4 / 0203a898 / 0203a83c treat NaN as "false". Keeping the
// same operations in the same order makes every walked cell identical.
#include "puzzles/line_puzzle.hpp"
#include "system/script.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace picarat {
namespace {
// The game's signed division helper (0203b744): division by zero returns the
// dividend, division by -1 negates with wrap-around.
int sdiv(int a, int b) {
    if (b == 0) {
        return a;
    }
    if (b == -1) {
        return int(0u - unsigned(a));
    }
    return a / b;
}

// Float to int (0203aed0): truncating and saturating; NaN converts to zero.
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

// Distance between two cells (overlay 2, 0206b0c4): single-precision squares
// and sum, then the square root (computed in double precision, which gives
// the correctly rounded single-precision result).
float distance(int x0, int y0, int x1, int y1) {
    const float dx = float(x0 - x1), dy = float(y0 - y1);
    return std::sqrt(dx * dx + dy * dy);
}

// 8-bit colour components to the DS's 15-bit RGB555 (overlay 2, 0206b114).
std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t(((r >> 3) & 31) | (((g >> 3) & 31) << 5) | (((b >> 3) & 31) << 10));
}
} // namespace

LinePuzzle::LinePuzzle(Rules rules)
    : rules_(rules), coverage_(Size * Size, 0), owner_(Size * Size, 0), cells_(Size * Size, 0),
      checked_(Size * Size, 0) {
    reset(); // 020f78a0 (first adventure 0205c574) ends with the reset.
}

LinePuzzle::LinePuzzle(std::span<const std::uint8_t> script, unsigned type, unsigned number)
    : LinePuzzle(Rules::Adventure2) {
    if (type != 9 && type != 15) {
        throw std::runtime_error("line engine type");
    }
    ScriptContext context;
    // CellKinds(x0, y0, x1, y1, kind): overlay 2 020790b8 -> virtual +12c (020f8ec0).
    context.bind(0x26, [&](CommandArguments& a) {
        const int x0 = a.integer(), y0 = a.integer(), x1 = a.integer(), y1 = a.integer();
        setCells(x0, y0, x1, y1, a.integer());
    });
    // Point(x, y): overlay 2 0207918c -> virtual +130 (020f7dfc).
    context.bind(0x28, [&](CommandArguments& a) {
        const int x = a.integer();
        addPoint(x, a.integer());
    });
    // AnswerSegment(x0, y0, x1, y1): overlay 2 020791e8 -> virtual +134 (020f7c80).
    context.bind(0x29, [&](CommandArguments& a) {
        Segment seg;
        seg.x0 = a.integer();
        seg.y0 = a.integer();
        seg.x1 = a.integer();
        seg.y1 = a.integer();
        addAnswer(seg);
    });
    // MaxLines(n): virtual +d0, stored at +0x34.
    context.bind(0x0d, [&](CommandArguments& a) { setMaxLines(a.integer()); });
    // GridOrigin(x, y): virtual +dc.
    context.bind(0x10, [&](CommandArguments& a) {
        const int x = a.integer();
        setOrigin(x, a.integer());
    });
    // GridSize(columns, rows): virtual +e4.
    context.bind(0x11, [&](CommandArguments& a) {
        const int w = a.integer();
        setGridSize(w, a.integer());
    });
    // CellSize(pixels): virtual +e0, square cells.
    context.bind(0x12, [&](CommandArguments& a) { setCellSize(a.integer()); });
    // LineColour(r, g, b) and PreviewColour(r, g, b): virtual +13c / +140.
    context.bind(0x24, [&](CommandArguments& a) {
        const int r = a.integer(), g = a.integer(), b = a.integer();
        setLineColour(rgb(r, g, b));
    });
    context.bind(0x25, [&](CommandArguments& a) {
        const int r = a.integer(), g = a.integer(), b = a.integer();
        setPreviewColour(rgb(r, g, b));
    });
    // AnyDirection(): virtual +138 with argument 1.
    context.bind(0x27, [&](CommandArguments&) { allowAnyDirection(); });
    ScriptInterpreter(script).run(context);
    start(type == 15, number);
}

void LinePuzzle::setCells(int x0, int y0, int x1, int y1, int value) {
    // Second adventure 020f8ec0, first 0205dc90.
    const auto kind = std::int8_t(value & 0xff);
    if (x0 < 0 || x0 >= width_ || y0 < 0 || y0 >= height_ || x1 < 0 || x1 >= width_ || y1 < 0 ||
        y1 >= height_) {
        return;
    }
    if (x1 <= x0) {
        std::swap(x0, x1);
    }
    if (y1 <= y0) {
        std::swap(y0, y1);
    }
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            at(cells_, x, y) = kind;
        }
    }
}

void LinePuzzle::addPoint(int x, int y) {
    // 020f7dfc / 0205cb80.
    if (x < width_ && x >= 0 && y < height_ && y >= 0) {
        at(cells_, x, y) = kPoint;
    }
}

void LinePuzzle::addAnswer(const Segment& seg) {
    // 020f7c80 / 0205ca04.
    if (answer_.size() < 32) {
        answer_.push_back(seg);
    }
}

void LinePuzzle::start(bool lines, unsigned number) {
    if (width_ < 0 || width_ > Size || height_ < 0 || height_ > Size) {
        throw std::runtime_error("line grid outside 188x188");
    }
    // 020f7e38 (first adventure 0205cbbc): list the point cells, row by row.
    points_.clear();
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (at(cells_, x, y) == kPoint) {
                points_.push_back({x, y});
            }
        }
    }
    // 020f9fdc: type 9 selects paths (with the loop sound for puzzles 42, 91
    // and 160); type 15 selects lines.
    mode_ = lines ? kLines : kPaths;
    number_ = number;
    loopFlag_ = rules_ == Rules::Adventure2 && !lines &&
                (number == 0x2a || number == 0x5b || number == 0xa0);
}

bool LinePuzzle::empty() const {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (coverage_[std::size_t(x * Size + y)] != 0) {
                return false;
            }
        }
    }
    return true;
}

int LinePuzzle::snapSound(bool limited) const {
    if (rules_ == Rules::Adventure2) {
        return 0xf9;
    }
    // 0205d1bc: puzzle 84 (internal 0x54) has its own sound with a limit.
    return limited && number_ == 0x54 ? 0xc3 : 200;
}

int LinePuzzle::placeSound() const {
    if (rules_ == Rules::Adventure2) {
        return 0xf9;
    }
    return number_ == 0x54 ? 0xc4 : 0xc9; // 0205e300
}

void LinePuzzle::emit(const Primitive& p) {
    primitives_.push_back(p);
    Item item;
    item.primitive = p;
    drawList_.push_back(item);
}

void LinePuzzle::emit(const SpriteDraw& d) {
    sprites_.push_back(d);
    Item item;
    item.sprite = true;
    item.draw = d;
    drawList_.push_back(item);
}

void LinePuzzle::setNoMoreLinesSize(int width, int height) {
    // 020f78a0: centred on the screen, then moved 32 pixels left.
    noMoreX_ = ((0x100 - width) >> 1) - 0x20;
    noMoreY_ = (0xc0 - height) >> 1;
}

void LinePuzzle::reset() {
    // 020f7f04. Cell kinds (including an active point) are left unchanged;
    // everything drawn is erased.
    touchX_ = touchY_ = -1;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            at(coverage_, x, y) = 0;
            at(owner_, x, y) = -1;
            at(checked_, x, y) = 0;
        }
    }
    lines_.fill({});
    lineColor_.fill(-1);
    previewActive_ = noMoreLines_ = loopSound_ = false;
    lastDistance_ = 0;
    // (The original also clears an unused 2D layer, 02072a10, and selects a
    // palette index, 020f8d14.)
}

void LinePuzzle::cancel() {
    // 020fa188
    markPoints();
    previewActive_ = false;
}

void LinePuzzle::markPoints() {
    // Turns the active point (if any) back into a plain point.
    for (auto [x, y] : points_) {
        at(cells_, x, y) = kPoint;
    }
}

int LinePuzzle::lineCount() const {
    int n = 0;
    for (int c : lineColor_) {
        if (c != -1) {
            ++n;
        }
    }
    return n;
}

int LinePuzzle::toggle(int x0, int y0, int x1, int y1) {
    // 020f7ccc: if the same line (in either direction) exists, remove it and
    // return -1. Otherwise add it in the first free slot and return its index,
    // or return -2 when the line limit is reached.
    int free = -1;
    for (int i = 0; i < 32; ++i) {
        if (lineColor_[i] == -1) {
            if (free == -1) {
                free = i;
            }
            continue;
        }
        const auto& l = lines_[i];
        if ((x0 == l.x0 && y0 == l.y0 && x1 == l.x1 && y1 == l.y1) ||
            (x1 == l.x0 && y1 == l.y0 && x0 == l.x1 && y0 == l.y1)) {
            lines_[i] = {};
            lineColor_[i] = -1;
            return -1;
        }
    }
    if (lineCount() >= maxLines_) {
        return -2;
    }
    lines_[free] = {x0, y0, x1, y1};
    lineColor_[free] = currentColor_;
    return free;
}

float LinePuzzle::totalLength() const {
    // 020f8bfc: the summed length of all line slots (free slots measure 0).
    float sum = 0;
    for (const auto& l : lines_) {
        sum = sum + distance(l.x0, l.y0, l.x1, l.y1);
    }
    return sum;
}

bool LinePuzzle::covered(int x0, int y0, int x1, int y1, bool stop, std::pair<int, int> stopAt) {
    // 020f7fe4. Walks the cells of the segment from its left end with a
    // float error accumulator (the same walk as walk() below). Without `stop`:
    // every cell must be covered, and each is marked as checked. With `stop`:
    // the walk only fails if it passes the cell `stopAt`.
    if (x0 >= width_ || x0 < 0 || x1 >= width_ || x1 < 0) {
        return false;
    }
    if (y0 >= height_ || y0 < 0 || y1 >= height_ || y1 < 0) {
        return false;
    }
    int cx = x0, cy = y0, ex = x1, ey = y1;
    if (!(x0 < x1)) {
        cx = x1;
        cy = y1;
        ex = x0;
        ey = y0;
    }
    float dx = float(ex - cx), dy = float(ey - cy);
    int sx = 1, sy = 1;
    if (dx < 0) {
        dx = 0.0f - dx;
        sx = -1;
    }
    if (dy < 0) {
        dy = 0.0f - dy;
        sy = -1;
    }
    auto visit = [&] {
        if (stop) {
            return !(cx == stopAt.first && cy == stopAt.second);
        }
        if (at(coverage_, cx, cy) <= 0) {
            return false;
        }
        at(checked_, cx, cy) = 1;
        return true;
    };
    if (dy < dx) {
        const float step = dy + 0.5f;
        float acc = dx;
        for (int n = 0; float(n) <= dx; ++n) {
            if (!visit()) {
                return false;
            }
            cx += sx;
            acc = acc - step;
            if (acc <= 0) {
                acc = acc + dx;
                cy += sy;
            }
        }
    } else {
        const float step = dx + 0.5f;
        float acc = dy;
        for (int n = 0; float(n) <= dy; ++n) {
            if (!visit()) {
                return false;
            }
            cy += sy;
            acc = acc - step;
            if (acc <= 0) {
                acc = acc + dy;
                cx += sx;
            }
        }
    }
    return true;
}

bool LinePuzzle::allMarked() const {
    // 020f82c4: no covered cell was left unchecked by the answer segments.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (coverage_[std::size_t(x * Size + y)] > 0 &&
                checked_[std::size_t(x * Size + y)] == 0) {
                return false;
            }
        }
    }
    return true;
}

bool LinePuzzle::solved() {
    // 020f8338: count the answer segments that are fully drawn (this marks
    // their cells; the marks persist between checks). All of them are needed,
    // unless the line limit is reached first. Then nothing may be drawn
    // outside the answer.
    int count = 0;
    for (const auto& s : answer_) {
        if (covered(s.x0, s.y0, s.x1, s.y1, false, {})) {
            ++count;
        }
    }
    if (int(answer_.size()) != count && count < maxLines_) {
        return false;
    }
    return allMarked();
}

void LinePuzzle::setPreview(int x0, int y0, int x1, int y1) {
    // 020f9500: the preview segment, clamped into the grid (so -1 becomes 0).
    auto cx = [&](int v) {
        return v < 0 ? 0 : v >= width_ ? width_ - 1 : v;
    };
    auto cy = [&](int v) {
        return v < 0 ? 0 : v >= height_ ? height_ - 1 : v;
    };
    preview_ = {cx(x0), cy(y0), cx(x1), cy(y1)};
}

void LinePuzzle::lineQuads(int x0, int y0, int x1, int y1, std::uint16_t color) {
    // Overlay 2, 02070fcc: a Bresenham line of 2x2-pixel quads. Quirk kept:
    // the deltas come from the unclamped ends, then only x (not y) is
    // clamped to 0..246.
    int dy = y1 - y0, dx = x1 - x0;
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 < 0) {
        x1 = 0;
    }
    if (x0 > 0xf6) {
        x0 = 0xf6;
    }
    if (x1 > 0xf6) {
        x1 = 0xf6;
    }
    int sy = dy < 0 ? -1 : 1;
    if (dy < 0) {
        dy = -dy;
    }
    int sx = dx < 0 ? -1 : 1;
    if (dx < 0) {
        dx = -dx;
    }
    dy *= 2;
    dx *= 2;
    auto quad = [&](int x, int y) { // 02070de8
        emit(Primitive{true, x, y, x + 2, y + 2, color});
    };
    quad(x0, y0);
    if (dx > dy) {
        int err = dy - (dx >> 1);
        while (x0 != x1) {
            if (err >= 0) {
                y0 += sy;
                err -= dx;
            }
            x0 += sx;
            err += dy;
            quad(x0, y0);
        }
    } else {
        int err = dx - (dy >> 1);
        while (y0 != y1) {
            if (err >= 0) {
                x0 += sx;
                err -= dy;
            }
            y0 += sy;
            err += dx;
            quad(x0, y0);
        }
    }
    Item end;
    end.countDepth = true; // 020101a4 counts once at its end
    drawList_.push_back(end);
}

bool LinePuzzle::walk(int x0, int y0, int x1, int y1, bool preview) {
    // 020f9594: walk the cells between two points. A preview only draws;
    // otherwise the coverage of the cells changes, and in lines mode a line
    // is added or removed.
    //
    // A real walk must end on a point. The walk starts at the left end and
    // stops at a wall (which blocks the whole segment).
    if (x0 == x1 && y0 == y1) {
        return false;
    }
    if (x0 >= width_ || x0 < 0 || x1 >= width_ || x1 < 0) {
        return false;
    }
    if (y0 >= height_ || y0 < 0 || y1 >= height_ || y1 < 0) {
        return false;
    }
    if (!preview && at(cells_, x1, y1) != kPoint) {
        return false;
    }
    int cx, cy, ex, ey;
    if (x0 < x1) {
        cx = x0;
        cy = y0;
        ex = x1;
        ey = y1;
    } else {
        cx = x1;
        cy = y1;
        ex = x0;
        ey = y0;
    }
    float dx = float(ex - cx), dy = float(ey - cy);
    int sx = 1, sy = 1;
    if (dx < 0) {
        dx = 0.0f - dx;
        sx = -1;
    }
    if (dy < 0) {
        dy = 0.0f - dy;
        sy = -1;
    }
    // Collect the cells and the points passed. If any cell is still
    // uncovered the segment is being drawn (+1); if all are covered it is
    // being erased (-1).
    std::vector<std::pair<int, int>> cells, points;
    bool blocked = false;
    int delta = -1;
    auto visit = [&] {
        const auto kind = at(cells_, cx, cy);
        if (kind == kWall) {
            blocked = true;
            return false;
        }
        if (kind == kPoint || kind == kActive) {
            points.push_back({cx, cy});
            // First adventure option +0x4fd (the second adventure's +4f1,
            // never set there): a point in between ends the walk. Only the
            // column is compared with the two ends.
            if (options.stopAtPoints && cx != x0 && cx != x1) {
                blocked = true;
                return false;
            }
        }
        if (at(coverage_, cx, cy) == 0) {
            delta = 1;
        }
        cells.push_back({cx, cy});
        return true;
    };
    if (dy < dx) {
        const float step = dy + 0.5f;
        float acc = dx;
        for (int n = 0; float(n) <= dx; ++n) {
            if (!visit()) {
                break;
            }
            cx += sx;
            acc = acc - step;
            if (acc <= 0) {
                acc = acc + dx;
                cy += sy;
            }
        }
    } else {
        const float step = dx + 0.5f;
        float acc = dy;
        for (int n = 0; float(n) <= dy; ++n) {
            if (!visit()) {
                break;
            }
            cy += sy;
            acc = acc - step;
            if (acc <= 0) {
                acc = acc + dy;
                cx += sx;
            }
        }
    }
    int result = -1;
    if (!blocked && mode_ == kLines) {
        if (preview) {
            if (rules_ == Rules::Adventure2) {
                lineQuads(cellWidth_ * x0 + originX_, cellHeight_ * y0 + originY_,
                          cellWidth_ * x1 + originX_, cellHeight_ * y1 + originY_, previewRgb_);
            } else if (!options.stopAtPoints || snapped_) {
                // 0205e300: thin previews one pixel lower right, quads not.
                const int d = options.thinLines ? 1 : 0;
                line(cellWidth_ * x0 + originX_ + d, cellHeight_ * y0 + originY_ + d,
                     cellWidth_ * x1 + originX_ + d, cellHeight_ * y1 + originY_ + d, previewRgb_);
            }
        } else {
            // Add or remove the line. A line that would make the total
            // length exceed 1300 pixels is taken back again and refused.
            int id = toggle(x0, y0, x1, y1);
            if (rules_ == Rules::Adventure2 && totalLength() > 1300.0f) {
                id = -2;
                toggle(x0, y0, x1, y1);
            }
            if (id == -1) {
                delta = -1;
                if (maxLines_ < 0x80) {
                    sounds.play(placeSound());
                }
            } else if (id == -2) {
                blocked = true;
                noMoreLines_ = true;
            } else {
                // Merge: where the new line runs along an existing one between
                // two points, replace that line by its parts beyond the new one.
                for (std::size_t i = 0; i + 1 < points.size(); ++i) {
                    if (!covered(points[i].first, points[i].second, points[i + 1].first,
                                 points[i + 1].second, false, {})) {
                        continue;
                    }
                    const int other = at(owner_, (points[i].first + points[i + 1].first) / 2,
                                         (points[i].second + points[i + 1].second) / 2);
                    if (other < 0) {
                        break;
                    }
                    const int saved = currentColor_;
                    currentColor_ = lineColor_[std::size_t(other)];
                    const auto l = lines_[std::size_t(other)];
                    // The end of the new line nearer to a given cell (020f9f70).
                    auto nearer = [&](int px, int py) {
                        return distance(px, py, x0, y0) < distance(px, py, x1, y1)
                                   ? std::pair{x0, y0}
                                   : std::pair{x1, y1};
                    };
                    const bool keepStart = covered(x0, y0, x1, y1, true, {l.x0, l.y0});
                    const auto toStart = nearer(l.x0, l.y0);
                    const bool keepEnd = covered(x0, y0, x1, y1, true, {l.x1, l.y1});
                    const auto toEnd = nearer(l.x1, l.y1);
                    // Remove the old line, then re-add the parts of it that
                    // stick out beyond the new line, in the old line's colour.
                    markPoints();
                    at(cells_, l.x0, l.y0) = kActive;
                    walk(l.x0, l.y0, l.x1, l.y1, false);
                    if (keepStart) {
                        markPoints();
                        at(cells_, l.x0, l.y0) = kActive;
                        walk(l.x0, l.y0, toStart.first, toStart.second, false);
                    }
                    if (keepEnd) {
                        markPoints();
                        at(cells_, l.x1, l.y1) = kActive;
                        walk(l.x1, l.y1, toEnd.first, toEnd.second, false);
                    }
                    currentColor_ = saved;
                }
                result = std::int8_t(id);
                delta = 1;
                // (020f8cb4 also draws the new line immediately; the frame's
                // display list draws it again from the line table.) The new
                // line's start becomes the active point.
                markPoints();
                at(cells_, x0, y0) = kActive;
                if (maxLines_ < 0x80) {
                    sounds.play(placeSound());
                }
            }
            setPreview(-1, -1, -1, -1);
        }
    }
    if (blocked) {
        snapped_ = false;
        return false;
    }
    // Apply the walk. Points along the way (except the end point) change by
    // twice the step, so a point where two paths meet keeps its coverage when
    // one of them is erased. Coverage never goes below zero.
    for (auto [x, y] : cells) {
        if (!preview) {
            if (at(cells_, x, y) == kPoint && !(x1 == x && y1 == y)) {
                at(coverage_, x, y) = std::int8_t(at(coverage_, x, y) + delta);
            }
            at(coverage_, x, y) = std::int8_t(at(coverage_, x, y) + delta);
            at(owner_, x, y) = std::int8_t(result);
            if (at(coverage_, x, y) < 0) {
                at(coverage_, x, y) = 0;
            }
        } else if (mode_ == kPaths) {
            // Path preview: fill each cell (overlay 2, 02070e8c; first
            // adventure 0200f520, on even pixels).
            const int px = even(cellWidth_ * x + originX_);
            const int py = even(cellHeight_ * y + originY_);
            emit(Primitive{false, px, py, px + cellWidth_, py + cellHeight_, previewRgb_});
        }
    }
    return true;
}

bool LinePuzzle::snap(int px, int py) {
    // 020f8f70: interprets the stylus position (in pixels). It finds
    //  - the active point (where the current path or line starts),
    //  - the nearest point within reach of the stylus ("hit"), and
    //  - the nearest point in the direction of the drag ("snapped"),
    // using squared distances in cells. Paths are committed here, point by
    // point; lines get a preview that is placed on release.
    const int gx = sdiv(px - originX_, cellWidth_); // stylus cell
    const int gy = sdiv(py - originY_, cellHeight_);
    // Reach in cells scales with the cell size: 32 pixels / (w + h) cells.
    const int q = sdiv(0x20, cellWidth_ + cellHeight_);
    int best = q * 3;
    int lineBest = mode_ == kLines ? q << 2 : 0xffff;
    bool hit = false, result = false;
    snapped_ = false;
    int ax = -1, ay = -1;
    for (auto [x, y] : points_) {
        if (at(cells_, x, y) == kActive) {
            ax = x;
            ay = y;
            break;
        }
    }
    const int ddx = gx - ax, ddy = gy - ay;
    int bx = 0, by = 0, lx = 0, ly = 0;
    for (auto [x, y] : points_) {
        const int d =
            int(unsigned(gy - y) * unsigned(gy - y) + unsigned(gx - x) * unsigned(gx - x));
        if (d < best) {
            hit = true;
            bx = x;
            by = y;
            best = d;
            result = true;
        }
        if (at(cells_, x, y) == kActive) {
            continue;
        }
        // Candidates for the next point must lie ahead in the main direction
        // of the drag (and, unless any direction is allowed, on the same row
        // or column as the active point).
        if (ax != -1) {
            const int adx = ddx < 0 ? -ddx : ddx, ady = ddy < 0 ? -ddy : ddy;
            if (adx > ady) {
                if (!freeDirection_ && ay != y) {
                    continue;
                }
                if (ax < gx) {
                    if (x <= ax) {
                        continue;
                    }
                } else if (ax > gx && x >= ax) {
                    continue;
                }
            } else {
                if (!freeDirection_ && ax != x) {
                    continue;
                }
                if (ay < gy) {
                    if (y <= ay) {
                        continue;
                    }
                } else if (ay > gy && y >= ay) {
                    continue;
                }
            }
        }
        if (d < lineBest) {
            lx = x;
            ly = y;
            lineBest = d;
            snapped_ = true;
        }
    }
    if (hit) {
        if (ax == -1) {
            // Nothing active yet: the touched point becomes the start.
            result = true;
            at(cells_, bx, by) = kActive;
        } else if (mode_ == kPaths && (freeDirection_ || ax == bx || ay == by)) {
            // Paths: reaching a point extends the path to it (020f957c),
            // and that point becomes the new active point.
            walk(ax, ay, bx, by, false);
            at(cells_, ax, ay) = kPoint;
            at(cells_, bx, by) = kActive;
        }
    }
    if (ax == -1) {
        return result;
    }
    if (mode_ == kLines) {
        // Lines: preview to the snapped point, or straight to the stylus.
        if (snapped_) {
            setPreview(ax, ay, lx, ly);
        } else {
            setPreview(ax, ay, gx, gy);
        }
        previewActive_ = true;
        return false;
    }
    // Paths: preview the path from the active point towards the next point
    // in the drag direction, as far as the stylus has gone.
    //
    // Without a direction candidate the original reads two uninitialized
    // stack words as the target (observed in one recorded run as an
    // unrelated cell, so no preview); that case is treated as "no target"
    // (a recorded gap).
    const bool ok = freeDirection_ || (snapped_ && (ax == lx || ay == ly));
    previewActive_ = false;
    if (!ok) {
        stopLoop();
        return result;
    }
    const float target = distance(ax, ay, lx, ly), touch = distance(ax, ay, gx, gy);
    const int stepX = f2i(touch * (float(lx - ax) / target)),
              stepY = f2i(touch * (float(ly - ay) / target));
    // (The second adventure also computes the change from the previous
    // distance but never uses it; the first plays its drag loop while the
    // stylus moves at least a cell further or back, 0205dd40.)
    if (rules_ == Rules::Adventure1) {
        if (std::fabs(touch - lastDistance_) >= 1.0f) {
            if (!loopSound_) {
                sounds.play(0xbe); // 0200552c
                loopSound_ = true;
            }
        } else {
            stopLoop();
        }
    }
    lastDistance_ = touch;
    auto wall = [&](int x, int y) {
        return x >= 0 && x < Size && y >= 0 && y < Size && at(cells_, x, y) == kWall;
    };
    if (touch > target) {
        setPreview(ax, ay, lx, ly);
        if (wall(lx, ly)) {
            stopLoop();
        }
    } else {
        setPreview(ax, ay, ax + stepX, ay + stepY);
        if (wall(ax + stepX, ay + stepY)) {
            stopLoop();
        }
    }
    if (touch >= 3.0f) {
        previewActive_ = true;
    }
    return result;
}

void LinePuzzle::input(const TouchFilter& t) {
    // 020f83d0: press, hold and release handling (the stylus point used is
    // 2 pixels up and left of the contact), then this frame's drawing
    // (020f8d9c).
    const int oldX = touchX_, oldY = touchY_;
    if (t.state == 1) {
        touchX_ = t.pressX;
        touchY_ = t.pressY;
        if (snap(touchX_ - 2, touchY_ - 2) && mode_ == kLines) {
            sounds.play(snapSound(maxLines_ < 0x80));
        }
        noMoreLines_ = false;
        heldFrames_ = 0;
    } else if (t.state == 2) {
        touchX_ = t.x;
        touchY_ = t.y;
        if (snap(touchX_ - 2, touchY_ - 2)) {
            // The drag sound starts once the stylus has moved more than 2
            // pixels in a frame, and stops when it stands still.
            if (mode_ == kPaths) {
                if (loopFlag_ && !loopSound_ &&
                    (std::abs(oldX - touchX_) > 2 || std::abs(oldY - touchY_) > 2)) {
                    sounds.play(0xfd);
                    loopSound_ = true;
                }
            } else if (maxLines_ < 0x80) {
                sounds.play(snapSound(true));
            }
        }
        if (mode_ == kPaths && loopFlag_ && loopSound_ && oldX == touchX_ && oldY == touchY_) {
            sounds.stop(0xfd);
            loopSound_ = false;
        }
        ++heldFrames_;
    } else if (t.state == 3) {
        const int rx = t.releaseX, ry = t.releaseY;
        touchX_ = touchY_ = -1;
        if (mode_ == kPaths) {
            stopLoop();
        } else if (snapped_) {
            // Lines: place (or remove) the previewed line.
            walk(preview_.x0, preview_.y0, preview_.x1, preview_.y1, false);
        } else if (heldFrames_ < 20) {
            // Lines: a short tap (under 20 frames) removes the line owning a
            // covered cell near the stylus.
            const int r = sdiv(8, cellWidth_ + cellHeight_);
            bool found = false;
            for (int oy = -r; oy <= r && !found; ++oy) {
                const int y = oy + sdiv(ry - 2 - originY_, cellHeight_);
                if (y < 0 || y >= height_) {
                    continue;
                }
                for (int ox = -r; ox <= r; ++ox) {
                    const int x = ox + sdiv(rx - 2 - originX_, cellWidth_);
                    if (x < 0 || x >= width_ || at(coverage_, x, y) != 1) {
                        continue;
                    }
                    markPoints();
                    const int id = at(owner_, x, y);
                    if (id < 0 || id >= 32) {
                        continue;
                    }
                    const auto l = lines_[std::size_t(id)];
                    at(cells_, l.x0, l.y0) = kActive;
                    walk(l.x0, l.y0, l.x1, l.y1, false);
                    found = true;
                    break;
                }
            }
        }
        markPoints();
        previewActive_ = false;
    }
    primitives_.clear();
    sprites_.clear();
    drawList_.clear();
    draw(true);
}

void LinePuzzle::line(int x0, int y0, int x1, int y1, std::uint16_t color) {
    if (options.thinLines) {
        Primitive p{true, x0, y0, x1, y1, color};
        p.thin = true;
        emit(p);
        Item end;
        end.countDepth = true; // 020103c8 counts once
        drawList_.push_back(end);
    } else {
        lineQuads(x0, y0, x1, y1, color);
    }
}

void LinePuzzle::draw(bool preview) {
    if (rules_ == Rules::Adventure1) {
        // 0205db78: cells or point markers (0205d578), the preview and its
        // "here" marks, the notice; the placed lines go to their own list
        // (0205d9d0, drawn by the engine after its buttons).
        drawCells();
        if (preview && previewActive_) {
            walk(preview_.x0, preview_.y0, preview_.x1, preview_.y1, true);
            if (mode_ == kLines) {
                if (snapped_) {
                    emit(SpriteDraw{Sprite::Here, preview_.x1 * cellWidth_ + originX_ - 7,
                                    preview_.y1 * cellHeight_ + originY_ - 7});
                }
                emit(SpriteDraw{Sprite::Here, preview_.x0 * cellWidth_ + originX_ - 7,
                                preview_.y0 * cellHeight_ + originY_ - 7});
            }
        }
        if (noMoreLines_) {
            emit(SpriteDraw{Sprite::NoMoreLines, noMoreX_, noMoreY_});
        }
        const std::size_t before = drawList_.size();
        drawLines();
        lineList_.assign(drawList_.begin() + std::ptrdiff_t(before), drawList_.end());
        drawList_.resize(before);
        return;
    }
    // 020f8d9c: the preview (with "here" highlights at its ends in lines
    // mode), the placed lines, the covered cells or point markers, and the
    // "no more lines" notice.
    if (preview && previewActive_) {
        walk(preview_.x0, preview_.y0, preview_.x1, preview_.y1, true);
        if (mode_ == kLines) {
            if (snapped_) {
                sprites_.push_back({Sprite::Here, preview_.x1 * cellWidth_ + originX_ - 7,
                                    preview_.y1 * cellHeight_ + originY_ - 7});
            }
            sprites_.push_back({Sprite::Here, preview_.x0 * cellWidth_ + originX_ - 7,
                                preview_.y0 * cellHeight_ + originY_ - 7});
        }
    }
    if (mode_ == kLines) {
        drawLines();
    }
    drawCells();
    if (mode_ == kLines && noMoreLines_) {
        sprites_.push_back({Sprite::NoMoreLines, noMoreX_, noMoreY_});
    }
}

void LinePuzzle::drawLines() {
    // 020f8c44 -> 020f8cb4: every placed line (020f8d14 skips free slots).
    for (int i = 0; i < 32; ++i) {
        if (lineColor_[std::size_t(i)] == -1) {
            continue;
        }
        const auto& l = lines_[std::size_t(i)];
        if (rules_ == Rules::Adventure2) {
            lineQuads(cellWidth_ * l.x0 + originX_, cellHeight_ * l.y0 + originY_,
                      cellWidth_ * l.x1 + originX_, cellHeight_ * l.y1 + originY_, lineRgb_);
            continue;
        }
        // 0205da40: one pixel lower right, in the colour of its index
        // (0205daf0 selects the text palette entry, 0200d3fc reads it).
        const int c = lineColor_[std::size_t(i)];
        if (c < 0 || c > 4) {
            continue;
        }
        line(cellWidth_ * l.x0 + originX_ + 1, cellHeight_ * l.y0 + originY_ + 1,
             cellWidth_ * l.x1 + originX_ + 1, cellHeight_ * l.y1 + originY_ + 1,
             palette[std::size_t(c)]);
    }
}

void LinePuzzle::drawCells() {
    // 020f87c4 (first adventure 0205d578). Paths: the active point in
    // green, covered area and point cells in the line colour. Lines: point
    // markers at both ends of every line; with the first adventure's
    // junction option also a marker on every cell where more than two line
    // ends meet, then the colour picker's selection mark (the second
    // adventure keeps this branch behind +4f0, never set).
    if (mode_ == kPaths) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const auto kind = at(cells_, x, y);
                const int px = even(originX_ + x * cellWidth_);
                const int py = even(originY_ + y * cellHeight_);
                if (kind == kActive) {
                    emit(Primitive{false, px, py, px + cellWidth_, py + cellHeight_,
                                   rgb(0, 0xff, 0)});
                } else if ((kind == kArea || kind == kPoint) && at(coverage_, x, y) != 0) {
                    emit(Primitive{false, px, py, px + cellWidth_, py + cellHeight_, lineRgb_});
                }
            }
        }
        return;
    }
    Grid ends;
    if (options.junctions) {
        ends.assign(Size * Size, 0);
    }
    for (int i = 0; i < 32; ++i) {
        if (lineColor_[std::size_t(i)] == -1) {
            continue;
        }
        const auto& l = lines_[std::size_t(i)];
        emit(SpriteDraw{Sprite::Point, cellWidth_ * l.x0 + originX_ - 1,
                        cellHeight_ * l.y0 + originY_ - 1});
        emit(SpriteDraw{Sprite::Point, cellWidth_ * l.x1 + originX_ - 1,
                        cellHeight_ * l.y1 + originY_ - 1});
        if (options.junctions && l.y0 < height_ && l.y1 < height_ && l.x0 < width_ &&
            l.x1 < width_) {
            ++at(ends, l.x0, l.y0);
            ++at(ends, l.x1, l.y1);
        }
    }
    if (!options.junctions) {
        return;
    }
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (std::uint8_t(at(ends, x, y)) > 2) {
                emit(SpriteDraw{Sprite::Junction, x * cellWidth_ + originX_ - 6,
                                y * cellHeight_ + originY_ - 6});
            }
        }
    }
    emit(SpriteDraw{Sprite::Picker, pickerX, pickerY});
}

} // namespace picarat
