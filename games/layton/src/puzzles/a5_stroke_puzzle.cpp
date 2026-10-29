// The fifth adventure's stroke puzzles (catalogue type 46, loop 3250;
// puzzle 76): dots joined by lines, some lines taken away or fixed; the
// player draws and erases lines by sliding the stylus from dot to dot, and
// the judge compares the drawn lines with the script's answer.
//
// Reconstructed from the loop's command handler 0x1e4a9c (vtable 0x4012d4),
// its input hook 0x1e6114 and helpers: the line object 0x1e66d4, the dot
// layouts (command 0 and 0x1e5bb8), the lines between neighbours 0x1e5ac4 /
// 0x28e8bc, taking lines and dots away 0x1e4998 / 0x28e98c, the stroke
// 0x1e45a0 with its line toggle 0x1e44e0 and its walks 0x1e4834 /
// 0x28ea34.
//
// How it plays: command 0 lays out a square dot grid, command 1 a staggered
// one (odd columns half a row lower; their grid rows count in halves). The
// lines join each dot to its right-hand neighbour and the one below, in the
// staggered layout also the odd columns' dots to the two dots diagonally
// below. Lines are numbered in that order of creation, which is what the
// script's numbers refer to. Taking a dot away (60) takes its lines with
// it; lines may be taken away (41) or fixed (42; fixed lines never toggle).
// The stroke runs in the common script's handlers (prog_nazo
// "Nazo_Cut2Select" / "Nazo_Cut2Click" on the full-screen "line_obj"): a
// press on a dot (50: the first dot within the touch radius) sets the
// stroke's dot (51); while sliding, the dot under the pen becomes the
// target (52). After the touch dispatch the input hook toggles the line
// between the stroke's dot and the target (with the draw or erase sound)
// and moves the stroke's dot there; with no such line it walks from the
// stroke's dot towards the target and toggles the first line it finds from
// the stroke's dot to a dot on the way. Drawn lines show as quads in the
// script's colour (2), and a square of half size 4 marks the stroke's dot.
// The judge (22) asks that every line still there is drawn exactly where
// the answer (21) has one.
//
// Not reconstructed: command 40 (0x1e5d58: a new dot in the middle of a
// line, shifting the grid rows or columns behind it), which no script of
// this game sends.
#include "puzzles/a5_special.hpp"
#include <cmath>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

// 0x1e65ec: the half size of the stroke's dot mark.
constexpr float kMark = 4.0f;

class StrokePuzzle final : public SpecialPuzzle {
public:
    explicit StrokePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;

private:
    // A dot (0x10 bytes).
    struct Dot {
        int id = -1;          // +0x00
        int px = 0, py = 0;   // +0x04: pixels
        int gx = 0, gy = 0;   // +0x08: grid position
        bool removed = false; // +0x0c
    };

    // A line (0x1c bytes).
    struct Line {
        int id = -1, a = -1, b = -1; // +0x00: its dots
        bool removed = false;        // +0x0c
        bool drawn = false;          // +0x10
        bool fixed = false;          // +0x14
        bool answer = false;         // +0x18
    };

    Dot* dot(int id) {
        for (Dot& d : dots_) {
            if (d.id == id) {
                return &d;
            }
        }
        return nullptr;
    }

    Line* line(int id) {
        for (Line& l : lines_) {
            if (l.id == id) {
                return &l;
            }
        }
        return nullptr;
    }

    void addDot(int px, int py, int gx, int gy) {
        Dot d;
        d.id = int(dots_.size());
        d.px = std::int16_t(px);
        d.py = std::int16_t(py);
        d.gx = std::int16_t(gx);
        d.gy = std::int16_t(gy);
        dots_.push_back(d);
    }

    // 0x28e8bc: a line between two different dots that both exist.
    void join(int a, int b) {
        if (!dot(a) || !dot(b) || a == b) {
            return;
        }
        Line l;
        l.id = int(lines_.size());
        l.a = a;
        l.b = b;
        lines_.push_back(l);
    }

    // 0x1e5ac4: each dot to its right-hand neighbour, then to the one below
    // (by number; numbers past the last dot join nothing).
    void joinNeighbours() {
        for (int y = 0; y < rows_; ++y) {
            for (int x = 0; x < columns_ - 1; ++x) {
                join(y * columns_ + x, y * columns_ + x + 1);
            }
        }
        for (int y = 0; y < rows_ - 1; ++y) {
            for (int x = 0; x < columns_; ++x) {
                join(y * columns_ + x, y * columns_ + x + columns_);
            }
        }
    }

    // Lines that touch dot `id` (the line itself included).
    int linesAt(int id) const {
        int n = 0;
        for (const Line& l : lines_) {
            if (l.a == id || l.b == id) {
                ++n;
            }
        }
        return n;
    }

    void removeLine(Line& l);
    void removeDot(int id);
    void createLineObject();
    bool setup(Call& c, std::size_t first, std::size_t count, bool staggered);
    int toggle(int from, int to);
    int walk(int x, int y, int dx, int dy, int steps, int& atX, int& atY);
    void stroke();
    void redraw();

    Game& game_;
    int strokeDot_ = -1;             // +0x160
    int target_ = -1;                // +0x164
    bool staggered_ = false;         // +0x168
    int originX_ = 0, originY_ = 0;  // +0x16c / +0x16e
    int columns_ = 0, rows_ = 0;     // +0x170 / +0x172: grid size in dots
    std::uint32_t object_ = 0;       // +0x174: "line_obj"
    int spacing_ = 0;                // +0x178 (square grid)
    int spacingX_ = 0, halfRow_ = 0; // +0x17c / +0x180 (staggered grid)
    float lineWidth_ = 0;            // +0x184
    float touchRadius_ = 0;          // +0x188
    bool dirty_ = false;             // +0x18c
    int drawSound_ = 0;              // +0x190
    int eraseSound_ = 0;             // +0x194
    std::vector<Dot> dots_;          // +0x1a4
    std::vector<Line> lines_;        // +0x1b0
};

// 0x1e4998: a line taken away; a dot left without lines would go too, but
// the count includes the line itself, so it never does.
void StrokePuzzle::removeLine(Line& l) {
    l.removed = true;
    if (linesAt(l.a) == 0) {
        removeDot(l.a);
    }
    if (linesAt(l.b) == 0) {
        removeDot(l.b);
    }
}

// 0x28e98c: a dot taken away with every line at it.
void StrokePuzzle::removeDot(int id) {
    Dot* d = dot(id);
    if (!d) {
        return;
    }
    d->removed = true;
    for (Line& l : lines_) {
        if (l.a == id || l.b == id) {
            removeLine(l);
        }
    }
}

// 0x1e66d4: "line_obj" (value: its name) on the lower screen (priority 111,
// kind 0) with a
// quad list holding one 1x1 quad at (-1, 0), touchable over the whole screen
// (kind 1), with the common script's stroke handlers (mode 2).
void StrokePuzzle::createLineObject() {
    Object& o = createQuadObject(game_, "line_obj");
    object_ = o.hash;
    o.values[0] = Value::ofText("line_obj"); // its one value: the name
    o.areas.push_back({1, 0, 0, 320, 240});
    const std::string handlers[] = {"Nazo_Cut2Normal", "Nazo_Cut2Select", "Nazo_Cut2Click"};
    game_.setHandlers(o, 2, handlers);
}

// Commands 0 and 1: the layout. Square (0): dots across, down; first dot
// x, y; spacing; a byte count (unused); line width; touch radius; draw and
// erase sounds. Staggered (1): the same with the column spacing and the
// half row height in place of the spacing.
bool StrokePuzzle::setup(Call& c, std::size_t first, std::size_t count, bool staggered) {
    if (count < (staggered ? 11u : 10u)) {
        return false;
    }
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    createLineObject();
    staggered_ = staggered;
    columns_ = std::int16_t(integer(0));
    rows_ = std::int16_t(integer(1));
    originX_ = std::int16_t(integer(2));
    originY_ = std::int16_t(integer(3));
    const std::size_t at = staggered ? 7 : 6;
    if (staggered) {
        spacingX_ = integer(4);
        halfRow_ = integer(5);
    } else {
        spacing_ = integer(4);
    }
    lineWidth_ = c.real(first + at);
    touchRadius_ = c.real(first + at + 1);
    drawSound_ = integer(at + 2);
    eraseSound_ = integer(at + 3);
    dots_.clear();
    lines_.clear();
    if (!staggered) {
        for (int y = 0; y < rows_; ++y) {
            for (int x = 0; x < columns_; ++x) {
                addDot(originX_ + x * spacing_, originY_ + y * spacing_, x, y);
            }
        }
        joinNeighbours();
        c.result = Value::ofInt(1);
        return true;
    }
    // 0x1e5bb8: odd columns half a row lower; grid rows in halves.
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < columns_; ++x) {
            const bool odd = (x & 1) != 0;
            addDot(originX_ + x * spacingX_, originY_ + y * halfRow_ * 2 + (odd ? halfRow_ : 0), x,
                   y * 2 + (odd ? 1 : 0));
        }
    }
    rows_ = std::int16_t(rows_ * 2);
    joinNeighbours();
    // The odd columns' dots to the dots diagonally below.
    for (int y = 0; y < rows_ - 1; ++y) {
        for (int x = 1; x < columns_; x += 2) {
            const int i = y * columns_ + x;
            join(i, i - 1 + columns_);
            if (x + 1 < columns_) {
                join(i, i + columns_ + 1);
            }
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

// 0x1e44e0: the line between dots `from` and `to` (either way round, still
// there) toggles with its sound: 1; a fixed one stays: -1; no line: 0.
int StrokePuzzle::toggle(int from, int to) {
    if (from == to) {
        return 0;
    }
    for (Line& l : lines_) {
        if (l.id < 0 || l.removed) {
            continue;
        }
        if (!((l.a == from && l.b == to) || (l.a == to && l.b == from))) {
            continue;
        }
        if (l.fixed) {
            return -1;
        }
        l.drawn = !l.drawn;
        if (const int sound = l.drawn ? drawSound_ : eraseSound_; sound != 0) {
            game_.playEffect(sound);
        }
        return 1;
    }
    return 0;
}

// 0x28ea34: from grid position (x, y), step by (dx, dy) up to `steps`
// times while inside the grid; at each step the line from the stroke's dot
// to the dot there (if any) is tried. Returns that dot once a line toggled
// (or was fixed), else -1. (atX, atY) follows the walk.
int StrokePuzzle::walk(int x, int y, int dx, int dy, int steps, int& atX, int& atY) {
    atX = x;
    atY = y;
    for (;;) {
        atX = std::int16_t(atX + dx);
        atY = std::int16_t(atY + dy);
        if (atX < 0 || atX >= columns_ || atY < 0 || atY >= rows_) {
            return -1;
        }
        int found = -1;
        for (const Dot& d : dots_) {
            if (d.gx == atX && d.gy == atY) {
                found = d.id;
                break;
            }
        }
        if (toggle(strokeDot_, found) != 0) {
            return found;
        }
        if (--steps <= 0) {
            return -1;
        }
    }
}

// 0x1e45a0: the stroke after the touch dispatch.
void StrokePuzzle::stroke() {
    if (strokeDot_ < 0 || target_ < 0 || strokeDot_ == target_) {
        return;
    }
    if (toggle(strokeDot_, target_) != 0) {
        strokeDot_ = target_;
        return;
    }
    const Dot* from = dot(strokeDot_);
    const Dot* to = dot(target_);
    int x = 0, y = 0, reached = -1;
    if (staggered_) {
        if (!from || !to) {
            return;
        }
        const int dx = std::int16_t(to->gx - from->gx), dy = std::int16_t(to->gy - from->gy);
        const int steps = std::max(columns_, rows_);
        if (to->gx == from->gx) {
            reached = walk(from->gx, from->gy, 0, dy < 0 ? -1 : 1, steps, x, y);
        } else if (to->gy == from->gy) {
            const int sx = dx < 0 ? -1 : 1;
            reached = walk(from->gx, from->gy, sx, 1, steps, x, y);
            if (reached < 0) {
                reached = walk(from->gx, from->gy, sx, -1, steps, x, y);
            }
        } else {
            const int sx = dx < 0 ? -1 : 1;
            reached = walk(from->gx, from->gy, sx, dy < 0 ? -1 : 1, steps, x, y);
            if (reached < 0) {
                reached = walk(from->gx, from->gy, 0, dy < 0 ? -1 : 1, steps, x, y);
            }
        }
    } else {
        // 0x1e4834, one step and then the whole grid: first along the
        // column, then along the row from where the first walk ended.
        auto grid = [&](int stepsX, int stepsY) {
            if (!from || !to) {
                return -1;
            }
            int px = from->gx, py = from->gy;
            if (to->gy != from->gy) {
                const int r = walk(px, py, 0, to->gy - from->gy > 0 ? 1 : -1, stepsY, x, y);
                if (r >= 0) {
                    return r;
                }
                px = x;
                py = y;
            }
            if (to->gx != from->gx) {
                const int r = walk(px, py, to->gx - from->gx > 0 ? 1 : -1, 0, stepsX, x, y);
                if (r >= 0) {
                    return r;
                }
            }
            return -1;
        };
        if (!from || !to) {
            return;
        }
        reached = grid(1, 1);
        if (reached < 0) {
            reached = grid(columns_, rows_);
        }
    }
    if (reached >= 0) {
        strokeDot_ = reached;
    }
}

// 0x1e6114 (drawing part): the drawn lines and the stroke's dot as quads.
// A line is drawn when at least one of its dots is found (0x1e62c0 tests
// "either", both always are).
void StrokePuzzle::redraw() {
    Object* o = game_.scene.object(object_);
    if (!o) {
        return;
    }
    o->quads.clear();
    for (const Line& l : lines_) {
        if (l.id < 0 || l.removed || !l.drawn) {
            continue;
        }
        const Dot* a = dot(l.a);
        const Dot* b = dot(l.b);
        if (!a || !b) {
            continue;
        }
        addLineQuad(*o, {float(a->px), float(a->py)}, {float(b->px), float(b->py)}, lineWidth_);
    }
    if (const Dot* d = dot(strokeDot_)) {
        addDotQuad(*o, {float(d->px), float(d->py)}, kMark);
    }
    dirty_ = false;
}

// 0x1e6114: the stroke, then the drawing while it is asked for or a stroke
// is on.
void StrokePuzzle::input() {
    stroke();
    if (!dirty_ && (strokeDot_ < 0 || target_ < 0)) {
        return;
    }
    redraw();
}

bool StrokePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    // The lines named by every operand.
    auto eachLine = [&](auto&& f) {
        for (std::size_t i = 0; i < count; ++i) {
            if (Line* l = line(integer(i))) {
                f(*l);
            }
        }
    };
    switch (number) {
    case 0:
    case 1:
        return setup(c, first, count, number == 1);
    case 2: {
        // The lines' colour (red, green, blue, alpha; 0..1, cut to five bits
        // and one bit and back through the tables at 0x3e72ac, n / 31, and
        // 0x3e6b3c).
        if (count < 4) {
            return false;
        }
        if (Object* o = game_.scene.object(object_)) {
            auto five = [&](std::size_t i) {
                const float v = std::clamp(c.real(first + i), 0.0f, 1.0f);
                return float(unsigned(v * 31.9999f) & 31) / 31.0f;
            };
            const float a = std::clamp(c.real(first + 3), 0.0f, 1.0f);
            o->colour.set({five(0), five(1), five(2)});
            o->alpha.set({(unsigned(a * 1.9999f) & 1) ? 1.0f : 0.0f});
        }
        return result(1);
    }
    case 10:
        // Every line undrawn.
        for (Line& l : lines_) {
            l.drawn = false;
        }
        dirty_ = true;
        return result(1);
    case 20:
        for (Line& l : lines_) {
            l.answer = false;
        }
        return result(1);
    case 21:
        if (count < 1) {
            return false;
        }
        eachLine([](Line& l) { l.answer = true; });
        return result(1);
    case 22:
        // Solved: every line still there drawn exactly where the answer is.
        for (const Line& l : lines_) {
            if (!l.removed && l.id >= 0 && l.answer != l.drawn) {
                return result(0);
            }
        }
        return result(1);
    case 23:
    case 30:
    case 31:
        return result(1);
    case 41:
        if (count < 1) {
            return false;
        }
        eachLine([&](Line& l) { removeLine(l); });
        return result(1);
    case 42:
        if (count < 1) {
            return false;
        }
        eachLine([](Line& l) { l.fixed = true; });
        return result(1);
    case 50: {
        // The first dot still there within the touch radius of (x, y) into
        // the register.
        if (count < 3) {
            return false;
        }
        const int x = std::int16_t(integer(0)), y = std::int16_t(integer(1));
        int found = -1;
        for (const Dot& d : dots_) {
            if (d.removed) {
                continue;
            }
            const int dx = std::int16_t(x - d.px), dy = std::int16_t(y - d.py);
            if (std::sqrt(float(dx * dx + dy * dy)) <= touchRadius_) {
                found = d.id;
                break;
            }
        }
        if (Value* r = c.reg(first + 2)) {
            *r = Value::ofInt(found);
        }
        return result(found >= 0 ? 1 : 0);
    }
    case 51:
        if (count < 1) {
            return false;
        }
        strokeDot_ = integer(0);
        return result(1);
    case 52:
        // The target; none ends the stroke.
        if (count < 1) {
            return false;
        }
        target_ = integer(0);
        if (target_ < 0) {
            strokeDot_ = -1;
            dirty_ = true;
        }
        return result(1);
    case 60:
        // Dots taken away with their lines.
        if (count < 1) {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            removeDot(integer(i));
        }
        return result(1);
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeStrokePuzzle(Game& game) {
    return std::make_unique<StrokePuzzle>(game);
}

} // namespace picarat::adventure5
