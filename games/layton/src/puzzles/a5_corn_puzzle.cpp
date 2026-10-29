// The fifth adventure's corn cob puzzles (catalogue type 33, loop 3150;
// puzzles 52, 117, 141): a ladybug walks over the kernels of a corn cob on
// the upper screen. The lower screen is a touch pad with an insect in its
// middle: the insect turns towards the touch and the ladybug walks that
// way; walking up or down turns the cob, walking sideways moves along it.
// Some kernels are walls, pairs of warp kernels carry the ladybug across,
// and it must reach the other ladybug.
//
// Reconstructed from the loop's command handler 0x1e2b2c (vtable 0x401244,
// constructor 0x3848e8; the input hook is the base's), its update 0x1e212c
// and helpers: setup 0x1e3324, reset 0x1e3130, map 0x1e3914, placing
// 0x1e3498, touch 0x1e4084 / 0x1e27f0, walking 0x1e4190, collisions
// 0x1e3afc / 0x28eb24 / 0x28f014, wrapping 0x347874, neighbours 0x3477c4,
// cell index 0x28f41c.
//
// How it plays: the cob's surface is a map of cells (command 2: across,
// down; 24 by 24 pixels each), rows of kinds from command 3 (0 wall, 1
// open, 2 start, 3 goal, 4 and up warp pairs). The map wraps from its last
// row to its first (the cob is round) and stops at its sides. While the pad
// is touched (14 each frame, 15 on release) the walker - a circle of radius
// 8.5 - moves towards the touch at a speed rising from the slow to the fast
// speed of command 5 over its frames, sliding along walls (each blocking
// neighbour cell tested along x and y apart; when both stop it, along the
// wall; when that still hits, away from the nearest corner). The cob turns
// by the vertical movement, the ladybug slides by the horizontal movement
// / 24 and faces the walking direction; walking and bumping sounds (setup
// operands 5 and 6, the bump below a movement of operand 8). On a warp cell
// the walker jumps to its partner and the script's "WarpMain" runs with
// the turn and slide still to make (until it clears flag 0x10). Near the
// goal (operand 1) the other ladybug turns to face it; within operand 2 it
// is reached: both play their goal motions, the pad stops, and after
// operand 0 frames the verdict is set; the goal sound repeats every operand
// 9 frames.
//
// Commands 10..13 (a warp target query and two register reads) are not
// used by this game's data and not reconstructed.
#include "graphics/a56_model.hpp"
#include "puzzles/a5_special.hpp"
#include <bit>
#include <cmath>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using adventure56::Object3D;
using adventure56::wrapAngle;
using xq::Call;
using xq::Value;

struct V2 {
    float x = 0, y = 0;
};

V2 operator+(V2 a, V2 b) {
    return {a.x + b.x, a.y + b.y};
}

V2 operator-(V2 a, V2 b) {
    return {a.x - b.x, a.y - b.y};
}

// 0x2d0fe0.
V2 operator*(V2 a, float k) {
    return {a.x * k, a.y * k};
}

// sqrt of a squared length, 0 when not above 0 (the game's guard).
float length(V2 v) {
    float s = v.x * v.x;
    s = s + v.y * v.y;
    return s > 0.0f ? std::sqrt(s) : 0.0f;
}

// 0x28efb0: v / |v|, zero for a zero vector.
V2 unit(V2 v) {
    float s = v.x * v.x;
    s = s + v.y * v.y;
    if (!(s > 0.0f)) {
        return {0.0f, 0.0f};
    }
    const float k = 1.0f / std::sqrt(s);
    return {v.x * k, v.y * k};
}

// 0x2be1d0 behind the game's check that y or x is non-zero.
float heading(float y, float x) {
    if (((std::bit_cast<std::uint32_t>(y) | std::bit_cast<std::uint32_t>(x)) & 0x7fffffffu) >> 23 ==
        0) {
        return 0.0f;
    }
    return std::atan2(y, x);
}

constexpr float kPi = 3.14159274f;             // 0x40490fdb
constexpr float kHalfPi = 1.57079637f;         // 0x3fc90fdb
constexpr float kDegree = 0.0174533f;          // 0x3c8efa35
constexpr float kRadius = 8.5f;                // 0x41080000: the walker's circle
constexpr float kHalfCell = 12.0f;             // 0x41400000
constexpr float kSlidePerPixel = 1.0f / 24.0f; // 0x3d2aaaab
constexpr float kTilt = 0.785398f;             // 0x3f490fdb: the ladybug's lean

// 0x443764 (filled once, 0x365650): the eight neighbours' offsets.
constexpr V2 kAround[8] = {{-24, -24}, {0, -24},  {24, -24}, {-24, 0},
                           {24, 0},    {-24, 24}, {0, 24},   {24, 24}};

// 0x4129f0 (filled once, 0x365650): the objects by name.
const char* const kInsect = "insect";
const char* const kCorn = "corn";
const char* const kLadybug = "ladybug";
const char* const kGoal = "goal_ladybug";

class CornPuzzle final : public SpecialPuzzle {
public:
    explicit CornPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void update() override;

private:
    struct Cell {
        int kind = 0;     // 0 wall, 1 open, 2 start, 3 goal, 4.. warp pairs
        int x = 0, y = 0; // top left, pixels (shorts)
    };

    enum : unsigned {
        kWalking = 1, // the pad is held
        kMapping = 2, // commands 3 may add rows (2 .. 4)
        kWalkMotion = 4,
        kReached = 8,
        kWarping = 0x10,
        kOnWarp = 0x20, // arrived on a warp cell, not left yet
        kHeld = 0x40,
    };

    Object* object2D(const char* name) { return game_.scene.object(xq::hash(name)); }

    Object3D* model(const char* name) { return game_.scene.object3D(xq::hash(name)); }

    // 0x28f41c: the cell under p (truncated, negative counts as 0).
    int cellIndex(V2 p) const {
        const auto cx = std::uint32_t(std::max(0.0f, p.x / cellW_));
        const auto cy = std::uint32_t(std::max(0.0f, p.y / cellH_));
        return int(cy * std::uint32_t(cols_) + cx);
    }

    Cell* cell(int i) {
        return i >= 0 && i < int(cells_.size()) ? &cells_[std::size_t(i)] : nullptr;
    }

    // 0x347874: rows wrap around the cob; x stays `r` inside the sides
    // (the low side clamps to r itself).
    void wrap(V2& p, float r) const {
        if (minY_ > p.y) {
            p.y = maxY_ + p.y;
        }
        if (maxY_ < p.y) {
            p.y = p.y - maxY_;
        }
        if (minX_ + r > p.x) {
            p.x = r;
        }
        if (maxX_ - r < p.x) {
            p.x = maxX_ - r;
        }
    }

    // 0x3477c4: the cell at p + offset (wrapped), if on the map.
    bool cellNear(int& index, V2 p, V2 offset) const {
        V2 q = p + offset;
        wrap(q, kRadius);
        if (minX_ > q.x || maxX_ < q.x || minY_ > q.y || maxY_ < q.y) {
            return false;
        }
        index = cellIndex(q);
        return true;
    }

    // A model's motion by its place in the resource's list (0x28f3e8 ->
    // 0x18f7f4: looping; the same motion keeps playing).
    void playMotion(const char* name, unsigned index) {
        Object3D* m = model(name);
        if (!m || !m->model || index >= m->model->motions.size()) {
            return;
        }
        const std::string& motion = m->model->motions[index].name;
        if (m->motion != motion || m->motionDone) {
            m->motionTime = 0;
        }
        m->motion = motion;
        m->motionOnce = false;
        m->motionDone = false;
    }

    // 0x18f904: the motion layer's speed.
    void motionSpeed(const char* name, float speed) {
        if (Object3D* m = model(name)) {
            m->motionSpeed = speed;
        }
    }

    static void setAngles(Object3D& m, float x, float y, float z) {
        m.rotation3.set({wrapAngle(x), wrapAngle(y), wrapAngle(z)});
    }

    bool verdictOpen();
    void touched(V2 at);
    void walk(float step);
    V2 collide(V2 pos, V2 velocity, signed char& hit, float r, float step);
    V2 collideCell(V2 pos, V2 move, const Cell& c, signed char& hit, float r, float shift,
                   float step);
    bool touchesBox(signed char& corner, V2& near, V2& normal, V2 q, const Cell& c, float r);
    void reset();
    void place();

    Game& game_;
    unsigned flags_ = 0;                              // +0x160
    float cellW_ = 24, cellH_ = 24;                   // +0x164 / +0x168
    float minX_ = 0, minY_ = 0, maxX_ = 0, maxY_ = 0; // +0x16c .. +0x178
    int cols_ = 0, rows_ = 0;                         // +0x17c / +0x180
    int rowsAdded_ = 0;                               // +0x184
    std::size_t capacity_ = 0;
    std::vector<Cell> cells_;                                       // +0x190
    std::vector<V2> corners_;                                       // +0x19c
    float turnPerPixel_ = 0;                                        // +0x1a8
    float startOffset_[3] = {0, 0, 0};                              // +0x1ac
    V2 pos_, goal_;                                                 // +0x1b8 / +0x1c0
    float cornStart_[2] = {0, 0};                                   // +0x1c8
    V2 startPos_;                                                   // +0x1d0
    float ladybugStart_[3] = {0, 0, 0};                             // +0x1d8
    float ladybugTurn_[3] = {0, 0, 0};                              // +0x1e4
    float slow_ = 0, fast_ = 0, ramp_ = 0, rampFrames_ = 0;         // +0x1f0 / 1f4 / 1f8 / 1fc
    std::uint32_t ladybugMotions_ = 0, goalMotions_ = 0;            // +0x200 / +0x204 (nibbles)
    float goalTimer_ = 0;                                           // +0x208
    float verdictDelay_ = 0, faceDistance_ = 0, reachDistance_ = 0; // +0x20c / 210 / 214
    float kernel_ = 0, goalTilt_ = 0;                               // +0x218 / +0x21c (radians)
    float cornShift_ = 0, startTilt_ = 45;                          // +0x220 / +0x224 (degrees)
    int walkSound_ = 0, bumpSound_ = 0, goalSound_ = 0;             // +0x228 / 22c / 230
    float bumpBelow_ = 0, goalSoundTimer_ = 0, goalSoundGap_ = 0;   // +0x234 / 238 / 23c
    V2 aim_, moved_;                                                // +0x240 / +0x248
};

// Whether the verdict "ナゾ判定" is still open (-1).
bool CornPuzzle::verdictOpen() {
    const auto it = game_.defs.variables.find(names::verdict);
    if (it == game_.defs.variables.end()) {
        return false;
    }
    return game_.variable(it->second) == -1;
}

// 0x1e3130: everything back to where command 4 put it.
void CornPuzzle::reset() {
    corners_.clear();
    if (Object3D* corn = model(kCorn)) {
        setAngles(*corn, cornStart_[0], 0.0f, 0.0f);
    }
    if (Object3D* l = model(kLadybug)) {
        l->position.set({ladybugStart_[0], ladybugStart_[1], ladybugStart_[2]});
        setAngles(*l, ladybugTurn_[0], ladybugTurn_[1], ladybugTurn_[2]);
    }
    if (Object3D* g = model(kGoal)) {
        const auto r = g->rotation3.value();
        setAngles(*g, r[0], kPi, r[2]);
    }
    pos_ = startPos_;
    playMotion(kLadybug, (ladybugMotions_ & 0xffff) >> 12);
    // The goal ladybug takes the standing motion of the ladybug's nibbles
    // too (+0x200, as the code reads it).
    playMotion(kGoal, (ladybugMotions_ & 0xffff) >> 12);
    flags_ = 0;
    goalTimer_ = 0;
    if (const int id = game_.flagId(names::judgeOff); id >= 0) {
        game_.setFlag(id, true);
    }
}

// 0x1e3498: the start and goal cells put the ladybugs on the cob.
void CornPuzzle::place() {
    float s = 180.0f / float(rows_);
    s = s * kDegree;
    const float radius = kernel_ / (std::sin(s) * 2.0f);
    float half = kernel_ * (float(cols_) * 0.5f);
    half = half - kernel_ * 0.5f;
    Object3D* corn = model(kCorn);
    if (!corn) {
        return;
    }
    {
        auto p = corn->position.value();
        p[0] = p[0] + cornShift_;
        corn->position.set(p);
    }
    const auto base = corn->position.value();
    float startAngle = 0.0f;
    const float rowStep = float(360 / rows_) * kDegree;
    for (int r = 0; r < rows_; ++r) {
        for (int col = 0; col < cols_; ++col) {
            Cell* c = cell(r * cols_ + col);
            if (!c || c->kind == 1) {
                continue;
            }
            if (c->kind == 2) {
                startAngle = rowStep * float(r);
                const float tilt = startTilt_ * kDegree;
                const float x = -half + kernel_ * float(col);
                const float y = std::cos(tilt) * radius;
                const float z = std::sin(tilt) * radius;
                startOffset_[0] = x;
                startOffset_[1] = y;
                startOffset_[2] = z;
                if (Object3D* l = model(kLadybug)) {
                    l->position.set({base[0] + x, base[1] + y, base[2] + z});
                    setAngles(*l, tilt, kPi, 0.0f);
                    const auto t = l->rotation3.value();
                    ladybugTurn_[0] = t[0];
                    ladybugTurn_[1] = t[1];
                    ladybugTurn_[2] = t[2];
                }
                ladybugStart_[0] = base[0] + x;
                ladybugStart_[1] = base[1] + y;
                ladybugStart_[2] = base[2] + z;
                pos_ = {float(c->x) + kHalfCell, float(c->y) + kHalfCell};
                startPos_ = pos_;
            }
            if (c->kind == 3) {
                if (Object3D* g = model(kGoal)) {
                    const float angle = rowStep * float(r);
                    const float x = -half + kernel_ * float(col);
                    const float a = angle - goalTilt_;
                    g->position.set({x, std::cos(a) * radius, std::sin(a) * radius});
                    setAngles(*g, angle - goalTilt_, kPi, 0.0f);
                }
                goal_ = {float(c->x) + kHalfCell, float(c->y) + kHalfCell};
            }
        }
    }
    const float turn = -startAngle + startTilt_ * kDegree;
    const float cornAngle = turn + goalTilt_;
    setAngles(*corn, cornAngle, 0.0f, 0.0f);
    cornStart_[0] = cornAngle;
    cornStart_[1] = 0.0f;
    float k = 360.0f / float(rows_);
    k = k * kDegree;
    turnPerPixel_ = k / cellH_;
    flags_ &= ~kMapping;
}

// 0x28f014: whether the circle (q, r) touches the cell's box. Edges first
// by perpendicular distance (inside an edge's span: its outward normal,
// corner flag 0), else a corner within r (that corner, normal (0, 1),
// corner flag 1). The box's corners go to +0x19c.
bool CornPuzzle::touchesBox(signed char& corner, V2& near, V2& normal, V2 q, const Cell& c,
                            float r) {
    corners_.clear();
    const float cx = float(c.x), cy = float(c.y);
    corners_.push_back({cx, cy});
    corners_.push_back({cellW_ + cx, cy});
    corners_.push_back({cellW_ + cx, cellH_ + cy});
    corners_.push_back({cx, cellH_ + cy});
    for (int i = 0; i < 4; ++i) {
        const int j = i == 3 ? 0 : i + 1;
        const V2 e = corners_[std::size_t(j)] - corners_[std::size_t(i)];
        float e2 = e.x * e.x;
        e2 = e2 + e.y * e.y;
        V2 u{0.0f, 0.0f};
        if (e2 > 0.0f) {
            const float k = 1.0f / std::sqrt(e2);
            u = {e.x * k, e.y * k};
        }
        const V2 a = q - corners_[std::size_t(i)];
        const V2 b = q - corners_[std::size_t(j)];
        float cross = e.x * a.y;
        cross = cross - a.x * e.y;
        float d = cross / (e2 > 0.0f ? std::sqrt(e2) : 0.0f);
        if (d < 0.0f) {
            d = -d;
        }
        if (!(d <= r)) {
            continue;
        }
        float pa = u.x * a.x;
        pa = pa + u.y * a.y;
        float pb = u.x * b.x;
        pb = pb + u.y * b.y;
        if (!(pa * pb > 0.0f)) {
            static constexpr V2 kNormals[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            normal = kNormals[i];
            corner = 0;
            return true;
        }
        if (r > length(a)) {
            near = corners_[std::size_t(i)];
            normal = {0.0f, 1.0f};
            corner = 1;
            return true;
        }
        if (r > length(b)) {
            near = corners_[std::size_t(j)];
            normal = {0.0f, 1.0f};
            corner = 1;
            return true;
        }
    }
    corner = 0;
    return false;
}

// 0x28eb24: the move against one blocking cell (shifted by a cob's
// height when the neighbour lies across the wrap).
V2 CornPuzzle::collideCell(V2 pos, V2 move, const Cell& c, signed char& hit, float r, float shift,
                           float step) {
    signed char cornerX = 0, cornerY = 0;
    V2 nearX{0, 0}, nearY{0, 0}, normal{0, 0};
    V2 result = move;
    V2 q{move.x + pos.x, pos.y + shift};
    if (touchesBox(cornerX, nearX, normal, q, c, r)) {
        result.x = 0.0f;
        hit = 1;
    }
    q = {pos.x, move.y + pos.y + shift};
    if (touchesBox(cornerY, nearY, normal, q, c, r)) {
        result.y = 0.0f;
        hit = 1;
    }
    // Both axes stopped with a corner involved: slide along the wall.
    if ((cornerX | cornerY) == 0 || result.x != 0.0f || result.y != 0.0f) {
        return result;
    }
    hit = 0;
    if (cornerX) {
        normal = unit(nearX - pos);
    }
    if (cornerY) {
        normal = unit(nearY - pos);
    }
    // Along the wall: the move without its part along the normal.
    float nm = normal.x * move.x;
    nm = nm + normal.y * move.y;
    float nn = normal.x * normal.x;
    nn = nn + normal.y * normal.y;
    const float t = -nm / nn;
    const V2 slide{move.x + t * normal.x, move.y + t * normal.y};
    result = slide;
    q = {pos.x + slide.x, pos.y + slide.y + shift};
    if (touchesBox(cornerY, nearY, normal, q, c, r)) {
        // Still blocked: away from the nearest corner of the box, `step`
        // far (the other way when the cell lies across the top wrap).
        float best = 1000.0f;
        std::size_t nearest = 5;
        for (std::size_t i = 0; i < 4 && i < corners_.size(); ++i) {
            const V2 p{pos.x + slide.x, pos.y + slide.y};
            const float d = length(p - corners_[i]);
            if (best > d) {
                best = d;
                nearest = i;
            }
        }
        const V2 centre{float(c.x) + kHalfCell, float(c.y) + kHalfCell};
        const V2 v = nearest < corners_.size() ? corners_[nearest] - centre : V2{};
        const V2 away = shift >= 0.0f ? v : V2{-v.x, -v.y};
        result = unit(away) * step;
    }
    return result;
}

// 0x1e3afc: one step of the walker against the walls around it.
V2 CornPuzzle::collide(V2 pos, V2 velocity, signed char& hit, float r, float step) {
    V2 move = velocity * step;
    V2 p = pos;
    wrap(p, kRadius);
    const int here = cellIndex(p);
    for (int d = 0; d < 8; ++d) {
        int index = 0;
        if (!cellNear(index, pos, kAround[d])) {
            continue;
        }
        const Cell* c = cell(index);
        if (!c || c->kind != 0) {
            continue;
        }
        const int hereRow = cols_ ? here / cols_ : 0;
        const int row = cols_ ? index / cols_ : 0;
        if (hereRow == 0 && row != 1 && row > 0) {
            // A wall across the wrap below the first row.
            move = collideCell(pos, move, *c, hit, r, maxY_, step);
        } else if (hereRow != 0 && hereRow == rows_ - 1 && row != rows_ - 2 && row < hereRow) {
            move = collideCell(p, move, *c, hit, r, -maxY_, step);
        } else {
            move = collideCell(p, move, *c, hit, r, 0.0f, step);
        }
    }
    return {move.x + p.x, move.y + p.y};
}

// 0x1e27f0: the pad pressed at `at`: the insect (its rotation at 0x284,
// wrapped) turns that way, the walk starts (its sound restarts, the walking
// motion comes on).
void CornPuzzle::touched(V2 at) {
    if (flags_ & kReached) {
        return;
    }
    if (Object* insect = object2D(kInsect)) {
        const V2 d{at.x - insect->position[0], at.y - insect->position[1]};
        insect->stepAngle.set({wrapAngle(heading(d.y, d.x) + kHalfPi)});
        aim_ = d;
    }
    ramp_ = 0.0f;
    game_.stopEffect(walkSound_, 0);
    flags_ |= kWalking | kWalkMotion;
}

// 0x1e4190: one frame of walking towards aim_.
void CornPuzzle::walk(float step) {
    Object* insect = object2D(kInsect);
    if (!insect) {
        return;
    }
    V2 dir = aim_;
    if (dir.x != 0.0f || dir.y != 0.0f) {
        dir = unit(aim_);
    }
    insect->stepAngle.set({wrapAngle(heading(dir.y, dir.x) + kHalfPi)});
    float k = 1.0f / rampFrames_;
    k = k * ramp_;
    float speed = (1.0f - k) * slow_;
    speed = speed + k * fast_;
    const V2 velocity = dir * speed;
    signed char hit = -1;
    const V2 next = collide(pos_, velocity, hit, kRadius, step);
    moved_ = next - pos_;
    if (hit == 1 || hit == 0) {
        if (bumpBelow_ > length(moved_)) {
            if (!game_.effectsSounding(bumpSound_)) {
                game_.playEffect(bumpSound_);
            }
        } else if (!game_.effectsSounding(walkSound_)) {
            game_.playEffect(walkSound_);
        }
    } else if (!game_.effectsSounding(walkSound_)) {
        game_.playEffect(walkSound_);
    }
    pos_ = next;
    if (Object3D* corn = model(kCorn)) {
        const float turn = -moved_.y * turnPerPixel_;
        const auto r = corn->rotation3.value();
        setAngles(*corn, r[0] + turn, r[1], r[2]);
    }
    if (Object3D* l = model(kLadybug)) {
        auto p = l->position.value();
        const auto o = l->offset.value();
        l->position.set({p[0] + o[0] + moved_.x * kSlidePerPixel, p[1] + o[1], p[2] + o[2]});
        setAngles(*l, kTilt, heading(-aim_.y, aim_.x) - kHalfPi, 0.0f);
        motionSpeed(kLadybug, speed);
    }
}

// 0x1e212c: before the frame's scripts.
void CornPuzzle::update() {
    const float step = 1.0f; // s0 of 0x37a030 (hypothesis: one per frame)
    if ((flags_ & kWarping) || (flags_ & kHeld)) {
        return;
    }
    if (!verdictOpen()) {
        return;
    }
    if (flags_ & kReached) {
        const float before = goalTimer_;
        goalTimer_ = before + step;
        if (before < verdictDelay_ && verdictDelay_ <= goalTimer_) {
            flags_ &= ~kReached;
            game_.setVariable(names::verdict, 1);
        }
        goalSoundTimer_ = goalSoundTimer_ + step;
        if (game_.effectsSounding(goalSound_)) {
            return;
        }
        if (goalSoundTimer_ < goalSoundGap_) {
            return;
        }
        game_.playEffect(goalSound_);
        goalSoundTimer_ = 0.0f;
        return;
    }
    if (!(flags_ & kOnWarp)) {
        Cell* c = cell(cellIndex(pos_));
        if (c && c->kind >= 4) {
            // A warp: to the other cell of the pair, and the script turns
            // the cob and slides the ladybug the rest of the way.
            const float dy = pos_.y - (float(c->y) + kHalfCell);
            float turn = kPi;
            turn = turn + turnPerPixel_ * dy;
            float slide = (float(c->x) + kHalfCell) - pos_.x;
            slide = slide * kSlidePerPixel;
            const Cell* other = nullptr;
            for (const Cell& o : cells_) {
                other = &o;
                if (o.kind == c->kind && (o.x != c->x || o.y != c->y)) {
                    break;
                }
            }
            if (other) {
                pos_ = {float(other->x) + kHalfCell, float(other->y) + kHalfCell};
            }
            const Value args[] = {Value::ofFloat(turn), Value::ofFloat(slide),
                                  Value::ofFloat(turnPerPixel_ * dy), Value::ofFloat(moved_.x),
                                  Value::ofFloat(moved_.y)};
            if (xq::Thread* t = game_.loopThreads[0]) {
                t->callback("WarpMain", args);
            }
            flags_ |= kWarping;
            return;
        }
    } else {
        Cell* c = cell(cellIndex(pos_));
        if (c && c->kind < 4) {
            flags_ &= ~kOnWarp;
        }
    }
    if (flags_ & kWalkMotion) {
        playMotion(kLadybug, (ladybugMotions_ & 0xfffff) >> 16);
        flags_ &= ~kWalkMotion;
    }
    if (!(flags_ & kWalking)) {
        return;
    }
    walk(step);
    if (rampFrames_ > ramp_) {
        ramp_ = ramp_ + step;
    }
    const V2 d = pos_ - goal_;
    const float distance = length(d);
    if (distance < faceDistance_) {
        if (Object3D* g = model(kGoal)) {
            const float a = heading(-d.y, d.x);
            const auto r = g->rotation3.value();
            setAngles(*g, r[0], a - kHalfPi, 0.0f);
        }
    }
    if (!(distance < reachDistance_)) {
        return;
    }
    flags_ = (flags_ & ~kWalking) | kReached;
    playMotion(kLadybug, (ladybugMotions_ & 0xfff) >> 8);
    playMotion(kGoal, (goalMotions_ & 0xfff) >> 8);
    if (Object3D* l = model(kLadybug)) {
        const V2 e = goal_ - pos_;
        const float a = heading(-e.y, e.x);
        const auto r = l->rotation3.value();
        setAngles(*l, r[0], a - kHalfPi, r[2]);
    }
    game_.playEffect(goalSound_);
    motionSpeed(kLadybug, 1.0f);
    // +0x1a: the loop's input byte (2109's): the touch queries go quiet.
    game_.objectTouchLocked = true;
    if (Object* pad = object2D("NazoCornPad")) {
        pad->touchOn = false;
        pad->switch14 = false;
    }
}

bool CornPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto real = [&](std::size_t i) {
        return c.real(first + i);
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0:
        // Verdict delay, face and reach distances, kernel size (3D), goal
        // tilt (degrees); walk, bump and goal sounds; bump threshold, goal
        // sound gap; optionally the cob's shift and the start tilt.
        if (count < 10) {
            return false;
        }
        verdictDelay_ = real(0);
        faceDistance_ = real(1);
        reachDistance_ = real(2);
        kernel_ = real(3);
        goalTilt_ = real(4) * kDegree;
        walkSound_ = integer(5);
        bumpSound_ = integer(6);
        goalSound_ = integer(7);
        bumpBelow_ = real(8);
        goalSoundGap_ = real(9);
        if (count > 10) {
            cornShift_ = real(10);
        }
        if (count > 11) {
            startTilt_ = real(11);
        }
        if (const int id = game_.flagId(names::judgeOff); id >= 0) {
            game_.setFlag(id, true);
        }
        return result(1);
    case 1:
        reset();
        return result(1);
    case 2:
        // The map: cells across, down.
        if (count < 2) {
            return false;
        }
        flags_ |= kMapping;
        cols_ = integer(0);
        rows_ = integer(1);
        capacity_ = std::size_t(std::max(0, cols_ * rows_));
        cells_.clear();
        cells_.reserve(capacity_);
        corners_.clear();
        maxX_ = cellW_ * float(cols_);
        maxY_ = cellH_ * float(rows_);
        return result(1);
    case 3:
        // A row of cell kinds.
        if (flags_ & kMapping) {
            for (std::size_t i = 0; i < count; ++i) {
                if (cells_.size() >= capacity_) {
                    break;
                }
                Cell cell;
                cell.kind = integer(i);
                cell.x = std::int16_t(int(cellW_ * float(i)));
                cell.y = std::int16_t(int(cellH_ * float(rowsAdded_)));
                cells_.push_back(cell);
            }
            ++rowsAdded_;
        }
        return result(1);
    case 4:
        if (flags_ & kMapping) {
            place();
        }
        return result(1);
    case 5:
        // Slow and fast speed, frames between them.
        if (count < 3) {
            return false;
        }
        slow_ = real(0);
        fast_ = real(1);
        rampFrames_ = real(2);
        return result(1);
    case 6:
    case 7: {
        // Five motion numbers (nibbles; added in) for the ladybug / the
        // goal ladybug; the fourth plays at once.
        if (count < 5) {
            return false;
        }
        std::uint32_t& m = number == 6 ? ladybugMotions_ : goalMotions_;
        for (int i = 0; i < 5; ++i) {
            m += std::uint32_t(integer(std::size_t(i))) << (4 * i);
        }
        playMotion(number == 6 ? kLadybug : kGoal, (m & 0xffff) >> 12);
        return result(1);
    }
    case 8: {
        // The named ladybug's motion number at a bit offset.
        if (count < 2) {
            return false;
        }
        const std::string* name = c.text(first);
        const std::uint32_t h = name ? xq::hash(*name) : c.unsignedInt(first);
        const unsigned shift = unsigned(integer(1)) & 0xff;
        if (h == xq::hash(kLadybug)) {
            playMotion(kLadybug, (ladybugMotions_ >> shift) & 0xf);
        }
        if (h == xq::hash(kGoal)) {
            playMotion(kGoal, (goalMotions_ >> shift) & 0xf);
        }
        return result(1);
    }
    case 9: {
        // Flags: 0 set, 1 clear, 2 test.
        if (count < 2) {
            return false;
        }
        const unsigned mask = unsigned(integer(1));
        c.result = Value::ofInt(1);
        switch (integer(0)) {
        case 0:
            flags_ |= mask;
            break;
        case 1:
            flags_ &= ~mask;
            break;
        case 2:
            if ((flags_ & mask) == 0) {
                c.result = Value::ofInt(0);
            }
            break;
        default:
            break;
        }
        return true;
    }
    case 14: {
        // Each frame the pad is held: a press starts the walk; the aim
        // follows the touch.
        const V2 at{float(game_.input.x), float(game_.input.y)};
        if (game_.pressed()) {
            touched(at);
        }
        if (flags_ & kWalking) {
            if (Object* insect = object2D(kInsect)) {
                aim_ = {at.x - insect->position[0], at.y - insect->position[1]};
            }
        }
        return true;
    }
    case 15:
        // Released: the ladybug stands.
        if (flags_ & kWalking) {
            flags_ &= ~kWalking;
            if ((flags_ & (kWarping | kOnWarp)) == 0) {
                playMotion(kLadybug, (ladybugMotions_ & 0xffff) >> 12);
                motionSpeed(kLadybug, 1.0f);
                ramp_ = 0.0f;
            }
        }
        return true;
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeCornPuzzle(Game& game) {
    return std::make_unique<CornPuzzle>(game);
}

} // namespace picarat::adventure5
