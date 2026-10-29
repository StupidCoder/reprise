// The trampoline loop (overlay 27). See a4_trampoline.hpp.
//
// The simulation is 20.12 fixed point with the DS divider (quotients toward
// zero); the editing goes through single floats in places, as the original.
#include "puzzles/a4_trampoline.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include <cmath>

// Each float operation rounds on its own, as the game's soft-float calls do.
#pragma STDC FP_CONTRACT OFF

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 27's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay27 = 0x02106260;
constexpr std::uint32_t kLockedFlag = 0x0210bef0; // the "puzzle operation off" flag
constexpr std::uint32_t kCrash = 0x0210bcf7;      // "msg_crash"
constexpr std::uint32_t kCrashLook = 0x0210bcdc;  // its sequence
constexpr std::uint32_t kObject = 0x0210bce3;     // "bound_obj"
constexpr std::uint32_t kMenu = 0x0210bced;       // the message's animation
constexpr std::uint32_t kAfterJudge = 0x0210bd01; // block run on landing or crashing
constexpr std::uint32_t kAfterReset = 0x0210bd19; // block run when the drop is undone
constexpr std::int32_t kGravity = 0x333;          // per frame
constexpr std::int32_t kTrampolineBack = -0x1000; // 0210aeb0's first divisors
constexpr std::int32_t kObstacleBack = -0x2000;
constexpr std::int32_t kSmallStep = 0x28; // backing off stops below this (squared)
constexpr int kStuckFrames = 10;
constexpr int kBounceSoundGap = 7;
constexpr int kCrashFrames = 30;
constexpr int kPickFrames = 10;
constexpr int kBendFrames = 10;
constexpr std::int32_t kShrinkFrames = 0x7000;
constexpr std::int32_t kRightEdge = 200;
// The gauge: a bar up to 64 pixels long ending at x = 176, 4 high at y = 20.
constexpr std::int32_t kGaugeRight = 0xb0000, kGaugeTop = 0x14000;
constexpr std::int32_t kGaugeLength = 0x40000, kGaugeHeight = 0x4000;
constexpr std::uint16_t kPickedColour = 0xffff; // the old place of a picked one

std::int32_t fx(float v) {
    return std::int32_t(4096.0f * v);
}

// The DS divider, 64 / 32 bits, toward zero.
std::int32_t divide(std::int64_t n, std::int64_t d) {
    return d == 0 ? 0 : std::int32_t(n / d);
}

std::int32_t squared(std::int64_t x, std::int64_t y) {
    return std::int32_t((x * x + y * y) / 4096);
}

// t of the point nearest p on a..b (unclamped), through the divider.
std::int32_t along(std::array<std::int32_t, 2> p, std::array<std::int32_t, 2> a,
                   std::array<std::int32_t, 2> b) {
    const std::int64_t ex = std::int64_t(b[0]) - a[0], ey = std::int64_t(b[1]) - a[1];
    const std::int64_t dx = std::int64_t(p[0]) - a[0], dy = std::int64_t(p[1]) - a[1];
    return divide(ex * dx + dy * ey, squared(ex, ey));
}

std::array<std::int32_t, 2> pointAt(std::array<std::int32_t, 2> a, std::array<std::int32_t, 2> b,
                                    std::int32_t t) {
    return {a[0] + mulFx(std::int64_t(b[0]) - a[0], t), a[1] + mulFx(std::int64_t(b[1]) - a[1], t)};
}

#define OP(i) (i + 1)
} // namespace

TrampolineLoop::TrampolineLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {}

TrampolineLoop::~TrampolineLoop() {
    scene_.loopShapes.clear();
}

std::string TrampolineLoop::text(std::uint32_t address) const {
    return scene_.assets.codeText(27, kOverlay27, address);
}

void TrampolineLoop::showMessage(bool shown) {
    if (Chara* m = scene_.find(text(kCrash))) {
        m->alpha.set(shown ? kOne : 0);
    }
}

// ------------------------------------------------------------ commands

bool TrampolineLoop::command(gss::Call& c, int id) {
    // 02107270.
    const std::size_t n = c.count() - 1;
    switch (id) {
    case 0:
        // 0210736c {x, y, across, down, cell}.
        if (n < 5) {
            return false;
        }
        originX_ = s16(c.integer(OP(0)));
        originY_ = s16(c.integer(OP(1)));
        across_ = s16(c.integer(OP(2)));
        down_ = s16(c.integer(OP(3)));
        cell_ = s16(c.integer(OP(4)));
        c.result = Value::ofInt(1);
        return true;
    case 1:
        return setCatch(c);
    case 2:
        // 021075d0 {width, length}.
        if (n < 2) {
            return false;
        }
        width_ = fx(c.real(OP(0)));
        total_ = left_ = fx(c.real(OP(1)));
        c.result = Value::ofInt(1);
        return true;
    case 3:
        return setColours(c);
    case 4:
        // 021077cc {drawing, taking away, bounce, crash, caught}.
        if (n < 5) {
            return false;
        }
        for (std::size_t k = 0; k < 5; ++k) {
            sounds_[k] = c.integer(OP(k));
        }
        c.result = Value::ofInt(1);
        return true;
    case 5:
        // 02107858 {obstacles}: room for both lists.
        trampolines_.clear();
        trampolines_.reserve(std::size_t(cell_ == 0 ? total_ >> 15 : (total_ >> 12) / cell_));
        if (n > 0) {
            obstacleRoom_ = c.unsignedInt(OP(0));
            obstacles_.reserve(obstacleRoom_);
        }
        c.result = Value::ofInt(1);
        return true;
    case 6:
        return addObstacle(c);
    case 7:
        reset();
        c.result = Value::ofInt(1);
        return true;
    case 8:
        // 02107df8: the drop starts from where the object is.
        if (object_) {
            position_ = object_->position.value();
        }
        speed_ = {0, 0};
        stuck_ = 0;
        flags_ |= Falling;
        c.result = Value::ofInt(1);
        return true;
    case 9:
        // 02107e78: the drop is undone.
        if (flags_ & Falling) {
            flags_ &= ~unsigned(Falling);
            position_ = speed_ = {0, 0};
            stuck_ = 0;
            showMessage(false);
            callback(0, text(kAfterReset), {});
        }
        c.result = Value::ofInt(1);
        return true;
    default:
        return Loop::command(c, id);
    }
}

bool TrampolineLoop::setCatch(gss::Call& c) {
    // 0210740c {catcher x1, y1, x2, y2, object radius, bounciness, ground}.
    if (c.count() - 1 < 7) {
        return false;
    }
    catchA_ = {fx(c.real(OP(0))), fx(c.real(OP(1)))};
    catchB_ = {fx(c.real(OP(2))), fx(c.real(OP(3)))};
    radius_ = fx(c.real(OP(4)));
    bounciness_ = fx(c.real(OP(5)));
    ground_ = fx(c.real(OP(6)));
    object_ = scene_.find(text(kObject));
    // 02108f18: the crash message, hidden.
    if (Chara* m = scene_.create(text(kCrash), 15, {})) {
        attach(scene_, *m, text(kMenu), 5);
        play(*m, text(kCrashLook));
        m->alpha.set(0);
    }
    c.result = Value::ofInt(1);
    return true;
}

bool TrampolineLoop::setColours(gss::Call& c) {
    // 02107658 {4 × red, green, blue}: drawing, trampolines, (unused), too
    // long or crossing.
    if (c.count() - 1 < 12) {
        return false;
    }
    for (std::size_t k = 0; k < 4; ++k) {
        auto channel = [&](std::size_t i) {
            return std::uint16_t(std::clamp(c.integer(OP(k * 3 + i)) >> 3, 0, 31));
        };
        colours_[k] = std::uint16_t(channel(0) | channel(1) << 5 | channel(2) << 10);
    }
    c.result = Value::ofInt(1);
    return true;
}

bool TrampolineLoop::addObstacle(gss::Call& c) {
    // 02107a28 {x1, y1, x2, y2} in cells.
    if (c.count() - 1 < 4 || obstacleRoom_ == 0 || obstacles_.size() == obstacleRoom_) {
        return false;
    }
    auto point = [&](std::size_t i) {
        const float y = float(originY_) + float(cell_) * c.real(OP(i + 1));
        const float x = float(originX_) + float(cell_) * c.real(OP(i));
        return dotAt({fx(x), fx(y)});
    };
    Segment s;
    s.a = point(0);
    s.b = point(2);
    obstacles_.push_back(s);
    c.result = Value::ofInt(1);
    return true;
}

void TrampolineLoop::reset() {
    // 02107cb0.
    from_ = to_ = {0, 0};
    crashWait_ = pickWait_ = 0;
    flags_ = 0;
    left_ = total_;
    drawing_ = shrinkingLength_ = 0;
    shrinkA_.set(shrinkA_.value());
    shrinkB_.set(shrinkB_.value());
    position_ = speed_ = {0, 0};
    stuck_ = 0;
    trampolines_.clear();
    showMessage(false);
}

// ------------------------------------------------------------ the grid

int TrampolineLoop::dotAt(Point p) const {
    // 021099a4: the nearest dot.
    const int x = p[0] / kOne - originX_ + (cell_ >> 1);
    const int y = p[1] / kOne - originY_ + (cell_ >> 1);
    if (x < 0 || y < 0 || cell_ == 0) {
        return -1;
    }
    const int col = x / cell_, row = y / cell_;
    if (col > across_ || row > down_) {
        return -1;
    }
    return (across_ + 1) * row + col;
}

TrampolineLoop::Point TrampolineLoop::dotPoint(int dot) const {
    // 02109918.
    const int row = dot / (across_ + 1), col = dot % (across_ + 1);
    return {fx(float(col * cell_ + originX_)), fx(float(row * cell_ + originY_))};
}

std::int32_t TrampolineLoop::length(Point a, Point b) const {
    // Across plus down, in floats (021096bc and others).
    const float dx = float(std::int64_t(b[0]) - a[0]) / 4096.0f;
    const float dy = float(std::int64_t(b[1]) - a[1]) / 4096.0f;
    return fx(std::fabs(dx) + std::fabs(dy));
}

int TrampolineLoop::endAt(int x, int y) const {
    // 021090b0: on an end (for a trampoline one cell long, near the end).
    if (trampolines_.empty()) {
        return -1;
    }
    const Point p{fx(float(x)), fx(float(y))};
    const Point g = dotPoint(dotAt(p));
    const int cell2 = cell_ * cell_;
    for (int i = int(trampolines_.size()) - 1; i >= 0; --i) {
        const Point a = dotPoint(trampolines_[std::size_t(i)].a);
        const Point b = dotPoint(trampolines_[std::size_t(i)].b);
        if (g != a && g != b) {
            continue;
        }
        const int len = squared(std::int64_t(a[0]) - b[0], std::int64_t(a[1]) - b[1]) / kOne;
        auto nearEnd = [&](std::size_t u) {
            return p[u] < std::min(a[u], b[u]) + kOne || std::max(a[u], b[u]) - kOne < p[u];
        };
        if (len == cell2) {
            if (nearEnd(a[0] == b[0] ? 1 : 0)) {
                return i;
            }
        } else if (len != cell2 * 2 || (nearEnd(0) && nearEnd(1))) {
            return i;
        }
    }
    return -1;
}

int TrampolineLoop::bodyAt(int x, int y) const {
    // 0210947c: within half a cell of a trampoline.
    const Point p{fx(float(x)), fx(float(y))};
    const std::int32_t reach = fx(float(cell_) / 1.99f);
    for (int i = int(trampolines_.size()) - 1; i >= 0; --i) {
        const Segment& s = trampolines_[std::size_t(i)];
        const Point a = dotPoint(s.a), b = dotPoint(s.b);
        // 0210abfc with the reach for the radius.
        const Point foot = pointAt(a, b, std::clamp(along(p, a, b), 0, kOne));
        if (squared(std::int64_t(p[0]) - foot[0], std::int64_t(p[1]) - foot[1]) <
            mulFx(reach, reach)) {
            return i;
        }
    }
    return -1;
}

bool TrampolineLoop::crosses(std::array<int, 2> a, std::array<int, 2> b,
                             const std::vector<Segment>& list) const {
    // 02109ce0, in whole pixels.
    for (int i = int(list.size()) - 1; i >= 0; --i) {
        const Point pc = dotPoint(list[std::size_t(i)].a), pd = dotPoint(list[std::size_t(i)].b);
        const int cx = pc[0] / kOne, cy = pc[1] / kOne, dx = pd[0] / kOne, dy = pd[1] / kOne;
        const int d1 = (cx - a[0]) * (dy - a[1]) - (cy - a[1]) * (dx - a[0]);
        const int d2 = (cx - b[0]) * (dy - b[1]) - (cy - b[1]) * (dx - b[0]);
        auto meets = [](int u, int v, int d) {
            if (u * d < 0 || v * d < 0) {
                return false;
            }
            return float(u + v) / float(d) >= 1.0f;
        };
        if (d1 != 0) {
            const int u = (dy - a[1]) * (b[0] - a[0]) - (dx - a[0]) * (b[1] - a[1]);
            const int v = (cx - a[0]) * (b[1] - a[1]) - (cy - a[1]) * (b[0] - a[0]);
            if (meets(u, v, d1)) {
                return true;
            }
        } else if (d2 != 0) {
            const int u = (dy - b[1]) * (a[0] - b[0]) - (dx - b[0]) * (a[1] - b[1]);
            const int v = (cx - b[0]) * (a[1] - b[1]) - (cy - b[1]) * (a[0] - b[0]);
            if (meets(u, v, d2)) {
                return true;
            }
        } else {
            // On one line: overlapping unless they only meet end to end.
            const int ta = (cy - a[1]) * (dy - a[1]) + (cx - a[0]) * (dx - a[0]);
            const int tb = (cy - b[1]) * (dy - b[1]) + (cx - b[0]) * (dx - b[0]);
            if (ta <= 0 || tb <= 0) {
                return true;
            }
            if ((a[1] - cy) * (b[1] - cy) + (a[0] - cx) * (b[0] - cx) <= 0) {
                return true;
            }
        }
    }
    return false;
}

bool TrampolineLoop::crosses(Point a, Point b) const {
    const std::array<int, 2> pa{a[0] / kOne, a[1] / kOne}, pb{b[0] / kOne, b[1] / kOne};
    return crosses(pa, pb, trampolines_) || crosses(pa, pb, obstacles_);
}

TrampolineLoop::Point TrampolineLoop::drawnEnd(Point pen, bool inside) const {
    // 02109a68: past the drawing area's right edge, along the line from the
    // fixed end; then onto the nearest dot.
    Point p = pen;
    if (!inside) {
        const std::int32_t edge = 0xc8000 - width_;
        const std::int64_t ax = from_[0], ay = from_[1];
        const std::int32_t n = mulFx(ay - pen[1], edge) + mulFx(pen[1], ax) - mulFx(pen[0], ay);
        p = {edge, divide(std::int64_t(n) * 4096, ax - pen[0])};
    }
    const int dot = dotAt(p);
    return dot >= 0 ? dotPoint(dot) : p;
}

// ------------------------------------------------------------ the pen

void TrampolineLoop::pickUp(int index, int end) {
    // 021096bc.
    const Segment s = trampolines_[std::size_t(index)];
    from_ = dotPoint(end == 0 ? s.a : s.b);
    to_ = dotPoint(end == 0 ? s.b : s.a);
    savedFrom_ = from_;
    savedTo_ = to_;
    const std::int32_t len = length(from_, to_);
    left_ += len;
    drawing_ = len;
    trampolines_.erase(trampolines_.begin() + index);
    pickWait_ = kPickFrames;
}

void TrampolineLoop::add(Point a, Point b, int sound) {
    // 021095ec.
    const int da = dotAt(a), db = dotAt(b);
    Segment s;
    s.a = std::min(da, db);
    s.b = std::max(da, db);
    trampolines_.push_back(s);
    left_ -= drawing_;
    drawing_ = 0;
    services_.effect(sound);
}

void TrampolineLoop::shrinkAway(Point a, Point b) {
    // 021097fc: both ends to the middle.
    const Point middle{std::int32_t(float(std::int64_t(a[0]) + b[0]) / 2.0f),
                       std::int32_t(float(std::int64_t(a[1]) + b[1]) / 2.0f)};
    shrinkA_.set(a);
    shrinkA_.move(middle, kShrinkFrames, 1);
    shrinkB_.set(b);
    shrinkB_.move(middle, kShrinkFrames, 1);
    flags_ |= Shrinking;
    services_.effect(sounds_[1]);
}

void TrampolineLoop::shrinkBack() {
    // 02109f68: the drawn end back to the fixed one.
    const std::int32_t frames = from_ == to_ ? 0 : kShrinkFrames;
    shrinkA_.set(from_);
    shrinkA_.move(from_, frames, 1);
    shrinkB_.set(to_);
    shrinkB_.move(from_, frames, 1);
    from_ = {0, 0};
    shrinkingLength_ = drawing_;
    drawing_ = 0;
    flags_ |= Shrinking;
    services_.effect(sounds_[1]);
}

void TrampolineLoop::press(int x, int y, bool inside) {
    // 02107f54: an end of a trampoline, its body, or a dot.
    if (!inside) {
        return;
    }
    const int dot = dotAt({fx(float(x)), fx(float(y))});
    const Point g = dotPoint(dot);
    if (const int e = endAt(x, y); e >= 0) {
        const int end = dotPoint(trampolines_[std::size_t(e)].a) == g ? 1 : 0;
        pickUp(e, end);
        flags_ |= Drawing | Unchanged | Picked;
        return;
    }
    if (const int b = bodyAt(x, y); b >= 0) {
        pickUp(b, 0);
        flags_ |= Moving | Unchanged | Picked;
        return;
    }
    if (dot < 0) {
        return;
    }
    from_ = to_ = g;
    services_.effect(sounds_[0]);
    flags_ |= Drawing | Invalid;
}

void TrampolineLoop::drag(int x, int y, bool inside) {
    // 02108138.
    if (flags_ & Moving) {
        // By whole cells from where the pen went down.
        const float c = float(cell_), half = float(cell_ >> 1);
        auto snap = [&](int d) {
            return c * float(std::floor(double((float(d) + half) / c)));
        };
        const float oy = snap(y - services_.touch.startY);
        const float ox = snap(x - services_.touch.startX);
        const Point off{fx(ox), fx(oy)};
        from_ = {savedFrom_[0] + off[0], savedFrom_[1] + off[1]};
        to_ = {savedTo_[0] + off[0], savedTo_[1] + off[1]};
        const bool bad = crosses(from_, to_) || dotAt(from_) < 0 || dotAt(to_) < 0;
        flags_ = bad ? flags_ | Invalid : flags_ & ~unsigned(Invalid);
        if (flags_ & Unchanged) {
            if (from_ == savedFrom_ && pickWait_ > 0) {
                return;
            }
            services_.effect(sounds_[0]);
            flags_ &= ~unsigned(Unchanged);
        }
        return;
    }
    if (!(flags_ & Drawing)) {
        return;
    }
    const Point before = to_;
    to_ = drawnEnd({fx(float(x)), fx(float(y))}, inside);
    drawing_ = length(from_, to_);
    const bool good = drawing_ <= left_ && !crosses(from_, to_) && dotAt(to_) >= 0 && from_ != to_;
    flags_ = good ? flags_ & ~unsigned(Invalid) : flags_ | Invalid;
    if (flags_ & Unchanged) {
        if (to_ == before && pickWait_ > 0) {
            return;
        }
        services_.effect(sounds_[0]);
        flags_ &= ~unsigned(Unchanged);
    }
}

void TrampolineLoop::release() {
    // 0210867c.
    if (trampolines_.capacity() == 0) {
        return;
    }
    if (flags_ & Unchanged) {
        // Tapped without moving: taken away.
        shrinkAway(from_, to_);
        flags_ &= ~unsigned(Drawing | Moving | Unchanged | Picked);
        shrinkingLength_ = drawing_;
        drawing_ = 0;
        pickWait_ = 0;
        return;
    }
    if (flags_ & Moving) {
        if (flags_ & Invalid) {
            add(savedFrom_, savedTo_, sounds_[1]);
        } else {
            add(from_, to_, sounds_[0]);
        }
        flags_ &= ~unsigned(Moving | Invalid | Picked);
        return;
    }
    if (!(flags_ & Drawing)) {
        return;
    }
    if (!(flags_ & Invalid)) {
        add(from_, to_, sounds_[0]);
    } else if (!(flags_ & Picked)) {
        shrinkBack();
    } else {
        drawing_ = length(savedFrom_, savedTo_);
        add(savedFrom_, savedTo_, sounds_[1]);
    }
    flags_ &= ~unsigned(Drawing | Invalid | Picked);
}

void TrampolineLoop::afterPen() {
    // 021068e0.
    if (flags_ & Falling) {
        return;
    }
    if (services_.data.flags[text(kLockedFlag) + "#0"] != 0) {
        return;
    }
    const auto& t = services_.touch;
    if (flags_ & Crashed) {
        if (crashWait_ > 0) {
            return;
        }
        if (t.state == 1) {
            showMessage(false);
            callback(0, text(kAfterReset), {});
            flags_ &= ~unsigned(Crashed);
        }
        return;
    }
    if ((flags_ & Shrinking) && !shrinkA_.moving()) {
        flags_ &= ~unsigned(Shrinking);
    }
    const bool inside = t.x <= kRightEdge - width_ / kOne;
    switch (t.state) {
    case 1:
        press(t.x, t.y, inside);
        break;
    case 2:
        drag(t.x, t.y, inside);
        break;
    case 3:
        release();
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------ simulation

bool TrampolineLoop::touches(Point p, Point a, Point b) const {
    // 0210abfc.
    const Point foot = pointAt(a, b, std::clamp(along(p, a, b), 0, kOne));
    return squared(std::int64_t(p[0]) - foot[0], std::int64_t(p[1]) - foot[1]) <
           mulFx(radius_, radius_);
}

void TrampolineLoop::backOff(Point a, Point b, std::int32_t divisor) {
    // 0210aeb0: steps of speed / divisor, halved each time, back while in
    // contact and forward while not.
    for (;;) {
        const Point step{divide(std::int64_t(speed_[0]) * 4096, divisor),
                         divide(std::int64_t(speed_[1]) * 4096, divisor)};
        if (squared(step[0], step[1]) < kSmallStep) {
            return;
        }
        position_ = {position_[0] + step[0], position_[1] + step[1]};
        const std::int32_t size = std::abs(divisor);
        divisor = touches(position_, a, b) ? mulFx(-size, 0x2000) : mulFx(size, 0x2000);
    }
}

bool TrampolineLoop::bounce(Point a, Point b, Segment* segment) {
    // 0210b178: off the segment's normal (the nearest point's direction, or
    // straight across for a level or upright segment hit between its ends).
    const std::int32_t t = along(position_, a, b);
    Point normal;
    if (t < 0 || t > kOne || (a[0] != b[0] && a[1] != b[1])) {
        const Point foot = pointAt(a, b, std::clamp(t, 0, kOne));
        normal = {position_[0] - foot[0], position_[1] - foot[1]};
    } else if (a[0] != b[0]) {
        normal = {0, a[1] < position_[1] ? kOne : -kOne};
    } else {
        normal = {a[0] < position_[0] ? kOne : -kOne, 0};
    }
    if (segment && t > 0 && t < kOne) {
        segment->where = fx(float(t) / 4096.0f);
        segment->frames = kBendFrames;
        segment->speed = speed_;
    }
    const std::int32_t dot = std::int32_t(
        (-std::int64_t(speed_[0]) * normal[0] + std::int64_t(normal[1]) * -speed_[1]) / 4096);
    const Point projected{mulFx(dot, normal[0]), mulFx(dot, normal[1])};
    const std::int32_t inverse = divide(0x1000000, squared(normal[0], normal[1]));
    const Point push{mulFx(mulFx(inverse, projected[0]), 0x2000),
                     mulFx(mulFx(inverse, projected[1]), 0x2000)};
    speed_ = {mulFx(bounciness_, speed_[0] + push[0]), mulFx(bounciness_, speed_[1] + push[1])};
    if (speed_[1] < 0) {
        const std::int32_t slowest = fx(std::pow(0.4f, 2.0f));
        if (squared(speed_[0], speed_[1]) < slowest) {
            return true;
        }
    }
    return false;
}

int TrampolineLoop::bounceOffTrampolines(int& result) {
    // 0210a370.
    ++sinceBounce_;
    bool hit = false;
    for (int i = int(trampolines_.size()) - 1; i >= 0; --i) {
        Segment& s = trampolines_[std::size_t(i)];
        const Point a = dotPoint(s.a), b = dotPoint(s.b);
        if (!touches(position_, a, b)) {
            continue;
        }
        backOff(a, b, kTrampolineBack);
        hit = true;
        if (sinceBounce_ > kBounceSoundGap) {
            services_.effect(sounds_[2]);
        }
        sinceBounce_ = 0;
        if (bounce(a, b, &s)) {
            result = 3;
        }
    }
    stuck_ = hit ? stuck_ + 1 : 0;
    if (stuck_ > kStuckFrames) {
        result = 3;
    }
    return hit;
}

bool TrampolineLoop::hitObstacle() {
    // 0210a258.
    bool hit = false;
    for (int i = int(obstacles_.size()) - 1; i >= 0; --i) {
        const Point a = dotPoint(obstacles_[std::size_t(i)].a);
        const Point b = dotPoint(obstacles_[std::size_t(i)].b);
        if (touches(position_, a, b)) {
            hit = true;
            backOff(a, b, kObstacleBack);
        }
    }
    if (hit) {
        services_.effect(sounds_[3]);
    }
    return hit;
}

int TrampolineLoop::catchCheck() {
    // 0210a518: falling onto the catcher's arms between their ends catches;
    // their tips throw it off once.
    if (speed_[1] < 1) {
        return 0;
    }
    const Point foot =
        pointAt(catchA_, catchB_, std::clamp(along(position_, catchA_, catchB_), 0, kOne));
    if (mulFx(radius_, radius_) <
        squared(std::int64_t(position_[0]) - foot[0], std::int64_t(position_[1]) - foot[1])) {
        touchingCatcher_ = false;
        return 0;
    }
    const Point saved = position_;
    backOff(catchA_, catchB_, kTrampolineBack);
    const std::int32_t above = position_[1] - speed_[1] + radius_;
    const std::int32_t t = along(position_, catchA_, catchB_);
    position_ = saved;
    if (speed_[0] > 0 && t > 0x800 && foot[1] < above) {
        return 0;
    }
    if (t > -0xcc && t < 0x10cc) {
        if (above <= foot[1]) {
            backOff(catchA_, catchB_, kTrampolineBack);
            services_.effect(sounds_[4]);
            return 4;
        }
        return 0;
    }
    if (!touchingCatcher_) {
        touchingCatcher_ = true;
        backOff(catchA_, catchB_, kTrampolineBack);
        bounce(catchA_, catchB_, nullptr);
        services_.effect(sounds_[2]);
    }
    return 0;
}

int TrampolineLoop::step() {
    // 0210a07c.
    speed_[1] += kGravity;
    position_ = {position_[0] + speed_[0], position_[1] + speed_[1]};
    int result = 0;
    if (bounceOffTrampolines(result)) {
        return result;
    }
    if (ground_ - radius_ < position_[1]) {
        position_[1] = ground_ - radius_;
        services_.effect(sounds_[3]);
        return 1;
    }
    const std::int32_t side = mulFx(radius_, 0x2000);
    if (position_[0] < -side || position_[0] > side + 0x100000) {
        return 2;
    }
    if (hitObstacle()) {
        return 1;
    }
    return catchCheck();
}

void TrampolineLoop::afterUpdate() {
    // 02106ac0.
    if (flags_ & Shrinking) {
        shrinkA_.update(kOne);
        shrinkB_.update(kOne);
        shrinkingLength_ = length(shrinkB_.value(), shrinkA_.value());
        return;
    }
    if (flags_ & Unchanged) {
        pickWait_ = std::max(0, pickWait_ - 1);
        return;
    }
    if (flags_ & Crashed) {
        crashWait_ = std::max(0, crashWait_ - 1);
        return;
    }
    if (!(flags_ & Falling)) {
        return;
    }
    const int result = step();
    if (object_) {
        object_->position.set(position_);
    }
    if (result >= 1 && result <= 3) {
        flags_ = (flags_ & ~unsigned(Falling)) | Crashed;
        showMessage(true);
        crashWait_ = kCrashFrames;
        if (result == 1) {
            callback(0, text(kAfterJudge), std::array<Value, 1>{Value::ofInt(0)});
        }
    } else if (result == 4) {
        flags_ &= ~unsigned(Falling);
        callback(0, text(kAfterJudge), std::array<Value, 1>{Value::ofInt(1)});
    }
}

// ------------------------------------------------------------ drawing

void TrampolineLoop::endFrame() {
    // 02106d58 (drawn this frame).
    auto& shapes = scene_.loopShapes;
    shapes.clear();
    const std::int32_t side = mulFx(width_, 0x2000);
    auto dots = [&](Point a, Point b, std::uint16_t colour) {
        shapes.push_back({true, {a[0] - width_, a[1] - width_}, {side, side}, 0, colour});
        shapes.push_back({true, {b[0] - width_, b[1] - width_}, {side, side}, 0, colour});
    };
    auto line = [&](Point a, Point b, std::uint16_t colour) {
        shapes.push_back({false, a, b, width_, colour});
        dots(a, b, colour);
    };
    // 021088fc: the trampolines, bending after a bounce.
    for (Segment& s : trampolines_) {
        const Point a = dotPoint(s.a), b = dotPoint(s.b);
        if (s.frames < 1) {
            line(a, b, colours_[1]);
            continue;
        }
        const std::int32_t speed = std::min(sqrtFx(squared(s.speed[0], s.speed[1])), 0x5000);
        const std::int32_t phase = fx(float(s.frames));
        std::int32_t dip;
        if (phase <= 0x4000) {
            dip = divide(std::int64_t(mulFx(phase - 0x4000, phase)) * 4096, 0x8000);
        } else {
            dip = divide(std::int64_t(mulFx(phase - 0xa000, -(phase - 0x4000))) * 4096, 0x9000);
        }
        const std::int32_t depth = mulFx(speed, dip);
        const Point hit = pointAt(a, b, s.where);
        const std::int32_t inverse = inverseSqrtFx(squared(s.speed[0], s.speed[1]));
        const Point unit{mulFx(inverse, s.speed[0]), mulFx(inverse, s.speed[1])};
        const Point bend{hit[0] + mulFx(depth, unit[0]), hit[1] + mulFx(depth, unit[1])};
        shapes.push_back({false, a, bend, width_, colours_[1]});
        shapes.push_back({false, bend, b, width_, colours_[1]});
        dots(a, b, colours_[1]);
        --s.frames;
    }
    if (flags_ & Picked) {
        line(savedFrom_, savedTo_, kPickedColour);
    }
    if (flags_ & (Drawing | Moving)) {
        line(from_, to_, (flags_ & Invalid) ? colours_[3] : colours_[0]);
    }
    if (flags_ & Shrinking) {
        line(shrinkA_.value(), shrinkB_.value(), colours_[0]);
    }
    // The gauge of the length left.
    const std::int32_t rest = left_ - drawing_ - shrinkingLength_;
    if (rest > 0) {
        const std::int32_t part = divide(std::int64_t(rest) * 4096, total_);
        const std::int32_t size = mulFx(part, kGaugeLength);
        shapes.push_back(
            {true, {kGaugeRight - size, kGaugeTop}, {size, kGaugeHeight}, 0, colours_[0]});
    }
}

} // namespace picarat::adventure4
