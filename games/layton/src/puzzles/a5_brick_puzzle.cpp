// The fifth adventure's totem brick puzzles (catalogue type 34, loop 3160;
// puzzles 51, 90, 124, 136): bricks stand in a grid; a hammer knocks a brick
// sideways - pushing the row in front of it up to the next gap - and bricks
// with nothing under them fall. The puzzle is solved when the named bricks
// stand on their goal cells.
//
// Reconstructed from the loop's command handler 0x211124 (vtable 0x401d98,
// constructor 0x385bf0; the input hook is the base's), its update 0x2109fc
// and helpers: setup 0x211e9c, reset 0x211d68, placing 0x2126bc, aiming
// 0x212970, the hit 0x212a84, the hit effect and the slide 0x212f34 /
// 0x2116f0, falling 0x2119dc, the arrows 0x286b24 / 0x2131b4 / 0x2124e8,
// the arrow under the hammer 0x210f7c, cells 0x2869c8 / 0x28698c and the
// arrows' parallax 0x28677c.
//
// How it plays: the grid (setup: first cell, cells across and down, cell
// size; a brick's size, wider bricks taking several cells) holds the bricks
// the scripts place (3) - each brick object stays at its first cell and
// moves by offset tweens. An arrow (4) appears beside every brick that can
// be knocked: one with something on top of it, its hammer side free and a
// gap somewhere ahead in the row. The pad drags the hammer (10 each frame:
// the aimed cell); a release (11) on the same cell as the release before
// hits: the hammer turns to the arrow's side and moves onto it; the script's
// hit motion then calls 13, which slides the bricks ahead by one cell (each
// brick's "Brick_Slide" block follows its tween) and shows the hit effect.
// When a slide ends, everything without support falls ("Brick_Fall", 10 /
// 6 frames a row); when nothing falls the goal list (2) is checked - all
// in place gives the verdict after the setup's delay. With no arrow left, or
// the moves (setup operand 11) used up, the error message shows and the
// script's "WaitTouchReTry" runs. A kind-5 object near the hammer (puzzle
// 124's "touch_pnt") is hit instead: state 4, the verdict.
//
// The states (+0x1e0 now, +0x1e1 next): 1 ready, 2 bricks moving, 4 goal, 5
// waiting for the timer (+0x1e4; negative: until a script sets the next
// state), -1 unset. The loop's input byte +0x1a (2109's) is set while a
// state waits and while bricks move.
//
// The draw hook 0x2123a8 draws the arrows with the first one's pop-up as an
// instance (the same picture); the port draws them normally.
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using adventure56::Object3D;
using adventure56::wrapAngle;
using xq::Call;
using xq::Value;

struct Cell2 {
    int x = 0, y = 0;
    bool operator==(const Cell2&) const = default;
};

struct V2 {
    float x = 0, y = 0;
};

constexpr float kPi = 3.14159274f;    // 0x40490fdb
constexpr float kDegree = 0.0174533f; // 0x3c8efa35

class BrickPuzzle final : public SpecialPuzzle {
public:
    explicit BrickPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void update() override;

private:
    struct Goal {
        std::uint32_t brick = 0;
        int x = -1, y = -1;
    };

    bool inGrid(int x, int y) const { return x >= 0 && x < cols_ && y >= 0 && y < rows_; }

    std::uint32_t& at(int x, int y) { return grid_[std::size_t(cols_ * y + x)]; }

    // A brick's width in cells (+0x1a4 / +0x170, compared as floats).
    float span() const { return brickW_ / cellW_; }

    // 0x28698c: a cell's place.
    V2 cellPlace(float x, float y) const {
        float px = x * cellW_;
        px = originX_ + px;
        float py = y * cellH_;
        py = originY_ - py;
        return {px, py};
    }

    // 0x2869c8: the cell under a point - truncated, or (`rounded`) to the
    // nearest cell centre.
    Cell2 cellAt(V2 p, bool rounded) const {
        if (!rounded) {
            return {std::int16_t(int((p.x - originX_) / cellW_)),
                    std::int16_t(int((originY_ - p.y) / cellH_))};
        }
        const float sx = p.x < originX_ ? -1.0f : 1.0f;
        const float sy = p.y <= originY_ ? 1.0f : -1.0f;
        float ax = cellW_ * sx;
        ax = ax * 0.5f;
        float ay = cellH_ * sy;
        ay = ay * 0.5f;
        return {std::int16_t(int(((p.x - originX_) + ax) / cellW_)),
                std::int16_t(int(((originY_ - p.y) + ay) / cellH_))};
    }

    // Where a brick at a cell stands: the cell plus half the size beyond one
    // cell.
    V2 brickPlace(int x, int y) const {
        V2 p = cellPlace(float(x), float(y));
        float dx = brickW_ - cellW_, dy = brickH_ - cellH_;
        dx = dx * 0.5f;
        dy = dy * 0.5f;
        return {p.x + dx, p.y + dy};
    }

    Object3D* model(std::uint32_t h) { return h ? game_.scene.object3D(h) : nullptr; }

    static V2 placed(const Object3D& o) {
        return {o.position[0] + o.offset[0], o.position[1] + o.offset[1]};
    }

    std::uint32_t playingHash(const Object& o) const {
        if (!o.animation || o.playing < 0 || o.playing >= int(o.animation->animations.size())) {
            return 0;
        }
        return o.animation->animations[std::size_t(o.playing)].hash;
    }

    static bool unturned(const Object3D& o) {
        return o.rotation3[0] == 0.0f && o.rotation3[1] == 0.0f && o.rotation3[2] == 0.0f;
    }

    V2 parallax(V2 p, float a, float b);
    bool canKnock(Cell2 c, int dir);
    bool arrow(int index, int x, int y, int dir, bool hide);
    bool arrows(bool force);
    std::uint32_t arrowUnderHammer(V2 hammer);
    void slide(Cell2 from, int dir);
    int fall(Cell2 c, int most);

    void setLock(bool on) { game_.objectTouchLocked = on; }

    bool setup(Call& c, std::size_t first);
    bool place(Call& c, std::size_t first);
    bool aim();
    bool hit(Call& c, std::size_t first);
    bool effect();

    Game& game_;
    Cell2 aimed_;                                 // +0x160
    float originX_ = 0, originY_ = 0;             // +0x164
    int cols_ = 0, rows_ = 0;                     // +0x16c
    float cellW_ = 0, cellH_ = 0;                 // +0x170
    std::vector<std::uint32_t> grid_, start_;     // +0x178 / +0x17c (1: a wide brick's rest)
    std::vector<std::uint32_t> arrows_;           // +0x180
    bool refresh_ = false;                        // +0x184
    std::uint32_t arrowSmall_ = 0, arrowBig_ = 0; // +0x188 / +0x18c
    std::uint32_t hammer_ = 0;                    // +0x190
    int slideDir_ = 1;                            // +0x194
    Cell2 slideCell_;                             // +0x196
    bool primed_ = false;                         // +0x19c
    std::uint32_t moving_ = 0;                    // +0x1a0
    float brickW_ = 0, brickH_ = 0;               // +0x1a4
    int arrowSound_ = 0, hitSound_ = 0;           // +0x1ac / +0x1b0
    std::vector<Goal> goals_;                     // +0x1b4
    std::size_t goalsSet_ = 0;                    // +0x1c0
    std::uint32_t message_ = 0, effect_ = 0;      // +0x1c4 / +0x1c8
    int moveLimit_ = 0, moves_ = 0;               // +0x1cc / +0x1d0
    float goalDelay_ = 60;                        // +0x1d4
    int state_ = -1, next_ = -1;                  // +0x1e0 / +0x1e1
    float timer_ = 0;                             // +0x1e4
};

// 0x28677c: an arrow's or the effect's offset, against the parallax of the
// screen's camera (its eye's z) - a * b - 0.04 b eye.z - 0.14 x, -2.1 -
// 0.17 y.
V2 BrickPuzzle::parallax(V2 p, float a, float b) {
    const auto& cam = game_.scene.cameras[0];
    const float eyeZ = cam.eye[2] + cam.eyeOffset[2];
    float rx = 0.0f;
    float k = b * -0.04f;
    k = eyeZ * k;
    rx = rx + k;
    float s = p.x * 0.07f;
    s = s + p.x * 0.07f;
    rx = rx - s;
    rx = a * b + rx;
    float t = p.y * 0.085f;
    t = t + p.y * 0.085f;
    const float ry = -2.1f - t;
    return {rx, ry + 0.0f};
}

// 0x2124e8: whether the brick at c can be knocked towards dir.
bool BrickPuzzle::canKnock(Cell2 c, int dir) {
    if (c.y == 0 || !inGrid(c.x, c.y)) {
        return false;
    }
    const float n = span();
    bool loaded = false;
    for (int j = 0; float(j) < n; ++j) {
        for (int i = 0; float(i) < n; ++i) {
            if (!loaded) {
                const int x = c.x + i - j;
                loaded = x >= 0 && x < cols_ && at(x, c.y - 1) != 0;
            }
        }
    }
    if (!loaded) {
        return false;
    }
    const int behind = c.x - dir;
    if (inGrid(behind, c.y) && at(behind, c.y) != 0) {
        return false;
    }
    for (int x = c.x; inGrid(x, c.y); x += dir) {
        if (at(x, c.y) == 0) {
            return true;
        }
    }
    return false;
}

// 0x2131b4: arrow `index` beside the brick at (x, y) for dir (shown and
// placed when the brick can go; big under the hammer), or (`hide`) hidden.
bool BrickPuzzle::arrow(int index, int x, int y, int dir, bool hide) {
    if (index < 0 || index >= int(arrows_.size())) {
        return false;
    }
    Object3D* a = model(arrows_[std::size_t(index)]);
    if (!a) {
        return false;
    }
    if (moving_ == 0 && !hide) {
        if (!canKnock({x, y}, dir)) {
            return false;
        }
        a->visible = true;
        const int ax = x - dir;
        const V2 p = cellPlace(float(ax), float(y));
        a->position.set({p.x, p.y, a->position[2] + a->offset[2]});
        float d = brickW_ - cellW_;
        d = d * 0.5f;
        float sz = 2.0f - d * 0.75f;
        const V2 o = parallax(p, sz, float(dir));
        a->offset.set({o.x, o.y, 0.0f});
        const std::uint32_t before = playingHash(*a);
        game_.playAnimation(*a, Value::ofUint(arrowSmall_), 0);
        a->rotation3.set({0.0f, 0.0f, wrapAngle(dir < 1 ? kPi : 0.0f)});
        Object3D* h = model(hammer_);
        if (!h) {
            return false;
        }
        const V2 hp = placed(*h);
        const Cell2 under = cellAt({hp.x, hp.y + 0.5f}, true);
        Cell2 c{ax, y};
        bool big = false;
        for (int i = 0; float(i) < span(); ++i) {
            if (c == under) {
                game_.playAnimation(*a, Value::ofUint(arrowBig_), 0);
                big = !(c.x == 0 && c.y == 0);
                break;
            }
            c.x += unturned(*a) ? -1 : 1;
        }
        if (big && arrowBig_ != before) {
            game_.playEffect(arrowSound_);
        }
        return true;
    }
    if (a->visible) {
        a->visible = false;
        game_.playAnimation(*a, Value::ofUint(arrowSmall_), 0);
        return true;
    }
    return false;
}

// 0x286b24: the arrows again when asked (or forced: all hidden); whether
// any shows.
bool BrickPuzzle::arrows(bool force) {
    if (!refresh_ && !force) {
        return true;
    }
    int n = 0;
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) {
            if (at(x, y) == 0) {
                continue;
            }
            if (arrow(n, x, y, 1, force)) {
                ++n;
            }
            if (arrow(n, x, y, -1, force)) {
                ++n;
            }
        }
    }
    refresh_ = false;
    return n != 0;
}

// 0x210f7c: the shown arrow covering the hammer's cell (the last such).
std::uint32_t BrickPuzzle::arrowUnderHammer(V2 hammer) {
    const Cell2 under = cellAt({hammer.x, hammer.y + 0.5f}, true);
    int found = -1;
    for (int i = 0; i < int(arrows_.size()); ++i) {
        Object3D* a = model(arrows_[std::size_t(i)]);
        if (!a) {
            continue;
        }
        if (!a->visible) {
            break;
        }
        Cell2 c = cellAt({a->position[0], a->position[1]}, true);
        const int step = unturned(*a) ? -1 : 1;
        for (int k = 0; float(k) < span(); ++k) {
            if (c == under) {
                found = i;
                break;
            }
            c.x += step;
        }
    }
    return found >= 0 ? arrows_[std::size_t(found)] : 0;
}

// 0x2116f0: the bricks from `from` up to the next gap one cell towards dir
// (offset tweens over 20 / width frames, curve 2; their "Brick_Slide").
void BrickPuzzle::slide(Cell2 from, int dir) {
    int u = from.x;
    while (inGrid(u, from.y) && at(u, from.y) != 0) {
        u += dir;
    }
    const int count = std::abs(u - from.x);
    int x = from.x;
    for (int i = 0; i < count; ++i, x += dir) {
        const std::uint32_t b = at(x, from.y);
        Object3D* o = model(b);
        if (!o) {
            continue;
        }
        const V2 target = brickPlace(x + dir, from.y);
        const V2 now = placed(*o);
        const V2 d{target.x - now.x, target.y - now.y};
        const auto off = o->offset.value();
        o->offset.start({off[0] + d.x, off[1] + d.y, 0.0f}, 20.0f / span(), 2);
        game_.startObjectBlock(*o, "Brick_Slide", {});
        moving_ = b;
    }
    for (int i = 0; i < count; ++i) {
        u -= dir;
        std::swap(at(u, from.y), at(u + dir, from.y));
    }
}

// 0x2119dc: the brick at c falls as far as the row below lets it ("Brick_
// Fall": offset, z 0, 10 n - 6 (n - 1) frames, curve 1); the rows it fell.
int BrickPuzzle::fall(Cell2 c, int most) {
    const std::uint32_t b = at(c.x, c.y);
    if (b == 1) {
        return 0;
    }
    const float n = span();
    int down = 0;
    for (;;) {
        const int y = c.y + down + 1;
        if (y == rows_) {
            break;
        }
        int empty = 0;
        while (float(empty) < n && c.x + empty < cols_ && at(c.x + empty, y) == 0) {
            ++empty;
        }
        if (float(empty) != n) {
            break;
        }
        ++down;
    }
    Object3D* o = model(b);
    if (down == 0 || !o) {
        return down;
    }
    const V2 target = brickPlace(c.x, c.y + down);
    const V2 now = placed(*o);
    float frames = float(down) * 10.0f;
    frames = frames - (float(down) - 1.0f) * 6.0f;
    const Value args[] = {Value::ofFloat(target.x - now.x), Value::ofFloat(target.y - now.y),
                          Value::ofFloat(0.0f), Value::ofFloat(frames), Value::ofInt(1)};
    game_.startObjectBlock(*o, "Brick_Fall", args);
    for (int i = 0; float(i) < n; ++i) {
        std::swap(at(c.x + i, c.y), at(c.x + i, c.y + down));
    }
    if (most < down) {
        moving_ = b;
    }
    return down;
}

bool BrickPuzzle::setup(Call& c, std::size_t first) {
    if (c.count() - first < 11) {
        return false;
    }
    auto whole = [&](std::size_t i) {
        return float(std::int16_t(int(c.real(first + i))));
    };
    originX_ = whole(0);
    originY_ = whole(1);
    cols_ = std::int16_t(c.integer(first + 2));
    rows_ = std::int16_t(c.integer(first + 3));
    cellW_ = whole(4);
    cellH_ = whole(5);
    goals_.assign(std::size_t(std::max(0, c.integer(first + 6))), {});
    arrowSound_ = c.integer(first + 7);
    hitSound_ = c.integer(first + 8);
    brickW_ = c.real(first + 9);
    brickH_ = c.real(first + 10);
    if (c.count() - first > 11) {
        moveLimit_ = c.integer(first + 11);
    }
    if (c.count() - first > 12) {
        goalDelay_ = c.real(first + 12);
    }
    const std::size_t cells = std::size_t(std::max(0, cols_ * rows_));
    grid_.assign(cells, 0);
    start_.assign(cells, 0);
    arrows_.assign(cells, 0);
    if (const int id = game_.flagId(names::judgeOff); id >= 0) {
        game_.setFlag(id, true);
    }
    refresh_ = true;
    c.result = Value::ofInt(1);
    return true;
}

// 0x2126bc: a brick (name, x, y, z) on its cells.
bool BrickPuzzle::place(Call& c, std::size_t first) {
    if (c.count() - first < 4) {
        return false;
    }
    const int x = std::int16_t(c.integer(first + 1)), y = std::int16_t(c.integer(first + 2));
    for (int i = 0; float(i) < span(); ++i) {
        if (!inGrid(x + i, y) || at(x + i, y) != 0) {
            return false;
        }
    }
    const std::string* name = c.text(first);
    const std::uint32_t h = name ? xq::hash(*name) : c.unsignedInt(first);
    Object3D* o = model(h);
    if (!o) {
        return false;
    }
    const V2 p = brickPlace(x, y);
    o->position.set({p.x, p.y, c.real(first + 3)});
    at(x, y) = h;
    start_[std::size_t(cols_ * y + x)] = h;
    for (int i = 1; float(i) < span(); ++i) {
        at(x + i, y) = 1;
        start_[std::size_t(cols_ * y + x + i)] = 1;
    }
    c.result = Value::ofInt(1);
    return true;
}

// 0x212970: the cell the hammer aims at (a long press or another cell takes
// back the first release).
bool BrickPuzzle::aim() {
    Object3D* h = model(hammer_);
    if (!h) {
        return false;
    }
    const V2 hp = placed(*h);
    const Cell2 cell = cellAt({hp.x, hp.y + 0.5f}, true);
    if (!(game_.heldFrames < 11 && cell == aimed_)) {
        primed_ = false;
    }
    if (!(cell == aimed_) && !refresh_) {
        aimed_ = cell;
        refresh_ = true;
    }
    return true;
}

// 0x212a84: a release; the register gets 0 (no hit), 1 (a brick knocked)
// or 2 (the kind-5 target hit).
bool BrickPuzzle::hit(Call& c, std::size_t first) {
    if (c.count() - first == 0) {
        return false;
    }
    Value* out = c.reg(first);
    auto answer = [&](int v) {
        if (out) {
            *out = Value::ofInt(v);
        }
    };
    if (state_ != 1) {
        return true;
    }
    Object3D* h = model(hammer_);
    if (!h) {
        return false;
    }
    const V2 hp = placed(*h);
    const Cell2 cell = cellAt({hp.x, hp.y + 0.5f}, true);
    if (!primed_ || !(cell == aimed_)) {
        answer(0);
        primed_ = true;
        refresh_ = true;
        c.result = Value::ofInt(1);
        return true;
    }
    // 0x2c9f90: the nearest kind-5 object of screen 0.
    Object3D* target = nullptr;
    float best = 3.4e38f;
    for (const auto& [hash, o] : game_.scene.objects(true)) {
        if (o->screen != 0 || o->kind != 5) {
            continue;
        }
        const float dx = o->position[0] + o->offset[0] - h->position[0] - h->offset[0];
        const float dy = o->position[1] + o->offset[1] - h->position[1] - h->offset[1];
        const float dz = o->position[2] + o->offset[2] - h->position[2] - h->offset[2];
        const float d = dx * dx + dy * dy + dz * dz;
        if (d <= best) {
            best = d;
            target = o.get();
        }
    }
    if (target) {
        const V2 tp = placed(*target);
        const float dx = tp.x - hp.x, dy = tp.y - hp.y;
        if (dx * dx + dy * dy < 6.0f) {
            h->scale.set({1.0f, 1.0f, 1.0f});
            const auto off = h->offset.value();
            h->offset.start({off[0] - (hp.x - tp.x), off[1] - (hp.y - tp.y), -6.0f}, 15.0f, 1);
            next_ = 4;
            state_ = 5;
            if (goalDelay_ >= 0.0f) {
                setLock(true);
            }
            timer_ = goalDelay_;
            slideDir_ = -1;
            slideCell_ = {0, 0};
            answer(2);
            arrows(true);
            return true;
        }
    }
    Object3D* a = model(arrowUnderHammer(hp));
    if (!a) {
        answer(0);
        return true;
    }
    const Cell2 ac = cellAt({a->position[0], a->position[1]}, true);
    const float dir = unturned(*a) ? 1.0f : -1.0f;
    h->scale.set({-dir, 1.0f, 1.0f});
    const V2 to = cellPlace(float(ac.x), float(ac.y));
    const auto off = h->offset.value();
    h->offset.start({off[0] - (hp.x - to.x), off[1] - (hp.y - to.y), -6.0f}, 15.0f, 1);
    slideDir_ = int(dir);
    slideCell_ = ac;
    next_ = 2;
    state_ = -1;
    setLock(true);
    timer_ = 0.0f;
    arrows(true);
    answer(1);
    if (moveLimit_ > 0) {
        ++moves_;
    }
    refresh_ = true;
    c.result = Value::ofInt(1);
    return true;
}

// 0x212f34: the slide (from the hit motion) and the hit effect.
bool BrickPuzzle::effect() {
    Object3D* h = model(hammer_);
    if (h) {
        V2 p, o;
        if (slideCell_ == Cell2{0, 0}) {
            p = placed(*h);
            o = parallax(p, 0.0f, 1.0f);
        } else {
            slide({slideCell_.x + slideDir_, slideCell_.y}, slideDir_);
            const V2 c = cellPlace(float(slideCell_.x), float(slideCell_.y));
            p = {c.x + 0.0f, c.y + -2.0f};
            float d = brickW_ - cellW_;
            d = d * 0.5f;
            o = parallax(p, 3.0f - d * 0.75f, float(slideDir_));
        }
        if (Object3D* e = model(effect_)) {
            e->position.set({p.x, p.y, e->position[2] + e->offset[2]});
            e->offset.set({o.x, o.y, 0.0f});
            // 0x1a803c / 0x1a8130: alpha 1, fading to 0 over 15 frames;
            // 0x338f10: one update at once.
            e->alpha.set({1.0f});
            e->alpha.start({0.0f}, 15.0f, 1);
            game_.scene.updateObject(*e);
        }
    }
    return true;
}

// 0x2109fc: before the frame's scripts.
void BrickPuzzle::update() {
    const float step = 1.0f; // s0 of 0x37a030 (hypothesis: one per frame)
    const auto verdict = game_.defs.variables.find(names::verdict);
    if (verdict == game_.defs.variables.end() || game_.variable(verdict->second) != -1) {
        return;
    }
    if (Object3D* h = model(hammer_)) {
        float r = h->position[1] + h->offset[1];
        r = r * 0.833333313f;
        r = 10.0f + r;
        h->rotation3.set({wrapAngle(r * kDegree), 0.0f, 0.0f});
    }
    // The camera of screen 0 still and nothing to do: the error message.
    if (!game_.scene.cameras[0].shake.moving() && state_ == 1 &&
        (!arrows(false) || (moveLimit_ > 0 && moveLimit_ <= moves_))) {
        arrows(true);
        if (Object* m = game_.scene.object(message_)) {
            game_.playEffect(0x70);
            m->visible = true;
        }
        next_ = state_ = 5;
        timer_ = -1.0f;
        if (xq::Thread* t = game_.loopThreads[0]) {
            t->callback("WaitTouchReTry", {});
        }
    }
    if (moving_ != 0 && state_ == 2) {
        Object3D* o = model(moving_);
        if (!o || o->position.moving() || o->offset.moving()) {
            return;
        }
        const Cell2 c = cellAt(placed(*o), false);
        int most = 0;
        for (int y = c.y; y >= 0; --y) {
            for (int x = 0; x < cols_; ++x) {
                if (at(x, y) != 0) {
                    most = std::max(most, fall({x, y}, most));
                }
            }
        }
        if (most == 0) {
            next_ = 1;
            state_ = 5;
            setLock(true);
            timer_ = 40.0f;
            refresh_ = true;
            if (goalsSet_ != 0) {
                bool done = true;
                for (std::size_t i = 0; i < goalsSet_ && done; ++i) {
                    const Goal& g = goals_[i];
                    done = g.brick != 0 && inGrid(g.x, g.y) && at(g.x, g.y) == g.brick;
                }
                if (done) {
                    arrows(true);
                    next_ = 4;
                    state_ = 5;
                    if (goalDelay_ >= 0.0f) {
                        setLock(true);
                    }
                    timer_ = goalDelay_;
                }
            }
            moving_ = 0;
        }
    }
    if (state_ != next_) {
        if (state_ == 5) {
            if (timer_ != 0.0f) {
                if (timer_ <= 0.0f) {
                    return;
                }
                timer_ = std::max(timer_ - step, 0.0f);
                return;
            }
            state_ = next_;
            timer_ = 0.0f;
            setLock(false);
        } else {
            state_ = next_;
        }
    }
    if (state_ == 4) {
        arrows(true);
        game_.setVariable(names::verdict, 1);
        next_ = state_ = 5;
        timer_ = -1.0f;
    }
}

bool BrickPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto hashOf = [&](std::size_t i) {
        const std::string* t = c.text(first + i);
        return t ? xq::hash(*t) : c.unsignedInt(first + i);
    };
    auto ok = [&]() {
        c.result = Value::ofInt(1);
        return true;
    };
    switch (number) {
    case 0:
        return setup(c, first);
    case 1:
        // 0x211d68: the grid as first placed; waiting for the script.
        grid_ = start_;
        next_ = state_ = 5;
        timer_ = -1.0f;
        setLock(true);
        arrows(true);
        moving_ = 0;
        refresh_ = true;
        primed_ = false;
        moves_ = 0;
        if (Object* m = game_.scene.object(message_)) {
            m->visible = false;
        }
        return ok();
    case 2:
        // A goal: brick name, x, y.
        if (count < 3 || goalsSet_ >= goals_.size()) {
            return false;
        }
        if (!model(hashOf(0))) {
            return false;
        }
        goals_[goalsSet_++] = {hashOf(0), std::int16_t(c.integer(first + 1)),
                               std::int16_t(c.integer(first + 2))};
        return ok();
    case 3:
        return place(c, first);
    case 4: {
        // An arrow: name, small and big animations, number.
        if (count < 3 || arrows_.empty()) {
            return false;
        }
        arrowSmall_ = hashOf(1);
        arrowBig_ = hashOf(2);
        if (model(hashOf(0))) {
            const int i = c.integer(first + 3);
            if (i >= 0 && i < int(arrows_.size())) {
                arrows_[std::size_t(i)] = hashOf(0);
            }
        }
        return ok();
    }
    case 5:
        if (count == 0 || !model(hashOf(0))) {
            return false;
        }
        hammer_ = hashOf(0);
        return ok();
    case 6:
        if (count == 0 || !model(hashOf(0))) {
            return false;
        }
        effect_ = hashOf(0);
        return ok();
    case 7:
        if (count == 0 || !game_.scene.object(hashOf(0))) {
            return false;
        }
        message_ = hashOf(0);
        return ok();
    case 8: {
        // The next state [, then after a time]: only while waiting or unset.
        if (count == 0 || (state_ != 5 && state_ != -1)) {
            return false;
        }
        const int s = std::int8_t(c.integer(first));
        float t = 0.0f;
        if (s == 5) {
            if (count != 3) {
                return false;
            }
            next_ = std::int8_t(c.integer(first + 1));
            state_ = 5;
            t = c.real(first + 2);
            if (0.0f <= t) {
                setLock(true);
            }
        } else {
            next_ = s;
            if (s == 2) {
                state_ = -1;
                setLock(true);
            }
        }
        timer_ = t;
        return ok();
    }
    case 9:
        if (count == 0) {
            return false;
        }
        if (Value* r = c.reg(first)) {
            *r = Value::ofInt(state_);
        }
        return ok();
    case 10:
        return aim() ? ok() : false;
    case 11:
        return hit(c, first);
    case 12: {
        // The brick's name at a cell into the register (0 for none).
        if (count < 2) {
            return false;
        }
        const int x = std::int16_t(c.integer(first)), y = std::int16_t(c.integer(first + 1));
        if (!inGrid(x, y)) {
            return false;
        }
        if (Value* r = c.reg(first + 2)) {
            const Object3D* o = model(at(x, y));
            *r = o ? Value::ofText(o->name) : Value::ofInt(0);
        }
        return ok();
    }
    case 13:
        return effect() ? ok() : false;
    case 14:
        // The hammer back: scale 1, its offset's z to 0 over 10 frames.
        if (Object3D* h = model(hammer_)) {
            h->scale.set({1.0f, 1.0f, 1.0f});
            const auto off = h->offset.value();
            h->offset.start({off[0], off[1], 0.0f}, 10.0f, 2);
        }
        slideCell_ = {0, 0};
        refresh_ = true;
        return ok();
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeBrickPuzzle(Game& game) {
    return std::make_unique<BrickPuzzle>(game);
}

} // namespace picarat::adventure5
