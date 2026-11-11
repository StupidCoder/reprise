// The jump-and-vanish loop (overlay 26). See a4_vanish.hpp.
#include "puzzles/a4_vanish.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include <algorithm>
#include <cstdio>

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 26's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay26 = 0x02106260;
constexpr std::uint32_t kVerdictName = 0x021084e4; // the verdict variable
constexpr std::uint32_t kBallFormat = 0x021084f0;  // "vnsh_%02x"
constexpr int kBallKind = 0x19;
constexpr int kBallPriority = 5;
// The directions (table 0207b828): right, down, left, up.
constexpr std::array<std::array<int, 2>, 4> kDirections{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
// Where a vanished ball's object goes (off the screens).
constexpr int kAway = 256;

// 021074fc: a 4.12 fraction clamped to 0..1, as 0..31 (0x1ffff >> 24).
unsigned fifths(std::int32_t v) {
    return unsigned((std::int64_t(std::clamp(v, 0, kOne)) * 0x1ffff) >> 24) & 0x1f;
}

std::uint16_t colour(std::int32_t r, std::int32_t g, std::int32_t b) {
    return std::uint16_t(fifths(r) | fifths(g) << 5 | fifths(b) << 10 | 0x8000);
}

// 021070f8: the held ball's cell bluish, its landing cells pink, both
// half transparent.
const std::uint16_t kHeldColour = colour(0, 0x666, 0x1000);
const std::uint16_t kLandingColour = colour(0x1000, 0x666, 0xc00);
const unsigned kTintAlpha = fifths(0x800);

#define OP(i) (i + 1)
} // namespace

VanishLoop::VanishLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {}

VanishLoop::~VanishLoop() {
    scene_.loopSprites.clear();
}

std::string VanishLoop::text(std::uint32_t address) const {
    return scene_.assets.codeText(26, kOverlay26, address);
}

bool VanishLoop::command(gss::Call& c, int id) {
    // 021075a0.
    switch (id) {
    case 0:
        reset();
        c.result = Value::ofInt(1);
        return true;
    case 1:
        c.result = Value::ofInt(takeBack() ? 1 : 0);
        return true;
    case 10:
        return setup(c);
    case 11:
        return addHoles(c);
    case 20:
        return judge(c);
    case 21:
        // {x, y}: the cell the last ball must stand on.
        if (c.count() - 1 < 2) {
            return false;
        }
        goal_ = {s16(c.integer(OP(0))), s16(c.integer(OP(1)))};
        c.result = Value::ofInt(1);
        return true;
    case 30:
        c.result = Value::ofInt(onBoard());
        return true;
    case 31:
        c.result = Value::ofInt(canJump());
        return true;
    case 40:
        return addBall(c);
    case 41:
        if (c.count() - 1 == 0 || !pickUp(kit::text(c, OP(0)))) {
            return false;
        }
        c.result = Value::ofInt(1);
        return true;
    case 42: {
        // {ball, x out, y out}: where the ball goes (the landing cell or its
        // own), or -1.
        if (c.count() - 1 < 3) {
            return false;
        }
        const int at = drop(kit::text(c, OP(0)));
        const Cell* to = at >= 0 ? &cells_[std::size_t(at)] : nullptr;
        if (Value* x = c.reg(OP(1))) {
            *x = Value::ofInt(to ? to->px : -1);
        }
        if (Value* y = c.reg(OP(2))) {
            *y = Value::ofInt(to ? to->py : -1);
        }
        c.result = Value::ofInt(to ? 1 : 0);
        return true;
    }
    default:
        return Loop::command(c, id);
    }
}

int VanishLoop::cellAt(int x, int y) const {
    if (unsigned(x) < unsigned(across_) && unsigned(y) < unsigned(down_)) {
        return across_ * y + x;
    }
    return -1;
}

VanishLoop::Cell* VanishLoop::cell(int index) {
    return index >= 0 && std::size_t(index) < cells_.size() ? &cells_[std::size_t(index)] : nullptr;
}

bool VanishLoop::landing(const Cell& from, int d) const {
    // A ball next to it in this direction and a free cell behind that.
    const auto i = std::size_t(d);
    if (from.next[i] < 0 || from.far[i] < 0) {
        return false;
    }
    const Cell& next = cells_[std::size_t(from.next[i])];
    const Cell& far = cells_[std::size_t(from.far[i])];
    return !next.hole && next.ball >= 0 && !far.hole && far.ball < 0;
}

void VanishLoop::moveBall(const Ball& ball, int px, int py) {
    if (Chara* o = scene_.find(ball.name)) {
        o->position.set({px * kOne, py * kOne});
    }
}

bool VanishLoop::setup(gss::Call& c) {
    // {across, down, x, y, cell size, tint animation, tint sequence}.
    if (c.count() - 1 < 7) {
        return false;
    }
    animation_ = kit::text(c, OP(5));
    look_ = kit::text(c, OP(6));
    across_ = s16(c.integer(OP(0)));
    down_ = s16(c.integer(OP(1)));
    originX_ = s16(c.integer(OP(2)));
    originY_ = s16(c.integer(OP(3)));
    size_ = s16(c.integer(OP(4)));
    balls_.clear();
    jumps_.clear();
    cells_.assign(std::size_t(std::max(0, across_ * down_)), {});
    for (int y = 0; y < down_; ++y) {
        for (int x = 0; x < across_; ++x) {
            Cell& cl = cells_[std::size_t(cellAt(x, y))];
            cl.px = s16(x * size_ + originX_);
            cl.py = s16(y * size_ + originY_);
            for (std::size_t d = 0; d < 4; ++d) {
                const auto [dx, dy] = kDirections[d];
                cl.next[d] = cellAt(x + dx, y + dy);
                cl.far[d] = cellAt(x + 2 * dx, y + 2 * dy);
            }
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

bool VanishLoop::addHoles(gss::Call& c) {
    // {x, y, across, down}: cells that are not part of the board.
    if (c.count() - 1 < 4) {
        return false;
    }
    const int x0 = s16(c.integer(OP(0))), y0 = s16(c.integer(OP(1)));
    const int w = s16(c.integer(OP(2))), h = s16(c.integer(OP(3)));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (Cell* cl = cell(cellAt(x0 + x, y0 + y))) {
                cl->hole = true;
            }
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

bool VanishLoop::addBall(gss::Call& c) {
    // {x, y, animation, sequence, normal block, drag block}: a ball object
    // "vnsh_NN" on the cell; the result is its name.
    if (c.count() - 1 < 6) {
        return false;
    }
    const int x = s16(c.integer(OP(0))), y = s16(c.integer(OP(1)));
    Cell* cl = cell(cellAt(x, y));
    if (!cl) {
        return false;
    }
    char name[32];
    std::snprintf(name, sizeof name, text(kBallFormat).c_str(), unsigned(balls_.size()));
    const std::array<Value, 2> values{Value::ofText(name), Value::ofInt(kBallKind)};
    Chara* o = scene_.create(name, kBallKind, values);
    if (!o) {
        return false;
    }
    balls_.push_back({nameHash(name), x, y, x, y});
    o->position.set({s16(x * size_ + originX_) * kOne, s16(y * size_ + originY_) * kOne});
    // 020db7f0: a slide button with the normal and the dragging block.
    o->touchKind = 3;
    o->callbacks = {};
    o->callbacks[0] = kit::text(c, OP(4));
    o->callbacks[1] = kit::text(c, OP(5));
    o->enterState(0);
    attach(scene_, *o, kit::text(c, OP(2)), kBallPriority);
    play(*o, kit::text(c, OP(3)));
    cl->ball = int(balls_.size()) - 1;
    c.result = Value::ofText(name);
    return true;
}

void VanishLoop::reset() {
    // 021065ec: every ball back on its first cell; nothing to take back.
    held_ = heldCell_ = -1;
    jumps_.clear();
    for (Cell& cl : cells_) {
        cl.ball = -1;
    }
    for (std::size_t i = 0; i < balls_.size(); ++i) {
        Ball& b = balls_[i];
        b.x = b.startX;
        b.y = b.startY;
        Cell* cl = cell(cellAt(b.x, b.y));
        if (cl && !cl->hole) {
            cl->ball = int(i);
            moveBall(b, cl->px, cl->py);
        }
    }
}

bool VanishLoop::takeBack() {
    // 0210677c: the last jump undone, both balls back on their cells. (The
    // original turns cell numbers back into x and y with the row count; the
    // boards are square.)
    if (jumps_.empty()) {
        return false;
    }
    const Jump j = jumps_.back();
    jumps_.pop_back();
    Ball& b = balls_[std::size_t(j.ball)];
    if (Cell* now = cell(cellAt(b.x, b.y))) {
        now->ball = -1;
    }
    if (Cell* from = cell(cellAt(j.fromX, j.fromY))) {
        b.x = j.fromX;
        b.y = j.fromY;
        from->ball = j.ball;
        moveBall(b, from->px, from->py);
    }
    Ball& v = balls_[std::size_t(j.jumped)];
    if (Cell* at = cell(cellAt(j.jumpedX, j.jumpedY))) {
        at->ball = j.jumped;
        v.x = j.jumpedX;
        v.y = j.jumpedY;
        moveBall(v, at->px, at->py);
    }
    return true;
}

bool VanishLoop::pickUp(const std::string& name) {
    // 02106af0: only one ball at a time, and only one that can jump.
    if (held_ >= 0 || heldCell_ >= 0) {
        return false;
    }
    const std::uint32_t h = nameHash(name);
    for (std::size_t i = 0; i < balls_.size(); ++i) {
        const Ball& b = balls_[i];
        if (b.name != h) {
            continue;
        }
        const int at = cellAt(b.x, b.y);
        const Cell* cl = cell(at);
        if (!cl) {
            return false;
        }
        for (int d = 0; d < 4; ++d) {
            if (landing(*cl, d)) {
                held_ = int(i);
                heldCell_ = at;
                return true;
            }
        }
        return false;
    }
    return false;
}

int VanishLoop::drop(const std::string& name) {
    // 02106c34: the landing cell nearest to where the ball was let go, if
    // nearer than its own cell; the jumped ball vanishes. Returns the cell
    // the ball goes to, or -1.
    if (held_ < 0 || std::size_t(held_) >= balls_.size()) {
        return -1;
    }
    Ball& b = balls_[std::size_t(held_)];
    if (b.name != nameHash(name)) {
        return -1;
    }
    Cell* from = cell(heldCell_);
    if (!from || from->hole) {
        return -1;
    }
    const Chara* o = scene_.find(b.name);
    if (!o) {
        return -1;
    }
    const auto p = o->position.value();
    const int x = (p[0] + 0x800) >> 12, y = (p[1] + 0x800) >> 12;
    auto distance = [&](const Cell& to) {
        const int dx = s16(x - to.px), dy = s16(y - to.py);
        return dx * dx + dy * dy;
    };
    int best = distance(*from), choice = -1;
    for (int d = 0; d < 4; ++d) {
        if (landing(*from, d)) {
            const int n = distance(cells_[std::size_t(from->far[std::size_t(d)])]);
            if (n < best) {
                best = n;
                choice = d;
            }
        }
    }
    from->ball = -1;
    int to = heldCell_;
    if (choice >= 0) {
        const auto d = std::size_t(choice);
        Cell& over = cells_[std::size_t(from->next[d])];
        Ball& v = balls_[std::size_t(over.ball)];
        const Jump j{held_,
                     over.ball,
                     heldCell_ % across_,
                     heldCell_ / across_,
                     from->next[d] % across_,
                     from->next[d] / across_};
        v.x = v.y = -1;
        moveBall(v, kAway, kAway);
        jumps_.push_back(j);
        over.ball = -1;
        to = from->far[d];
    }
    b.x = to % across_;
    b.y = to / across_;
    cells_[std::size_t(to)].ball = held_;
    held_ = heldCell_ = -1;
    return to;
}

int VanishLoop::onBoard() const {
    int n = 0;
    for (const Ball& b : balls_) {
        if (cellAt(b.x, b.y) >= 0) {
            ++n;
        }
    }
    return n;
}

int VanishLoop::canJump() const {
    int n = 0;
    for (const Ball& b : balls_) {
        const int at = cellAt(b.x, b.y);
        if (at < 0) {
            continue;
        }
        for (int d = 0; d < 4; ++d) {
            if (landing(cells_[std::size_t(at)], d)) {
                ++n;
                break;
            }
        }
    }
    return n;
}

bool VanishLoop::judge(gss::Call& c) {
    // One ball left, on the goal when there is one.
    bool solved = false;
    if (onBoard() < 2) {
        if (goal_[0] < 0 || goal_[1] < 0) {
            solved = true;
        } else {
            for (const Ball& b : balls_) {
                if (cellAt(b.x, b.y) >= 0) {
                    solved = b.x == goal_[0] && b.y == goal_[1];
                    break;
                }
            }
        }
    }
    if (solved) {
        services_.data.variables[text(kVerdictName) + "#0"] = 1;
    }
    c.result = Value::ofInt(solved ? 1 : 0);
    return true;
}

void VanishLoop::endFrame() {
    // 021070f8 (drawn this frame): the held ball's cell and its landings.
    scene_.loopSprites.clear();
    const Cell* from = held_ >= 0 ? cell(heldCell_) : nullptr;
    const AnimationResource* a = from ? scene_.animation(animation_) : nullptr;
    if (!a) {
        return;
    }
    const Animation::Sequence* look = a->animation->sequence(look_);
    auto tint = [&](const Cell& at, std::uint16_t colour) {
        scene_.loopSprites.push_back({a, look, {at.px * kOne, at.py * kOne}, colour, kTintAlpha});
    };
    tint(*from, kHeldColour);
    for (int d = 0; d < 4; ++d) {
        if (landing(*from, d)) {
            tint(cells_[std::size_t(from->far[std::size_t(d)])], kLandingColour);
        }
    }
}

} // namespace picarat::adventure4
