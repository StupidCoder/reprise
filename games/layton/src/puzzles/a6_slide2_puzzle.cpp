// The sixth adventure's train puzzles (loop 3270, class "Slide2"; catalogue
// type 14: 023, 088, 163): an engine and goods wagons stand on a rail map
// of cells. The engine is dragged along the rails a cell at a time; wagons
// it runs against couple on and are pulled or pushed with it, a tap on a
// coupled wagon uncouples it (and every wagon behind it), a tap on a loose
// wagon next to the train couples it. One button turns all the points.
// The puzzle is solved when every car stands where the judge list says,
// coupled to the cars it names.
//
// Reconstructed from the class's command method 0x306e94 (vtable
// 0x3c8f6c), its frame update 0x3066dc, the event machine 0x308404 (10
// press, 11 drag, 12 release, 0x15 couple, 0x16 coupling check, 0x1e a
// car's cell move), the move planning 0x309f98 / 0x3068e0 / 0x306d88 /
// 0x306354 / 0x309930 / 0x309ca0, the move 0x309e2c, the open sides
// 0x21a520, the heading 0x307e98 / 0x3076b4, the link sides 0x21a8b0,
// the coupling test 0x3079ec, the tap 0x30a810 and the judge 0x309074.
//
// Rails: a cell's piece is a mask of track bits (0x3b2618's side table):
// 1 up-down, 2 left-right, 4 up-left, 8 down-left, 16 up-right, 32
// down-right. Sides are numbered 0 up, 1 down, 2 left, 3 right (the
// direction function 0x21a9bc). A cell type with two masks is a point:
// the second mask is in use while the points are turned (flag 8).
//
// Commands: 10 board (x, y of cell 0's centre, across, down, cell size);
// 20 / 22 the map begins / ends; 21 a row of cell types (row, types);
// 23 the number of cell types; 24 a cell type (its masks); 30 dragging
// on / off; 40 the number of cars; 41 a car (name, kind 1 engine / 2
// wagon, x, y, pick radius, heading code, pick priority, coupled flag
// 16); 47 a car's look for its icon; 50..53 the 3D train (models follow
// their cars; 53 binds a model); 54 whether a car's model is moving; 60
// everything back to the start; 70 / 71 the judge list (kind 0 place,
// 1 links, 2 both; name, x, y, front, back); 72 the judge; 73 the end
// run; 80 a flag (kept); 81 the points (refused while a car stands on
// one); 90 the frame's events (handled by update()).
#include "puzzles/a5_special.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace picarat::adventure5 {
namespace {

using xq::Call;
using xq::Value;

constexpr int kNone = 4; // "no side"

// The upper screen's train view: the drum turns 40 degrees per engine move
// at 1.1 degrees a frame (fitted to console captures of 023,
// .analysis/adventure56/research/slide2-drum-fit.txt); the wagons' slide
// time is a hypothesis.
constexpr float kDrumStep = 0.6981317f;  // radians per engine move
constexpr float kDrumSpeed = 0.0191986f; // radians per frame
constexpr float kSlideFrames = 30;
constexpr float kDegree = 0.0174533f;

// Cell offsets by side.
constexpr std::array<std::array<int, 2>, 4> kStep = {{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};

constexpr int opposite(int side) {
    return side ^ 1;
}

// The two sides each track bit joins (0x3b2618: per bit, the sides it
// reaches from either of them).
constexpr std::array<int, 6> kPieceSides = {0b0011, 0b1100, 0b0101, 0b0110, 0b1001, 0b1010};

// 0x21a96c: the sides reachable from `side` on a piece (every side the
// piece touches for kNone).
int sidesFrom(int mask, int side) {
    int out = 0;
    for (std::size_t bit = 0; bit < kPieceSides.size(); ++bit) {
        if ((mask & (1 << bit)) && (side == kNone || (kPieceSides[bit] & (1 << side)))) {
            out |= kPieceSides[bit];
        }
    }
    return out;
}

// 0x21a9bc: the side of a towards which b lies (the longer axis wins;
// kNone for the same cell or a diagonal).
int sideTowards(int ax, int ay, int bx, int by) {
    const int dx = bx - ax, dy = by - ay;
    if ((dx == 0 && dy == 0) || std::abs(dx) == std::abs(dy)) {
        return kNone;
    }
    if (std::abs(dx) > std::abs(dy)) {
        return dx < 0 ? 2 : 3;
    }
    return dy < 0 ? 0 : 1;
}

std::array<float, 2> unit(float x, float y) {
    const float l = std::sqrt(x * x + y * y);
    return l > 0 ? std::array<float, 2>{x / l, y / l} : std::array<float, 2>{0, 0};
}

// 0x308168's angle: the angle from straight up (0, -1), 2 pi minus it for
// a vector pointing left (the icons' rotation).
float angleOf(std::array<float, 2> v) {
    const float a = std::acos(std::clamp(-v[1], -1.0f, 1.0f));
    return v[0] < 0 ? 6.2831855f - a : a;
}

class Slide2Puzzle final : public SpecialPuzzle {
public:
    explicit Slide2Puzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;
    void update() override;
    std::string describe() const override;

private:
    struct Car {
        std::string name;
        std::uint32_t hash = 0;
        int kind = 2; // 1 engine, 2 wagon
        int x = 0, y = 0, prevX = 0, prevY = 0;
        float radius = 0;
        int priority = 0;
        bool coupled = false; // part of the engine's train
        int front = -1, back = -1;
        int frontSide = kNone, backSide = kNone;
        std::array<float, 2> heading{0, 0}, bend{0, 0};
        int exit = kNone; // the way it takes at points it stands on
        bool changed = true;
    };

    // A cell move of the car leading a train (0x306d88's result).
    struct Move {
        int car = -1, side = kNone, exit = kNone;
    };

    // 0x306354's look ahead: the ways on from the next cell.
    struct Ahead {
        int ways = 0, count = 0;
    };

    struct Goal {
        int kind = 2;
        std::uint32_t car = 0, front = 0, back = 0;
        int x = 0, y = 0;
    };

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < across_ && y < down_; }

    int mask(int x, int y) const;
    int carAt(int x, int y) const;
    int find(std::uint32_t hash) const;
    int entrySide(const Car& car) const;
    int openSides(const Car& car) const;
    std::optional<Ahead> ahead(const Car& car, int side) const;
    bool leadsAway(const Car& car, int side) const;
    std::optional<Move> lead(int car, int side) const;
    std::optional<Move> pushed(int car, int side) const;
    std::optional<Move> plan(int car, int side) const;
    int pointsChoice(int ways) const;
    void apply(const Move& m);
    void moveCar(int car, int side, int exit);
    void turnHeading(Car& car);
    void setHeading(Car& car, int code);
    bool canCouple(int a, int b) const;
    void couple(int a, int b, bool onward);
    void coupleAround(int a);
    void tap(int car);
    bool judge() const;
    void reset();
    void fillWindow();
    void showSections();
    void turnDrum(float by);
    std::uint32_t modelOf(std::uint32_t car) const;
    void followChains();

    std::array<int, 2> centre(const Car& c) const {
        return {originX_ + cell_ * c.x, originY_ + cell_ * c.y};
    }

    Game& game_;
    int originX_ = 0, originY_ = 0, across_ = 0, down_ = 0, cell_ = 22;
    std::vector<std::vector<int>> types_;
    std::vector<int> cells_; // type per cell
    std::vector<Car> cars_, start_;
    std::vector<Goal> goals_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> models_; // model, car
    // The upper screen's drum (0x11b5cc's train system at +0xcdc): the map
    // section types along the way (command 51, the list at +0xbbc), the
    // one in front of the engine (+0xbd0) and the four the drum's quarters
    // MAP_OBJ_000..003 show (+0xbd4: sections index - 2 .. index + 1).
    std::vector<int> sections_;
    int sectionIndex_ = 0;
    std::array<int, 4> window_{3, 3, 3, 3};
    bool sectionsDue_ = false;
    float drumAngle_ = 0; // radians, DummyMap's turn about x, kept within 40 degrees
    // A drum turn under way (0x24c848 restarts it at each engine move) and
    // the models' coupling chains as last shown (+0xb94 front, +0xba8 back).
    float drumLeft_ = 0;
    bool drumBusy_ = false, motionDue_ = false;
    std::array<std::uint32_t, 4> shownFront_{}, shownBack_{};
    // Dragging is on from the start (0x1ca8 bit 4; the icon script turns it
    // off while an icon slides).
    bool turned_ = false, dragging_ = true, judgeDue_ = false;
    // The held car and the drag (0xca0..: the car's centre, the pen at the
    // last move, the last ten pen positions).
    int held_ = -1;
    bool heldStill_ = false, heldNear_ = false, movedSincePress_ = false;
    std::array<int, 2> anchor_{0, 0}, penAtMove_{-1, -1};
    std::vector<std::array<int, 2>> pen_;
};

int Slide2Puzzle::mask(int x, int y) const {
    if (!inside(x, y)) {
        return -1;
    }
    const int type = cells_[std::size_t(y * across_ + x)];
    if (type < 0 || type >= int(types_.size()) || types_[std::size_t(type)].empty()) {
        return 0;
    }
    const auto& masks = types_[std::size_t(type)];
    return turned_ && masks.size() >= 2 ? masks[1] : masks[0];
}

int Slide2Puzzle::carAt(int x, int y) const {
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        if (cars_[i].x == x && cars_[i].y == y) {
            return int(i);
        }
    }
    return -1;
}

int Slide2Puzzle::find(std::uint32_t hash) const {
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        if (cars_[i].hash == hash) {
            return int(i);
        }
    }
    return -1;
}

int Slide2Puzzle::entrySide(const Car& car) const {
    return sideTowards(car.x, car.y, car.prevX, car.prevY);
}

// 0x21a520: the sides a car may leave its cell by: those its piece joins
// to the side it came in by (all of the piece's before it has moved),
// where the next cell's piece meets them; on points only the way chosen
// and the way back.
int Slide2Puzzle::openSides(const Car& car) const {
    const int entry = entrySide(car);
    const int m = mask(car.x, car.y);
    if (m <= 0) {
        return 0;
    }
    int open = sidesFrom(m, entry);
    for (int side = 0; side < 4; ++side) {
        const int nx = car.x + kStep[std::size_t(side)][0],
                  ny = car.y + kStep[std::size_t(side)][1];
        const int nm = mask(nx, ny);
        bool blocked = nm < 0 || !(sidesFrom(nm, kNone) & (1 << opposite(side))) ||
                       !(sidesFrom(nm, opposite(side)));
        if (car.exit != kNone && side != car.exit && side != entry) {
            blocked = true;
        }
        if (blocked) {
            open &= ~(1 << side);
        }
    }
    return open;
}

// 0x306354: a car can go to the next cell by `side` when the side is open
// and nobody stands there; the ways on from there, without the way back
// and dead ends.
std::optional<Slide2Puzzle::Ahead> Slide2Puzzle::ahead(const Car& car, int side) const {
    const int nx = car.x + kStep[std::size_t(side)][0], ny = car.y + kStep[std::size_t(side)][1];
    if (!(openSides(car) & (1 << side)) || mask(nx, ny) < 0 || carAt(nx, ny) >= 0) {
        return std::nullopt;
    }
    const int back = opposite(side);
    Ahead a;
    a.ways = sidesFrom(mask(nx, ny), back);
    for (int s = 0; s < 4; ++s) {
        const int fm = mask(nx + kStep[std::size_t(s)][0], ny + kStep[std::size_t(s)][1]);
        if (s == back || fm < 0 || !(sidesFrom(fm, kNone) & (1 << opposite(s)))) {
            a.ways &= ~(1 << s);
        }
        if (a.ways & (1 << s)) {
            ++a.count;
        }
    }
    return a;
}

// 0x30a9f4: a car may lead its train by `side` when it is alone, or at an
// end of the train going away from the car it is coupled to (judged along
// its heading).
bool Slide2Puzzle::leadsAway(const Car& car, int side) const {
    if (car.front >= 0 && car.back >= 0) {
        return false;
    }
    if (car.front < 0 && car.back < 0) {
        return true;
    }
    const Car& other = cars_[std::size_t(car.front >= 0 ? car.front : car.back)];
    const auto toLink = unit(float(other.x - car.x), float(other.y - car.y));
    const auto way = unit(float(kStep[std::size_t(side)][0]), float(kStep[std::size_t(side)][1]));
    const auto h = unit(car.heading[0], car.heading[1]);
    const float along = h[0] * way[0] + h[1] * way[1];
    const float link = h[0] * toLink[0] + h[1] * toLink[1];
    return (along < 0 && link > 0) || (along > 0 && link < 0);
}

// 0x309ca0: at points, the way out nearest to where the pen has been going
// (its last ten positions).
int Slide2Puzzle::pointsChoice(int ways) const {
    float sx = 0, sy = 0;
    for (std::size_t i = 0; i + 1 < pen_.size(); ++i) {
        sx += float(pen_[i][0] - pen_[i + 1][0]);
        sy += float(pen_[i][1] - pen_[i + 1][1]);
    }
    const auto v = unit(sx, sy);
    int best = kNone;
    float bestDot = -2;
    for (int s = 0; s < 4; ++s) {
        if (ways & (1 << s)) {
            const float d =
                v[0] * float(kStep[std::size_t(s)][0]) + v[1] * float(kStep[std::size_t(s)][1]);
            if (d > bestDot) {
                bestDot = d, best = s;
            }
        }
    }
    return best;
}

// 0x306d88: the car leads a move by `side`; on a bend a pull across it
// follows the bend (0x309930: the way round whose next piece turns towards
// the pull; a hypothesis on the choice between two such ways).
std::optional<Slide2Puzzle::Move> Slide2Puzzle::lead(int index, int side) const {
    const Car& car = cars_[std::size_t(index)];
    if (!leadsAway(car, side)) {
        return std::nullopt;
    }
    std::optional<Ahead> a = ahead(car, side);
    if (!a) {
        if (car.heading[0] == 0 || car.heading[1] == 0) {
            return std::nullopt;
        }
        // 0x309930: of the other ways, the last one on the same side of the
        // heading as the pull (the signs of their dot products with it)
        // goes round the bend, if the piece past it turns towards the pull.
        const auto h = unit(car.heading[0], car.heading[1]);
        auto along = [&](int s) {
            return h[0] * float(kStep[std::size_t(s)][0]) + h[1] * float(kStep[std::size_t(s)][1]);
        };
        const float pull = along(side);
        int round = kNone;
        for (int u = 0; u < 4; ++u) {
            if (u != side && ((pull < 0 && along(u) < 0) || (pull > 0 && along(u) > 0))) {
                round = u;
            }
        }
        if (round == kNone) {
            return std::nullopt;
        }
        const int nm =
            mask(car.x + kStep[std::size_t(round)][0], car.y + kStep[std::size_t(round)][1]);
        if (nm <= 0 || !(sidesFrom(nm, opposite(round)) & (1 << side))) {
            return std::nullopt;
        }
        a = ahead(car, round);
        if (!a || a->count != 1) {
            return std::nullopt;
        }
        side = round;
    }
    Move m{index, side, kNone};
    if (a->count > 1) {
        m.exit = pointsChoice(a->ways);
        if (m.exit == kNone) {
            return std::nullopt;
        }
    }
    return m;
}

// 0x3068e0: a push: the car coupled on the side of the pull is pushed, so
// the far end of the train leads by its one way on (not back towards the
// train). On a bend the pull picks the coupled car lying its way.
std::optional<Slide2Puzzle::Move> Slide2Puzzle::pushed(int index, int side) const {
    const Car& car = cars_[std::size_t(index)];
    auto linkSide = [&](int other) {
        return other < 0 ? kNone
                         : sideTowards(car.x, car.y, cars_[std::size_t(other)].x,
                                       cars_[std::size_t(other)].y);
    };
    bool viaFront;
    if (car.front >= 0 && linkSide(car.front) == side) {
        viaFront = true;
    } else if (car.back >= 0 && linkSide(car.back) == side) {
        viaFront = false;
    } else {
        if (car.heading[0] == 0 || car.heading[1] == 0) {
            return std::nullopt;
        }
        // Hypothesis (0x3068e0's bend case): the coupled car lying along
        // the pull and able to go that way.
        int pick = -1;
        for (int other : {car.front, car.back}) {
            if (other < 0 || pick >= 0) {
                continue;
            }
            const Car& o = cars_[std::size_t(other)];
            const float d = float(o.x - car.x) * float(kStep[std::size_t(side)][0]) +
                            float(o.y - car.y) * float(kStep[std::size_t(side)][1]);
            if (d > 0 && (openSides(o) & (1 << side))) {
                pick = other;
            }
        }
        if (pick < 0) {
            return std::nullopt;
        }
        viaFront = pick == car.front;
    }
    int far = viaFront ? car.front : car.back, before = index;
    for (;;) {
        const int next = viaFront ? cars_[std::size_t(far)].front : cars_[std::size_t(far)].back;
        if (next < 0) {
            break;
        }
        before = far, far = next;
    }
    const Car& end = cars_[std::size_t(far)];
    const int towardsTrain =
        sideTowards(end.x, end.y, cars_[std::size_t(before)].x, cars_[std::size_t(before)].y);
    int count = 0, way = kNone;
    for (int s = 0; s < 4; ++s) {
        if (!(openSides(end) & (1 << s)) || s == towardsTrain) {
            continue;
        }
        if (mask(end.x + kStep[std::size_t(s)][0], end.y + kStep[std::size_t(s)][1]) > 0) {
            ++count, way = s;
        }
    }
    if (count != 1) {
        return std::nullopt;
    }
    return lead(far, way);
}

// 0x309f98: the move a pull on a car by `side` makes. A car turning back
// the way it came needs the pen to have gone half a cell's diagonal since
// the last move.
std::optional<Slide2Puzzle::Move> Slide2Puzzle::plan(int index, int side) const {
    const Car& car = cars_[std::size_t(index)];
    std::optional<Move> m = leadsAway(car, side) ? lead(index, side) : pushed(index, side);
    if (!m) {
        return std::nullopt;
    }
    int goes = m->side;
    if (m->car != index) {
        // The held car steps into the cell of its neighbour towards the
        // leader.
        int at = m->car;
        const bool viaFront = cars_[std::size_t(at)].front >= 0;
        for (int next = viaFront ? cars_[std::size_t(at)].front : cars_[std::size_t(at)].back;
             next >= 0;
             next = viaFront ? cars_[std::size_t(next)].front : cars_[std::size_t(next)].back) {
            if (next == index) {
                goes =
                    sideTowards(car.x, car.y, cars_[std::size_t(at)].x, cars_[std::size_t(at)].y);
                break;
            }
            at = next;
        }
    }
    if (goes == entrySide(car) && !pen_.empty()) {
        const int dx = penAtMove_[0] - pen_[0][0], dy = penAtMove_[1] - pen_[0][1];
        if (float(dx * dx + dy * dy) < float(cell_ * cell_) * 0.5f) {
            return std::nullopt;
        }
    }
    return m;
}

// 0x307e98: after a move the heading follows the piece: front and back are
// the piece's two sides at the car, the front staying on the side the
// heading pointed to.
void Slide2Puzzle::turnHeading(Car& car) {
    const int entry = entrySide(car);
    if (entry == kNone) {
        return;
    }
    int sides = sidesFrom(mask(car.x, car.y), entry);
    if (__builtin_popcount(unsigned(sides)) > 2) {
        sides = openSides(car);
    }
    int other = kNone;
    for (int s = 0; s < 4; ++s) {
        if ((sides & (1 << s)) && s != entry) {
            other = s;
        }
    }
    if (other == kNone) {
        return;
    }
    const auto e = kStep[std::size_t(entry)];
    const bool backwards = car.heading[0] * float(e[0]) + car.heading[1] * float(e[1]) >= 0;
    car.frontSide = backwards ? entry : other;
    car.backSide = backwards ? other : entry;
    const auto f = kStep[std::size_t(car.frontSide)], b = kStep[std::size_t(car.backSide)];
    car.heading = {float(f[0] - b[0]), float(f[1] - b[1])};
    car.bend = {float(f[0] + b[0]), float(f[1] + b[1])};
}

// 0x3076b4: the heading codes of the car list (front side, back side).
void Slide2Puzzle::setHeading(Car& car, int code) {
    static constexpr std::array<std::array<int, 2>, 9> kCodes = {
        {{4, 4}, {0, 1}, {3, 1}, {3, 2}, {3, 0}, {1, 0}, {2, 0}, {2, 3}, {0, 3}}};
    const auto& [f, b] = kCodes[std::size_t(std::clamp(code, 0, 8))];
    car.frontSide = f, car.backSide = b;
    if (f == kNone) {
        car.heading = {0, 0};
        return;
    }
    const auto h = unit(float(kStep[std::size_t(f)][0] - kStep[std::size_t(b)][0]),
                        float(kStep[std::size_t(f)][1] - kStep[std::size_t(b)][1]));
    car.heading = h;
}

// Event 0x1e: a car steps into the next cell (empty by then: the train's
// moves go leader first).
void Slide2Puzzle::moveCar(int index, int side, int exit) {
    Car& car = cars_[std::size_t(index)];
    // Event 0x14's flag: the engine runs forward when the step goes along
    // its heading (before the heading follows the new piece).
    const bool forward = car.heading[0] * float(kStep[std::size_t(side)][0]) +
                             car.heading[1] * float(kStep[std::size_t(side)][1]) >=
                         0;
    car.prevX = car.x, car.prevY = car.y;
    car.x += kStep[std::size_t(side)][0], car.y += kStep[std::size_t(side)][1];
    car.exit = exit;
    turnHeading(car);
    car.changed = true;
    if (index == held_) {
        anchor_ = centre(car);
        penAtMove_ = pen_.empty() ? std::array<int, 2>{-1, -1} : pen_[0];
    }
    if (car.coupled) {
        coupleAround(index);
    }
    if (car.kind == 1) {
        // Event 0x14: the engine moved, the drum turns on 40 degrees, back
        // when the engine runs backwards (console: 023's first pull out of
        // the siding).
        drumLeft_ += forward ? kDrumStep : -kDrumStep;
        if (!drumBusy_) {
            drumBusy_ = motionDue_ = true;
        }
    }
    judgeDue_ = true;
}

// 0x309e2c: the leader moves, then each car behind it into the cell of the
// one before, taking the way that one took.
void Slide2Puzzle::apply(const Move& m) {
    struct Step {
        int car, side, exit;
    };

    std::vector<Step> steps{{m.car, m.side, m.exit}};
    int at = m.car;
    const bool viaFront = cars_[std::size_t(at)].front >= 0;
    for (int next = viaFront ? cars_[std::size_t(at)].front : cars_[std::size_t(at)].back;
         next >= 0;
         next = viaFront ? cars_[std::size_t(next)].front : cars_[std::size_t(next)].back) {
        const Car& a = cars_[std::size_t(at)];
        const Car& n = cars_[std::size_t(next)];
        const int open = openSides(a) & ~(1 << sideTowards(a.x, a.y, n.x, n.y));
        int exit = kNone;
        for (int s = 0; s < 4; ++s) {
            if (open & (1 << s)) {
                exit = s;
            }
        }
        steps.push_back({next, sideTowards(n.x, n.y, a.x, a.y), exit});
        at = next;
    }
    for (const Step& s : steps) {
        moveCar(s.car, s.side, s.exit);
    }
}

// 0x3079ec: a coupled car and a loose one in neighbouring cells couple
// when the rails at both lead to each other.
bool Slide2Puzzle::canCouple(int a, int b) const {
    const Car& ca = cars_[std::size_t(a)];
    const Car& cb = cars_[std::size_t(b)];
    if (!ca.coupled || cb.coupled || std::abs(ca.x - cb.x) + std::abs(ca.y - cb.y) != 1) {
        return false;
    }
    const int ab = sideTowards(ca.x, ca.y, cb.x, cb.y), ba = opposite(ab);
    return (openSides(ca) & (1 << ab)) && (openSides(cb) & (1 << ba));
}

// Event 0x15: b joins the train at a. On the engine it goes behind when it
// stands behind the heading (or square to it), else in front; on a wagon
// it takes the free end. Coupling on moves goes on through b (0x16).
void Slide2Puzzle::couple(int a, int b, bool onward) {
    Car& ca = cars_[std::size_t(a)];
    Car& cb = cars_[std::size_t(b)];
    bool behind;
    if (ca.kind == 1) {
        behind = ca.heading[0] * float(cb.x - ca.x) + ca.heading[1] * float(cb.y - ca.y) <= 0;
    } else if (ca.back >= 0) {
        behind = false;
    } else if (ca.front >= 0) {
        behind = true;
    } else {
        return;
    }
    if (behind) {
        ca.back = b, cb.front = a;
    } else {
        ca.front = b, cb.back = a;
    }
    ca.changed = cb.changed = true;
    cb.coupled = true;
    if (onward) {
        coupleAround(b);
    }
}

// Event 0x16 without a partner: the loose cars around a coupled one.
void Slide2Puzzle::coupleAround(int a) {
    for (int s = 0; s < 4; ++s) {
        const int b = carAt(cars_[std::size_t(a)].x + kStep[std::size_t(s)][0],
                            cars_[std::size_t(a)].y + kStep[std::size_t(s)][1]);
        if (b >= 0 && canCouple(a, b)) {
            couple(a, b, true);
        }
    }
}

// 0x30a810: a tap on a wagon. Coupled, it comes off with every car behind
// it (all of them loose from each other); loose, it couples to the first
// coupled car next to it.
void Slide2Puzzle::tap(int index) {
    Car& car = cars_[std::size_t(index)];
    if (car.kind != 2) {
        return;
    }
    if (!car.coupled) {
        for (int s = 0; s < 4; ++s) {
            const int a = carAt(car.x + kStep[std::size_t(s)][0], car.y + kStep[std::size_t(s)][1]);
            if (a >= 0 && cars_[std::size_t(a)].coupled) {
                if (canCouple(a, index)) {
                    couple(a, index, false);
                }
                break;
            }
        }
        return;
    }
    bool engineInFront = false;
    for (int p = car.front; p >= 0; p = cars_[std::size_t(p)].front) {
        if (cars_[std::size_t(p)].kind == 1) {
            engineInFront = true;
            break;
        }
    }
    const int towards = engineInFront ? car.front : car.back;
    if (towards >= 0) {
        Car& t = cars_[std::size_t(towards)];
        (engineInFront ? t.back : t.front) = -1;
        t.changed = true;
    }
    for (int p = index; p >= 0;) {
        Car& c = cars_[std::size_t(p)];
        const int next = engineInFront ? c.back : c.front;
        c.front = c.back = -1;
        c.coupled = false;
        c.changed = true;
        p = next;
    }
}

// 0x309074: every judge entry holds: the car's cell (kinds 0, 2) and its
// front and back neighbours (kinds 1, 2; none named means none coupled).
bool Slide2Puzzle::judge() const {
    if (goals_.empty()) {
        return false;
    }
    auto linkOk = [&](std::uint32_t want, int have) {
        return want == 0 ? have < 0 : have >= 0 && cars_[std::size_t(have)].hash == want;
    };
    for (const Goal& g : goals_) {
        const int i = find(g.car);
        if (i < 0) {
            return false;
        }
        const Car& c = cars_[std::size_t(i)];
        const bool placed = c.x == g.x && c.y == g.y;
        const bool linked = linkOk(g.front, c.front) && linkOk(g.back, c.back);
        if ((g.kind == 0 && !placed) || (g.kind == 1 && !linked) ||
            (g.kind == 2 && !(placed && linked)) || g.kind > 2) {
            return false;
        }
    }
    return true;
}

// Command 60 (0x307810): the cars back where they started, the points
// straight.
void Slide2Puzzle::reset() {
    cars_ = start_;
    for (Car& c : cars_) {
        c.changed = true;
    }
    turned_ = false;
    shownFront_ = {};
    shownBack_ = {};
}

// 0x24cab8: the drum's quarters show the sections two before the index to
// one after it (around the list; type 3 where the list has none).
void Slide2Puzzle::fillWindow() {
    const int count = int(sections_.size());
    if (count < 4) {
        return;
    }
    for (int k = 0; k < 4; ++k) {
        const int i = ((sectionIndex_ + k - 2) % count + count) % count;
        window_[std::size_t(k)] = sections_[std::size_t(i)];
    }
    sectionsDue_ = true;
}

// 0x24c6d4: each quarter's three map pieces of its section type shown
// ("_%03d_%02d_obj", types 1..8, pieces 1..3), every other type's hidden.
void Slide2Puzzle::showSections() {
    sectionsDue_ = false;
    for (int k = 0; k < 4; ++k) {
        char name[32];
        std::snprintf(name, sizeof name, "MAP_OBJ_%03d", k);
        adventure56::Object3D* map = game_.scene.object3D(xq::hash(name));
        if (!map) {
            continue;
        }
        for (int type = 1; type <= 8; ++type) {
            for (int piece = 1; piece <= 3; ++piece) {
                std::snprintf(name, sizeof name, "_%03d_%02d_obj", type, piece);
                const std::uint32_t h = xq::hash(name);
                if (type == window_[std::size_t(k)]) {
                    map->hiddenMeshes.erase(h);
                } else {
                    map->hiddenMeshes.insert(h);
                }
            }
        }
    }
}

// 0x1be3f0: the drum turns; past 40 degrees either way it steps back by
// 40 and the section window moves one along (0x1b5780 -> 0x24cab8).
void Slide2Puzzle::turnDrum(float by) {
    constexpr float kQuarter = 0.6981317f; // 40 degrees
    drumAngle_ += by;
    if (drumAngle_ >= kQuarter) {
        drumAngle_ -= kQuarter;
        ++sectionIndex_;
        fillWindow();
    } else if (drumAngle_ <= -kQuarter) {
        drumAngle_ += kQuarter;
        --sectionIndex_;
        fillWindow();
    }
    if (const int count = int(sections_.size())) {
        sectionIndex_ = (sectionIndex_ % count + count) % count;
    }
    if (adventure56::Object3D* map = game_.scene.object3D(xq::hash("DummyMap"))) {
        auto r = map->rotation3Offset.value();
        r[0] = drumAngle_;
        map->rotation3Offset.set(r);
    }
}

std::uint32_t Slide2Puzzle::modelOf(std::uint32_t car) const {
    for (const auto& [model, owner] : models_) {
        if (owner == car) {
            return model;
        }
    }
    return 0;
}

// 0x1b6900 / event 0x28: the cars coupled in front of and behind the
// engine (up to four each) against those shown. A newly coupled wagon's
// model is put on the drum 55 (front) / 15 (back) degrees beyond its
// place and slides to 8 degrees per place from the engine's turn; one
// uncoupled slides 55 degrees away (front) / 15 (back).
void Slide2Puzzle::followChains() {
    int engine = -1;
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        if (cars_[i].kind == 1) {
            engine = int(i);
        }
    }
    const adventure56::Object3D* engineModel =
        engine >= 0 ? game_.scene.object3D(modelOf(cars_[std::size_t(engine)].hash)) : nullptr;
    if (!engineModel) {
        return;
    }
    const float base = engineModel->rotation3.value()[0];
    for (int list = 0; list < 2; ++list) {
        const bool back = list == 1;
        std::array<std::uint32_t, 4> now{};
        int at = back ? cars_[std::size_t(engine)].back : cars_[std::size_t(engine)].front;
        for (std::size_t k = 0; k < 4 && at >= 0; ++k) {
            now[k] = cars_[std::size_t(at)].hash;
            at = back ? cars_[std::size_t(at)].back : cars_[std::size_t(at)].front;
        }
        auto& shown = back ? shownBack_ : shownFront_;
        for (std::size_t k = 0; k < 4; ++k) {
            if (now[k] == shown[k]) {
                continue;
            }
            const bool attach = shown[k] == 0;
            adventure56::Object3D* m = game_.scene.object3D(modelOf(attach ? now[k] : shown[k]));
            if (m) {
                auto r = m->rotation3Offset.value();
                if (attach) {
                    const float far = (back ? 15.0f : 55.0f) + 8.0f * float(k + 1);
                    r[0] = base + (back ? far : -far) * kDegree;
                    m->rotation3Offset.set(r);
                    r[0] += (back ? -15.0f : 55.0f) * kDegree;
                } else {
                    r[0] += (back ? 15.0f : -55.0f) * kDegree;
                }
                m->rotation3Offset.start(r, kSlideFrames, 0);
                motionDue_ = true;
            }
            shown[k] = now[k];
        }
    }
}

bool Slide2Puzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t n = c.count() - first;
    auto arg = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto name = [&](std::size_t i) -> std::uint32_t {
        if (i >= n) {
            return 0;
        }
        const std::string* t = c.text(first + i);
        return t ? (t->empty() ? 0 : xq::hash(*t)) : c.unsignedInt(first + i);
    };
    auto put = [&](std::size_t i, Value v) {
        if (i < n) {
            if (Value* r = c.reg(first + i)) {
                *r = v;
            }
        }
    };
    c.result = Value::ofInt(1);
    switch (number) {
    case 10:
        if (n < 5) {
            return false;
        }
        originX_ = arg(0), originY_ = arg(1), across_ = arg(2), down_ = arg(3), cell_ = arg(4);
        cells_.assign(std::size_t(std::max(0, across_ * down_)), 0);
        return true;
    case 20: // map edit begins / ends (0x1f8 bit 2)
    case 22:
        return true;
    case 21:
        if (n < 1) {
            return false;
        }
        for (std::size_t k = 1; k < n && inside(int(k - 1), arg(0)); ++k) {
            cells_[std::size_t(arg(0) * across_ + int(k - 1))] = arg(k);
        }
        return true;
    case 23:
        types_.clear();
        return true;
    case 24: {
        std::vector<int> masks;
        for (std::size_t k = 0; k < n; ++k) {
            masks.push_back(arg(k));
        }
        types_.push_back(masks);
        return true;
    }
    case 30:
        dragging_ = n >= 1 && arg(0) != 0;
        return true;
    case 40:
        cars_.clear(), start_.clear();
        return true;
    case 41: {
        if (n < 6) {
            return false;
        }
        Car car;
        const std::string* t = c.text(first);
        car.name = t ? *t : std::string();
        car.hash = xq::hash(car.name);
        car.kind = arg(1);
        car.x = car.prevX = arg(2), car.y = car.prevY = arg(3);
        car.radius = c.real(first + 4);
        setHeading(car, arg(5));
        car.priority = n > 6 ? arg(6) & 15 : 0;
        car.coupled = n > 7 && (arg(7) & 16);
        if (mask(car.x, car.y) <= 0 || carAt(car.x, car.y) >= 0) {
            c.result = Value::ofInt(0);
            return true;
        }
        cars_.push_back(car);
        start_.push_back(car);
        return true;
    }
    case 47: {
        const int i = find(name(0));
        if (n < 7 || i < 0) {
            c.result = Value::ofInt(0);
            return true;
        }
        Car& car = cars_[std::size_t(i)];
        put(1, Value::ofInt(car.changed ? 1 : 0));
        if (car.changed) {
            const auto p = centre(car);
            put(2, Value::ofInt(p[0] + cell_ / 2));
            put(3, Value::ofInt(p[1] + cell_ / 2));
            put(4, Value::ofInt(int(car.bend[0])));
            put(5, Value::ofInt(int(car.bend[1])));
            put(6, Value::ofFloat(angleOf(unit(car.heading[0], car.heading[1]))));
            put(7, Value::ofInt(car.coupled ? 1 : 0));
            // The icon's turn: the last step, along the heading.
            auto v = unit(float(car.x - car.prevX), float(car.y - car.prevY));
            if (v[0] * car.heading[0] + v[1] * car.heading[1] < 0) {
                v = {-v[0], -v[1]};
            }
            put(8, Value::ofFloat(angleOf(v)));
            car.changed = false;
        }
        return true;
    }
    case 50: // the train view's lists (sections, models) and two speeds, kept
        sections_.clear();
        return true;
    case 51:
        if (n >= 1) {
            sections_.push_back(arg(0));
        }
        return true;
    case 52:
        fillWindow();
        showSections();
        return true;
    case 53:
        if (n >= 2) {
            models_.push_back({name(0), name(1)});
        }
        return true;
    case 54: {
        // 0x306e94 0x36: a model moves while the drum turns or its slide
        // runs (the list at +0x18b4, states 1 / 2).
        const adventure56::Object3D* m = game_.scene.object3D(name(0));
        c.result = Value::ofInt(drumBusy_ || (m && m->rotation3Offset.moving()) ? 1 : 0);
        return true;
    }
    case 60:
        reset();
        return true;
    case 70:
        goals_.clear();
        return true;
    case 71:
        if (n < 6) {
            return false;
        }
        goals_.push_back({arg(0), name(1), name(4), name(5), arg(2), arg(3)});
        return true;
    case 72:
        put(0, Value::ofInt(judge() ? 1 : 0));
        return true;
    case 90:
        // 0x24c6d4: the map pieces follow the section window when it moved.
        if (sectionsDue_) {
            showSections();
        }
        return true;
    case 73:
    case 80:
        return true;
    case 81: {
        for (const Car& car : cars_) {
            const int type = cells_[std::size_t(car.y * across_ + car.x)];
            if (type >= 0 && type < int(types_.size()) && types_[std::size_t(type)].size() >= 2) {
                c.result = Value::ofInt(0);
                return true;
            }
        }
        turned_ = n >= 1 && arg(0) != 0;
        return true;
    }
    default:
        return false;
    }
}

void Slide2Puzzle::input() {
    const std::array<int, 2> pen{game_.input.x, game_.input.y};
    if (game_.pressed()) {
        // Event 10: the car of highest priority whose pick circle holds the
        // pen.
        pen_.assign(1, pen);
        held_ = -1;
        for (std::size_t i = 0; i < cars_.size(); ++i) {
            const auto p = centre(cars_[i]);
            const float dx = float(p[0] - pen[0]), dy = float(p[1] - pen[1]);
            if ((held_ < 0 || cars_[std::size_t(held_)].priority < cars_[i].priority) &&
                dx * dx + dy * dy < cars_[i].radius * cars_[i].radius) {
                held_ = int(i);
            }
        }
        heldStill_ = heldNear_ = held_ >= 0;
        movedSincePress_ = false;
        penAtMove_ = {-1, -1};
        if (held_ >= 0) {
            anchor_ = centre(cars_[std::size_t(held_)]);
        }
        return;
    }
    if (game_.input.touching && held_ >= 0) {
        // Event 11: half a cell off the car's centre across (else along) it
        // pulls the car a cell that way. A loose wagon does not move.
        pen_.insert(pen_.begin(), pen);
        if (pen_.size() > 10) {
            pen_.pop_back();
        }
        const Car& car = cars_[std::size_t(held_)];
        const int dx = pen[0] - anchor_[0], dy = pen[1] - anchor_[1];
        const bool movable = car.kind != 2 || car.front >= 0 || car.back >= 0;
        bool moved = false;
        if (movable && dragging_ && std::abs(dx) * 2 > cell_) {
            if (auto m = plan(held_, dx < 0 ? 2 : 3)) {
                apply(*m);
                moved = movedSincePress_ = true;
                heldStill_ = false;
            }
        }
        if (movable && !moved && dragging_ && std::abs(dy) * 2 > cell_) {
            if (auto m = plan(held_, dy < 0 ? 0 : 1)) {
                apply(*m);
                movedSincePress_ = true;
                heldStill_ = false;
            }
        }
        const Car& now = cars_[std::size_t(held_)];
        if (!movedSincePress_ && dragging_ && now.kind == 2) {
            const auto p = centre(now);
            const float ex = float(p[0] - pen[0]), ey = float(p[1] - pen[1]);
            heldNear_ = ex * ex + ey * ey <= now.radius * now.radius;
        }
        return;
    }
    if (game_.released()) {
        // Event 12: a tap on a wagon (still on it, nothing moved).
        if (held_ >= 0 && heldNear_ && heldStill_ && cars_[std::size_t(held_)].kind == 2) {
            tap(held_);
        }
        held_ = -1;
        pen_.clear();
        penAtMove_ = {-1, -1};
    }
}

void Slide2Puzzle::update() {
    // 0x1b58cc event 0x15: the drum turns while a turn is left.
    if (drumBusy_) {
        const float by = std::clamp(drumLeft_, -kDrumSpeed, kDrumSpeed);
        turnDrum(by);
        drumLeft_ -= by;
        if (std::fabs(drumLeft_) < 1e-6f) {
            drumLeft_ = 0;
            drumBusy_ = false;
            motionDue_ = true; // event 0x5a
        }
    }
    followChains();
    if (motionDue_) {
        motionDue_ = false;
        if (xq::Thread* t = game_.loopThreads[0]) {
            t->callback("ModelMotionUpdate");
        }
    }
    // 0x3066dc: after a move the script's judge block runs.
    if (judgeDue_) {
        judgeDue_ = false;
        if (xq::Thread* t = game_.loopThreads[0]) {
            t->callback("NazoJdg");
        }
    }
}

std::string Slide2Puzzle::describe() const {
    std::string s = "slide2 " + std::to_string(across_) + "x" + std::to_string(down_) +
                    (turned_ ? " turned" : "") + " judge " + std::to_string(judge() ? 1 : 0) + "\n";
    for (int y = 0; y < down_; ++y) {
        for (int x = 0; x < across_; ++x) {
            const int i = carAt(x, y);
            char cell[8];
            if (i >= 0) {
                std::snprintf(cell, sizeof cell, " %c%d ",
                              cars_[std::size_t(i)].kind == 1 ? 'E' : 'W', i);
            } else {
                std::snprintf(cell, sizeof cell, " %3d", mask(x, y));
            }
            s += cell;
        }
        s += "\n";
    }
    // The upper screen's drum: the rail map's meshes (research).
    if (const auto* map = game_.scene.object3D(xq::hash("MAP_OBJ_000")); map && map->model) {
        s += "map meshes:";
        for (const auto& mesh : map->model->meshes) {
            s += " " + mesh.name + (map->hiddenMeshes.count(xq::hash(mesh.name)) ? "-" : "+");
        }
        s += "\n";
    }
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        const Car& c = cars_[i];
        char line[160];
        std::snprintf(line, sizeof line,
                      "car %zu (%d,%d) front %d back %d %s heading %g,%g open %d\n", i, c.x, c.y,
                      c.front, c.back, c.coupled ? "coupled" : "loose", c.heading[0], c.heading[1],
                      openSides(c));
        s += line;
    }
    return s;
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeSlide2Puzzle(Game& game) {
    return std::make_unique<Slide2Puzzle>(game);
}

} // namespace picarat::adventure5
