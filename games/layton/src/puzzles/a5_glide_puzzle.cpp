// The fifth adventure's gliding puzzles (catalogue type 13, loop 3230;
// puzzles 112, 148): a tall map on the upper screen with paths made of
// lines; a glider follows them from the start, and where a crossing comes
// within reach the player may make it jump across to another line. It must
// arrive at the goal.
//
// Reconstructed from the loop's command handler 0x2e5168 (vtable 0x402e40,
// constructor 0x37e904), its input hook 0x2e59b8 (the glider's state
// machine, run every frame after the touch dispatch) and helpers: reset
// 0x2e671c, scrolling 0x2e64bc, position along a line 0x279ef4, moving
// 0x279e0c, progress 0x2e62ec, crossings in reach 0x2e6040, the jump
// 0x2e65c0, crossings 0x2e6ab0, start / goal objects 0x2e6964.
//
// How it plays: the script lays out points (40), lines between two points
// with the line that follows (50), crossings from a place on one line to a
// place on another (60, each an object "glid_NN" with an off and an on
// animation), the start and goal (70) and the map object (80), all on the
// upper screen inside the map. Command 20 starts: after 30 frames the
// glider moves along the start line at the setup's speed (pixels per
// frame), then along each next line. A crossing on its line within the
// setup's radius shows its on animation; command 22 (the jump button)
// picks it: the glider stops, turns ("rot"), jumps along the crossing
// ("glid") and goes on along the other line from there. The map follows the
// glider (its y less 64, within the map); command 24 scrolls it (over
// three frames). Arriving within 16 pixels of the goal (on the goal line,
// before the goal's place) stops the glider and its sounds; 30 frames
// later the verdict "ナゾ判定" becomes 1. A line without a next one ends in
// failure: the fail sound, the message object, the "fail" animation;
// command 25 reports when the sound is over.
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

struct Point {
    int x = -1, y = -1;
};

class GlidePuzzle final : public SpecialPuzzle {
public:
    explicit GlidePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;

    std::string describe() const override {
        char line[96];
        std::snprintf(line, sizeof line, "state %d line %d reach %d arrived %d\n", state_, line_,
                      inReach_, int(arrived_));
        return line;
    }

private:
    enum State { kIdle = 0, kWaiting = 1, kOnLine = 2, kJumping = 3, kFailed = 4 };

    struct Node {
        int id = -1;
        int x = 0, y = 0;
    };

    struct Line {
        int id = -1;
        int a = 0, b = 0; // node ids
        int next = -1;    // the line after it
        float length = 0;
    };

    struct Crossing {
        int id = -1;
        int lineA = 0;
        float tA = 0;
        int lineB = 0;
        float tB = 0;
        std::uint32_t object = 0; // "glid_NN"
        float length = 0;
        std::string off, on; // animations
    };

    static constexpr int kGlideSound = 0x1ea;
    static constexpr int kJumpSound = 0x1eb;
    static constexpr int kFailSound = 0x70;

    const Line* line(int id) const {
        for (const Line& l : lines_) {
            if (l.id == id) {
                return &l;
            }
        }
        return nullptr;
    }

    const Crossing* crossing(int id) const {
        for (const Crossing& c : crossings_) {
            if (c.id == id) {
                return &c;
            }
        }
        return nullptr;
    }

    // 0x279ef4: the place t along a line (truncated pixels), or (-1, -1).
    Point at(int lineId, float t) const {
        const Line* l = line(lineId);
        const Node* a = nullptr;
        const Node* b = nullptr;
        if (l) {
            for (const Node& n : nodes_) {
                if (!a && n.id == l->a) {
                    a = &n;
                }
                if (!b && n.id == l->b) {
                    b = &n;
                }
            }
        }
        if (!a || !b) {
            return {-1, -1};
        }
        return {std::int16_t(int(float(std::int16_t(b->x - a->x)) * t) + a->x),
                std::int16_t(a->y + int(float(std::int16_t(b->y - a->y)) * t))};
    }

    static float distance(Point a, Point b) {
        const int dx = std::int16_t(b.x - a.x), dy = std::int16_t(b.y - a.y);
        return std::sqrt(float(dy * dy + dx * dx));
    }

    Object* glider() { return game_.scene.object(start_); }

    static Point basePosition(const Object& o) {
        // 0x2ca1d0 truncated.
        return {std::int16_t(int(o.position[0])), std::int16_t(int(o.position[1]))};
    }

    void reset();
    void scroll(bool smooth);
    void move(Point from, Point to, bool keep);
    void updateProgress();
    void updateReach();
    bool jump();
    Object* makeMapObject(const std::string& name, Point p, const Value& animation);

    Game& game_;
    bool failDone_ = false;           // +0x160 (25)
    bool failSounded_ = false;        // +0x164
    bool verdictSet_ = false;         // +0x168
    bool arrived_ = false;            // +0x16c
    int arrivedFrames_ = 0;           // +0x170
    int waitFrames_ = 0;              // +0x174
    std::string resource_;            // +0x178: the animation set's resource
    std::string message_;             // +0x18c: the failure message object
    std::uint32_t start_ = 0;         // +0x1a0: the glider ("start")
    int startLine_ = -1;              // +0x1a4
    float startT_ = 0;                // +0x1a8
    std::uint32_t goal_ = 0;          // +0x1ac
    int goalLine_ = -1;               // +0x1b0
    float goalT_ = 0;                 // +0x1b4
    std::uint32_t map_ = 0;           // +0x1b8
    int mapHeight_ = 0;               // +0x1bc
    int scrollX_ = 0;                 // +0x1c0
    int scrollY_ = 0;                 // +0x1c2
    bool smooth_ = false;             // +0x1c4
    int state_ = kIdle;               // +0x1c8
    int line_ = -1;                   // +0x1cc: the line it is on
    int jumpingOn_ = -1;              // +0x1d0: the crossing it jumps along
    float t_ = 0;                     // +0x1d4: how far along
    Point here_;                      // +0x1d8
    float reachDistance_ = 0;         // +0x1dc
    int inReach_ = -1;                // +0x1e0: the crossing in reach
    int picked_ = -1;                 // +0x1e4: the crossing to jump along
    float speed_ = 0;                 // +0x1e8
    float radius_ = 0;                // +0x1ec
    float lastX_ = 0, lastY_ = 0;     // +0x1f0
    std::vector<Node> nodes_;         // +0x200
    std::vector<Line> lines_;         // +0x20c
    std::vector<Crossing> crossings_; // +0x218
};

// An object on the upper screen inside the map (priority 111, kind 7) at a
// place, with an animation of the setup's set (0x2e6964 / 0x2e6ab0).
Object* GlidePuzzle::makeMapObject(const std::string& name, Point p, const Value& animation) {
    Object& o = game_.createObject(name, 0, 111, 7, {});
    o.position.set({float(p.x), float(p.y), o.position[2]});
    game_.setAnimationSet(o, xq::hash(resource_));
    game_.playAnimation(o, animation, 0);
    o.parent = map_;
    return &o;
}

// 0x279e0c: towards `to` in a straight line at the setup's speed (from
// `from`, unless `keep`).
void GlidePuzzle::move(Point from, Point to, bool keep) {
    Object* o = glider();
    if (!o) {
        return;
    }
    if (!keep) {
        o->position.set({float(from.x), float(from.y), o->position[2]});
    }
    const float frames = distance(from, to) / (speed_ > 0.0f ? speed_ : 1.0f);
    o->position.start({float(to.x), float(to.y), o->position[2]}, frames, 0);
}

// 0x2e671c: the glider back at the start line's beginning, facing along
// it; the map scrolled to it; every crossing off.
void GlidePuzzle::reset() {
    failDone_ = failSounded_ = false;
    state_ = kIdle;
    jumpingOn_ = -1;
    line_ = startLine_;
    here_ = at(startLine_, 0.0f);
    t_ = startT_;
    reachDistance_ = 0;
    inReach_ = picked_ = -1;
    scrollY_ = std::int16_t(here_.y - 64);
    scrollY_ = std::max(scrollY_, 0);
    scrollY_ = std::min(scrollY_, mapHeight_ - 240);
    if (Object* m = game_.scene.object(map_)) {
        m->position.set({float(-scrollX_), float(-scrollY_), m->position[2]});
    }
    if (Object* g = glider()) {
        const Point a = at(line_, 0.0f), b = at(line_, 1.0f);
        g->scale.set({a.x > b.x ? -1.0f : 1.0f, 1.0f, g->scale[2]});
        game_.playAnimation(*g, Value::ofText("start"), 0);
        g->position.set({float(here_.x), float(here_.y), g->position[2]});
        lastX_ = float(here_.x);
        lastY_ = float(here_.y);
    }
    for (const Crossing& c : crossings_) {
        if (Object* o = game_.scene.object(c.object)) {
            game_.playAnimation(*o, Value::ofText(c.off), 0);
        }
    }
}

// 0x2e64bc: the map at the scroll (kept within the map), at once unless
// it is still gliding, or over three frames when asked.
void GlidePuzzle::scroll(bool smooth) {
    scrollY_ = std::max(scrollY_, 0);
    if (mapHeight_ - 240 <= scrollY_) {
        scrollY_ = std::int16_t(mapHeight_ - 240);
    }
    Object* m = game_.scene.object(map_);
    if (!m) {
        return;
    }
    const std::array<float, 3> to{float(-scrollX_), float(-scrollY_), m->position[2]};
    if (smooth) {
        m->position.start(to, 3, 0);
    } else if (!m->position.moving()) {
        m->position.set(to);
    }
}

// 0x2e62ec: how far along its line (or crossing) the glider is.
void GlidePuzzle::updateProgress() {
    Object* g = glider();
    if (!g) {
        return;
    }
    const Point p = basePosition(*g);
    Point a, b;
    if (state_ == kOnLine) {
        a = at(line_, 0.0f);
        b = at(line_, 1.0f);
    } else if (state_ == kJumping) {
        const Crossing* c = crossing(jumpingOn_);
        if (!c) {
            return;
        }
        a = at(c->lineA, c->tA);
        b = at(c->lineB, c->tB);
    } else {
        return;
    }
    t_ = distance(a, p) / distance(a, b);
    here_ = p;
}

// 0x2e6040: the crossings on the glider's line within reach (the last in
// reach is remembered) show their on animation, the others off.
void GlidePuzzle::updateReach() {
    inReach_ = -1;
    for (const Crossing& c : crossings_) {
        bool on = false;
        if (state_ != kWaiting && c.lineA == line_) {
            const Point p = at(line_, c.tA);
            const float d = distance(here_, p);
            if (!(radius_ < d)) {
                reachDistance_ = d;
                on = true;
                inReach_ = c.id;
            }
        }
        if (Object* o = game_.scene.object(c.object)) {
            game_.playAnimation(*o, Value::ofText(on ? c.on : c.off), 0);
        }
    }
}

// 0x2e65c0: the picked crossing, once the turn is over: jump along it.
// True while it waits for the turn or has jumped.
bool GlidePuzzle::jump() {
    const Crossing* c = picked_ >= 0 ? crossing(picked_) : nullptr;
    Object* g = glider();
    if (!c || !g) {
        return false;
    }
    // 0x340030: whether the animation (the turn, "rot", played once) has
    // run through (the console trace shows "glid" 23 frames after "rot");
    // it waits until then.
    if (!(g->content == 9 && g->finished)) {
        return true;
    }
    game_.playAnimation(*g, Value::ofText("glid"), 0);
    move(at(c->lineA, c->tA), at(c->lineB, c->tB), true);
    state_ = kJumping;
    jumpingOn_ = c->id;
    line_ = -1;
    t_ = c->tA;
    inReach_ = -1;
    reachDistance_ = 0;
    picked_ = -1;
    game_.stopEffect(kGlideSound, 0);
    game_.playEffect(kJumpSound);
    return true;
}

// 0x2e59b8
void GlidePuzzle::input() {
    if (state_ != kIdle) {
        scrollY_ = std::int16_t(here_.y - 64);
    }
    scroll(smooth_);
    smooth_ = false;
    if (state_ == kIdle) {
        return;
    }
    Object* g = glider();
    if (!g) {
        return;
    }
    // It faces the way it moves.
    const float x = g->position[0];
    if (lastX_ < x) {
        g->scale.set({1, 1, g->scale[2]});
    } else if (lastX_ > x) {
        g->scale.set({-1, 1, g->scale[2]});
    }
    lastX_ = g->position[0];
    lastY_ = g->position[1];
    updateProgress();
    updateReach();
    if (goalLine_ == line_ && goalT_ >= t_) {
        // 0x1229f4: the distance (a square root) within 16 pixels.
        if (distance(here_, at(goalLine_, goalT_)) <= 16.0f) {
            g->switch14 = false;
            if (!arrived_) {
                game_.stopEffect(kJumpSound, 0);
                game_.stopEffect(kGlideSound, 0);
                arrivedFrames_ = 0;
                arrived_ = true;
                verdictSet_ = false;
            }
            if (++arrivedFrames_ > 30 && !verdictSet_) {
                game_.setVariable(names::verdict, 1);
                verdictSet_ = true;
            }
            return;
        }
    }
    if (state_ == kFailed) {
        game_.stopEffect(kJumpSound, 0);
        game_.stopEffect(kGlideSound, 0);
        if (!failSounded_) {
            if (!game_.effectsSounding(kFailSound)) {
                game_.playEffect(kFailSound);
                failSounded_ = true;
            }
        } else if (!game_.effectsSounding(kFailSound)) {
            failDone_ = true;
        }
        if (Object* m = game_.scene.object(xq::hash(message_))) {
            m->visible = true;
        }
        game_.playAnimation(*g, Value::ofText("fail"), 0);
        return;
    }
    if (g->position.moving()) {
        // The jump button was pressed: stop and turn.
        if (picked_ < 0 || !crossing(picked_)) {
            return;
        }
        g->position.set(g->position.value());
        game_.playAnimation(*g, Value::ofText("rot"), 8);
        game_.stopEffect(kGlideSound, 0);
        return;
    }
    if (jump()) {
        return;
    }
    if (state_ == kWaiting) {
        if (++waitFrames_ <= 30) {
            return;
        }
        if (startLine_ >= 0) {
            const Point from = at(startLine_, 0.0f), to = at(startLine_, 1.0f);
            move(from, to, false);
            jumpingOn_ = -1;
            state_ = kOnLine;
            line_ = startLine_;
            t_ = 0;
            here_ = from;
            inReach_ = -1;
            reachDistance_ = 0;
            picked_ = -1;
        }
        game_.playEffect(kGlideSound);
    } else if (state_ == kOnLine) {
        const Line* l = line(line_);
        const int next = l ? l->next : -1;
        if (next < 0) {
            state_ = kFailed;
            return;
        }
        const Point from = at(next, 0.0f), to = at(next, 1.0f);
        move(from, to, false);
        line_ = next;
        jumpingOn_ = -1;
        state_ = kOnLine;
        t_ = 0;
        here_ = from;
        inReach_ = -1;
        reachDistance_ = 0;
        picked_ = -1;
    } else if (state_ == kJumping) {
        const Crossing* c = crossing(jumpingOn_);
        const int next = c ? c->lineB : -1;
        const float tB = c ? c->tB : 0.0f;
        if (next < 0) {
            state_ = kFailed;
            return;
        }
        game_.playAnimation(*g, Value::ofText("start"), 0);
        const Point from = at(next, tB), to = at(next, 1.0f);
        move(from, to, false);
        line_ = next;
        jumpingOn_ = -1;
        state_ = kOnLine;
        t_ = tB;
        here_ = from;
        inReach_ = -1;
        reachDistance_ = 0;
        picked_ = -1;
        game_.stopEffect(kJumpSound, 0);
        game_.playEffect(kGlideSound);
    }
}

bool GlidePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto text = [&](std::size_t i) {
        const std::string* t = c.text(first + i);
        return t ? *t : std::string();
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0:
        reset();
        return result(1);
    case 10:
        // Animation resource, numbers of points, lines and crossings,
        // speed, reach radius, failure message object.
        if (count < 7) {
            return false;
        }
        resource_ = text(0);
        speed_ = c.real(first + 4);
        radius_ = c.real(first + 5);
        message_ = text(6);
        nodes_.clear();
        lines_.clear();
        crossings_.clear();
        nodes_.reserve(std::size_t(std::max(0, integer(1))));
        lines_.reserve(std::size_t(std::max(0, integer(2))));
        crossings_.reserve(std::size_t(std::max(0, integer(3))));
        return result(1);
    case 20:
        waitFrames_ = 0;
        state_ = kWaiting;
        return result(1);
    case 21:
        return result(state_ != kIdle ? 1 : 0);
    case 22:
        // The jump button: the crossing in reach.
        if (state_ < kOnLine) {
            return result(0);
        }
        if (inReach_ >= 0) {
            picked_ = inReach_;
        }
        return result(inReach_ >= 0 ? 1 : 0);
    case 23:
        return result(state_ == kFailed ? 1 : 0);
    case 24:
        if (count == 0) {
            return false;
        }
        scrollY_ = std::int16_t(integer(0));
        smooth_ = true;
        return result(1);
    case 25:
        return result(failDone_ ? 1 : 0);
    case 40:
        // A point (id, x, y).
        if (count < 3) {
            return false;
        }
        nodes_.push_back({integer(0), std::int16_t(integer(1)), std::int16_t(integer(2))});
        return result(1);
    case 50: {
        // A line (id, from point, to point, next line).
        if (count < 4) {
            return false;
        }
        lines_.push_back({integer(0), integer(1), integer(2), integer(3), 0});
        Line& l = lines_.back();
        l.length = distance(at(l.id, 0.0f), at(l.id, 1.0f));
        return result(1);
    }
    case 60: {
        // A crossing (id, line, place, other line, place, off and on
        // animations) and its object "glid_NN".
        if (count < 7) {
            return false;
        }
        Crossing x;
        x.id = integer(0);
        x.lineA = integer(1);
        x.tA = c.real(first + 2);
        x.lineB = integer(3);
        x.tB = c.real(first + 4);
        x.off = text(5);
        x.on = text(6);
        x.length = distance(at(x.lineA, x.tA), at(x.lineB, x.tB));
        char name[16];
        std::snprintf(name, sizeof name, "glid_%02x", unsigned(x.id));
        x.object = makeMapObject(name, at(x.lineA, x.tA), Value::ofText(x.off))->hash;
        crossings_.push_back(x);
        return result(1);
    }
    case 70: {
        // The start (the glider) and the goal: names, lines, places.
        if (count < 6) {
            return false;
        }
        startLine_ = integer(1);
        startT_ = c.real(first + 2);
        goalLine_ = integer(4);
        goalT_ = c.real(first + 5);
        start_ = makeMapObject(text(0), at(startLine_, startT_), Value::ofText(text(0)))->hash;
        goal_ = makeMapObject(text(3), at(goalLine_, goalT_), Value::ofText(text(3)))->hash;
        const Point p = at(startLine_, startT_);
        lastX_ = float(p.x);
        lastY_ = float(p.y);
        return result(1);
    }
    case 80: {
        // The map (name, animation, height) on the upper screen.
        if (count < 3) {
            return false;
        }
        Object& o = game_.createObject(text(0), 0, 110, 7, {});
        map_ = o.hash;
        mapHeight_ = integer(2);
        scrollX_ = scrollY_ = 0;
        o.position.set({0, 0, o.position[2]});
        game_.setAnimationSet(o, xq::hash(resource_));
        game_.playAnimation(o, c.at(first + 1), 0);
        return result(1);
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeGlidePuzzle(Game& game) {
    return std::make_unique<GlidePuzzle>(game);
}

} // namespace picarat::adventure5
