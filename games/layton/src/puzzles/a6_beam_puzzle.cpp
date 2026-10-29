// The sixth adventure's lantern puzzles (loop 3290, class "Beam"; catalogue
// type 16: 057, 096, 105, "light up the village"): lamps of three colours
// go on the street crossings of a 6 x 6 grid. A lamp lights its crossing
// and every street running straight out of it to the street's end.
//
// Reconstructed from the class's command method 0x2c94a0 (vtable 0x3c7b7c;
// the rest to the base loop 0x230e94), the event machine 0x2c8874 with its
// touch states 0x2274a0, the light spread 0x226120, the judge 0x226adc and
// the street check 0x2c7e74 / 0x22850c. The judge holds when every lamp
// is placed and
//  - no lamp stands in another lamp's light (flag 4),
//  - no crossing is lit twice by one colour (flag 2: "already lit"),
//  - every crossing with a street is lit, and
//  - along every street through a crossing, one way or the other, there is
//    a lamp.
// The puzzle's script shows the village (LightUp with the lamps per
// colour), the "already lit" message (AlreadyLumpBegin / Exit) and the
// answer button; the class keeps the board, the pallet and the markers.
//
// Commands: 1 board (across, down, cell w, h, gap w, h, x, y); 2 pallet
// (the same for its slots); 3 a pallet item (colour, count, lamp, marker,
// counter object, marker when in conflict); 5 the animation resource; 4 /
// 6 crossing rows; 7 street bits for a row (row, bit, one flag per
// column); 8 the crossings that take a lamp (row, flags); 9 clear; 10
// undo; 11 / 12 / 14 / 15 settings (kept); 13 the judge (result; the lamps
// placed and their total into the registers named).
#include "puzzles/a5_special.hpp"
#include <array>
#include <cstdio>
#include <vector>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

class BeamPuzzle final : public SpecialPuzzle {
public:
    explicit BeamPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;
    void update() override;
    std::string describe() const override;

private:
    // Street bits by direction (0x226120's order): 1 left, 2 up-left, 4 up,
    // 8 up-right, 16 right, 32 down-right, 64 down, 128 down-left.
    static constexpr std::array<std::array<int, 3>, 8> kWays = {{{1, -1, 0},
                                                                 {2, -1, -1},
                                                                 {4, 0, -1},
                                                                 {8, 1, -1},
                                                                 {16, 1, 0},
                                                                 {32, 1, 1},
                                                                 {64, 0, 1},
                                                                 {128, -1, 1}}};

    struct Grid {
        int across = 0, down = 0, cellW = 0, cellH = 0, gapW = 0, gapH = 0, x = 0, y = 0;

        // The cell under a point, or -1 (gaps take nothing).
        int at(int px, int py) const {
            const int sx = cellW + gapW, sy = cellH + gapH;
            const int dx = px - x - gapW, dy = py - y - gapH;
            if (dx < 0 || dy < 0 || sx <= 0 || sy <= 0) {
                return -1;
            }
            const int cx = dx / sx, cy = dy / sy;
            if (cx >= across || cy >= down || dx % sx >= cellW || dy % sy >= cellH) {
                return -1;
            }
            return cy * across + cx;
        }

        std::array<float, 2> centre(int i) const {
            return {float(x + gapW + (cellW + gapW) * (i % across) + cellW / 2),
                    float(y + gapH + (cellH + gapH) * (i / across) + cellH / 2)};
        }
    };

    struct Item {
        int count = 0;
        std::string lamp, marker, counter, conflict;
    };

    struct Crossing {
        int streets = 0;
        bool takesLamp = false;
        int lamp = -1; // colour 0..2
    };

    // A step of the undo history: a lamp placed (from -1), taken off (to
    // -1) or moved.
    struct Step {
        int colour = 0, from = -1, to = -1;
    };

    struct Light {
        std::vector<int> times; // per crossing: lamps of each colour lighting it
        bool lampLit = false, twice = false;
    };

    std::size_t index(int x, int y) const { return std::size_t(y * board_.across + x); }

    bool inside(int x, int y) const {
        return x >= 0 && y >= 0 && x < board_.across && y < board_.down;
    }

    // The crossings a lamp at `i` lights, its own first (0x226120).
    std::vector<int> lit(int i) const;
    Light light() const;
    bool streetHasLamp(int x, int y, int bit) const;
    bool judge() const;
    int placed(int colour) const;
    void refresh();
    void place(int colour, int at);
    void showMarkers();

    Game& game_;
    Grid board_, pallet_;
    std::array<Item, 3> items_;
    std::vector<Crossing> crossings_;
    std::vector<Step> history_;
    std::uint32_t resource_ = 0;
    int flags_ = 0;
    bool conflictShown_ = false, dirty_ = false;
    // The lamp under the pen: its colour and where it came from (-1 the
    // pallet), drawn by the "ANM_OBJ" object.
    int carried_ = -1, carriedFrom_ = -1;
};

std::vector<int> BeamPuzzle::lit(int i) const {
    std::vector<int> out{i};
    const int x0 = i % board_.across, y0 = i / board_.across;
    for (const auto& [bit, dx, dy] : kWays) {
        int x = x0, y = y0;
        while (inside(x, y) && (crossings_[index(x, y)].streets & bit)) {
            x += dx, y += dy;
            if (!inside(x, y)) {
                break;
            }
            out.push_back(int(index(x, y)));
        }
    }
    return out;
}

BeamPuzzle::Light BeamPuzzle::light() const {
    Light l;
    l.times.assign(crossings_.size() * 3, 0);
    for (std::size_t i = 0; i < crossings_.size(); ++i) {
        const int colour = crossings_[i].lamp;
        if (colour < 0) {
            continue;
        }
        const auto cells = lit(int(i));
        for (std::size_t k = 0; k < cells.size(); ++k) {
            const auto c = std::size_t(cells[k]);
            if (k > 0 && crossings_[c].lamp >= 0) {
                l.lampLit = true;
            }
            if (++l.times[c * 3 + std::size_t(colour)] > 1) {
                l.twice = true;
            }
        }
    }
    return l;
}

// 0x22850c: from a crossing along a street bit, a lamp on the way (the
// crossing itself included).
bool BeamPuzzle::streetHasLamp(int x, int y, int bit) const {
    int dx = 0, dy = 0;
    for (const auto& [b, wx, wy] : kWays) {
        if (b == bit) {
            dx = wx, dy = wy;
        }
    }
    while (inside(x, y)) {
        const Crossing& c = crossings_[index(x, y)];
        if (c.lamp >= 0) {
            return true;
        }
        if (!(c.streets & bit)) {
            return false;
        }
        x += dx, y += dy;
    }
    return false;
}

bool BeamPuzzle::judge() const {
    for (std::size_t colour = 0; colour < 3; ++colour) {
        if (placed(int(colour)) < items_[colour].count) {
            return false;
        }
    }
    const Light l = light();
    if (l.lampLit || l.twice) {
        return false;
    }
    for (int y = 0; y < board_.down; ++y) {
        for (int x = 0; x < board_.across; ++x) {
            const std::size_t i = index(x, y);
            const int s = crossings_[i].streets;
            if (s && !l.times[i * 3] && !l.times[i * 3 + 1] && !l.times[i * 3 + 2]) {
                return false;
            }
            // 0x2c7e74: each axis with a street needs a lamp along it.
            for (const auto& [a, b] : {std::pair{1, 16}, {2, 32}, {4, 64}, {8, 128}}) {
                if ((s & (a | b)) && !streetHasLamp(x, y, a) && !streetHasLamp(x, y, b)) {
                    return false;
                }
            }
        }
    }
    return true;
}

int BeamPuzzle::placed(int colour) const {
    int n = 0;
    for (const Crossing& c : crossings_) {
        n += c.lamp == colour;
    }
    return n;
}

void BeamPuzzle::place(int colour, int at) {
    if (at >= 0) {
        crossings_[std::size_t(at)].lamp = colour;
    }
    dirty_ = true;
}

void BeamPuzzle::showMarkers() {
    // A marker object per crossing with a lamp: the colour's marker, its
    // conflict form when the crossing is lit twice by the colour or the lamp
    // stands in another lamp's light (event 0x1c).
    const Light l = light();
    for (std::size_t i = 0; i < crossings_.size(); ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "ANM_OBJ_0x%08x", unsigned(0x10000 + i));
        const int colour = crossings_[i].lamp;
        if (colour < 0) {
            game_.scene.remove(xq::hash(name));
            continue;
        }
        Object* o = game_.scene.object(xq::hash(name));
        if (!o) {
            o = &game_.createObject(name, 1, 112, 0, {}, 0);
            game_.setAnimationSet(*o, resource_);
        }
        bool clash = l.times[i * 3 + std::size_t(colour)] > 1;
        for (int c : lit(int(i))) {
            clash = clash || (c != int(i) && crossings_[std::size_t(c)].lamp >= 0);
        }
        const Item& item = items_[std::size_t(colour)];
        game_.playAnimation(*o, Value::ofText(clash ? item.conflict : item.marker), 0);
        const auto p = board_.centre(int(i));
        o->position.set({p[0], p[1]});
    }
}

void BeamPuzzle::refresh() {
    dirty_ = false;
    showMarkers();
    // Event 0x1c: the counters show the lamps left ("%02d"), the script
    // lights the village (LightUp with the lamps per colour).
    for (std::size_t colour = 0; colour < 3; ++colour) {
        const Item& item = items_[colour];
        Object* o = game_.scene.object(xq::hash(item.counter));
        if (!o && !item.counter.empty()) {
            dirty_ = true; // the script makes the counters after command 3
        }
        if (o) {
            char text[8];
            std::snprintf(text, sizeof text, "%02d", item.count - placed(int(colour)));
            game_.playAnimation(*o, Value::ofText(text), 0);
        }
    }
    if (xq::Thread* t = game_.loopThreads[0]) {
        const Value counts[3] = {Value::ofInt(placed(0)), Value::ofInt(placed(1)),
                                 Value::ofInt(placed(2))};
        t->callback("LightUp", counts);
        // Events 0x1d / 0x1e: the "already lit" message while a colour
        // lights a crossing twice or a lamp stands in light.
        const Light l = light();
        const bool clash = l.twice || l.lampLit;
        if (clash != conflictShown_) {
            const Value zero[1] = {Value::ofInt(0)};
            t->callback(clash ? "AlreadyLumpBegin" : "AlreadyLumpExit", zero);
            conflictShown_ = clash;
        }
    }
}

bool BeamPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t n = c.count() - first;
    auto arg = [&](std::size_t i) {
        return c.integer(first + i);
    };
    switch (number) {
    case 1:
    case 2: {
        if (n < 8) {
            return false;
        }
        Grid& g = number == 1 ? board_ : pallet_;
        g = {arg(0), arg(1), arg(2), arg(3), arg(4), arg(5), arg(6), arg(7)};
        if (number == 1) {
            crossings_.assign(std::size_t(g.across * g.down), {});
        }
        c.result = Value::ofInt(1);
        return true;
    }
    case 3: {
        if (n < 2 || arg(0) < 0 || arg(0) > 2) {
            return false;
        }
        Item& item = items_[std::size_t(arg(0))];
        item.count = arg(1);
        auto text = [&](std::size_t i) {
            const std::string* t = i < n ? c.text(first + i) : nullptr;
            return t ? *t : std::string();
        };
        item.lamp = text(2), item.marker = text(3), item.counter = text(4), item.conflict = text(5);
        // The board shows itself once set up (the console's start: the
        // counters at the full counts, LightUp with none placed).
        dirty_ = true;
        c.result = Value::ofInt(1);
        return true;
    }
    case 5:
        if (n < 2) {
            return false;
        }
        if (arg(0) == 0) {
            resource_ =
                c.at(first + 1).isText() ? xq::hash(*c.text(first + 1)) : c.unsignedInt(first + 1);
        }
        c.result = Value::ofInt(1);
        return true;
    case 7:
    case 8: {
        if (n < 2) {
            return false;
        }
        const int y = arg(0);
        const std::size_t from = number == 7 ? 2 : 1;
        for (std::size_t k = from; k < n; ++k) {
            const int x = int(k - from);
            if (!inside(x, y)) {
                break;
            }
            Crossing& cr = crossings_[index(x, y)];
            if (number == 7) {
                cr.streets |= arg(k) ? arg(1) : 0;
            } else {
                cr.takesLamp = arg(k) != 0;
            }
        }
        c.result = Value::ofInt(1);
        return true;
    }
    case 4:
    case 6: {
        // A crossing's (4: x, y, bits) or a row's (6: y, bits per column)
        // streets set outright.
        if (number == 4 && n >= 3 && inside(arg(0), arg(1))) {
            crossings_[index(arg(0), arg(1))].streets = arg(2);
        } else if (number == 6 && n >= 1) {
            for (std::size_t k = 1; k < n && inside(int(k - 1), arg(0)); ++k) {
                crossings_[index(int(k - 1), arg(0))].streets = arg(k);
            }
        }
        c.result = Value::ofInt(1);
        return true;
    }
    case 9: // 0x2ca1dc: every lamp back to the pallet
        for (Crossing& cr : crossings_) {
            cr.lamp = -1;
        }
        history_.clear();
        dirty_ = true;
        c.result = Value::ofInt(1);
        return true;
    case 10: { // 0x2c9b74: the last step taken back
        if (history_.empty()) {
            c.result = Value::ofInt(0);
            return true;
        }
        const Step s = history_.back();
        history_.pop_back();
        if (s.to >= 0) {
            crossings_[std::size_t(s.to)].lamp = -1;
        }
        if (s.from >= 0) {
            crossings_[std::size_t(s.from)].lamp = s.colour;
        }
        dirty_ = true;
        c.result = Value::ofInt(1);
        return true;
    }
    case 11:
    case 12:
    case 14:
    case 15:
        if (number == 12 && n >= 1) {
            flags_ = arg(0);
        }
        c.result = Value::ofInt(1);
        return true;
    case 13: {
        int total = 0, now = 0;
        for (int colour = 0; colour < 3; ++colour) {
            total += items_[std::size_t(colour)].count;
            now += placed(colour);
        }
        if (n >= 1) {
            if (Value* r = c.reg(first)) {
                *r = Value::ofInt(now);
            }
        }
        if (n >= 2) {
            if (Value* r = c.reg(first + 1)) {
                *r = Value::ofInt(total);
            }
        }
        c.result = Value::ofInt(judge() ? 1 : 0);
        return true;
    }
    default:
        return false;
    }
}

void BeamPuzzle::input() {
    // 0x2274a0: a press on a pallet slot (state 3) picks up a lamp of its
    // colour while any are left; on a crossing with a lamp (state 4) it
    // picks that lamp up. The lamp follows the pen (state 2); let go on a
    // free crossing that takes a lamp it stays there, anywhere else it goes
    // back to the pallet.
    const int px = game_.input.x, py = game_.input.y;
    if (game_.pressed() && carried_ < 0) {
        const int slot = pallet_.at(px, py);
        const int at = board_.at(px, py);
        if (slot >= 0 && slot < 3 && placed(slot) < items_[std::size_t(slot)].count) {
            carried_ = slot, carriedFrom_ = -1;
        } else if (at >= 0 && crossings_[std::size_t(at)].lamp >= 0) {
            carried_ = crossings_[std::size_t(at)].lamp, carriedFrom_ = at;
            crossings_[std::size_t(at)].lamp = -1;
            dirty_ = true;
        }
    }
    Object* held = game_.scene.object(xq::hash("ANM_OBJ_carried"));
    if (carried_ >= 0 && game_.input.touching) {
        if (!held) {
            held = &game_.createObject("ANM_OBJ_carried", 1, 113, 0, {}, 0);
            game_.setAnimationSet(*held, resource_);
            game_.playAnimation(*held, Value::ofText(items_[std::size_t(carried_)].lamp), 0);
        }
        held->position.set({float(px), float(py)});
    }
    if (carried_ >= 0 && game_.released()) {
        const int at = board_.at(game_.previousInput.x, game_.previousInput.y);
        const bool free = at >= 0 && crossings_[std::size_t(at)].takesLamp &&
                          crossings_[std::size_t(at)].lamp < 0;
        const int to = free ? at : -1;
        if (to != carriedFrom_) {
            history_.push_back({carried_, carriedFrom_, to});
        }
        place(carried_, to);
        carried_ = -1;
        game_.scene.remove(xq::hash("ANM_OBJ_carried"));
    }
}

void BeamPuzzle::update() {
    if (dirty_) {
        refresh();
    }
}

std::string BeamPuzzle::describe() const {
    std::string s = "beam " + std::to_string(board_.across) + "x" + std::to_string(board_.down) +
                    " judge " + std::to_string(judge() ? 1 : 0) + "\n";
    for (int y = 0; y < board_.down; ++y) {
        for (int x = 0; x < board_.across; ++x) {
            const Crossing& c = crossings_[index(x, y)];
            char cell[24];
            std::snprintf(cell, sizeof cell, " %3d%c%c", c.streets, c.takesLamp ? '+' : '.',
                          c.lamp < 0 ? '-' : char('0' + c.lamp));
            s += cell;
        }
        s += "\n";
    }
    return s;
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeBeamPuzzle(Game& game) {
    return std::make_unique<BeamPuzzle>(game);
}

} // namespace picarat::adventure5
