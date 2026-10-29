// The sixth adventure's guard robot puzzles (loop 3320, class "GuardRobot";
// catalogue type 25: 128, 130, "reach the exit safely"): the player walks a
// grid of floor, walls and robots with the arrow pad. Each robot looks
// one way along its row or column up to the next wall or robot; stepping
// into a robot's view fails, stepping onto a robot breaks it.
//
// Reconstructed from the class's command method 0x335f20 (vtable 0x3ca004,
// the rest to the arrow pad router 0x22fd90; input and update are the base
// loop's, the update redrawing the pad), the move 0x337004, the robots'
// view 0x336350 and the shared grid helpers 0x213b18 (a position's cell),
// 0x213998 (the robots beside a cell) and 0x2138a0 (Nazo_SetAroundStep).
// The class works out the step and leaves the playing of it to the
// puzzle's script blocks: Idle_GuardRobot (no step), Move_GuardRobot and
// AtkMove_GuardRobot (onto a robot), which walk the player, break robots,
// show the failure and call the judge on the exit.
//
// Cells (command 1): 0 floor, 16 the start, 32 the exit, 64 a wall; a
// robot is its facing - 1 up, 2 down, 4 left, 8 right. Ways (command 5)
// use the same numbers.
#include "puzzles/a5_special.hpp"
#include "puzzles/a6_arrow_pad.hpp"
#include <array>
#include <cmath>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

class GuardPuzzle final : public SpecialPuzzle {
public:
    explicit GuardPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;

private:
    enum : std::uint32_t {
        kRobot = 0x0f,
        kWall = 0x40,
        kBlocks = 0x4f,
        kStart = 0x10,
        kExit = 0x20
    };

    struct Point {
        int x = 0, y = 0;
        bool operator==(const Point&) const = default;
    };

    std::uint32_t* cell(Point p) {
        return p.x >= 0 && p.x < across_ && p.y >= 0 && p.y < down_
                   ? &grid_[std::size_t(p.y * across_ + p.x)]
                   : nullptr;
    }

    std::uint32_t at(Point p) {
        const std::uint32_t* v = cell(p);
        return v ? *v : 0;
    }

    // 0x213b18: the cell under a position (x, z), kept on the board.
    Point cellOf(float x, float z) const {
        auto axis = [this](float v, int origin, int count) {
            int i = (int(v) - origin + size_ / 2) / size_;
            return std::clamp(i, 0, count - 1);
        };
        return {axis(x, originX_, across_), axis(z, originY_, down_)};
    }

    // 0x213998: the robots right beside a cell (1 above, 2 below, 4 left,
    // 8 right; the bottom row and the right column are not looked at from
    // their own neighbours, as the console's bounds are).
    std::uint32_t robotsBeside(Point p) {
        std::uint32_t m = 0;
        if (p.x - 1 >= 0 && (at({p.x - 1, p.y}) & kRobot)) {
            m |= 4;
        }
        if (p.x + 1 < across_ - 1 && (at({p.x + 1, p.y}) & kRobot)) {
            m |= 8;
        }
        if (p.y - 1 >= 0 && (at({p.x, p.y - 1}) & kRobot)) {
            m |= 1;
        }
        if (p.y + 1 < down_ - 1 && (at({p.x, p.y + 1}) & kRobot)) {
            m |= 2;
        }
        return m;
    }

    std::vector<Point> watchers(Point p, int way);
    void move(int way);

    void call(const char* block, std::span<const Value> args) {
        if (xq::Thread* t = game_.loopThreads[0]) {
            t->callback(block, args);
        }
    }

    Object* player() { return game_.scene.object3D(player_); }

    void playValueMotion(Object& o, std::size_t value);
    void aimBeams();

    Game& game_;
    ArrowPad arrows_;
    int originX_ = 0, originY_ = 0;     // +0x1bc
    int across_ = 0, down_ = 0;         // +0x1c0
    int size_ = 1;                      // +0x1c4
    std::vector<std::uint32_t> grid_;   // +0x1b0
    std::uint32_t robotCount_ = 0;      // +0x1dc
    std::vector<std::uint32_t> robots_; // +0x1e0: the robot objects
    std::uint32_t player_ = 0;          // +0x1c8
    std::array<float, 3> walk_{};       // +0x1cc (command 4's three values; kept)
};

// 0x336350: the robots that see the cell, looking each way from it up to
// the first wall or robot (the order starts with the way walked and goes
// round clockwise; hypothesis for left and right, the order only lists
// them for the failure display).
std::vector<GuardPuzzle::Point> GuardPuzzle::watchers(Point p, int way) {
    struct Look {
        int dx, dy;
        std::uint32_t facing; // the robot that faces back
    };

    static constexpr Look kUp{0, -1, 2}, kRight{1, 0, 4}, kDown{0, 1, 1}, kLeft{-1, 0, 8};
    std::array<Look, 4> order{kUp, kRight, kDown, kLeft};
    if (way == 2) {
        order = {kDown, kLeft, kUp, kRight};
    } else if (way == 4) {
        order = {kLeft, kUp, kRight, kDown};
    } else if (way == 8) {
        order = {kRight, kDown, kLeft, kUp};
    }
    std::vector<Point> out;
    for (const Look& l : order) {
        for (Point q{p.x + l.dx, p.y + l.dy}; cell(q); q = {q.x + l.dx, q.y + l.dy}) {
            const std::uint32_t v = at(q);
            if ((v & 0xff) == l.facing) {
                out.push_back(q);
                break;
            }
            if (v & kBlocks) {
                break;
            }
        }
    }
    return out;
}

// The motion an object keeps the hash of in its value (0x221158 +
// 0x1c99bc: the wall bump plays the player's value 4, its standing).
void GuardPuzzle::playValueMotion(Object& o, std::size_t value) {
    if (value >= o.values.size() || !o.model) {
        return;
    }
    const std::uint32_t h = o.values[value].toUint();
    for (const auto& m : o.model->motions) {
        if (m.hash == h || xq::hash(m.name) == h) {
            o.motion = m.name;
            o.motionTime = 0;
            o.motionStops = false;
            o.motionOnce = false;
            o.motionDone = false;
        }
    }
}

// 0x337004: a step from the player's cell the way given.
void GuardPuzzle::move(int way) {
    Object* o = player();
    if (!o) {
        return;
    }
    const float px = o->position[0], pz = o->position[2];
    const Point from = cellOf(px, pz);
    float tx = px, tz = pz;
    float angle = 0;
    const float step = float(size_);
    if (way == 1) {
        angle = 3.14159274f;
        if (from.y > 0) {
            tz -= step;
        }
    } else if (way == 2) {
        if (from.y + 1 < down_ - 1) {
            tz += step;
        }
    } else if (way == 4) {
        angle = 4.71238899f;
        if (from.x > 0) {
            tx -= step;
        }
    } else if (way == 8) {
        angle = 1.57079637f;
        if (from.x + 1 < across_) {
            tx += step;
        }
    }
    const Point to = cellOf(tx, tz);
    const std::uint32_t beside = robotsBeside(to);
    o->rotation3.set({0, angle, 0});
    if (tx == px && tz == pz) {
        const Value args[] = {Value::ofUint(beside)};
        call("Idle_GuardRobot", args);
        return;
    }
    const std::uint32_t v = at(to);
    if ((v & 0xff) == kWall) {
        playValueMotion(*o, 4);
        return;
    }
    const std::vector<Point> seen = watchers(to, way);
    std::array<int, 8> spots;
    spots.fill(-1);
    for (std::size_t i = 0; i < seen.size() && i < 4; ++i) {
        spots[2 * i] = seen[i].x;
        spots[2 * i + 1] = seen[i].y;
    }
    auto aroundStep = [&] {
        // 0x2138a0: Nazo_SetAroundStep (the new cell's and the old cell's
        // robots beside them).
        const Value args[] = {
            Value::ofInt(from.x), Value::ofInt(from.y), Value::ofUint(robotsBeside(from)),
            Value::ofInt(to.x),   Value::ofInt(to.y),   Value::ofUint(beside)};
        call("Nazo_SetAroundStep", args);
    };
    std::vector<Value> args;
    if (v & kRobot) {
        if (seen.empty()) {
            aroundStep();
        }
        args = {Value::ofInt(to.x), Value::ofInt(to.y), Value::ofInt(way),
                Value::ofUint(beside & 0xf), Value::ofInt(int(seen.size()))};
        for (int s : spots) {
            args.push_back(Value::ofInt(s));
        }
        call("AtkMove_GuardRobot", args);
    } else if ((v & 0xff) == 0 || (v & 0xff) == kStart || (v & 0xff) == kExit) {
        if (seen.empty()) {
            aroundStep();
        }
        args = {Value::ofFloat(tx), Value::ofFloat(tz), Value::ofInt(int(seen.size()))};
        for (int s : spots) {
            args.push_back(Value::ofInt(s));
        }
        call("Move_GuardRobot", args);
    }
}

// Command 7 (0x336af8): each robot's beam (its value 9) the cells it sees
// up to the next wall or robot; 0 once its cell holds no robot.
void GuardPuzzle::aimBeams() {
    for (std::uint32_t h : robots_) {
        Object* r = game_.scene.object3D(h);
        if (!r || r->values.size() < 10) {
            continue;
        }
        const Point p{r->values[6].toInt(), r->values[7].toInt()};
        int length = 0;
        if (at(p) & kBlocks) {
            const int facing = r->values[5].toInt();
            const int dx = facing == 4 ? -1 : facing == 8 ? 1 : 0;
            const int dy = facing == 1 ? -1 : facing == 2 ? 1 : 0;
            for (Point q{p.x + dx, p.y + dy}; cell(q) && !(at(q) & kBlocks);
                 q = {q.x + dx, q.y + dy}) {
                ++length;
            }
        }
        r->values[9] = Value::ofInt(length);
    }
}

bool GuardPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return int(std::int16_t(c.integer(first + i)));
    };
    switch (number) {
    case 0: // 0x336d90: origin x, z, across, down, cell size, robots
        if (count < 6) {
            return false;
        }
        originX_ = integer(0);
        originY_ = integer(1);
        across_ = std::max(0, integer(2));
        down_ = std::max(0, integer(3));
        size_ = std::max(1, c.integer(first + 4));
        robotCount_ = c.unsignedInt(first + 5);
        grid_.assign(std::size_t(across_ * down_), 0);
        robots_.clear();
        return true;
    case 1: { // a row (row, cell...)
        if (int(count) != across_ + 1) {
            return false;
        }
        const int row = integer(0);
        for (int x = 0; x < across_; ++x) {
            if (std::uint32_t* v = cell({x, row})) {
                *v = c.unsignedInt(first + 1 + std::size_t(x));
            }
        }
        return true;
    }
    case 2: // a cell (x, y, value)
        if (count < 3) {
            return false;
        }
        if (std::uint32_t* v = cell({integer(0), integer(1)})) {
            *v = c.unsignedInt(first + 2);
        }
        return true;
    case 3: // a cell's value (x, y) as the result
        if (count < 2) {
            return false;
        }
        c.result = Value::ofUint(at({integer(0), integer(1)}));
        return true;
    case 4: // the player object and three values
        if (count < 4) {
            return false;
        }
        player_ = c.at(first).isText() ? xq::hash(c.at(first).text) : c.unsignedInt(first);
        for (std::size_t i = 0; i < 3; ++i) {
            walk_[i] = c.real(first + 1 + i);
        }
        return true;
    case 5: // a step the way given (not while the player walks)
        if (count == 0) {
            return false;
        }
        if (Object* o = player(); o && !o->position.moving()) {
            move(c.integer(first));
        }
        return true;
    case 6: { // the player stands: Idle_GuardRobot with the robots beside it
        Object* o = player();
        if (!o) {
            return true;
        }
        const Value args[] = {Value::ofUint(robotsBeside(cellOf(o->position[0], o->position[2])))};
        call("Idle_GuardRobot", args);
        return true;
    }
    case 7: // [name]: the robots "name_NN" found; their beams
        if (count == 1) {
            const std::string name = c.at(first).isText() ? c.at(first).text : std::string();
            robots_.clear();
            for (std::uint32_t i = 0; i < robotCount_; ++i) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "%s_%02u", name.c_str(), i);
                robots_.push_back(xq::hash(buf));
            }
        }
        aimBeams();
        return true;
    case 8: { // Nazo_SetAroundStep for (x0, y0) -> (x1, y1)
        if (count < 4) {
            return false;
        }
        const Point a{integer(0), integer(1)}, b{integer(2), integer(3)};
        const Value args[] = {Value::ofInt(b.x), Value::ofInt(b.y), Value::ofUint(robotsBeside(b)),
                              Value::ofInt(a.x), Value::ofInt(a.y), Value::ofUint(robotsBeside(a))};
        call("Nazo_SetAroundStep", args);
        return true;
    }
    default:
        return arrows_.command(game_, number, c, first);
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeGuardPuzzle(Game& game) {
    return std::make_unique<GuardPuzzle>(game);
}

} // namespace picarat::adventure5
