// The fifth adventure's pairing puzzles (catalogue type 11, loop 3130;
// puzzles 36, 73, 88, 100): two groups of points; the player joins points
// of different groups with straight lines by sliding from one to the
// other, and the script checks the pairs.
//
// Reconstructed from the loop's command handler 0x1ecbb0 (vtable 0x4013f4,
// constructor 0x384e60), its input hook 0x1ed47c (press 0x1ec9dc, slide
// 0x1ee9b8, release 0x1ee2a4), its update 0x1ec8e8 (before the scripts)
// with the drawing 0x1ed96c, and the base loop's helpers (point under the
// pen 0x28d808 / 0x3462c4, line removal 0x28d0ec, line and dot quads
// 0x28d5b0 / 0x28d488).
//
// How it plays: the points are the script's objects that show nothing
// (content 0) on the lower screen; their fifth value is their group. A
// press within the snap radius of a point starts a line there (with the
// join sound); while sliding, the line's end snaps to a point of the other
// group under the pen. Releasing on such a point joins the two unless one
// of them is taken already (a point takes one line) - then nothing joins
// (the cancel sound) - or the lines are all used up (an error message
// with the error sound locks the puzzle until a tap after 15 frames).
// Releasing on the line's own pair takes that line away. Releasing
// elsewhere draws the line back to its start over 7 frames. A press on a
// joined line picks it (it shows in the drawing colour); releasing while
// still on it takes it away: both halves shrink to its middle over 7
// frames (the remove sound). While a line shrinks or draws back, the
// loop's input switch is off. The judge button turns on once the required
// number of lines is drawn (flag "＜ナゾボタン＞判定無効").
//
// Command 6 (0x1ece50; sixth adventure 0x2ccf84) sets four switches
// (+0x2a0..+0x2ac): no dots at the line ends (drawn: 0x1edae4), a maximum
// line length, lines locked to the starting point's row or column. Only
// the first is reconstructed; no script of either game sets the others
// (the sixth's puzzle 018 sends "6 1").
#include "puzzles/a5_special.hpp"
#include "system/a56_tween.hpp"
#include <cmath>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

class PairPuzzle final : public SpecialPuzzle {
public:
    explicit PairPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;
    void update() override;

private:
    enum : unsigned {
        kDrawing = 1,  // a line follows the pen from a point
        kPicked = 2,   // a joined line is pressed
        kError = 4,    // the error message shows
        kShrinking = 8 // a line shrinks away or draws back
    };

    struct Pair {
        std::uint32_t a = 0, b = 0;
    };

    Vec2 positionOf(std::uint32_t hash) {
        // 0x2ca270: the object's position with its offset.
        if (Object* o = game_.scene.object(hash)) {
            return {o->position[0] + o->offset[0], o->position[1] + o->offset[1]};
        }
        return {0, 0};
    }

    // The object's fifth value (0x291714 with 4): its group.
    int groupOf(const Object& o) const { return o.values.size() > 4 ? o.values[4].toInt() : 0; }

    std::uint32_t pointAt(Vec2 p);
    int lineAt(Vec2 p);
    int findPair(std::uint32_t a, std::uint32_t b) const;
    bool used(std::uint32_t point) const;
    void removePair(int index);
    void showError();
    void press(bool inside, Vec2 p);
    void slide(bool inside, Vec2 p);
    void release(Vec2 p);
    void redraw();
    bool setup(Call& c, std::size_t first, std::size_t count);
    bool setColour(Call& c, std::size_t first, std::size_t count, std::uint32_t object);

    void setFlag(const char* name, bool on) {
        if (const int id = game_.flagId(name); id >= 0) {
            game_.setFlag(id, on);
        }
    }

    void setErrorAlpha(float alpha) {
        // 0x1a803c: the message object's alpha at once.
        if (Object* o = game_.scene.object(errorHash_)) {
            // The sixth adventure also shows the message with it and hides
            // it with alpha 0 (vtable +0x0c, as 5005: 0x2ce7d4, 0x2cd730).
            if (game_.edition == Edition::Sixth) {
                o->visible = alpha != 0;
            }
            o->alpha.set({alpha});
        }
    }

    Game& game_;
    std::vector<Pair> pairs_;                   // +0x168
    std::uint32_t origin_ = 0;                  // +0x174: the point a line starts at
    Vec2 end_{0, 0};                            // +0x178: the drawn line's end
    int picked_ = -1;                           // +0x180
    float errorTimer_ = 0;                      // +0x184
    unsigned state_ = 0;                        // +0x188
    float lineWidth_ = 0;                       // +0x18c
    float snapRadius_ = 0;                      // +0x190
    unsigned maxPairs_ = 0;                     // +0x194
    float dotSize_ = -1;                        // +0x198 (-1: dots as wide as the lines)
    int joinSound_ = 0;                         // +0x19c
    int removeSound_ = 0;                       // +0x1a0
    int cancelSound_ = 0;                       // +0x1a4
    adventure56::Tween<2> shrinkA_;             // +0x1ac: one end of a shrinking line
    adventure56::Tween<2> shrinkB_;             // +0x214: the other
    int shared_ = -1;                           // +0x27c: 0 when a point takes one line
    float area_[4] = {0, 0, 320, 240};          // +0x280: where a line may go (7)
    unsigned required_ = 1;                     // +0x290: lines that turn the judge on
    std::uint32_t drawHash_ = 0;                // +0x294 "drw_name": picked / moving lines
    std::uint32_t fixedHash_ = 0;               // +0x298 "fix_name": the joined lines
    std::uint32_t errorHash_ = 0;               // "error_msg"
    bool dirty_ = false;                        // +0x29c
    bool noDots_ = false;                       // +0x2a0 (6)
    int maxLength_ = 0;                         // +0x2a4 (6; kept, not applied)
    bool lockRow_ = false, lockColumn_ = false; // +0x2a8 / +0x2ac (6; kept)
};

// 0x28d808 / 0x3462c4: the nearest shown object on the lower screen that
// shows nothing (ties: the later one), if within the snap radius.
std::uint32_t PairPuzzle::pointAt(Vec2 p) {
    const Object* best = nullptr;
    float bestDistance = 1000000.0f;
    for (const Object* o : game_.scene.screenList(1)) {
        if (!o->visible || o->content != 0) {
            continue;
        }
        const float dx = o->position[0] + o->offset[0] - p[0],
                    dy = o->position[1] + o->offset[1] - p[1];
        const float d = dx * dx + dy * dy;
        if (d <= bestDistance) {
            best = o;
            bestDistance = d;
        }
    }
    if (!best) {
        return 0;
    }
    const float dx = p[0] - (best->position[0] + best->offset[0]),
                dy = p[1] - (best->position[1] + best->offset[1]);
    return dx * dx + dy * dy <= snapRadius_ * snapRadius_ ? best->hash : 0;
}

// 0x1edd9c: the newest joined line passing within the snap radius of p
// (between its ends), or -1.
int PairPuzzle::lineAt(Vec2 p) {
    const float x = p[0], y = p[1];
    for (int i = int(pairs_.size()) - 1; i >= 0; --i) {
        const Vec2 a = positionOf(pairs_[std::size_t(i)].a),
                   b = positionOf(pairs_[std::size_t(i)].b);
        const float minX = a[0] > b[0] ? b[0] : a[0], maxX = a[0] < b[0] ? b[0] : a[0];
        const float minY = a[1] > b[1] ? b[1] : a[1], maxY = a[1] < b[1] ? b[1] : a[1];
        bool hit = false;
        if (a[0] == b[0]) {
            hit = std::fabs(a[0] - x) < snapRadius_ && minY < y && y < maxY;
        } else if (a[1] == b[1]) {
            hit = std::fabs(a[1] - y) < snapRadius_ && minX < x && x < maxX;
        } else {
            const float m = (a[1] - b[1]) / (a[0] - b[0]);
            const float c = (a[0] * b[1] - b[0] * a[1]) / (a[0] - b[0]);
            const float off = y - m * x - c;
            if ((off * off) / (m * m + 1.0f) <= snapRadius_ * snapRadius_) {
                const float n = -1.0f / m;
                const float fx = (y - n * x - c) / (m - n);
                const float fy = c + m * fx;
                hit = minX < fx && fx < maxX && minY < fy && fy < maxY;
            }
        }
        if (hit) {
            return i;
        }
    }
    return -1;
}

int PairPuzzle::findPair(std::uint32_t a, std::uint32_t b) const {
    for (int i = int(pairs_.size()) - 1; i >= 0; --i) {
        const Pair& p = pairs_[std::size_t(i)];
        if ((p.a == a && p.b == b) || (p.b == a && p.a == b)) {
            return i;
        }
    }
    return -1;
}

bool PairPuzzle::used(std::uint32_t point) const {
    for (const Pair& p : pairs_) {
        if (p.a == point || p.b == point) {
            return true;
        }
    }
    return false;
}

// 0x28d0ec: a joined line goes; its halves shrink to its middle.
void PairPuzzle::removePair(int index) {
    const Vec2 a = positionOf(pairs_[std::size_t(index)].a),
               b = positionOf(pairs_[std::size_t(index)].b);
    const Vec2 mid{(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f};
    shrinkA_.set(a);
    shrinkA_.start(mid, 7, 1);
    shrinkB_.set(b);
    shrinkB_.start(mid, 7, 1);
    pairs_.erase(pairs_.begin() + index);
    state_ |= kShrinking;
    game_.objectTouchLocked = true;
    game_.playEffect(removeSound_);
}

// All lines in use: the message, a scope with every object's touch off,
// the error sound, the puzzle's input off.
void PairPuzzle::showError() {
    errorTimer_ = 15;
    setErrorAlpha(1);
    state_ |= kError;
    game_.scene.openScope(true);
    game_.setEveryObject(true, false, ~0u);
    game_.playEffect(0x73);
    setFlag(names::inputOff, true);
}

// 0x1ec9dc
void PairPuzzle::press(bool inside, Vec2 p) {
    if (!inside) {
        return;
    }
    const std::uint32_t point = pointAt(p);
    dirty_ = true;
    if (point) {
        state_ |= kDrawing;
        origin_ = point;
        end_ = p;
        game_.playEffect(joinSound_);
        return;
    }
    if (const int line = lineAt(p); line >= 0) {
        picked_ = line;
        state_ |= kPicked;
    }
}

// 0x1ee9b8
void PairPuzzle::slide(bool inside, Vec2 p) {
    if (state_ & kDrawing) {
        if (!inside) {
            // 0x1ed788 / 0x3478e8: the line stops where it leaves the
            // area (the edges tested top, right, bottom, left; 0x260594).
            const Vec2 from = positionOf(origin_);
            const Vec2 edges[4][2] = {{{area_[0], area_[1]}, {area_[2], area_[1]}},
                                      {{area_[2], area_[1]}, {area_[2], area_[3]}},
                                      {{area_[2], area_[3]}, {area_[0], area_[3]}},
                                      {{area_[0], area_[3]}, {area_[0], area_[1]}}};
            for (const auto& e : edges) {
                const float rx = p[0] - from[0], ry = p[1] - from[1];
                const float sx = e[1][0] - e[0][0], sy = e[1][1] - e[0][1];
                const float den = rx * sy - sx * ry;
                if (den == 0.0f) {
                    continue;
                }
                const float wx = e[0][0] - from[0], wy = e[0][1] - from[1];
                const float u = (wx * ry - rx * wy) / den;
                const float t = (wx * sy - sx * wy) / den;
                if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
                    p = {from[0] + rx * t, from[1] + ry * t};
                    break;
                }
            }
        }
        dirty_ = true;
        end_ = p;
        const std::uint32_t point = pointAt(p);
        if (point && point != origin_) {
            const Object* a = game_.scene.object(origin_);
            const Object* b = game_.scene.object(point);
            if (!(a && b && groupOf(*a) == groupOf(*b))) {
                end_ = positionOf(point);
            }
        }
    } else if (state_ & kPicked) {
        if (lineAt(p) < 0) {
            picked_ = -1;
            state_ &= ~kPicked;
            dirty_ = true;
        }
    }
}

// 0x1ee2a4
void PairPuzzle::release(Vec2 p) {
    dirty_ = true;
    if (state_ & kDrawing) {
        const std::uint32_t target = pointAt(p);
        if (!target) {
            // Draw the line back to its start.
            const Vec2 from = positionOf(origin_);
            shrinkA_.set(from);
            shrinkA_.start(from, 7, 1);
            shrinkB_.set(end_);
            shrinkB_.start(from, 7, 1);
            origin_ = 0;
            state_ |= kShrinking;
            game_.objectTouchLocked = true;
            game_.playEffect(cancelSound_);
        } else if (target != origin_) {
            const int index = findPair(origin_, target);
            const Object* a = game_.scene.object(origin_);
            const Object* b = game_.scene.object(target);
            const bool sameGroup = a && b && groupOf(*a) == groupOf(*b);
            const bool conflict = sameGroup || (shared_ == 0 && (used(origin_) || used(target)));
            if (!conflict) {
                if (pairs_.size() < maxPairs_) {
                    if (index >= 0) {
                        removePair(index);
                    } else {
                        pairs_.push_back({origin_, target});
                        game_.playEffect(joinSound_);
                    }
                } else if (index < 0) {
                    showError();
                } else {
                    removePair(index);
                }
                origin_ = 0;
            } else if (index >= 0) {
                removePair(index);
            } else if (pairs_.size() == maxPairs_ && !sameGroup) {
                showError();
            } else {
                game_.playEffect(cancelSound_);
            }
        }
        state_ &= ~kDrawing;
    } else if (state_ & kPicked) {
        removePair(picked_);
        picked_ = -1;
        state_ &= ~kPicked;
    }
    if (required_ == pairs_.size()) {
        setFlag(names::judgeOff, false);
    } else if (required_ - 1 == pairs_.size()) {
        setFlag(names::judgeOff, true);
    }
}

// 0x1ed96c: the joined lines on "fix_name"; the picked line, the line
// being drawn and a shrinking line on "drw_name"; each with a dot at both
// ends.
void PairPuzzle::redraw() {
    Object* fixed = game_.scene.object(fixedHash_);
    Object* drawn = game_.scene.object(drawHash_);
    if (!drawHash_ || !fixedHash_ || !fixed) {
        return;
    }
    const float dot = dotSize_ != -1.0f ? dotSize_ * 0.5f : lineWidth_;
    auto line = [&](Object& o, Vec2 a, Vec2 b) {
        addLineQuad(o, a, b, lineWidth_);
        if (!noDots_) {
            addDotQuad(o, a, dot);
            addDotQuad(o, b, dot);
        }
    };
    fixed->quads.clear();
    bool havePicked = false;
    Vec2 pickedA{}, pickedB{};
    for (std::size_t i = 0; i < pairs_.size(); ++i) {
        const Vec2 a = positionOf(pairs_[i].a), b = positionOf(pairs_[i].b);
        if (picked_ == int(i)) {
            havePicked = true;
            pickedA = a;
            pickedB = b;
        }
        line(*fixed, a, b);
    }
    if (!drawn) {
        return;
    }
    drawn->quads.clear();
    if (havePicked) {
        line(*drawn, pickedA, pickedB);
    }
    if (state_ & kDrawing) {
        line(*drawn, positionOf(origin_), end_);
    }
    if (state_ & kShrinking) {
        line(*drawn, shrinkA_.value(), shrinkB_.value());
    }
}

// 0x1ed47c: after the touch dispatch.
void PairPuzzle::input() {
    if (game_.objectTouchLocked) {
        return;
    }
    const int inputOff = game_.flagId(names::inputOff);
    if (inputOff >= 0 && game_.flag(inputOff) && (state_ & kError)) {
        // The error message goes with a tap once its 15 frames are over.
        if (errorTimer_ > 0 || !game_.pressed()) {
            return;
        }
        setErrorAlpha(0);
        state_ &= ~kError;
        game_.setFlag(inputOff, false);
        game_.scene.closeScope();
        return;
    }
    if (inputOff >= 0 && game_.flag(inputOff)) {
        return;
    }
    const Vec2 p{float(std::int16_t(game_.touchX)), float(std::int16_t(game_.touchY))};
    // The area test (as the game makes it: both edges moved by the line
    // width).
    const bool inside = !(area_[0] - lineWidth_ > p[0]) && !(area_[2] - lineWidth_ < p[0]) &&
                        !(area_[1] - lineWidth_ > p[1]) && !(area_[3] - lineWidth_ < p[1]);
    if (game_.pressed()) {
        press(inside, p);
    } else if (game_.input.touching) {
        slide(inside, p);
    } else if (game_.released()) {
        release(p);
    }
}

// 0x1ec8e8: before the frame's scripts.
void PairPuzzle::update() {
    shrinkA_.step();
    shrinkB_.step();
    if (state_ & kShrinking) {
        dirty_ = true;
        if (!shrinkA_.moving() && !shrinkB_.moving()) {
            state_ &= ~kShrinking;
            shrinkA_.set({0, 0});
            shrinkB_.set({0, 0});
            game_.objectTouchLocked = false;
        }
    }
    if (state_ & kError) {
        errorTimer_ = std::max(0.0f, errorTimer_ - 1.0f);
    }
    if (dirty_) {
        redraw();
        dirty_ = false;
    }
}

// Command 0, 0x1ecf60: line width, snap radius, lines at most, join /
// remove / cancel sounds, whether points share lines, lines that turn the
// judge on [, dot size]; the line objects and the error message.
bool PairPuzzle::setup(Call& c, std::size_t first, std::size_t count) {
    if (count < 7) {
        return false;
    }
    lineWidth_ = c.real(first);
    snapRadius_ = c.real(first + 1);
    maxPairs_ = c.unsignedInt(first + 2);
    joinSound_ = c.integer(first + 3);
    removeSound_ = c.integer(first + 4);
    cancelSound_ = c.integer(first + 5);
    shared_ = c.integer(first + 6);
    required_ = 1;
    if (count >= 8 && c.integer(first + 7) >= 1) {
        required_ = std::min(unsigned(c.integer(first + 7)), maxPairs_);
    }
    if (count > 8) {
        dotSize_ = c.real(first + 8);
    }
    pairs_.clear();
    pairs_.reserve(maxPairs_);
    fixedHash_ = createQuadObject(game_, "fix_name").hash;
    drawHash_ = createQuadObject(game_, "drw_name").hash;
    // "error_msg": nazo_menu's "msg_line" at (160, 120), transparent, no
    // touch.
    Object& e = game_.createObject("error_msg", 1, 111, 0, {});
    errorHash_ = e.hash;
    game_.setAnimationSet(e, xq::hash("nazo_menu"));
    game_.playAnimation(e, Value::ofText("msg_line"), 0);
    e.position.set({160, 120, e.position[2]});
    e.alpha.set({0});
    e.touchOn = false;
    setFlag(names::judgeOff, true);
    c.result = Value::ofInt(1);
    return true;
}

// Commands 1 / 2 (0x1edfa0 / 0x1ee114): the drawing and the joined lines'
// colour (red, green, blue 0..255 cut to five bits [, alpha on]).
bool PairPuzzle::setColour(Call& c, std::size_t first, std::size_t count, std::uint32_t object) {
    if (count < 3) {
        return false;
    }
    auto channel = [&](std::size_t i) {
        return float(std::clamp(c.integer(first + i) >> 3, 0, 31)) / 31.0f;
    };
    const bool opaque = count < 4 || c.integer(first + 3) > 0;
    if (Object* o = game_.scene.object(object)) {
        o->colour.set({channel(0), channel(1), channel(2)});
        o->alpha.set({opaque ? 1.0f : 0.0f});
    }
    c.result = Value::ofInt(1);
    return true;
}

bool PairPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    auto name = [&](std::size_t i) {
        const Value& v = c.at(first + i);
        return v.isText() ? xq::hash(v.text) : v.toUint();
    };
    switch (number) {
    case 0:
        return setup(c, first, count);
    case 1:
        return setColour(c, first, count, drawHash_);
    case 2:
        return setColour(c, first, count, fixedHash_);
    case 3:
        // Every line gone.
        origin_ = 0;
        end_ = {0, 0};
        picked_ = -1;
        errorTimer_ = 0;
        state_ = 0;
        shrinkA_.set({0, 0});
        shrinkB_.set({0, 0});
        pairs_.clear();
        dirty_ = true;
        setErrorAlpha(0);
        setFlag(names::judgeOff, true);
        return result(1);
    case 4:
        // Whether that many lines are drawn.
        if (count == 0) {
            return false;
        }
        return result(pairs_.size() == c.unsignedInt(first) ? 1 : 0);
    case 5:
        // Whether two points are joined.
        if (count < 2) {
            return false;
        }
        return result(findPair(name(0), name(1)) >= 0 ? 1 : 0);
    case 6:
        // Switches: no end dots [, maximum length, row lock, column lock].
        if (count == 0) {
            return false;
        }
        noDots_ = c.at(first).truthy();
        if (count > 1) {
            maxLength_ = c.integer(first + 1);
        }
        if (count > 2) {
            lockRow_ = c.at(first + 2).truthy();
        }
        if (count > 3) {
            lockColumn_ = c.at(first + 3).truthy();
        }
        return result(1);
    case 7:
        // The area lines stay in (x0, y0, x1, y1).
        if (count < 4) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            area_[i] = c.real(first + std::size_t(i));
        }
        return result(1);
    case 8:
        // Sixth adventure only (0x2cd03c; the fifth's table ends at 7):
        // the number of lines drawn.
        if (game_.edition != Edition::Sixth) {
            return false;
        }
        return result(int(pairs_.size()));
    case 9: {
        // Sixth adventure only (0x2cd064): line i's two points (index,
        // register, register) into the registers; result 0.
        if (game_.edition != Edition::Sixth) {
            return false;
        }
        c.result = Value::ofInt(0);
        const int i = count >= 3 ? c.integer(first) : -1;
        if (i < 0 || i >= int(pairs_.size())) {
            return false;
        }
        const Pair& p = pairs_[std::size_t(i)];
        if (Value* r = c.reg(first + 1)) {
            *r = Value::ofInt(std::int32_t(p.a));
        }
        if (Value* r = c.reg(first + 2)) {
            *r = Value::ofInt(std::int32_t(p.b));
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makePairPuzzle(Game& game) {
    return std::make_unique<PairPuzzle>(game);
}

} // namespace picarat::adventure5
