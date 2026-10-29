// The sixth adventure's route puzzles (loop 3260, class "Route"; catalogue
// type 13: 001, 026, 160, "bring the airship to its goal"): a grid of
// cells; from the start cell the player draws one path by tapping the
// neighbouring cells, each step using fuel, and has to visit every cell
// that is not blocked. Some cells refill the tank.
//
// Reconstructed from the class's command method 0x2e0c0c (vtable
// 0x3c860c, constructor 0x11536c), its input method 0x2e2198, the step
// list 0x2216cc, the press pick 0x2e0ac0 and the drawing 0x2e1010 (vtable
// +0x74 0x2e234c, when the board changed).
//
// How it plays: the cells next to the current one (the four straight
// neighbours, then the four diagonal ones) that are not blocked and not on
// the path yet are the steps, while there is fuel - a diagonal one only
// with fuel for its cost and when the path does not already cross it (the
// two cells beside the diagonal joined by the path). A press within the
// touch radius of a step's centre picks it; it shows pressed while the pen
// stays within the radius, and lifting the pen there takes the step: the
// path goes on to the cell, the fuel drops by the step's cost (a refill
// cell fills the tank), and the script's BtnClickCall block runs (it moves
// the airship, shows the fuel and judges: the path has to cover every cell
// that is not blocked, with no step left).
//
// Cell kinds (command 11): 1 plain, 2 refill, 3 the start, 4 blocked.
#include "puzzles/a5_special.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

class RoutePuzzle final : public SpecialPuzzle {
public:
    explicit RoutePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;
    void update() override;
    std::string describe() const override;

private:
    enum : int { kPlain = 1, kRefill = 2, kStart = 3, kBlocked = 4 };

    struct Point {
        int x = -1, y = -1;
        bool operator==(const Point&) const = default;
    };

    struct Cell { // 0x1c bytes in the list at +0x194
        int kind = 0;
        float x = 0, y = 0;         // +4: the centre in pixels (command 20)
        float halfW = 0, halfH = 0; // +0xc: half its button's size, rounded
        int next = -1;              // +0x14: the path's next cell (an index)
    };

    struct Step { // +0x1d6: up to eight
        Point to;
        int cost = 0;
    };

    Cell* cell(Point p) {
        return p.x >= 0 && p.x < across_ && p.y >= 0 && p.y < down_
                   ? &cells_[std::size_t(p.y * across_ + p.x)]
                   : nullptr;
    }

    const Cell* cell(Point p) const { return const_cast<RoutePuzzle*>(this)->cell(p); }

    int indexOf(Point p) const { return p.y * across_ + p.x; }

    Point pointOf(int i) const { return {i % across_, i / across_}; }

    bool near(const Cell& k, int px, int py) const {
        const float dx = k.x - float(px), dy = k.y - float(py);
        return std::sqrt(dx * dx + dy * dy) < radius_;
    }

    void findSteps();
    int heading(Point from, Point to) const;
    void reset();
    void redraw();

    Game& game_;
    int across_ = 0, down_ = 0;
    std::vector<Cell> cells_;
    Point start_;             // +0x1c9
    Point at_;                // +0x1d4: the path's end
    int tank_ = 0;            // +0x1cb: a full tank
    int fuel_ = 0;            // +0x1f7
    int straightCost_ = 0;    // +0x1f8
    int diagonalCost_ = 0;    // +0x1f9
    int heading_ = 1;         // +0x214: the last step's way (1 down, 2 up, 3 left, 4 right)
    std::vector<Step> steps_; // +0x1d6, count +0x1f6
    float originX_ = 0, originY_ = 0, pitchX_ = 0, pitchY_ = 0; // +0x200..+0x20c
    float radius_ = 0;                                          // +0x1cc
    int pressDrop_ = 0;        // +0x210: how far a pressed button sinks
    Point shown_;              // +0x218: the step shown pressed
    Point picked_;             // +0x21c: the step the press picked
    int pickedCost_ = 0;       // +0x21e
    std::vector<float> turns_; // +0x1ac: values kept for undo (30 at most)
    bool dirty_ = false;       // +0x220
};

// 0x2216cc: the steps from the path's end.
void RoutePuzzle::findSteps() {
    steps_.clear();
    if (fuel_ == 0) {
        return;
    }
    static constexpr std::array<std::array<int, 2>, 8> kWays{
        {{0, -1}, {-1, 0}, {1, 0}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}}};
    for (std::size_t i = 0; i < kWays.size(); ++i) {
        const Point to{at_.x + kWays[i][0], at_.y + kWays[i][1]};
        const Cell* k = cell(to);
        const bool diagonal = i >= 4;
        if (!k || k->next >= 0 || k->kind == kBlocked || (diagonal && fuel_ < diagonalCost_)) {
            continue;
        }
        if (diagonal) {
            // The two cells beside the diagonal: no step across a path
            // that joins them.
            const Cell* a = cell({to.x, at_.y});
            const Cell* b = cell({at_.x, to.y});
            if (a && b) {
                const int ia = indexOf({to.x, at_.y}), ib = indexOf({at_.x, to.y});
                if (a->next == ib || b->next == ia) {
                    continue;
                }
            }
        }
        steps_.push_back({to, diagonal ? diagonalCost_ : straightCost_});
    }
}

// The way from one cell to the next as the airship faces it.
int RoutePuzzle::heading(Point from, Point to) const {
    const int dx = to.x - from.x, dy = to.y - from.y;
    int way = heading_;
    if (dx == 0 && dy < 0) {
        way = 2;
    } else if (dx == 0 && dy > 0) {
        way = 1;
    }
    if (dx < 0) {
        way = 3;
    }
    if (dx > 0) {
        way = 4;
    }
    return way;
}

// Command 13 (0x2e1cbc): back to the start with a full tank.
void RoutePuzzle::reset() {
    at_ = start_;
    fuel_ = tank_;
    heading_ = 1;
    for (Cell& k : cells_) {
        k.next = -1;
    }
    findSteps();
    dirty_ = true;
}

bool RoutePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto store = [&](std::size_t i, Value v) {
        if (i < count) {
            if (Value* r = c.reg(first + i)) {
                *r = std::move(v);
            }
        }
    };
    auto result = [&](bool b) {
        c.result = Value::ofInt(b ? 1 : 0);
        return true;
    };
    switch (number) {
    case 10: // 0x2e1e30: the board (across, down)
        if (count < 2) {
            return false;
        }
        across_ = std::max(0, integer(0));
        down_ = std::max(0, integer(1));
        cells_.assign(std::size_t(across_ * down_), Cell{});
        turns_.clear();
        return result(true);
    case 11: { // a row's kinds (row, kind...)
        const int row = count > 0 ? integer(0) : -1;
        if (count == 0 || count > std::size_t(across_) + 1 || row < 0 || row >= down_) {
            return false;
        }
        for (std::size_t i = 0; i + 1 < count; ++i) {
            Cell* k = cell({int(i), row});
            k->kind = integer(i + 1);
            if (k->kind == kStart) {
                start_ = {int(i), row};
            }
        }
        return result(true);
    }
    case 12: // a full tank, a straight and a diagonal step's cost
        if (count < 3) {
            return false;
        }
        tank_ = fuel_ = integer(0);
        straightCost_ = integer(1);
        diagonalCost_ = integer(2);
        at_ = start_;
        findSteps();
        dirty_ = true;
        return true;
    case 13:
        reset();
        return true;
    case 14: { // 0x2e18cc: the last step undone [the newest kept value -> register]
        if (count == 0) {
            return false;
        }
        Cell* k = cell(start_);
        if (!k || k->next < 0) {
            return result(false);
        }
        // The path's last cell before its end becomes the end.
        int before = indexOf(start_);
        while (cells_[std::size_t(cells_[std::size_t(before)].next)].next >= 0) {
            before = cells_[std::size_t(before)].next;
        }
        cells_[std::size_t(before)].next = -1;
        at_ = pointOf(before);
        // The fuel again along the path; the heading of its last step.
        int fuel = tank_;
        heading_ = 1;
        for (int i = indexOf(start_); cells_[std::size_t(i)].next >= 0;) {
            const int j = cells_[std::size_t(i)].next;
            const Point a = pointOf(i), b = pointOf(j);
            fuel -= a.x != b.x && a.y != b.y ? diagonalCost_ : straightCost_;
            if (cells_[std::size_t(j)].kind == kRefill) {
                fuel = tank_;
            }
            heading_ = heading(a, b);
            i = j;
        }
        fuel_ = fuel;
        findSteps();
        if (!turns_.empty()) {
            store(0, Value::ofFloat(turns_.back()));
            turns_.pop_back();
        }
        dirty_ = true;
        return result(true);
    }
    case 15: { // 0x2e1b44: the judge -> register
        if (count == 0) {
            return false;
        }
        bool solved = false;
        if (cell(start_) && steps_.empty()) {
            int length = 1, blocked = 0;
            for (int i = indexOf(start_); cells_[std::size_t(i)].next >= 0;
                 i = cells_[std::size_t(i)].next) {
                ++length;
            }
            for (const Cell& k : cells_) {
                blocked += k.kind == kBlocked;
            }
            solved = across_ * down_ - blocked <= length;
        }
        store(0, Value::ofInt(solved ? 1 : 0));
        return true;
    }
    case 20: { // 0x2e2384: x, y of the first centre, the pitch, the touch radius, the press drop
        if (count < 6) {
            return false;
        }
        originX_ = c.real(first);
        originY_ = c.real(first + 1);
        pitchX_ = c.real(first + 2);
        pitchY_ = c.real(first + 3);
        radius_ = c.real(first + 4);
        pressDrop_ = integer(5);
        const Object* o = game_.scene.object(xq::hash("route_brd_anm_obj"));
        for (int y = 0; y < down_; ++y) {
            for (int x = 0; x < across_; ++x) {
                Cell* k = cell({x, y});
                k->x = originX_ + pitchX_ * float(x);
                k->y = originY_ + pitchY_ * float(y);
                // Half the size of the kind's button, rounded (0x2413c4 on
                // the set "route_brd_anm_res").
                static constexpr const char* kButton[] = {"btn_02", "btn_05", "btn_01"};
                const char* name = k->kind == kPlain    ? kButton[0]
                                   : k->kind == kRefill ? kButton[1]
                                   : k->kind == kStart  ? kButton[2]
                                                        : nullptr;
                if (!name || !o || !o->animation) {
                    continue;
                }
                for (const auto& cl : o->animation->cells) {
                    if (cl.hash == xq::hash(name)) {
                        k->halfW = std::round((cl.box[3] - cl.box[0]) * 0.5f);
                        k->halfH = std::round((cl.box[4] - cl.box[1]) * 0.5f);
                    }
                }
            }
        }
        return true;
    }
    case 21: { // the path's end: x, y [, fuel [, heading]]
        const Cell* k = count > 1 ? cell(at_) : nullptr;
        if (count < 2) {
            return false;
        }
        if (!k) {
            return result(false);
        }
        store(0, Value::ofFloat(k->x));
        store(1, Value::ofFloat(k->y));
        store(2, Value::ofInt(fuel_));
        store(3, Value::ofInt(heading_));
        return result(true);
    }
    case 30: // a value kept for undo (the globe's turn)
        if (count == 0 || turns_.size() >= 30) {
            return false;
        }
        turns_.push_back(c.real(first));
        return true;
    case 31: // the number of steps -> register
        if (count == 0) {
            return false;
        }
        store(0, Value::ofInt(int(steps_.size())));
        return result(true);
    default:
        return false;
    }
}

// 0x2e2198.
void RoutePuzzle::input() {
    if (const int id = game_.flagId(names::inputOff); id >= 0 && game_.flag(id)) {
        return;
    }
    const int px = game_.input.x, py = game_.input.y;
    if (game_.pressed()) {
        // 0x2e0ac0: the first step whose centre is within the radius.
        shown_ = picked_ = {};
        pickedCost_ = 0;
        for (const Step& s : steps_) {
            if (const Cell* k = cell(s.to); k && near(*k, px, py)) {
                shown_ = picked_ = s.to;
                pickedCost_ = s.cost;
                dirty_ = true;
                break;
            }
        }
    }
    if (game_.input.touching) {
        const Cell* k = cell(picked_);
        if (!k) {
            shown_ = picked_ = {};
            pickedCost_ = 0;
            return;
        }
        const Point show = near(*k, px, py) ? picked_ : Point{};
        if (show != shown_) {
            shown_ = show;
            dirty_ = true;
        }
        return;
    }
    if (!game_.released()) {
        return;
    }
    // The release: the pen is where it was lifted (game +0x520).
    const int lx = game_.previousInput.x, ly = game_.previousInput.y;
    Cell* to = cell(picked_);
    Cell* from = cell(at_);
    if (to && from && near(*to, lx, ly)) {
        heading_ = heading(at_, picked_);
        from->next = indexOf(picked_);
        at_ = picked_;
        fuel_ -= pickedCost_;
        if (to->kind == kRefill) {
            fuel_ = tank_;
        }
        findSteps();
        if (xq::Thread* t = game_.loopThreads[0]) {
            t->callback("BtnClickCall");
        }
        dirty_ = true;
    }
    shown_ = picked_ = {};
    pickedCost_ = 0;
}

void RoutePuzzle::update() {
    if (dirty_) {
        redraw();
        dirty_ = false;
    }
}

// 0x2e1010: three sprite batches - the path's segments (at each segment's
// middle), the cells' buttons (by kind: as they are, on the path, a step;
// the pressed one lower) and the arrows from the path's end to the steps.
void RoutePuzzle::redraw() {
    auto batch = [this](const char* name) -> Object* {
        Object* o = game_.scene.object(xq::hash(name));
        if (o && o->animation) {
            o->pieces.clear();
            return o;
        }
        return nullptr;
    };
    auto add = [](Object& o, const char* name, float x, float y) {
        const std::uint32_t h = xq::hash(name);
        for (std::size_t i = 0; i < o.animation->cells.size(); ++i) {
            if (o.animation->cells[i].hash == h) {
                o.pieces.push_back({int(i), x, y});
                return;
            }
        }
    };
    if (Object* o = batch("route_brd_anm_obj_lin"); o && cell(start_)) {
        for (int i = indexOf(start_); cells_[std::size_t(i)].next >= 0;) {
            const int j = cells_[std::size_t(i)].next;
            const Point a = pointOf(i), b = pointOf(j);
            const int dx = a.x - b.x, dy = a.y - b.y;
            static constexpr const char* kLine[] = {"line_00", "line_01", "line_02", "line_03"};
            const int which = dx == 0 ? 0 : dy == 0 ? 1 : (dx > 0) == (dy > 0) ? 3 : 2;
            const Cell& p = cells_[std::size_t(i)];
            const Cell& q = cells_[std::size_t(j)];
            add(*o, kLine[which], (p.x + q.x) * 0.5f, (p.y + q.y) * 0.5f);
            i = j;
        }
    }
    if (Object* o = batch("route_brd_anm_obj")) {
        static constexpr const char* kButton[] = {"btn_01",  "btn_02",  "btn_05",
                                                  "btn_b01", "btn_b02", "btn_b05",
                                                  "btn_01",  "btn_a02", "btn_a05"};
        for (int x = 0; x < across_; ++x) {
            for (int y = 0; y < down_; ++y) {
                const Cell& k = *cell({x, y});
                int which = k.kind == kStart    ? 0
                            : k.kind == kPlain  ? 1
                            : k.kind == kRefill ? 2
                                                : -1;
                if (which < 0) {
                    continue;
                }
                const bool free = k.next < 0;
                if (!free) {
                    which += 3;
                }
                const bool step = std::any_of(steps_.begin(), steps_.end(),
                                              [&](const Step& s) { return s.to == Point{x, y}; });
                if (free && step) {
                    which += 6;
                }
                const float drop = shown_ == Point{x, y} ? float(pressDrop_) : 0.0f;
                add(*o, kButton[which], k.x - k.halfW, k.y - k.halfH + drop);
            }
        }
    }
    if (Object* o = batch("route_brd_anm_obj_arrow"); o && cell(at_)) {
        const Cell& from = *cell(at_);
        for (const Step& s : steps_) {
            const int dx = s.to.x - at_.x, dy = s.to.y - at_.y;
            // up, up-right, right, down-right, down, down-left, left, up-left
            const int which = dx == 0  ? (dy < 0 ? 0 : 4)
                              : dx > 0 ? (dy < 0    ? 1
                                          : dy == 0 ? 2
                                                    : 3)
                                       : (dy > 0    ? 5
                                          : dy == 0 ? 6
                                                    : 7);
            static constexpr const char* kArrow[] = {"arrow_00", "arrow_01", "arrow_02",
                                                     "arrow_03", "arrow_04", "arrow_05",
                                                     "arrow_06", "arrow_07"};
            const Cell& to = *cell(s.to);
            add(*o, kArrow[which], (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f);
        }
    }
}

std::string RoutePuzzle::describe() const {
    std::string out = "route " + std::to_string(across_) + " " + std::to_string(down_) + " start " +
                      std::to_string(start_.x) + " " + std::to_string(start_.y) + " at " +
                      std::to_string(at_.x) + " " + std::to_string(at_.y) + " fuel " +
                      std::to_string(fuel_) + "/" + std::to_string(tank_) + " cost " +
                      std::to_string(straightCost_) + " " + std::to_string(diagonalCost_) + "\n";
    for (int y = 0; y < down_; ++y) {
        out += "row";
        for (int x = 0; x < across_; ++x) {
            const Cell& k = *cell({x, y});
            out += " " + std::to_string(k.kind) + (k.next >= 0 ? "*" : "");
        }
        out += "\n";
    }
    for (const Step& s : steps_) {
        const Cell& k = *cell(s.to);
        out += "step " + std::to_string(s.to.x) + " " + std::to_string(s.to.y) + " at " +
               std::to_string(int(k.x)) + " " + std::to_string(int(k.y)) + " cost " +
               std::to_string(s.cost) + "\n";
    }
    return out;
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeRoutePuzzle(Game& game) {
    return std::make_unique<RoutePuzzle>(game);
}

} // namespace picarat::adventure5
