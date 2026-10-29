// The third adventure's special puzzle engines. See a3_engines.hpp.
#include "puzzles/a3_engines.hpp"
#include "sound/a3_sound.hpp"
#include <cmath>
#include <optional>
#include <tuple>
#include <utility>

namespace picarat::adventure3 {
namespace {
using lscr::Call;
using lscr::Object;
using lscr::Scene;
using lscr::Value;

using Box = std::array<int, 4>; // left, top, right, bottom (exclusive)

void addArea(Object& o, const Box& b, int id) {
    o.areas.push_back({b[0], b[1], b[2] - b[0], b[3] - b[1], id});
}

// SLIDE (overlay 38, loaded at 0211f560). The board is a grid of square
// cells; blocks are sliders (4022 objects) snapping to it, walled in by the
// board's frame, the walls the script adds and each other. The engine keeps
// the goals - a cell and the name of the block (its sequence) that must
// cover it - and declares the puzzle solved once every goal is covered.
class SlideEngine final : public lscr::SceneEngine {
public:
    explicit SlideEngine(Assets& assets) {
        auto text = [&](std::uint32_t a) {
            return assets.codeText(kOverlay, kLoad, a);
        };
        board_ = text(0x021203cc);    // lt3_slide_back
        boardRes_ = text(0x021203dc); // nazo_base
        counter_ = text(0x021203e8);  // lt3_slide_count
        counterRes_ = text(0x021203f8);
        counterFormat_ = text(0x02120404);
        released_ = text(0x02120410); // CHR_SLIDE_N
        touched_ = text(0x0212041c);  // CHR_SLIDE_M
        verdict_ = text(0x02120428);  // the game variable of the verdict
    }

    void command(Scene& scene, int hook, Call& c) override {
        switch (hook) {
        case 0:
            board(scene, c);
            break;
        case 1:
            reset(scene);
            break;
        case 2:
            goal(c);
            break;
        case 3:
            wall(scene, c);
            break;
        case 4:
            block(scene, c);
            break;
        default:
            break; // the common engine's: nothing
        }
    }

    void frame(Scene& scene) override;

private:
    static constexpr int kOverlay = 38;
    static constexpr std::uint32_t kLoad = 0x0211f560;

    // A cell's box on the screen (x, y in cells, w x h cells).
    Box cells(int x, int y, int w, int h) const {
        return {origin_[0] + cell_ * x, origin_[1] + cell_ * y, origin_[0] + cell_ * (x + w),
                origin_[1] + cell_ * (y + h)};
    }

    // 1280 (0211f630): the board {x, y, columns, rows, cell size, counter
    // x, counter y, move sound, [moves at the start]}: its frame of four
    // walls one cell thick, and the move counter.
    void board(Scene& scene, Call& c) {
        if (c.count() < 8) {
            return;
        }
        origin_ = {c.integer(0), c.integer(1)};
        size_ = {c.integer(2), c.integer(3)};
        cell_ = c.integer(4);
        if (Object* o = scene.create(Object::Kind::Sprite, board_, boardRes_)) {
            scene.setLayer(*o, 3);
            const int w = size_[0], h = size_[1];
            addArea(*o, cells(-1, -1, w + 2, 1), 4);
            addArea(*o, cells(-1, -1, 1, h + 2), 4);
            addArea(*o, cells(w, -1, 1, h + 2), 4);
            addArea(*o, cells(-1, h, w + 2, 1), 4);
        }
        sound_ = c.integer(7);
        Value* moves = scene.sceneVariable(6);
        if (moves) {
            *moves = Value::of(c.count() >= 9 ? c.integer(8) : 0);
        }
        if (Object* o = scene.create(Object::Kind::Number, counter_, counterRes_)) {
            o->format = counterFormat_;
            o->prefix = "";
            o->spacing = 6;
            o->position.set({c.integer(5) << 12, c.integer(6) << 12});
            o->user[0] = moves ? *moves : Value::of(0);
        }
    }

    // 1281 (0211f98c): every block back to the cell it started in (its
    // values 0 and 1), taking input, its value 4 cleared; the counter shows
    // the moves again (scene variable 6).
    void reset(Scene& scene) {
        for (Object* o : scene.creationOrder()) {
            if (o->style != Object::Style::Slider || !o->thread) {
                continue;
            }
            auto& v = o->thread->variables;
            const auto& g = o->grid;
            o->cell = {std::clamp(v[0].toInt(), 0, g.last[0]),
                       std::clamp(v[1].toInt(), 0, g.last[1])};
            o->position.set({(o->cell[0] * g.size + g.origin[0]) << 12,
                             (o->cell[1] * g.size + g.origin[1]) << 12});
            o->switches[1].on = true;
            v[4] = Value::of(0);
        }
        if (Object* o = scene.find(counter_)) {
            const Value* moves = scene.sceneVariable(6);
            o->user[0] = moves ? *moves : Value::of(0);
        }
    }

    // 1282 (0211fac8): a goal {x, y, block}; at most sixteen.
    void goal(Call& c) {
        if (c.count() < 3 || goals_.size() >= 16) {
            return;
        }
        goals_.push_back({cells(c.integer(0), c.integer(1), 1, 1), c.text(2)});
    }

    // 1283 (0211fbc8): a wall {x, y, w, h} in cells, on the board.
    void wall(Scene& scene, Call& c) {
        if (c.count() < 4) {
            return;
        }
        if (Object* o = scene.find(board_)) {
            addArea(*o, cells(c.integer(0), c.integer(1), c.integer(2), c.integer(3)), 4);
        }
    }

    // 1284 (0211fc9c): a block {name, resource, sequence, cell x, cell y,
    // then its shape as boxes x, y, w, h in cells}: a slider with the
    // engine's two sections, on the board's grid (snapping in five frames),
    // each box both a handle (id 1) and a wall (id 4).
    void block(Scene& scene, Call& c) {
        if (c.count() < 9) {
            return;
        }
        Object* o = scene.create(Object::Kind::Button, c.text(0), c.text(1));
        if (!o) {
            return;
        }
        const std::array<Value, 2> start{Value::of(c.integer(3)), Value::of(c.integer(4))};
        scene.objectThread(*o, start);
        o->style = Object::Style::Slider;
        o->releasedSection = released_;
        o->touchedSection = touched_;
        scene.setLayer(*o, 2);
        lscr::play(*o, lscr::sequenceOf(*o, c.at(2)), 0);
        o->grid = {origin_, size_, cell_, 0x5000};
        o->cell = {std::clamp(c.integer(3), 0, size_[0]), std::clamp(c.integer(4), 0, size_[1])};
        o->position.set(
            {(o->cell[0] * cell_ + origin_[0]) << 12, (o->cell[1] * cell_ + origin_[1]) << 12});
        o->kindTag = 9;
        o->flags[0] = sound_;
        for (std::size_t i = 5; i + 3 < c.count(); i += 4) {
            const Box b{cell_ * c.integer(i), cell_ * c.integer(i + 1),
                        cell_ * (c.integer(i) + c.integer(i + 2)),
                        cell_ * (c.integer(i + 1) + c.integer(i + 3))};
            addArea(*o, b, 1);
            addArea(*o, b, 4);
        }
    }

    struct Goal {
        Box box;
        std::string block;
    };

    std::string board_, boardRes_, counter_, counterRes_, counterFormat_;
    std::string released_, touched_, verdict_;
    std::array<int, 2> origin_{}, size_{};
    int cell_ = 0, sound_ = 0;
    std::vector<Goal> goals_;
    bool solved_ = false;
};

void SlideEngine::frame(Scene& scene) {
    // 0211ff20: a goal is covered when a block of its name, at the cell it
    // would snap to, has a wall box overlapping the goal's cell. Once all
    // are, the verdict is set and the blocks stop taking the pen.
    if (!solved_ && !goals_.empty()) {
        std::vector<bool> covered(goals_.size());
        for (Object* o : scene.creationOrder()) {
            if (o->style != Object::Style::Slider) {
                continue;
            }
            const auto px = scene.pixel(*o);
            const auto& g = o->grid;
            std::array<int, 2> at{};
            for (int k = 0; k < 2 && g.size > 0; ++k) {
                int cell = px[size_t(k)] + (g.size >> 1) - g.origin[size_t(k)];
                cell = cell >= 0 ? cell / g.size : -((-cell) / g.size);
                at[size_t(k)] = std::clamp(cell, 0, size_[size_t(k)]) * cell_ + origin_[size_t(k)];
            }
            const std::string name = o->sequence ? o->sequence->name : "";
            for (std::size_t i = 0; i < goals_.size(); ++i) {
                const Box& b = goals_[i].box;
                for (const auto& a : o->areas) {
                    if (a.id == 4 && a.x < b[2] - at[0] && a.y < b[3] - at[1] &&
                        b[0] - at[0] < a.x + a.w && b[1] - at[1] < a.y + a.h &&
                        name == goals_[i].block) {
                        covered[i] = true;
                        break;
                    }
                }
            }
        }
        solved_ = std::all_of(covered.begin(), covered.end(), [](bool b) { return b; });
        if (solved_) {
            scene.machine.state.setVariable(Value::ofText(verdict_), 1);
        }
    }
    scene.inputBlocked = solved_;
}

// The pen's direction as the game's helpers split it (020408a0 / 02040914):
// the longer axis first, then the shorter; 0 right, 1 down, 2 left, 3 up,
// -1 none.
int majorAxis(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return -1;
    }
    return std::abs(dx) < std::abs(dy) ? (dy < 0 ? 3 : 1) : (dx < 0 ? 2 : 0);
}

int minorAxis(int dx, int dy) {
    if (std::abs(dx) < std::abs(dy)) {
        return dx == 0 ? -1 : dx < 1 ? 2 : 0;
    }
    return dy == 0 ? -1 : dy < 1 ? 3 : 1;
}

constexpr std::array<std::array<int, 2>, 4> kUnit{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

// Fills [left, right) x [top, bottom) given in fixed point, a pixel being
// covered when its top-left corner is inside (the 3D quads, measured on
// puzzle 67).
void fillFixed(Surface& s, int left, int top, int right, int bottom, std::uint16_t colour) {
    auto first = [](int v) {
        return (v + 0xfff) >> 12;
    };
    for (int y = std::max(first(top), 0); y < std::min(first(bottom), int(s.height)); ++y) {
        for (int x = std::max(first(left), 0); x < std::min(first(right), int(s.width)); ++x) {
            s.pixels[size_t(y) * size_t(s.width) + size_t(x)] = colour;
        }
    }
}

// CUT (overlay 36, loaded at 0211f560). A grid of points; the script
// enables the edges that may be drawn (1283) and, for the verdict, names
// the expected ones (1282 between 1284 and 1285). The player draws from a
// point along enabled edges, each edge passed toggling it.
class CutEngine final : public lscr::SceneEngine {
public:
    explicit CutEngine(Assets& assets) {
        auto text = [&](std::uint32_t a) {
            return assets.codeText(kOverlay, kLoad, a);
        };
        error_ = text(0x021211b8);    // chr_err
        errorRes_ = text(0x021211c0); // nazo_base
        errorSeq_ = text(0x021211cc); // msg_nowrite
        noAnswer_ = text(0x021211d8); // the flag: answer button off
        verdict_ = text(0x021211f0);
    }

    void command(Scene& scene, int hook, Call& c) override {
        switch (hook) {
        case 0:
            board(scene, c);
            break;
        case 1: // 0211ffdc: nothing drawn
            for (auto& e : edges_) {
                e.drawn = false;
            }
            scene.machine.state.setFlag(Value::ofText(noAnswer_), true);
            break;
        case 2: // 02120068: expected edges {x0, y0, x1, y1}
            line(c, [](Edge& e) { e.expected = true; });
            break;
        case 3: // 0211fde4: edges that may be drawn {x0, y0, x1, y1}
            line(c, [this](Edge& e) {
                e.enabled = true;
                points_[size_t(e.a)].used = true;
                points_[size_t(e.b)].used = true;
            });
            break;
        case 4: // 02120030: no edge expected
            for (auto& e : edges_) {
                e.expected = false;
            }
            break;
        case 5: { // 02120208: right when every edge that may be drawn is
                  // drawn exactly when expected
            const bool right = std::none_of(edges_.begin(), edges_.end(), [](const Edge& e) {
                return e.enabled && e.drawn != e.expected;
            });
            if (right) {
                scene.machine.state.setVariable(Value::ofText(verdict_), 1);
            }
            break;
        }
        default:
            break;
        }
    }

    void update(Scene& scene) override;
    void draw(const Scene& scene, Surface& screen, lscr::Screen which) const override;

private:
    static constexpr int kOverlay = 36;
    static constexpr std::uint32_t kLoad = 0x0211f560;

    struct Point {
        bool used = false;
        int x = 0, y = 0; // pixels (the game keeps bytes)
    };

    struct Edge {
        bool enabled = false, drawn = false, expected = false;
        int a = 0, b = 0; // points
    };

    // 021204f4: a grid point's index, -1 outside.
    int index(int i, int j) const {
        return i < 0 || j < 0 || i > size_[0] || j > size_[1] ? -1 : j * (size_[0] + 1) + i;
    }

    // 0212036c: the edge between two points.
    Edge* edge(int a, int b) {
        for (auto& e : edges_) {
            if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) {
                return &e;
            }
        }
        return nullptr;
    }

    // 1280 (0211f6ac): {x, y, columns, rows, spacing, line width, catch
    // distance, red, green, blue}: the points, then the edges - the rows'
    // first, then the columns'.
    void board(Scene& scene, Call& c) {
        if (c.count() < 10) {
            return;
        }
        origin_ = {c.integer(0), c.integer(1)};
        size_ = {c.integer(2), c.integer(3)};
        cell_ = c.integer(4);
        points_.clear();
        edges_.clear();
        for (int j = 0; j <= size_[1]; ++j) {
            for (int i = 0; i <= size_[0]; ++i) {
                points_.push_back(
                    {false, (origin_[0] + cell_ * i) & 0xff, (origin_[1] + cell_ * j) & 0xff});
            }
        }
        for (int j = 0; j <= size_[1]; ++j) {
            for (int i = 0; i < size_[0]; ++i) {
                edges_.push_back({false, false, false, index(i, j), index(i + 1, j)});
            }
        }
        for (int i = 0; i <= size_[0]; ++i) {
            for (int j = 0; j < size_[1]; ++j) {
                edges_.push_back({false, false, false, index(i, j), index(i, j + 1)});
            }
        }
        auto fixed = [&](std::size_t i) {
            return int(c.real(i) * 4096.0f);
        };
        width_ = fixed(5);
        reach_ = fixed(6);
        // The colour's channels 0..1, clamped, each times 31.99 (0x1ffff).
        auto channel = [&](std::size_t i) {
            const int v = std::clamp(fixed(i), 0, lscr::fx::kOne);
            return int(((std::int64_t(v) * 0x1ffff + 0x800) >> 12) >> 12) & 0x1f;
        };
        colour_ = std::uint16_t(0x8000 | channel(7) | channel(8) << 5 | channel(9) << 10);
        if (Object* o = scene.create(Object::Kind::Sprite, error_, errorRes_)) {
            lscr::play(*o, lscr::sequenceOf(*o, Value::ofText(errorSeq_)), 0);
            o->switches[0].on = false;
            scene.setLayer(*o, 1);
        }
        scene.machine.state.setFlag(Value::ofText(noAnswer_), true);
    }

    // The edges of a straight run between two grid points.
    template <typename F>
    void line(Call& c, F mark) {
        if (c.count() < 4) {
            return;
        }
        const int x0 = c.integer(0), y0 = c.integer(1), x1 = c.integer(2), y1 = c.integer(3);
        if (y0 == y1) {
            for (int i = std::min(x0, x1); i < std::max(x0, x1); ++i) {
                if (Edge* e = edge(index(i, y0), index(i + 1, y0))) {
                    mark(*e);
                }
            }
        }
        if (x0 == x1) {
            for (int j = std::min(y0, y1); j < std::max(y0, y1); ++j) {
                if (Edge* e = edge(index(x0, j), index(x0, j + 1))) {
                    mark(*e);
                }
            }
        }
    }

    std::string error_, errorRes_, errorSeq_, noAnswer_, verdict_;
    std::array<int, 2> origin_{}, size_{};
    int cell_ = 1, width_ = 0, reach_ = 0;
    std::uint16_t colour_ = 0x8000;
    std::vector<Point> points_;
    std::vector<Edge> edges_;
    int current_ = -1; // the point the pen is at
    // The tracing sound: the last marked place and whether it plays.
    static constexpr int kTraceSound = 0xfd;
    std::array<int, 2> lastMarker_{};
    bool tracing_ = false, moved_ = false;
    void step(std::array<int, 2> pen);
    std::array<int, 2> marker(std::array<int, 2> pen);
};

void CutEngine::update(Scene& scene) {
    // 02120558: with scene variable 4 set, the pen draws - unless an object
    // took it this frame.
    const Value* on = scene.sceneVariable(4);
    if (!on || on->toInt() == 0) {
        return;
    }
    const auto& in = scene.input();
    if (scene.penTaken() || !in.held) {
        if (!scene.penTaken() && in.released) {
            // Lifted: the answer button works once something is drawn; the
            // tracing sound stops.
            const bool any =
                std::any_of(edges_.begin(), edges_.end(), [](const Edge& e) { return e.drawn; });
            scene.machine.state.setFlag(Value::ofText(noAnswer_), !any);
            if (tracing_ && scene.host.sound) {
                scene.host.sound->stopEffect(kTraceSound, 0);
            }
            tracing_ = false;
        }
        current_ = -1;
        return;
    }
    const auto pen = scene.smoothPen();
    if (in.pressed) {
        // Touched: the nearest point an edge may be drawn from, closer than
        // the catch distance (Manhattan, 02120294).
        current_ = -1;
        int best = reach_;
        for (std::size_t i = 0; i < points_.size(); ++i) {
            const auto& p = points_[i];
            const int d = std::abs((p.x - pen[0]) << 12) + std::abs((p.y - pen[1]) << 12);
            if (p.used && d < best) {
                best = d;
                current_ = int(i);
            }
        }
        return;
    }
    if (current_ < 0) {
        return;
    }
    step(pen);
    // The tracing sound (0xfd) plays while the place on the lines nearest
    // the pen moves.
    const auto mark = marker(pen);
    if (scene.host.sound) {
        if (mark == lastMarker_) {
            if (tracing_) {
                scene.host.sound->stopEffect(kTraceSound, 0);
                tracing_ = false;
            }
        } else if (!tracing_) {
            scene.host.sound->playEffect(kTraceSound);
            tracing_ = true;
        }
    }
    lastMarker_ = mark;
}

void CutEngine::step(std::array<int, 2> pen) {
    // Moved: the grid point nearest the pen (02120454); when it is another
    // one, a step from the current point towards it along the longer axis,
    // else the shorter, over an edge that may be drawn - toggling it.
    const int columns = size_[0] + 1;
    const std::array<int, 2> at{current_ % columns, current_ / columns};
    std::array<int, 2> to{};
    for (int k = 0; k < 2; ++k) {
        const int p = pen[size_t(k)] + (cell_ >> 1) - origin_[size_t(k)];
        to[size_t(k)] = std::clamp(cell_ > 0 ? p / cell_ : 0, 0, size_[size_t(k)]);
    }
    if (to == at) {
        return;
    }
    const int dx = to[0] - at[0], dy = to[1] - at[1];
    for (int axis : {majorAxis(dx, dy), minorAxis(dx, dy)}) {
        if (axis < 0) {
            continue;
        }
        const int next = index(at[0] + kUnit[size_t(axis)][0], at[1] + kUnit[size_t(axis)][1]);
        Edge* e = next >= 0 ? edge(current_, next) : nullptr;
        if (e && e->enabled) {
            e->drawn = !e->drawn;
            current_ = next;
            moved_ = true;
            return;
        }
    }
}

std::array<int, 2> CutEngine::marker(std::array<int, 2> pen) {
    // 02120558: unless the pen just stepped to another point, the place
    // nearest it (Manhattan, below 512 pixels) on the edges at the current
    // point (0203b6bc), rounded; else none.
    std::array<int, 2> out{-1, -1};
    if (std::exchange(moved_, false)) {
        return out;
    }
    const std::int64_t px = std::int64_t(pen[0]) << 12, py = std::int64_t(pen[1]) << 12;
    std::int64_t best = 0x200000;
    for (const auto& e : edges_) {
        if (!e.enabled || (e.a != current_ && e.b != current_)) {
            continue;
        }
        const auto& a = points_[size_t(e.a)];
        const auto& b = points_[size_t(e.b)];
        const std::int64_t ax = std::int64_t(a.x) << 12, ay = std::int64_t(a.y) << 12;
        const std::int64_t vx = (std::int64_t(b.x) << 12) - ax, vy = (std::int64_t(b.y) << 12) - ay;
        const std::int64_t length = vx * vx + vy * vy;
        const std::int64_t dot =
            length ? std::clamp<std::int64_t>((px - ax) * vx + (py - ay) * vy, 0, length) : 0;
        const std::int64_t qx = length ? ax + vx * dot / length : ax;
        const std::int64_t qy = length ? ay + vy * dot / length : ay;
        const std::int64_t d = std::abs(qx - px) + std::abs(qy - py);
        if (d < best) {
            best = d;
            out = {int((qx + 0x800) >> 12), int((qy + 0x800) >> 12)};
        }
    }
    return out;
}

void CutEngine::draw(const Scene&, Surface& screen, lscr::Screen which) const {
    // 02120ad0: the drawn edges as lines of the given width (square ends),
    // and a square of twice the width on the point the pen is at.
    if (which != lscr::Screen::Lower) {
        return;
    }
    const int half = width_ / 2;
    for (const auto& e : edges_) {
        if (!e.drawn) {
            continue;
        }
        const auto& p = points_[size_t(e.a)];
        const auto& q = points_[size_t(e.b)];
        fillFixed(screen, (std::min(p.x, q.x) << 12) - half, (std::min(p.y, q.y) << 12) - half,
                  (std::max(p.x, q.x) << 12) + half, (std::max(p.y, q.y) << 12) + half, colour_);
    }
    if (current_ >= 0) {
        const auto& p = points_[size_t(current_)];
        fillFixed(screen, (p.x << 12) - width_, (p.y << 12) - width_, (p.x << 12) + width_,
                  (p.y << 12) + width_, colour_);
    }
}

// Fills a convex polygon (fixed-point corners, in order), a pixel being
// covered when its top-left corner is inside; corners on an edge count for
// top and left edges only (the 3D quads; the DS's own edge stepping leaves a
// few pixels' difference on slanted edges).
void fillPolygon(Surface& s, const std::vector<std::array<std::int64_t, 2>>& p,
                 std::uint16_t colour) {
    std::int64_t top = p[0][1], bottom = p[0][1], left = p[0][0], right = p[0][0];
    for (const auto& c : p) {
        top = std::min(top, c[1]);
        bottom = std::max(bottom, c[1]);
        left = std::min(left, c[0]);
        right = std::max(right, c[0]);
    }
    std::int64_t area = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& a = p[i];
        const auto& b = p[(i + 1) % p.size()];
        area += a[0] * b[1] - b[0] * a[1];
    }
    if (area == 0) {
        return;
    }
    const int sign = area > 0 ? 1 : -1;
    for (int y = std::max(int((top + 0xfff) >> 12), 0); y <= std::min(int(bottom >> 12), 191);
         ++y) {
        for (int x = std::max(int((left + 0xfff) >> 12), 0); x <= std::min(int(right >> 12), 255);
             ++x) {
            const std::int64_t px = std::int64_t(x) << 12, py = std::int64_t(y) << 12;
            bool in = true;
            for (std::size_t i = 0; i < p.size() && in; ++i) {
                const auto& a = p[i];
                const auto& b = p[(i + 1) % p.size()];
                const std::int64_t c =
                    ((b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0])) * sign;
                if (c < 0) {
                    in = false;
                } else if (c == 0) {
                    // on the edge: kept for top and left edges
                    const std::int64_t dy = (b[1] - a[1]) * sign, dx = (b[0] - a[0]) * sign;
                    in = dy < 0 || (dy == 0 && dx > 0);
                }
            }
            if (in) {
                s.pixels[size_t(y) * size_t(s.width) + size_t(x)] = colour;
            }
        }
    }
}

// 020553d4: a line `width` wide from a to b (fixed point), its ends
// squared off half the width beyond them.
void thickLine(Surface& s, std::array<int, 2> a, std::array<int, 2> b, int width,
               std::uint16_t colour) {
    const double dx = b[0] - a[0], dy = b[1] - a[1], len = std::hypot(dx, dy);
    if (len == 0) {
        return;
    }
    const double h = width / 2.0, ux = dx / len * h, uy = dy / len * h;
    auto at = [](double x, double y) {
        return std::array<std::int64_t, 2>{std::llround(x), std::llround(y)};
    };
    fillPolygon(s,
                {at(a[0] - ux + uy, a[1] - uy - ux), at(b[0] + ux + uy, b[1] + uy - ux),
                 at(b[0] + ux - uy, b[1] + uy + ux), at(a[0] - ux - uy, a[1] - uy + ux)},
                colour);
}

// LINE (overlay 37, loaded at 0211f560). Straight lines between the
// scene's anchor points (34xx): drawn from the point nearest where the pen
// comes down to the one nearest where it is lifted, up to a number of lines;
// drawing a line again, or tapping it, takes it away. Each 1282 names one
// right set of lines.
class LineEngine final : public lscr::SceneEngine {
public:
    explicit LineEngine(Assets& assets) {
        auto text = [&](std::uint32_t a) {
            return assets.codeText(kOverlay, kLoad, a);
        };
        error_ = text(0x0212131c);    // chr_err
        errorRes_ = text(0x02121324); // nazo_base
        errorSeq_ = text(0x02121330); // msg_nowrite
        noAnswer_ = text(0x0212133c);
        verdict_ = text(0x02121354);
    }

    void command(Scene& scene, int hook, Call& c) override {
        switch (hook) {
        case 0:
            setup(scene, c);
            break;
        case 1: // 0211fb44: no lines
            lines_.clear();
            scene.machine.state.setFlag(Value::ofText(noAnswer_), true);
            break;
        case 2:
            judge(scene, c);
            break;
        default:
            break;
        }
    }

    void update(Scene& scene) override;
    void draw(const Scene& scene, Surface& screen, lscr::Screen which) const override;

private:
    static constexpr int kOverlay = 37;
    static constexpr std::uint32_t kLoad = 0x0211f560;

    struct Line {
        std::string a, b;
    };

    // 1280 (0211f66c): {lines at most, width, reach, drawing red, green,
    // blue, line red, green, blue, [start sound, end sound]}.
    void setup(Scene& scene, Call& c) {
        if (c.count() < 9) {
            return;
        }
        capacity_ = std::max(c.integer(0), 0);
        lines_.clear();
        auto fixed = [&](std::size_t i) {
            return int(c.real(i) * 4096.0f);
        };
        width_ = fixed(1);
        reach_ = fixed(2);
        auto colour = [&](std::size_t first) {
            auto channel = [&](std::size_t i) {
                const int v = std::clamp(fixed(i), 0, lscr::fx::kOne);
                return int(((std::int64_t(v) * 0x1ffff + 0x800) >> 12) >> 12) & 0x1f;
            };
            return std::uint16_t(0x8000 | channel(first) | channel(first + 1) << 5 |
                                 channel(first + 2) << 10);
        };
        drawing_ = colour(3);
        colour_ = colour(6);
        // The sounds as a line starts and ends (both 0xf9 unless given).
        startSound_ = c.count() >= 11 ? c.integer(9) : 0xf9;
        endSound_ = c.count() >= 11 ? c.integer(10) : 0xf9;
        if (Object* o = scene.create(Object::Kind::Sprite, error_, errorRes_)) {
            lscr::play(*o, lscr::sequenceOf(*o, Value::ofText(errorSeq_)), 0);
            o->switches[0].on = false;
            scene.setLayer(*o, 1);
        }
        scene.machine.state.setFlag(Value::ofText(noAnswer_), true);
    }

    // 1282 (0211fb78): {point, point, ...}: right when the lines are
    // exactly these pairs.
    void judge(Scene& scene, Call& c) {
        std::vector<bool> named(lines_.size());
        for (std::size_t i = 0; i + 1 < c.count(); i += 2) {
            const auto l = find(c.text(i), c.text(i + 1));
            if (l < 0) {
                return;
            }
            named[size_t(l)] = true;
        }
        if (std::all_of(named.begin(), named.end(), [](bool b) { return b; })) {
            scene.machine.state.setVariable(Value::ofText(verdict_), 1);
        }
    }

    int find(const std::string& a, const std::string& b) const {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            if ((lines_[i].a == a && lines_[i].b == b) || (lines_[i].a == b && lines_[i].b == a)) {
                return int(i);
            }
        }
        return -1;
    }

    // 0211fd74: the line the pen is on - nearer to it than the reach and
    // strictly within its extent (its ends do not count; puzzle 18 starts a
    // line from a point another line ends on) - the last drawn first; -1 if
    // none.
    int lineAt(const Scene& scene, std::array<int, 2> pen) const;

    std::string error_, errorRes_, errorSeq_, noAnswer_, verdict_;
    int capacity_ = 0, width_ = 0, reach_ = 0, startSound_ = 0xf9, endSound_ = 0xf9;
    std::uint16_t drawing_ = 0x8000, colour_ = 0x8000;
    std::vector<Line> lines_;
    bool pulling_ = false, refused_ = false;
    std::string start_;
    int tapped_ = -1, tapFrames_ = 0;
};

int LineEngine::lineAt(const Scene& scene, std::array<int, 2> pen) const {
    const double px = pen[0], py = pen[1], reach = reach_ / 4096.0;
    for (int i = int(lines_.size()) - 1; i >= 0; --i) {
        const auto a = scene.pointPosition(lines_[size_t(i)].a);
        const auto b = scene.pointPosition(lines_[size_t(i)].b);
        if (!a || !b) {
            continue;
        }
        const double x1 = (*a)[0] / 4096.0, y1 = (*a)[1] / 4096.0;
        const double x2 = (*b)[0] / 4096.0, y2 = (*b)[1] / 4096.0;
        const double lo[2] = {std::min(x1, x2), std::min(y1, y2)};
        const double hi[2] = {std::max(x1, x2), std::max(y1, y2)};
        if (x1 == x2) {
            if (std::abs(x1 - px) < reach && lo[1] < py && py < hi[1]) {
                return i;
            }
        } else if (y1 == y2) {
            if (std::abs(y1 - py) < reach && lo[0] < px && px < hi[0]) {
                return i;
            }
        } else {
            // the distance to the line y = k x + m, then the foot of the
            // perpendicular inside the line's box
            const double k = (y2 - y1) / (x2 - x1), m = (x2 * y1 - x1 * y2) / (x2 - x1);
            if (std::abs(k * px - py + m) / std::sqrt(k * k + 1) < reach) {
                const double fx = (px + k * (py - m)) / (k * k + 1), fy = k * fx + m;
                if (lo[0] < fx && fx < hi[0] && lo[1] < fy && fy < hi[1]) {
                    return i;
                }
            }
        }
    }
    return -1;
}

void LineEngine::update(Scene& scene) {
    // 02120348, with scene variable 4 set.
    const Value* on = scene.sceneVariable(4);
    if (!on || on->toInt() == 0) {
        return;
    }
    const auto& in = scene.input();
    auto& state = scene.machine.state;
    auto sound = [&](int id) {
        if (scene.host.sound) {
            scene.host.sound->playEffect(id);
        }
    };
    auto removed = [&] {
        if (lines_.empty()) {
            state.setFlag(Value::ofText(noAnswer_), true);
        }
    };
    if (refused_ && in.pressed) {
        // A touch takes the "no more lines" sign away.
        if (Object* o = scene.find(error_)) {
            o->switches[0].on = false;
        }
        refused_ = false;
        return;
    }
    bool started = false; // a held tap that turned into drawing
    if (!pulling_) {
        if (tapped_ >= 0) {
            // A line touched: lifted within eight frames, it goes; moved
            // off it or held longer, the pen draws instead.
            if (++tapFrames_ < 8) {
                if (!in.held) {
                    lines_.erase(lines_.begin() + tapped_);
                    sound(endSound_);
                    removed();
                    tapped_ = -1;
                    tapFrames_ = 0;
                } else if (lineAt(scene, scene.smoothPen()) != tapped_) {
                    tapped_ = -1;
                    tapFrames_ = 0;
                    started = true;
                }
            } else {
                tapped_ = -1;
                tapFrames_ = 0;
                started = true;
            }
        }
    } else if (!in.held) {
        // Lifted: a line to the point nearest the pen - or that line taken
        // away if it is there already.
        const std::string end = scene.nearestPoint(
            {scene.smoothPen()[0] << 12, scene.smoothPen()[1] << 12}, ~0u, start_);
        if (!end.empty() && end != start_) {
            if (const int l = find(start_, end); l >= 0) {
                lines_.erase(lines_.begin() + l);
                sound(endSound_);
                removed();
            } else if (int(lines_.size()) < capacity_) {
                lines_.push_back({start_, end});
                sound(endSound_);
                state.setFlag(Value::ofText(noAnswer_), false);
            } else {
                sound(0x90);
                if (Object* o = scene.find(error_)) {
                    o->switches[0].on = true;
                }
                refused_ = true;
            }
        }
        start_.clear();
        pulling_ = false;
    }
    if (!started && (scene.penTaken() || !in.pressed)) {
        return;
    }
    const auto down = scene.pressPen();
    if (!started) {
        tapped_ = lineAt(scene, down);
    }
    if (tapped_ >= 0) {
        return;
    }
    const std::string from = scene.nearestPoint({down[0] << 12, down[1] << 12});
    if (!from.empty()) {
        sound(startSound_);
        start_ = from;
        pulling_ = true;
    }
}

void LineEngine::draw(const Scene& scene, Surface& screen, lscr::Screen which) const {
    // 021207c0: each line with a square of twice the width on its ends;
    // while drawing, the line from its point to the pen.
    if (which != lscr::Screen::Lower) {
        return;
    }
    auto square = [&](std::array<int, 2> c, std::uint16_t colour) {
        fillFixed(screen, c[0] - width_, c[1] - width_, c[0] + width_, c[1] + width_, colour);
    };
    for (const auto& l : lines_) {
        const auto a = scene.pointPosition(l.a), b = scene.pointPosition(l.b);
        if (!a || !b) {
            continue;
        }
        thickLine(screen, *a, *b, width_, colour_);
        square(*a, colour_);
        square(*b, colour_);
    }
    if (pulling_) {
        if (const auto a = scene.pointPosition(start_)) {
            const auto& in = scene.input();
            const std::array<int, 2> pen{in.x << 12, in.y << 12};
            thickLine(screen, *a, pen, width_, drawing_);
            square(*a, drawing_);
            square(pen, drawing_);
        }
    }
}

// BANANA (overlay 40, loaded at 0211f560). A walker on a grid of 12 x 10
// cells of 16 pixels - walls (1), the goal (2), holes (3) and ice (4) - moved
// with four arrow buttons (switches running the script's BTN_BANANA_T with
// their direction); on ice it slides on to the next wall.
class BananaEngine final : public lscr::SceneEngine {
public:
    explicit BananaEngine(Assets& assets) {
        auto text = [&](std::uint32_t a) {
            return assets.codeText(kOverlay, kLoad, a);
        };
        walker_ = text(0x021200dc); // the walking character's animation name
        res_ = text(0x021200ec);    // anm_obj
        section_ = text(0x021200f4);
        standing_ = text(0x02120104);
        for (int d = 0; d < 4; ++d) {
            // 0211fe74: per direction the walk and slide sequences;
            // 0211fe94: the arrow buttons and their sequences
            walk_[size_t(d)] = text(assets.codeWord(kOverlay, kLoad, 0x0211fe74 + 8 * d));
            slide_[size_t(d)] = text(assets.codeWord(kOverlay, kLoad, 0x0211fe78 + 8 * d));
            arrow_[size_t(d)] = text(assets.codeWord(kOverlay, kLoad, 0x0211fe94 + 8 * d));
            arrowSeq_[size_t(d)] = text(assets.codeWord(kOverlay, kLoad, 0x0211fe98 + 8 * d));
        }
    }

    void command(Scene& scene, int hook, Call& c) override {
        auto cell = [&](int value) {
            const int x = c.integer(0), y = c.integer(1);
            if (c.count() >= 2 && x >= 0 && x < kColumns && y >= 0 && y < kRows) {
                grid_[size_t(y)][size_t(x)] = value;
            }
        };
        switch (hook) {
        case 0:
            setup(scene, c);
            break;
        case 1: // 0211f7a0: back to the start
            at_ = start_;
            if (Object* o = scene.find(walker_)) {
                lscr::play(*o, lscr::sequenceOf(*o, Value::ofText(walk_[size_t(facing_)])), 0);
                o->position.set(pixel(at_));
            }
            step(scene, nullptr, -1, false);
            break;
        case 2: // 0211f858: walls {x, y, w, h}
            if (c.count() >= 4) {
                for (int y = c.integer(1); y < c.integer(1) + c.integer(3); ++y) {
                    for (int x = c.integer(0); x < c.integer(0) + c.integer(2); ++x) {
                        if (x >= 0 && x < kColumns && y >= 0 && y < kRows) {
                            grid_[size_t(y)][size_t(x)] = 1;
                        }
                    }
                }
            }
            break;
        case 3: // 0211f8fc: the goal {x, y}
            cell(2);
            break;
        case 4: // 0211f95c: a hole
            cell(3);
            break;
        case 5: // 0211f9bc: ice
            cell(4);
            break;
        case 6: // 0211fa1c: {variable, [direction, go]}
            step(scene, c.count() >= 1 ? c.variable(0) : nullptr,
                 c.count() >= 3 ? c.integer(1) : -1, c.count() >= 3 && c.integer(2) != 0);
            break;
        default:
            break;
        }
    }

private:
    static constexpr int kOverlay = 40;
    static constexpr std::uint32_t kLoad = 0x0211f560;
    static constexpr int kColumns = 12, kRows = 10;

    // 0211fde8: a cell's place on the screen.
    static std::array<int, 2> pixel(std::array<int, 2> c) {
        return {(c[0] * 16) << 12, (c[1] * 16 + 16) << 12};
    }

    int at(std::array<int, 2> c) const { return grid_[size_t(c[1])][size_t(c[0])]; }

    static bool inside(std::array<int, 2> c) {
        return c[0] >= 0 && c[0] < kColumns && c[1] >= 0 && c[1] < kRows;
    }

    // 1280 (0211f5fc): {x, y}: the walker at its start, facing down, and
    // the arrows, hidden.
    void setup(Scene& scene, Call& c) {
        if (c.count() < 2) {
            return;
        }
        start_ = at_ = {c.integer(0), c.integer(1)};
        facing_ = 1;
        if (Object* o = scene.create(Object::Kind::Sprite, walker_, res_)) {
            scene.setLayer(*o, 2);
            lscr::play(*o, lscr::sequenceOf(*o, Value::ofText(walk_[size_t(facing_)])), 0);
            o->position.set(pixel(at_));
        }
        for (int d = 0; d < 4; ++d) {
            Object* o = scene.create(Object::Kind::Button, arrow_[size_t(d)], res_);
            if (!o) {
                continue;
            }
            const std::array<Value, 1> direction{Value::of(d)};
            scene.objectThread(*o, direction);
            o->style = Object::Style::Switch;
            o->touchedSection = section_;
            scene.setLayer(*o, 1);
            lscr::play(*o, lscr::sequenceOf(*o, Value::ofText(arrowSeq_[size_t(d)])), 0);
            o->switches[1].on = false;
            o->switches[3].on = false;
        }
    }

    // 1286 (0211fa1c): hides the arrows; with a direction, a step that way
    // (on ice, or when told to go) - on ice on until a wall - walked in 15
    // frames a cell (sliding three times as fast); standing still, the
    // arrows for the open directions come back (not on ice). The variable
    // gets the cell's kind.
    void step(Scene& scene, Value* result, int direction, bool go) {
        for (const auto& name : arrow_) {
            if (Object* o = scene.find(name)) {
                o->switches[3].on = false;
                o->switches[1].on = false;
                o->position.set({0x100000, 0x100000});
            }
        }
        const auto from = at_;
        const bool ice = at(at_) == 4;
        if (direction >= 0 && direction < 4 && (ice || go)) {
            facing_ = direction;
            for (;;) {
                const std::array<int, 2> next{at_[0] + kUnit[size_t(direction)][0],
                                              at_[1] + kUnit[size_t(direction)][1]};
                if (!inside(next) || at(next) == 1) {
                    break;
                }
                at_ = next;
                if (!ice) {
                    break;
                }
            }
        }
        const int kind = at(at_);
        if (result) {
            *result = Value::of(kind);
        }
        Object* walker = scene.find(walker_);
        if (at_ != from) {
            if (walker) {
                const int cells = std::abs(at_[0] - from[0]) + std::abs(at_[1] - from[1]);
                walker->position.to(1, pixel(at_), (cells * 15 / (ice ? 3 : 1)) << 12);
                const auto& names = ice ? slide_ : walk_;
                lscr::play(*walker,
                           lscr::sequenceOf(*walker, Value::ofText(names[size_t(facing_)])), 0);
            }
            return;
        }
        if (kind == 4) {
            return;
        }
        for (int d = 0; d < 4; ++d) {
            const std::array<int, 2> next{at_[0] + kUnit[size_t(d)][0],
                                          at_[1] + kUnit[size_t(d)][1]};
            Object* o = scene.find(arrow_[size_t(d)]);
            if (o && inside(next) && at(next) != 1) {
                o->switches[1].on = true;
                o->switches[3].on = true;
                o->position.set(pixel(at_));
            }
        }
        if (walker) {
            lscr::play(*walker, lscr::sequenceOf(*walker, Value::ofText(standing_)), 0);
        }
    }

    std::string walker_, res_, section_, standing_;
    std::array<std::string, 4> walk_, slide_, arrow_, arrowSeq_;
    std::array<std::array<int, kColumns>, kRows> grid_{};
    std::array<int, 2> start_{}, at_{};
    int facing_ = 1;
};

// GRID (overlay 42, loaded at 0211f560). Pieces (4021) on a grid of cells,
// each carrying an anchor point; the one being dragged is kept inside the
// board. The answer is chosen with the script's own buttons.
class GridEngine final : public lscr::SceneEngine {
public:
    explicit GridEngine(Assets& assets) {
        auto text = [&](std::uint32_t a) {
            return assets.codeText(kOverlay, kLoad, a);
        };
        board_ = text(0x0211fc5c); // lt3_grid_back
        boardRes_ = text(0x0211fc6c);
        released_ = text(0x0211fc78); // CHR_GRID_N
        touched_ = text(0x0211fc84);  // CHR_GRID_M
    }

    void command(Scene& scene, int hook, Call& c) override {
        switch (hook) {
        case 0: // 0211f734: the board {x, y, last column, last row, cell size}
            if (c.count() >= 5) {
                origin_ = {c.integer(0), c.integer(1)};
                last_ = {c.integer(2), c.integer(3)};
                cell_ = c.integer(4);
                if (Object* o = scene.create(Object::Kind::Sprite, board_, boardRes_)) {
                    scene.setLayer(*o, 3);
                }
            }
            break;
        case 1: // 0211f7d8: every piece back to its first cell (values 0, 1)
            for (Object* o : scene.creationOrder()) {
                if (o->style == Object::Style::Piece && o->thread && o->grid.size > 0) {
                    place(*o, o->thread->variables[0].toInt(), o->thread->variables[1].toInt());
                }
            }
            break;
        case 2:
            piece(scene, c);
            break;
        case 3: // 0211f9d8: {on}: the piece of the calling thread is dragged
            dragged_.clear();
            if (c.count() >= 1 && c.integer(0) != 0) {
                if (Object* o = scene.owner(c.thread)) {
                    dragged_ = o->name;
                }
            }
            break;
        default:
            break;
        }
    }

    void update(Scene& scene) override;

private:
    static constexpr int kOverlay = 42;
    static constexpr std::uint32_t kLoad = 0x0211f560;

    static void place(Object& o, int x, int y) {
        const auto& g = o.grid;
        o.cell = {std::clamp(x, 0, g.last[0]), std::clamp(y, 0, g.last[1])};
        o.position.set(
            {(o.cell[0] * g.size + g.origin[0]) << 12, (o.cell[1] * g.size + g.origin[1]) << 12});
    }

    // 1282 (0211f894): {name, resource, sequence, cell x, cell y, point,
    // sounds...}: a piece with the engine's sections, its values from the
    // cell on, turning in four steps of five frames, on the board's grid,
    // carrying the point.
    void piece(Scene& scene, Call& c) {
        if (c.count() < 8) {
            return;
        }
        Object* o = scene.create(Object::Kind::Button, c.text(0), c.text(1));
        if (!o) {
            return;
        }
        std::vector<Value> values;
        for (std::size_t i = 3; i < c.count(); ++i) {
            values.push_back(c.at(i).get());
        }
        scene.objectThread(*o, values);
        o->style = Object::Style::Piece;
        o->touch = 0;
        o->releasedSection = released_;
        o->touchedSection = touched_;
        o->clickedSection = released_;
        scene.callOn(*o, o->releasedSection);
        scene.setLayer(*o, 2);
        lscr::play(*o, lscr::sequenceOf(*o, c.at(2)), 0);
        o->steps = 4;
        o->step = 0;
        o->turnTime = 0x5000;
        o->grid = {origin_, last_, cell_, 0x5000};
        place(*o, c.integer(3), c.integer(4));
        scene.point(c.text(5)).attachedTo = o->name;
    }

    std::string board_, boardRes_, released_, touched_;
    std::array<int, 2> origin_{}, last_{};
    int cell_ = 1;
    std::string dragged_;
};

void GridEngine::update(Scene& scene) {
    // 0211fa28 / 0211f5e0: while the pen holds the dragged piece, its box
    // stays within the board - the smoothed pen place less the grip, pushed
    // back inside, then dragged there.
    if (dragged_.empty() || !scene.input().held) {
        return;
    }
    Object* o = scene.find(dragged_);
    if (!o) {
        return;
    }
    const auto pen = scene.smoothPen();
    std::array<int, 2> p{(pen[0] - o->grab[0]) << 12, (pen[1] - o->grab[1]) << 12};
    const auto box = lscr::touchBox(*o);
    const std::array<int, 2> lo = origin_,
                             hi{origin_[0] + last_[0] * cell_, origin_[1] + last_[1] * cell_};
    for (int k = 0; k < 2; ++k) {
        const int first = box[size_t(k)] + (p[size_t(k)] >> 12);
        const int second = box[size_t(k) + 2] + (p[size_t(k)] >> 12);
        if (first < lo[size_t(k)]) {
            p[size_t(k)] += (lo[size_t(k)] - first) << 12;
        } else if (hi[size_t(k)] < second) {
            p[size_t(k)] += (hi[size_t(k)] - second) << 12;
        }
    }
    scene.dragTo(*o, p);
}

// Whether any object is moving or turning (020f2390, condition 4013).
bool anyMoving(Scene& scene) {
    for (Object* o : scene.creationOrder()) {
        if (o->shown() && (o->position.active() || o->offset.active() || o->rotation.active())) {
            return true;
        }
    }
    return false;
}

// SCALE (overlay 41, loaded at 0211f560). A balance: the script gives
// objects weights (1283); the judge button (1282) weighs what is linked to
// the dish points - two, or a lower and an upper pair - tilting the arm and
// moving the dishes and what is on them, a wobble when it comes out level or
// the same as last time, and counts the weighings with new contents. The
// answer itself is written.
class ScaleEngine final : public lscr::SceneEngine {
public:
    explicit ScaleEngine(Assets& assets) {
        auto text = [&](std::uint32_t a) {
            return assets.codeText(kOverlay, kLoad, a);
        };
        auto entry = [&](int i) { // 02121ba0: the scale's object names
            return text(assets.codeWord(kOverlay, kLoad, 0x02121ba0 + 4 * std::uint32_t(i)));
        };
        dishR_ = entry(0);
        arm_ = entry(1);
        counterFormat_ = entry(2);
        counter_ = entry(3);
        dishL_ = entry(4);
        counterRes_ = text(0x02121dec);
        memo_ = text(0x02121de0); // the flag: the memo is open
        upperL_ = text(0x02121df8);
        lowerL_ = text(0x02121dfc);
        upperR_ = text(0x02121e00);
        lowerR_ = text(0x02121e04);
    }

    void command(Scene& scene, int hook, Call& c) override {
        switch (hook) {
        case 0:
            setup(scene, c);
            break;
        case 1:
            reset(scene);
            break;
        case 2:
            weigh(scene);
            break;
        case 3: // 0211fefc: a weight {object, weight}
            if (c.count() >= 2 && registry_.size() < capacity_) {
                const Weight w{c.text(0), c.integer(1)};
                if (std::none_of(registry_.begin(), registry_.end(), [&](const Weight& r) {
                        return r.name == w.name && r.weight == w.weight;
                    })) {
                    registry_.push_back(w);
                }
            }
            break;
        case 4:
            redirect(scene, c);
            break;
        case 5:
            fall(scene, c);
            break;
        default:
            break;
        }
    }

    void update(Scene& scene) override;

private:
    static constexpr int kOverlay = 41;
    static constexpr std::uint32_t kLoad = 0x0211f560;
    static constexpr int kTilt = 0x4cc; // the arm's turn, 0.3 radians

    struct Weight {
        std::string name;
        int weight = 0;
    };

    struct Dish {
        std::string point;
        std::array<int, 2> home{};
    };

    static int fixedOf(float v) { return int(v * 4096.0f); }

    int dishes() const { return mode_ == 0 ? 2 : 4; }

    // 020bd080: the object linked to a point.
    static std::string on(Scene& scene, const std::string& point) {
        if (point.empty()) {
            return {};
        }
        for (Object* o : scene.creationOrder()) {
            if (o->link == point) {
                return o->name;
            }
        }
        return {};
    }

    std::string on(Scene& scene, int dish) const {
        return dish < int(dishes_.size()) ? on(scene, dishes_[size_t(dish)].point) : "";
    }

    // 02120954
    int weight(const std::string& name) const {
        for (const auto& w : registry_) {
            if (!name.empty() && w.name == name) {
                return w.weight;
            }
        }
        return 0;
    }

    void homePoints(Scene& scene) {
        for (const auto& d : dishes_) {
            scene.point(d.point).local = d.home;
        }
    }

    void showCount(Scene& scene) {
        if (Object* o = scene.find(counter_)) {
            o->user[0] = Value::of(count_);
        }
    }

    // 1280 (0211f98c): {weights at most, upper dishes, counter x, y, dish
    // points...}.
    void setup(Scene& scene, Call& c) {
        if (c.count() < 4) {
            return;
        }
        capacity_ = size_t(std::max(c.integer(0), 0));
        registry_.clear();
        mode_ = c.integer(1);
        dishes_.clear();
        // as many points as operands beyond the first operand's number
        const int points = int(c.count()) - c.integer(0);
        for (int i = 0; i < points && std::size_t(4 + i) < c.count(); ++i) {
            if (const auto at = scene.pointPosition(c.text(std::size_t(4 + i)))) {
                dishes_.push_back({c.text(std::size_t(4 + i)), *at});
            }
        }
        if (c.integer(2) != -1) {
            if (Object* o = scene.create(Object::Kind::Number, counter_, counterRes_)) {
                o->format = counterFormat_;
                o->prefix = "";
                o->spacing = 6;
                o->position.set({c.integer(2) << 12, c.integer(3) << 12});
                o->user[0] = Value::of(count_);
            }
        }
        state_ = 1;
    }

    // 1281 (0211fc6c / 021210cc): level at once, everything at home.
    void reset(Scene& scene) {
        for (const auto& name : {dishL_, dishR_}) {
            if (Object* o = scene.find(name)) {
                o->offset.set({0, 0});
            }
        }
        if (Object* o = scene.find(arm_)) {
            o->rotation.set({0});
        }
        homePoints(scene);
        for (const auto& w : registry_) {
            if (Object* o = scene.find(w.name)) {
                o->offset.set({0, 0});
                o->position.set({0, 0});
            }
        }
        count_ = 0;
        showCount(scene);
        last_ = {};
        tilt_ = 0;
        state_ = 3;
        wobble_ = false;
    }

    // 1282 (0211fcfc): weighs.
    void weigh(Scene& scene) {
        const int before = tilt_;
        std::array<std::string, 4> now{};
        for (int i = 0; i < dishes(); ++i) {
            now[size_t(i)] = on(scene, i);
        }
        // 02120260: a weighing counts when something is on the dishes and
        // it is not what was weighed last.
        const bool any = std::any_of(now.begin(), now.end(), [](auto& n) { return !n.empty(); });
        if (any && now != last_) {
            count_ = std::min(count_ + 1, 999);
            showCount(scene);
        }
        // 02120f04: left (lower and upper) against right.
        const int left = weight(now[0]) + weight(now[2]);
        const int right = weight(now[1]) + weight(now[3]);
        int tilt = 0;
        if (left == right) {
            if (tilt_ == 0) {
                wobble_ = true;
            }
            pan(scene, now[0], now[1], 0, 0, 0, 1, 0);
            pan(scene, now[2], now[3], 0, 0, 2, 3, 0);
        } else if (left < right) {
            pan(scene, now[0], now[1], -20, 20, 0, 1, 1);
            pan(scene, now[2], now[3], -20, 20, 2, 3, 1);
            tilt = 1;
        } else {
            pan(scene, now[0], now[1], 20, -20, 0, 1, -1);
            pan(scene, now[2], now[3], 20, -20, 2, 3, -1);
            tilt = -1;
        }
        tilt_ = tilt;
        last_ = now;
        // 020d9134 / 020f1be0: input off, in a scope of its own.
        scene.pushScope();
        scene.setAllInput(false);
        if (tilt_ == 0 || before != tilt_) {
            state_ = 2;
        } else {
            const float t = float(tilt_);
            wobble(scene, -1, 4, 20.0f * -t - 5.0f, 5.0f + 20.0f * t, 0.1f + 0.3f * t);
            state_ = 5;
            wobble_ = true;
        }
    }

    // 02120ad8: a pair of dishes to their places for `tilt`: the dishes
    // in 15 frames, their points at once; when the tilt changes, the arm
    // turns and what is on the dishes comes along, left where it was and
    // moved by the dishes' change.
    void pan(Scene& scene, const std::string& a, const std::string& b, int offL, int offR, int ia,
             int ib, int tilt) {
        if (mode_ == 0 && ia > 1) {
            return;
        }
        if (Object* o = scene.find(dishL_)) {
            o->offset.to(1, {0, offL << 12}, 15 << 12);
        }
        if (Object* o = scene.find(dishR_)) {
            o->offset.to(1, {0, offR << 12}, 15 << 12);
        }
        std::array<std::optional<std::array<int, 2>>, 2> was;
        for (int k = 0; k < 2; ++k) {
            if (Object* o = scene.find(k ? b : a)) {
                was[size_t(k)] = scene.absolute(*o);
            }
        }
        if (tilt == 0) {
            homePoints(scene);
        } else {
            for (const auto& [i, off] : {std::pair{ia, offL}, std::pair{ib, offR}}) {
                if (i < int(dishes_.size())) {
                    const auto& d = dishes_[size_t(i)];
                    scene.point(d.point).local = {d.home[0], d.home[1] + (off << 12)};
                }
            }
        }
        if (tilt_ == tilt) {
            return;
        }
        for (int k = 0; k < 2; ++k) {
            Object* o = scene.find(k ? b : a);
            if (!o || !was[size_t(k)]) {
                continue;
            }
            o->offset.to(1, {0, fixedOf(shift(k ? 1 : -1, tilt))}, 15 << 12);
            // 020ad7b4: unlinked where it was shown
            o->link.clear();
            o->position.set({(*was[size_t(k)])[0] - o->offset.value[0],
                             (*was[size_t(k)])[1] - o->offset.value[1]});
        }
        if (Object* o = scene.find(arm_)) {
            o->rotation.to(1, {tilt * kTilt}, 15 << 12);
        }
    }

    // 021209ac: how far what is on a side moves for a new tilt.
    float shift(int side, int tilt) const {
        const float v = tilt_ != 0 ? 2.0f : 1.0f;
        if (tilt == -1) {
            return side == -1 ? 20.0f * v : -20.0f * v;
        }
        if (tilt == 1) {
            return side == -1 ? -20.0f * v : 20.0f * v;
        }
        if (tilt_ == -1) {
            return (side == -1 ? -20.0f * v : 20.0f * v) * 0.5f;
        }
        if (tilt_ == 1) {
            return (side == -1 ? 20.0f * v : -20.0f * v) * 0.5f;
        }
        return 0;
    }

    // 02121550: a wobble in ten frames - what is on the dishes five pixels
    // (left and right opposite), the dishes and the arm to the given places.
    void wobble(Scene& scene, int dir, int curve, float left, float right, float turn) {
        bool moved = false;
        for (int i = 0; i < dishes(); ++i) {
            if (Object* o = scene.find(on(scene, i))) {
                o->offset.to(curve, {0, fixedOf(float(dir) * (5.0f * (i % 2 ? -1.0f : 1.0f)))},
                             10 << 12);
                moved = true;
            }
        }
        if (!moved) {
            return;
        }
        if (Object* o = scene.find(dishL_)) {
            o->offset.to(curve, {0, fixedOf(left)}, 10 << 12);
        }
        if (Object* o = scene.find(dishR_)) {
            o->offset.to(curve, {0, fixedOf(right)}, 10 << 12);
        }
        if (Object* o = scene.find(arm_)) {
            o->rotation.to(curve, {fixedOf(turn)}, 10 << 12);
        }
    }

    // 02121764: the dishes and the arm to the given places, everything on
    // them back, in ten frames.
    void level(Scene& scene, int curve, float left, float right, float turn) {
        if (Object* o = scene.find(dishL_)) {
            o->offset.to(curve, {0, fixedOf(left)}, 10 << 12);
        }
        if (Object* o = scene.find(dishR_)) {
            o->offset.to(curve, {0, fixedOf(right)}, 10 << 12);
        }
        if (Object* o = scene.find(arm_)) {
            o->rotation.to(curve, {fixedOf(turn)}, 10 << 12);
        }
        for (const auto& w : registry_) {
            if (Object* o = scene.find(w.name)) {
                o->offset.to(curve, {0, 0}, 10 << 12);
            }
        }
    }

    // 021203a4: what was weighed on a side back on its dish points; every
    // weighed object without offset or position.
    void settle(Scene& scene, int side) {
        const int first = side == -1 ? 0 : 1;
        for (int i = first; i < dishes(); i += 2) {
            if (Object* o = scene.find(last_[size_t(i)])) {
                o->link = dishes_[size_t(i)].point;
            }
        }
        for (const auto& w : registry_) {
            if (Object* o = scene.find(w.name)) {
                o->offset.set({0, 0});
                o->position.set({0, 0});
            }
        }
    }

    // 1284 (0211ffa8): {from, variable to, variable direction}: an upper
    // dish point with its lower one empty becomes the lower; moving onto
    // the point it came from in a direction makes it that side's upper.
    void redirect(Scene& scene, Call& c) {
        Value* to = c.count() >= 3 ? c.variable(2) : nullptr;
        if (!to) {
            return;
        }
        if (c.text(2) == upperL_ && on(scene, lowerL_).empty()) {
            *to = Value::ofText(lowerL_);
        }
        if (c.text(2) == upperR_ && on(scene, lowerR_).empty()) {
            *to = Value::ofText(lowerR_);
        }
        if (Value* direction = c.count() >= 4 ? c.variable(3) : nullptr) {
            if (c.integer(3) == -1 && c.text(1) == c.text(2)) {
                *to = Value::ofText(upperL_);
            }
            if (c.integer(3) == 1 && c.text(1) == c.text(2)) {
                *to = Value::ofText(upperR_);
            }
            *direction = Value::of(0);
        }
    }

    // 1285 (0212010c): {object, variable side, variable point}: taking an
    // object off a lower dish brings the one above it down (02121400).
    void fall(Scene& scene, Call& c) {
        Value* side = c.count() >= 2 ? c.variable(1) : nullptr;
        Value* point = c.count() >= 3 ? c.variable(2) : nullptr;
        for (const auto& [lower, upper, dir] :
             {std::tuple{lowerL_, upperL_, -1}, std::tuple{lowerR_, upperR_, 1}}) {
            const std::string below = on(scene, lower);
            if (below.empty() || c.text(0) != below) {
                continue;
            }
            if (Object* o = scene.find(on(scene, upper))) {
                // Hypothesis: relinked where it is and moved down in ten
                // frames (the tween's operands are not recovered).
                const auto here = scene.absolute(*o);
                const auto base = *scene.pointPosition(lower);
                o->link = lower;
                o->position.set({here[0] - base[0] - o->offset.value[0],
                                 here[1] - base[1] - o->offset.value[1]});
                o->position.to(1, {0, 0}, 10 << 12);
                if (side) {
                    *side = Value::of(dir);
                }
                if (point) {
                    *point = Value::ofText(upper);
                }
            }
            return;
        }
        if (side) {
            *side = Value::of(0);
        }
    }

    std::string arm_, dishL_, dishR_, counter_, counterRes_, counterFormat_, memo_;
    std::string upperL_, lowerL_, upperR_, lowerR_;
    std::vector<Weight> registry_;
    std::size_t capacity_ = 0;
    std::vector<Dish> dishes_;
    int mode_ = 0, state_ = 0, count_ = 0, tilt_ = 0;
    bool wobble_ = false;
    std::array<std::string, 4> last_{};
};

void ScaleEngine::update(Scene& scene) {
    // 0211f5fc, after the common step; nothing while the memo is open.
    if (scene.machine.state.flag(Value::ofText(memo_)) || state_ == 0) {
        return;
    }
    const float t = float(tilt_);
    if (state_ == 1 && on(scene, 0).empty() && on(scene, 1).empty()) {
        // 02121250: both dishes empty: level again.
        for (const auto& name : {dishL_, dishR_}) {
            if (Object* o = scene.find(name)) {
                o->offset.to(1, {0, 0}, 10 << 12);
            }
        }
        if (Object* o = scene.find(arm_)) {
            o->rotation.to(1, {0}, 10 << 12);
        }
        homePoints(scene);
        for (const auto& w : registry_) {
            if (Object* o = scene.find(w.name)) {
                o->switches[1].on = true;
            }
        }
        scene.pushScope();
        scene.setAllInput(false);
        state_ = 7;
    }
    if (state_ == 7 && !anyMoving(scene)) {
        if (Object* o = scene.find(arm_)) {
            o->rotation.set({0});
        }
        tilt_ = 0;
        scene.popScope();
        state_ = 1;
    }
    if (state_ == 2 && !anyMoving(scene)) {
        if (!last_[0].empty()) {
            settle(scene, -1);
        }
        if (!last_[1].empty()) {
            settle(scene, 1);
        }
        if (wobble_) {
            wobble(scene, 1, 4, 5.0f, -5.0f, -0.1f);
            state_ = 4;
            return;
        }
        scene.popScope();
        state_ = 1;
    }
    if (state_ == 4 && !anyMoving(scene)) {
        if (wobble_) {
            wobble(scene, -1, 4, -5.0f, 5.0f, 0.1f);
            wobble_ = false;
            return;
        }
        level(scene, 1, 0, 0, 0);
        scene.popScope();
        state_ = 1;
    }
    if ((state_ == 5 || state_ == 6) && !anyMoving(scene)) {
        const float a = 20.0f * -t, b = 20.0f * t;
        if (state_ == 5) {
            wobble(scene, 1, 4, 5.0f + a, b - 5.0f, 0.3f * t - 0.1f);
            state_ = 6;
        } else if (wobble_) {
            level(scene, 4, a, b, 0.3f * t);
            wobble_ = false;
        } else {
            if (Object* o = scene.find(arm_)) {
                o->rotation.set({fixedOf(0.3f * t)});
            }
            scene.popScope();
            state_ = 1;
        }
    }
}

// LTGRAM (overlay 39, loaded at 0211f560). Pieces (4021: N CHR_LTGRAM_N, T
// CHR_LTGRAM_M, C CHR_LTGRAM_R) on a grid, turning in quarter steps, with a
// button that mirrors the piece last put down; solved once every cell of the
// target regions (1286) lies in some piece's shape (id-2 areas).
class LtgramEngine final : public lscr::SceneEngine {
public:
    explicit LtgramEngine(Assets& assets) {
        auto text = [&](std::uint32_t a) {
            return assets.codeText(kOverlay, kLoad, a);
        };
        board_ = text(0x02120228); // lt3_ltgrm_back
        boardRes_ = text(0x02120238);
        turn_ = text(0x02120244); // lt3_ltgrm_turn
        turnSection_ = text(0x02120254);
        released_ = text(0x02120264);
        touched_ = text(0x02120274);
        turned_ = text(0x02120284);
        // 0211fd7c..: the button's look by the quarter of the board
        turnSeq_ = {text(0x02120294), text(0x0212029c), text(0x021202a4), text(0x021202ac)};
        verdict_ = text(0x021202b4);
    }

    void command(Scene& scene, int hook, Call& c) override {
        switch (hook) {
        case 0:
            setup(scene, c);
            break;
        case 1:
            reset(scene);
            break;
        case 2: // 0211fde0
            judge(scene);
            break;
        case 3:
            piece(scene, c);
            break;
        case 4:
            select(scene, c);
            break;
        case 5: // 0211fd8c: the selected piece mirrored (up and down when
                // it stands on an odd step), then judged
            if (Object* o = scene.find(selected_)) {
                o->mirror ^= o->step & 1 ? 2 : 1;
                if (autoJudge_) {
                    judge(scene);
                }
            }
            break;
        case 6:
            region(scene, c);
            break;
        default:
            break;
        }
    }

    void frame(Scene& scene) override {
        // 0211ffd4: a solved puzzle takes no more input.
        scene.inputBlocked = solved_;
    }

private:
    static constexpr int kOverlay = 39;
    static constexpr std::uint32_t kLoad = 0x0211f560;

    std::array<int, 2> cellPixel(std::array<int, 2> cell) const {
        return {cell[0] * cell_ + origin_[0], cell[1] * cell_ + origin_[1]};
    }

    // 1280 (0211f650): {x, y, last column, last row, cell size, resource,
    // [judge as pieces are put down]}: the board and the mirror button.
    void setup(Scene& scene, Call& c) {
        if (c.count() < 6) {
            return;
        }
        origin_ = {c.integer(0), c.integer(1)};
        last_ = {c.integer(2), c.integer(3)};
        cell_ = c.integer(4);
        if (Object* o = scene.create(Object::Kind::Sprite, board_, boardRes_)) {
            scene.setLayer(*o, 3);
        }
        if (Object* o = scene.create(Object::Kind::Button, turn_, c.text(5))) {
            scene.objectThread(*o, {});
            o->style = Object::Style::Switch;
            o->touchedSection = turnSection_;
            scene.setLayer(*o, 1);
            o->switches[0].on = false;
        }
        autoJudge_ = !(c.count() >= 7 && c.integer(6) == 0);
    }

    // 1281 (0211f778): every piece back to its cell, step and mirroring
    // (values 0, 1, 2, 6); the button hidden.
    void reset(Scene& scene) {
        for (Object* o : scene.creationOrder()) {
            if (o->style != Object::Style::Piece || !o->thread || o->grid.size <= 0) {
                continue;
            }
            const auto& v = o->thread->variables;
            const auto& g = o->grid;
            o->cell = {std::clamp(v[0].toInt(), 0, g.last[0]),
                       std::clamp(v[1].toInt(), 0, g.last[1])};
            o->position.set({(o->cell[0] * g.size + g.origin[0]) << 12,
                             (o->cell[1] * g.size + g.origin[1]) << 12});
            scene.setStep(*o, v[2].toInt());
            o->mirror = v[6].toInt() != 0 ? 1 : 0;
        }
        if (Object* o = scene.find(turn_)) {
            o->switches[0].on = false;
        }
        selected_.clear();
    }

    // 1283 (0211f9e0): {name, resource, sequence, cell x, cell y, step,
    // point, sounds..., [mirrored]}.
    void piece(Scene& scene, Call& c) {
        if (c.count() < 9) {
            return;
        }
        Object* o = scene.create(Object::Kind::Button, c.text(0), c.text(1));
        if (!o) {
            return;
        }
        std::vector<Value> values;
        for (std::size_t i = 3; i < c.count(); ++i) {
            values.push_back(c.at(i).get());
        }
        scene.objectThread(*o, values);
        o->style = Object::Style::Piece;
        o->touch = 0;
        o->releasedSection = released_;
        o->touchedSection = touched_;
        o->clickedSection = turned_;
        scene.callOn(*o, o->releasedSection);
        scene.setLayer(*o, 2);
        lscr::play(*o, lscr::sequenceOf(*o, c.at(2)), 0);
        o->steps = 4;
        o->step = 0;
        o->turnTime = 0x5000;
        o->grid = {origin_, last_, cell_, 0x5000};
        o->cell = {std::clamp(c.integer(3), 0, last_[0]), std::clamp(c.integer(4), 0, last_[1])};
        o->position.set({cellPixel(o->cell)[0] << 12, cellPixel(o->cell)[1] << 12});
        scene.setStep(*o, c.integer(5));
        scene.point(c.text(6)).attachedTo = o->name;
        if (c.count() == 10 && c.integer(9) != 0) {
            o->mirror = 1;
        }
    }

    // 1284 (0211fb5c): {shown}: judged (when judging as pieces go down);
    // unless solved, the mirror button shown at the calling piece - on the
    // corner of its box towards the board's middle - or hidden.
    void select(Scene& scene, Call& c) {
        Object* piece = scene.owner(c.thread);
        if (c.count() < 1 || !piece) {
            return;
        }
        if (autoJudge_) {
            judge(scene);
        }
        if (solved_) {
            return;
        }
        Object* button = scene.find(turn_);
        if (!button) {
            return;
        }
        if (c.integer(0) == 0) {
            button->switches[0].on = false;
            selected_.clear();
            return;
        }
        button->switches[0].on = true;
        selected_ = piece->name;
        const auto at = cellPixel(piece->cell);
        const std::array<int, 2> middle{(last_[0] / 2) * cell_ + origin_[0],
                                        (last_[1] / 2) * cell_ + origin_[1]};
        const auto box = scene.turnedTouchBox(*piece);
        const bool left = at[0] < middle[0], top = at[1] < middle[1];
        const int quarter = left ? (top ? 0 : 1) : (top ? 2 : 3);
        lscr::play(*button, lscr::sequenceOf(*button, Value::ofText(turnSeq_[size_t(quarter)])), 0);
        button->position.set(
            {(at[0] + (left ? box[2] : box[0])) << 12, (at[1] + (top ? box[3] : box[1])) << 12});
    }

    // 1286 (0211f8d0): a target region {x, y, w, h} in cells (at most
    // sixteen), also an id-3 area of the board.
    void region(Scene& scene, Call& c) {
        if (c.count() < 4 || regions_.size() >= 16) {
            return;
        }
        const Box r{c.integer(0), c.integer(1), c.integer(2), c.integer(3)};
        regions_.push_back(r);
        if (Object* o = scene.find(board_)) {
            const auto a = cellPixel({r[0], r[1]});
            o->areas.push_back({a[0], a[1], r[2] * cell_, r[3] * cell_, 3});
        }
    }

    // 0211fdf0: with nothing moving, solved when every target cell's corner
    // lies within an id-2 area.
    void judge(Scene& scene) {
        if (solved_ || regions_.empty() || anyMoving(scene)) {
            return;
        }
        const auto shapes = scene.areasWith(2);
        for (const auto& r : regions_) {
            for (int y = r[1]; y < r[1] + r[3]; ++y) {
                for (int x = r[0]; x < r[0] + r[2]; ++x) {
                    const auto p = cellPixel({x, y});
                    const bool inside =
                        std::any_of(shapes.begin(), shapes.end(), [&](const Box& b) {
                            return b[0] <= p[0] && b[1] <= p[1] && p[0] < b[2] && p[1] < b[3];
                        });
                    if (!inside) {
                        return;
                    }
                }
            }
        }
        solved_ = true;
        scene.machine.state.setVariable(Value::ofText(verdict_), 1);
    }

    std::string board_, boardRes_, turn_, turnSection_, released_, touched_, turned_, verdict_;
    std::array<std::string, 4> turnSeq_;
    std::array<int, 2> origin_{}, last_{};
    int cell_ = 1;
    bool autoJudge_ = true, solved_ = false;
    std::vector<Box> regions_;
    std::string selected_;
};
} // namespace

std::unique_ptr<lscr::SceneEngine> makeEngine(Assets& assets, int type) {
    switch (type) {
    case 9:
        return std::make_unique<CutEngine>(assets);
    case 10:
        return std::make_unique<LineEngine>(assets);
    case 11:
        return std::make_unique<SlideEngine>(assets);
    case 12:
    case 18:
    case 22:
        return std::make_unique<LtgramEngine>(assets);
    case 14:
        return std::make_unique<BananaEngine>(assets);
    case 19:
        return std::make_unique<ScaleEngine>(assets);
    case 21:
        return std::make_unique<GridEngine>(assets);
    default:
        return nullptr;
    }
}

std::vector<std::string> engineSections(Assets& assets, int type) {
    switch (type) {
    case 11: // CHR_SLIDE_N, CHR_SLIDE_M
        return {assets.codeText(38, 0x0211f560, 0x02120410),
                assets.codeText(38, 0x0211f560, 0x0212041c)};
    case 14: // BTN_BANANA_T
        return {assets.codeText(40, 0x0211f560, 0x021200f4)};
    case 12:
    case 18:
    case 22: // CHR_LTGRAM_N, CHR_LTGRAM_M, CHR_LTGRAM_R, BTN_LTGRAM_T
        return {assets.codeText(39, 0x0211f560, 0x02120264),
                assets.codeText(39, 0x0211f560, 0x02120274),
                assets.codeText(39, 0x0211f560, 0x02120284),
                assets.codeText(39, 0x0211f560, 0x02120254)};
    case 21: // CHR_GRID_N, CHR_GRID_M
        return {assets.codeText(42, 0x0211f560, 0x0211fc78),
                assets.codeText(42, 0x0211f560, 0x0211fc84)};
    default:
        return {};
    }
}

void drawThickLine(Surface& s, std::array<int, 2> a, std::array<int, 2> b, int width,
                   std::uint16_t colour) {
    thickLine(s, a, b, width, colour);
}

void drawBox(Surface& s, int left, int top, int right, int bottom, std::uint16_t colour) {
    fillFixed(s, left, top, right, bottom, colour);
}

} // namespace picarat::adventure3
