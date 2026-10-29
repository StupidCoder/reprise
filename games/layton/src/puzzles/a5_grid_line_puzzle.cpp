// The fifth adventure's grid line puzzles (catalogue type 10, loop 3120;
// puzzles 16, 65, 66, 99, 107, 143): a dot grid whose allowed edges the
// script lists; the player draws and erases edges by sliding the stylus
// from dot to dot, and the script compares the drawing with its solutions.
//
// Reconstructed from the loop's command handler 0x1b5bfc (vtable 0x400980,
// constructor 0x3825a8), its input hook 0x1b6744 (press 0x1b5468, slide
// 0x1b7718, release 0x1b75c4) and the drawing 0x1b7fb4 / 0x1b50c0, with
// the edge helpers the play loops share (0x29789c edge index, 0x297774
// edge allowed, 0x2977f8 pixel to dot).
//
// How it plays: a press within the snap radius of a dot that has an allowed
// edge starts a stroke there. While sliding, the neighbouring dot nearest
// the pen becomes the target; once the pen is close enough to it, the edge
// to it toggles (drawn or erased, with the draw or erase sound) and the
// stroke continues from that dot. The judge button stays off while nothing
// is drawn (flag "＜ナゾボタン＞判定無効"). Drawn edges show as quads
// ("line_obj", priority 111) in the script's colour; straight runs merge
// into one quad, and an 8x8 square marks the stroke's dot.
//
// Not reconstructed: the class's start-point mode (setup operand 9, the
// fields from +0x1b8 and commands 7-10), its buttons (command 11, +0x1a4)
// and its erase-by-tap mode (command 12, +0x1a0); no puzzle of this game
// turns them on (every setup passes 0 for both counts, no script sends
// 7-12).
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

class GridLinePuzzle final : public SpecialPuzzle {
public:
    explicit GridLinePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;
    std::string describe() const override;

private:
    // Edge flags (+0 of the ten-byte entries at +0x190).
    enum : std::uint8_t { kAllowed = 1, kDrawn = 2, kSolution = 4 };

    struct Edge {
        std::uint8_t flags = 0;
        int x1 = -1, y1 = -1, x2 = -1, y2 = -1; // its dots (command 2)
    };

    struct Point {
        int x = -1, y = -1;
        bool operator==(const Point&) const = default;
    };

    // 0x29789c: the edge from dot (x, y) in direction (dx, dy): the w x
    // (h + 1) horizontal edges first, then the (w + 1) x h vertical ones.
    // There is no upper bound check (callers check).
    Edge* edge(int x, int y, int dx, int dy) {
        const int ix = std::int16_t(x + (dx < 0 ? -1 : 0));
        const int iy = std::int16_t(y + (dy < 0 ? -1 : 0));
        const int v = std::abs(dy);
        if (ix < 0 || iy < 0) {
            return nullptr;
        }
        const int index = iy * (v + width_) + width_ * (height_ + 1) * v + ix;
        return index < int(edges_.size()) ? &edges_[std::size_t(index)] : nullptr;
    }

    // 0x297774: whether that edge lies in the grid and is allowed.
    bool allowed(int x, int y, int dx, int dy) {
        const auto inside = [&](int px, int py) {
            return px >= 0 && px <= width_ && py >= 0 && py <= height_;
        };
        if (!inside(x, y) || !inside(x + dx, y + dy)) {
            return false;
        }
        const Edge* e = edge(x, y, dx, dy);
        return e && (e->flags & kAllowed);
    }

    // 0x2977f8: a pixel position to dot coordinates, rounded to the
    // nearest dot or towards the first dot (both by truncating division).
    Point toDot(Point p, bool round) const {
        if (size_ == 0) {
            return p;
        }
        const int half = round ? size_ / 2 : 0;
        return {std::int16_t((p.x - originX_ + half) / size_),
                std::int16_t((p.y - originY_ + half) / size_)};
    }

    Point toPixel(Point d) const {
        return {std::int16_t(d.x * size_ + originX_), std::int16_t(d.y * size_ + originY_)};
    }

    bool setup(Call& c, std::size_t first, std::size_t count);
    bool reset();
    bool markLine(int x1, int y1, int x2, int y2, bool solution);
    void press(Point touch);
    void slide(Point touch);
    void release();
    bool toggle(Point from, int dx, int dy, bool& drawn);
    void redraw();
    void addRun(Object& o, int x, int y, int dx, int dy);

    Object* lines() { return game_.scene.object(linesHash_); }

    void setFlag(const char* name, bool on) {
        if (const int id = game_.flagId(name); id >= 0) {
            game_.setFlag(id, on);
        }
    }

    Game& game_;
    int originX_ = 0, originY_ = 0; // +0x160 / +0x162: the first dot
    int width_ = 0, height_ = 0;    // +0x164 / +0x166: cells across, down
    int size_ = 0;                  // +0x168: dot spacing
    float lineWidth_ = 0;           // +0x16c
    float snapRadius_ = 0;          // +0x170: a press must be this near a dot
    int drawSound_ = 0;             // +0x174
    int eraseSound_ = 0;            // +0x178
    std::uint32_t linesHash_ = 0;   // +0x188
    std::vector<Edge> edges_;       // +0x190
    bool dirty_ = false;            // +0x19c
    int stroke_ = 0;                // +0x19d: 1 while a stroke draws
    Point last_;                    // +0x1a6 (only ever cleared)
    Point current_;                 // +0x1aa: the stroke's dot
    int drawnCount_ = 0;            // +0x1b0
};

// Command 0, 0x1b61f4: the grid (first dot x, y; cells across, down; dot
// spacing; line width; snap radius; draw and erase sounds [; start-point
// mode; start points]) and its line object.
bool GridLinePuzzle::setup(Call& c, std::size_t first, std::size_t count) {
    if (count < 8) {
        return false;
    }
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    originX_ = std::int16_t(integer(0));
    originY_ = std::int16_t(integer(1));
    width_ = std::int16_t(integer(2));
    height_ = std::int16_t(integer(3));
    size_ = integer(4);
    lineWidth_ = c.real(first + 5);
    snapRadius_ = c.real(first + 6);
    drawSound_ = integer(7);
    eraseSound_ = c.count() > first + 8 ? integer(8) : 0;
    // "line_obj" on the lower screen, priority 111, kind 0, with a quad
    // list (0x297400) holding one 1x1 quad at (-1, 0) until the first
    // drawing.
    linesHash_ = createQuadObject(game_, "line_obj").hash;
    edges_.assign(std::size_t(std::max(0, width_ * 2 * height_ + width_ + height_)), Edge{});
    setFlag(names::judgeOff, true);
    c.result = Value::ofInt(1);
    return true;
}

std::string GridLinePuzzle::describe() const {
    std::string out;
    char line[96];
    std::snprintf(line, sizeof line, "grid %d %d origin %d %d size %d\n", width_, height_, originX_,
                  originY_, size_);
    out += line;
    for (const Edge& e : edges_) {
        if (e.flags & kSolution) {
            std::snprintf(line, sizeof line, "solution %d %d %d %d%s\n", e.x1, e.y1, e.x2, e.y2,
                          (e.flags & kAllowed) ? "" : " (not allowed)");
            out += line;
        }
    }
    return out;
}

// Command 3, 0x1b60c0: every edge undrawn, no stroke, the judge button off.
bool GridLinePuzzle::reset() {
    for (auto& e : edges_) {
        e.flags &= std::uint8_t(~kDrawn);
    }
    last_ = current_ = Point{};
    drawnCount_ = 0;
    dirty_ = true;
    setFlag(names::judgeOff, true);
    return true;
}

// Commands 2 and 5 (0x1b6c80 / 0x1b70dc): the edges along a straight line
// between two dots of the grid become allowed (with their dots) or part
// of the solution.
bool GridLinePuzzle::markLine(int x1, int y1, int x2, int y2, bool solution) {
    const bool inside = x1 >= 0 && x1 <= width_ && x2 >= 0 && x2 <= width_ && y1 >= 0 &&
                        y1 <= height_ && y2 >= 0 && y2 <= height_;
    if (!inside || (x1 != x2 && y1 != y2) || (x1 == x2 && y1 == y2)) {
        return false;
    }
    if (x2 + y2 < x1 + y1) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    const int dx = x1 != x2 ? 1 : 0, dy = y1 != y2 ? 1 : 0;
    for (int x = x1, y = y1; x != x2 || y != y2; x += dx, y += dy) {
        if (Edge* e = edge(x, y, dx, dy)) {
            if (solution) {
                e->flags |= kSolution;
            } else {
                e->flags |= kAllowed;
                e->x1 = x;
                e->y1 = y;
                e->x2 = x + dx;
                e->y2 = y + dy;
            }
        }
    }
    return true;
}

// 0x1b5468: a press near a dot with an allowed edge starts a stroke there.
void GridLinePuzzle::press(Point touch) {
    // 0x1b6e58: the nearest dot, if within the snap radius.
    Point dot = toPixel(toDot(touch, true));
    if (snapRadius_ != 0) {
        const float dx = float(touch.x - dot.x), dy = float(touch.y - dot.y);
        if (dx * dx + dy * dy > snapRadius_ * snapRadius_) {
            return;
        }
    }
    dot = toDot(dot, false);
    if (!allowed(dot.x, dot.y, 0, 1) && !allowed(dot.x, dot.y, 0, -1) &&
        !allowed(dot.x, dot.y, 1, 0) && !allowed(dot.x, dot.y, -1, 0)) {
        return;
    }
    current_ = dot;
    stroke_ = 1;
}

// 0x1b7438: the edge from `from` toggles; the judge button turns on with
// the first drawn edge and off when none is left.
bool GridLinePuzzle::toggle(Point from, int dx, int dy, bool& drawn) {
    Edge* e = edge(from.x, from.y, dx, dy);
    if (!e || !(e->flags & kAllowed)) {
        return false;
    }
    e->flags ^= kDrawn;
    drawn = (e->flags & kDrawn) != 0;
    drawnCount_ += drawn ? 1 : -1;
    if (drawnCount_ > 0) {
        if (const int id = game_.flagId(names::judgeOff); id >= 0 && game_.flag(id)) {
            game_.setFlag(id, false);
        }
    } else if (drawnCount_ == 0) {
        setFlag(names::judgeOff, true);
    }
    return true;
}

// 0x1b7718: the pen moves during a stroke.
void GridLinePuzzle::slide(Point touch) {
    if (stroke_ != 1 || current_ == Point{}) {
        return;
    }
    if (!(current_ == last_)) {
        dirty_ = true;
    }
    // Whether an allowed edge borders the cell under the pen.
    Point cell = toDot(touch, false);
    if (touch.x - originX_ < 0) {
        cell.x = -1;
    }
    if (touch.y - originY_ < 0) {
        cell.y = -1;
    }
    const bool onEdge = allowed(cell.x, cell.y, 0, 1) || allowed(cell.x + 1, cell.y, 0, 1) ||
                        allowed(cell.x, cell.y, 1, 0) || allowed(cell.x, cell.y + 1, 1, 0);
    // The target: the neighbour of the stroke's dot nearest the pen, as a
    // pixel position. While the nearest dot is the stroke's own, the dot
    // coordinates stay (as the game leaves them) and the tests below fail.
    Point target = toDot(touch, true);
    if (!(target == current_)) {
        const Point next[4] = {{current_.x + 1, current_.y},
                               {current_.x, current_.y - 1},
                               {current_.x - 1, current_.y},
                               {current_.x, current_.y + 1}};
        float best = 1000000.0f;
        int pick = 0;
        for (int i = 0; i < 4; ++i) {
            const Point p = toPixel(next[i]);
            const float dx = float(p.x) - float(touch.x), dy = float(p.y) - float(touch.y);
            const float d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                pick = i;
            }
        }
        target = toPixel(next[pick]);
    }
    const Point targetDot = toDot(target, false);
    if (targetDot.x > width_ || targetDot.x < 0 || targetDot.y > height_ || targetDot.y < 0) {
        return;
    }
    const Point here = toPixel(current_);
    const int dx = std::int16_t(target.x - touch.x), dy = std::int16_t(target.y - touch.y);
    const int half = size_ / 2;
    // Near the target on one axis; off the allowed edges the pen must also
    // have gone past the target as seen from the stroke's dot.
    bool reach = std::abs(dx) < half || std::abs(dy) < half;
    if (reach && !onEdge) {
        float ox = 0, oy = 0;
        if (std::abs(dx) > std::abs(dy)) {
            ox = float(dx < 0 ? -size_ : size_);
        } else {
            oy = float(dy < 0 ? -size_ : size_);
        }
        const float bx = float(std::int16_t(target.x + int(ox))),
                    by = float(std::int16_t(target.y + int(oy)));
        const float d1 = (float(here.x) - bx) * (float(here.x) - bx) +
                         (float(here.y) - by) * (float(here.y) - by);
        const float d2 = (float(here.x) - float(target.x)) * (float(here.x) - float(target.x)) +
                         (float(here.y) - float(target.y)) * (float(here.y) - float(target.y));
        reach = d1 < d2;
    }
    if (!reach || last_ == current_) {
        return;
    }
    const Point a = toDot(here, false), b = targetDot;
    int stepX = 0, stepY = 0;
    bool ok = false;
    if (a.y < b.y) {
        stepY = 1;
        const Edge* e = edge(a.x, a.y, 0, 1);
        ok = e && (e->flags & kAllowed);
    }
    if (b.y < a.y && !ok) {
        stepY = -1;
        const Edge* e = edge(a.x, a.y, 0, -1);
        ok = e && (e->flags & kAllowed);
    }
    if (a.x < b.x && !ok) {
        stepX = 1;
        stepY = 0;
        const Edge* e = edge(a.x, a.y, 1, 0);
        ok = e && (e->flags & kAllowed);
    }
    if (b.x < a.x && !ok) {
        stepX = -1;
        stepY = 0;
        const Edge* e = edge(a.x, a.y, -1, 0);
        ok = e && (e->flags & kAllowed);
    }
    if (!ok) {
        return;
    }
    bool drawn = false;
    const bool toggled = toggle(a, stepX, stepY, drawn);
    current_ = {a.x + stepX, a.y + stepY};
    if (toggled) {
        if (const int sound = drawn ? drawSound_ : eraseSound_; sound != 0) {
            game_.playEffect(sound);
        }
    }
    dirty_ = true;
}

// 0x1b75c4: the stroke ends.
void GridLinePuzzle::release() {
    last_ = current_ = Point{};
    dirty_ = true;
    stroke_ = 0;
}

// 0x1b50c0: the run of drawn edges starting at dot (x, y) in direction
// (dx, dy) as one quad, half the line width wider than the run all round;
// nothing when the edge before is drawn too (the run started earlier).
void GridLinePuzzle::addRun(Object& o, int x, int y, int dx, int dy) {
    if (x - dx >= 0 && y - dy >= 0) {
        const Edge* before = edge(x - dx, y - dy, dx, dy);
        if (before && (before->flags & kDrawn)) {
            return;
        }
    }
    const int startX = x, startY = y;
    while (x + dx <= width_ && y + dy <= height_) {
        const Edge* e = edge(x, y, dx, dy);
        if (!e || !(e->flags & kDrawn)) {
            break;
        }
        x += dx;
        y += dy;
    }
    if (x == startX && y == startY) {
        return;
    }
    const Edge* e1 = edge(startX, startY, dx, dy);
    const Edge* e2 = edge(x - dx, y - dy, dx, dy);
    if (!e1 || !e2) {
        return;
    }
    const Point pa = toPixel({e1->x1, e1->y1}), pb = toPixel({e2->x2, e2->y2});
    const float ax = float(pa.x), ay = float(pa.y), bx = float(pb.x), by = float(pb.y);
    const float half = lineWidth_ * 0.5f;
    float nx = bx - ax, ny = by - ay;
    const float length = nx * nx + ny * ny;
    if (length <= 0.0f) {
        nx = ny = 0.0f;
    } else {
        const float inv = 1.0f / std::sqrt(length);
        nx *= inv;
        ny *= inv;
    }
    // Along the run (a) and across it (p).
    const float alx = nx * half, aly = ny * half;
    const float px = -aly, py = alx;
    o.quads.push_back({ax - alx - px, ay - aly - py});
    o.quads.push_back({ax - alx + px, ay - aly + py});
    o.quads.push_back({bx + alx + px, by + aly + py});
    o.quads.push_back({bx + alx - px, by + aly - py});
}

// 0x1b7fb4: the line object's quads anew: the horizontal runs, the
// vertical runs, the stroke's dot.
void GridLinePuzzle::redraw() {
    if (!dirty_) {
        return;
    }
    Object* o = lines();
    if (!o) {
        return;
    }
    o->quads.clear();
    for (int x = 0; x < width_; ++x) {
        for (int y = 0; y <= height_; ++y) {
            addRun(*o, x, y, 1, 0);
        }
    }
    for (int x = 0; x <= width_; ++x) {
        for (int y = 0; y < height_; ++y) {
            addRun(*o, x, y, 0, 1);
        }
    }
    if (!(current_ == Point{})) {
        const Point c = toPixel(current_);
        const float cx = float(c.x), cy = float(c.y);
        o->quads.push_back({cx - 4, cy - 4});
        o->quads.push_back({cx - 4, cy + 4});
        o->quads.push_back({cx + 4, cy + 4});
        o->quads.push_back({cx + 4, cy - 4});
    }
    dirty_ = false;
}

// 0x1b6744: after the touch dispatch, unless the loop's input switch or
// the flag "＜ナゾ＞操作無効" is on: a press, then the slide while
// touching (the press frame too) or the release; then the drawing.
void GridLinePuzzle::input() {
    if (game_.objectTouchLocked) {
        return;
    }
    if (const int id = game_.flagId(names::inputOff); id >= 0 && game_.flag(id)) {
        return;
    }
    const Point touch{std::int16_t(game_.input.x), std::int16_t(game_.input.y)};
    if (game_.pressed()) {
        press(touch);
    }
    if (game_.input.touching) {
        slide(touch);
    } else if (game_.released()) {
        release();
    }
    redraw();
}

bool GridLinePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return int(std::int16_t(c.integer(first + i)));
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0:
        return setup(c, first, count);
    case 1: {
        // 0x1b6b00: the lines' colour (red, green, blue; 0..255 cut to
        // five bits, 0x345bfc, and back through the table at 0x3e72ac,
        // n / 31).
        Object* o = lines();
        if (count < 3 || !o) {
            return false;
        }
        const float r = float(c.integer(first) >> 3) / 31.0f,
                    g = float(c.integer(first + 1) >> 3) / 31.0f,
                    b = float(c.integer(first + 2) >> 3) / 31.0f;
        o->colour.set({r, g, b});
        return result(1);
    }
    case 2:
    case 5:
        if (count < 4 || !markLine(integer(0), integer(1), integer(2), integer(3), number == 5)) {
            return false;
        }
        return result(1);
    case 3:
        return result(reset() ? 1 : 0);
    case 4:
        // The solution cleared (before the script marks another).
        for (auto& e : edges_) {
            e.flags &= std::uint8_t(~kSolution);
        }
        return result(1);
    case 6: {
        // Solved: every allowed edge drawn exactly where the solution is.
        bool solved = true;
        for (const Edge& e : edges_) {
            if ((e.flags & kAllowed) && bool(e.flags & kDrawn) != bool(e.flags & kSolution)) {
                solved = false;
                break;
            }
        }
        return result(solved ? 1 : 0);
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeGridLinePuzzle(Game& game) {
    return std::make_unique<GridLinePuzzle>(game);
}

} // namespace picarat::adventure5
