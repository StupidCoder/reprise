// The line-drawing loop (overlay 19). See a4_line.hpp.
//
// The original works in single-precision floats (pixels) for the pen and in
// 20.12 fixed point for the places; the same steps are kept here (float
// arithmetic, truncating conversions) so lines end where the game's do.
#include "puzzles/a4_line.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include <algorithm>
#include <cmath>

// Each float operation rounds on its own, as the game's soft-float calls do.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif // GCC uses the target-wide -ffp-contract=off.

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 19's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay19 = 0x02106260;
constexpr std::uint32_t kError = 0x02108b68;          // "err"
constexpr std::uint32_t kErrorAnimation = 0x02108b6c; // its animation
constexpr std::uint32_t kErrorLook = 0x02108b76;      // its sequence
constexpr std::uint32_t kLockedFlag = 0x02108d4c;     // the "puzzle operation off" flag
constexpr std::uint32_t kNoLinesFlag = 0x02108d60;    // set while no line is drawn
constexpr int kErrorKind = 15, kErrorPriority = 5;
constexpr std::int32_t kErrorFrames = 0xf000;  // 15 frames before a tap hides it
constexpr std::int32_t kShrinkFrames = 0x7000; // a line shrinking away
constexpr int kShrinkCurve = 1;
// The drawing area ends this far right (less the line width): the buttons.
constexpr float kRightEdge = 200.0f;

// 02029318: float to integer, toward zero.
std::int32_t fx(float v) {
    return std::int32_t(4096.0f * v);
}

float pixels(std::int32_t v) {
    return float(v) / 4096.0f;
}

// The length of a 20.12 vector in whole pixels (toward zero).
int lengthPixels(std::int32_t dx, std::int32_t dy) {
    const std::int64_t sum = std::int64_t(dx) * dx + std::int64_t(dy) * dy;
    return sqrtFx(sum >> 12) / 4096;
}

#define OP(i) (i + 1)
} // namespace

LineLoop::LineLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {}

LineLoop::~LineLoop() {
    scene_.loopShapes.clear();
}

std::string LineLoop::text(std::uint32_t address) const {
    return scene_.assets.codeText(19, kOverlay19, address);
}

bool LineLoop::command(gss::Call& c, int id) {
    // 02106e2c.
    switch (id) {
    case 0:
        return setup(c);
    case 1:
        return setColour(c, current_);
    case 2:
        return setColour(c, other_);
    case 3:
        clear();
        c.result = Value::ofInt(1);
        return true;
    case 4:
        // 02107434 {n}: are there n lines.
        if (c.count() - 1 == 0) {
            return false;
        }
        c.result = Value::ofInt(lines_.size() == c.unsignedInt(OP(0)) ? 1 : 0);
        return true;
    case 5:
        // 02107478 {dot, dot}: is there a line between them.
        if (c.count() - 1 < 2) {
            return false;
        }
        c.result = Value::ofInt(
            find(nameHash(kit::text(c, OP(0))), nameHash(kit::text(c, OP(1)))) >= 0 ? 1 : 0);
        return true;
    case 6:
        return options(c);
    default:
        return Loop::command(c, id);
    }
}

bool LineLoop::setup(gss::Call& c) {
    // 02106eec {width, reach, most lines, drawing sound, line sound, rub-out
    // sound}.
    if (c.count() - 1 < 6) {
        return false;
    }
    width_ = c.real(OP(0));
    reach_ = c.real(OP(1));
    maxLines_ = c.unsignedInt(OP(2));
    drawSound_ = c.integer(OP(3));
    addSound_ = c.integer(OP(4));
    removeSound_ = c.integer(OP(5));
    lines_.clear();
    // 02107cbc: the "too many lines" message, hidden.
    if (Chara* o = scene_.create(text(kError), kErrorKind, {})) {
        attach(scene_, *o, text(kErrorAnimation), kErrorPriority);
        play(*o, text(kErrorLook));
        o->alpha.set(0);
    }
    noLinesFlag(true);
    if (noEdge_) {
        // The outer edge of the dots on the screen.
        const std::array<std::int32_t, 2> at{0, 0}, size{256 * kOne, 192 * kOne};
        const int n = scene_.placesInside(at, size);
        for (int i = 0; i < n; ++i) {
            const Place* p = scene_.placeInside(at, size, i);
            if (!p) {
                break;
            }
            const int x = p->position[0] / kOne, y = p->position[1] / kOne;
            if (i == 0) {
                bounds_ = {x, y, x, y};
                continue;
            }
            if (x < bounds_[0]) {
                bounds_[0] = x;
            } else if (bounds_[2] < x) {
                bounds_[2] = x;
            }
            if (y < bounds_[1]) {
                bounds_[1] = y;
            } else if (bounds_[3] < y) {
                bounds_[3] = y;
            }
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

bool LineLoop::setColour(gss::Call& c, std::uint16_t& colour) {
    // 021071b8 / 021072a0 {red, green, blue, [flag]}: 0..255 each.
    if (c.count() - 1 < 3) {
        return false;
    }
    auto channel = [&](std::size_t i) {
        return std::uint16_t(std::clamp(c.integer(OP(i)) >> 3, 0, 31));
    };
    const int flag = c.count() - 1 >= 4 ? c.integer(OP(3)) : 1;
    colour =
        std::uint16_t(channel(0) | channel(1) << 5 | channel(2) << 10 | (flag > 0 ? 0x8000 : 0));
    c.result = Value::ofInt(1);
    return true;
}

bool LineLoop::options(gss::Call& c) {
    // 021074f4 {no dots, longest line, keep y, keep x, not along the edge}.
    const std::size_t n = c.count() - 1;
    if (n == 0) {
        return false;
    }
    noDots_ = c.integer(OP(0)) != 0;
    if (n > 1) {
        maxLength_ = c.integer(OP(1));
    }
    if (n > 2) {
        keepY_ = c.integer(OP(2)) != 0;
    }
    if (n > 3) {
        keepX_ = c.integer(OP(3)) != 0;
    }
    if (n > 4) {
        noEdge_ = c.integer(OP(4)) != 0;
    }
    c.result = Value::ofInt(1);
    return true;
}

void LineLoop::clear() {
    // 0210738c.
    start_ = 0;
    endX_ = endY_ = 0;
    chosen_ = -1;
    errorTime_ = 0;
    drawing_ = choosing_ = error_ = shrinking_ = false;
    shrinkA_.set(shrinkA_.value());
    shrinkB_.set(shrinkB_.value());
    lines_.clear();
    showError(false);
    noLinesFlag(true);
}

void LineLoop::showError(bool shown) {
    // 02107d50: the message's alpha.
    if (Chara* o = scene_.find(text(kError))) {
        o->alpha.set(shown ? kOne : 0);
    }
}

void LineLoop::noLinesFlag(bool on) {
    services_.data.flags[text(kNoLinesFlag) + "#0"] = on ? 1 : 0;
}

std::array<std::int32_t, 2> LineLoop::where(std::uint32_t place) const {
    const Place* p = scene_.findPlace(place);
    return p ? p->position : std::array<std::int32_t, 2>{};
}

std::uint32_t LineLoop::placeAt(float x, float y) const {
    const Place* p = scene_.nearestPlace({fx(x), fx(y)}, fx(reach_));
    return p ? p->hash : 0;
}

bool LineLoop::tooLong(std::uint32_t a, std::uint32_t b) const {
    const auto pa = where(a), pb = where(b);
    return maxLength_ > 0 && maxLength_ < lengthPixels(pb[0] - pa[0], pb[1] - pa[1]);
}

int LineLoop::find(std::uint32_t a, std::uint32_t b) const {
    for (int i = int(lines_.size()) - 1; i >= 0; --i) {
        const Line& l = lines_[std::size_t(i)];
        if ((l.a == a && l.b == b) || (l.a == b && l.b == a)) {
            return i;
        }
    }
    return -1;
}

bool LineLoop::alongEdge(std::uint32_t a, std::uint32_t b) const {
    if (!noEdge_) {
        return false;
    }
    const auto pa = where(a), pb = where(b);
    if (pa[0] == pb[0]) {
        const int x = pa[0] / kOne;
        return x == bounds_[0] || x == bounds_[2];
    }
    if (pa[1] == pb[1]) {
        const int y = pa[1] / kOne;
        return y == bounds_[1] || y == bounds_[3];
    }
    return false;
}

int LineLoop::lineAt(float x, float y) const {
    // Near a line: within the reach of it and strictly between its ends
    // (the foot of the perpendicular for a slanted line).
    for (int i = int(lines_.size()) - 1; i >= 0; --i) {
        const Line& l = lines_[std::size_t(i)];
        const auto pa = where(l.a), pb = where(l.b);
        const float ax = pixels(pa[0]), ay = pixels(pa[1]);
        const float bx = pixels(pb[0]), by = pixels(pb[1]);
        const float minX = std::min(ax, bx), maxX = ax < bx ? bx : ax;
        const float minY = std::min(ay, by), maxY = ay < by ? by : ay;
        if (ax == bx) {
            if (std::fabs(ax - x) < reach_ && minY < y && y < maxY) {
                return i;
            }
            continue;
        }
        if (ay == by) {
            if (std::fabs(ay - y) < reach_ && minX < x && x < maxX) {
                return i;
            }
            continue;
        }
        const float m = (ay - by) / (ax - bx);
        const float k = (ax * by - bx * ay) / (ax - bx);
        const float off = y - m * x - k;
        if (off * off / (1.0f + m * m) > reach_ * reach_) {
            continue;
        }
        const float n = -1.0f / m;
        const float fx0 = (y - n * x - k) / (m - n);
        const float fy0 = k + m * fx0;
        if (minX < fx0 && fx0 < maxX && minY < fy0 && fy0 < maxY) {
            return i;
        }
    }
    return -1;
}

void LineLoop::adjust(float& x, float& y, bool inside) const {
    // 021083d4.
    const auto s = where(start_);
    const float sx = pixels(s[0]), sy = pixels(s[1]);
    if (!inside) {
        // Onto the right edge of the drawing area, along the line from the
        // start.
        const float edge = kRightEdge - width_;
        y = (sx * y + edge * (sy - y) - sy * x) / (sx - x);
        x = edge;
    }
    if (keepY_) {
        y = sy;
    }
    if (keepX_) {
        x = sx;
    }
    if (maxLength_ <= 0) {
        return;
    }
    const float dx = x - sx, dy = y - sy;
    const float most = float(maxLength_);
    if (dx * dx + dy * dy <= most * most) {
        return;
    }
    if (sx == x) {
        y = sy + most * (dy / std::fabs(dy));
        return;
    }
    if (sy == y) {
        x = sx + most * (dx / std::fabs(dx));
        return;
    }
    const float length = pixels(sqrtFx(
        (std::int64_t(fx(sx - x)) * fx(sx - x) + std::int64_t(fx(sy - y)) * fx(sy - y)) >> 12));
    x = sx + dx * (most / length);
    y = sy + dy * (most / length);
}

void LineLoop::press(float x, float y, bool inside) {
    // 021075a0: on a dot a line starts; on a line it is chosen.
    if (!inside) {
        return;
    }
    if (const std::uint32_t place = placeAt(x, y)) {
        drawing_ = true;
        start_ = place;
        endX_ = x;
        endY_ = y;
        services_.effect(drawSound_);
        const auto p = where(place);
        if (keepY_) {
            endX_ = pixels(p[1]); // (sic: the original keeps y in x)
        }
        if (keepX_) {
            endX_ = pixels(p[0]);
        }
        return;
    }
    if (const int i = lineAt(x, y); i >= 0) {
        chosen_ = i;
        choosing_ = true;
    }
}

void LineLoop::drag(float x, float y, bool inside) {
    // 02107714: the end follows the pen and snaps onto another dot.
    if (drawing_) {
        adjust(x, y, inside);
        const std::uint32_t hit = placeAt(x, y);
        if (hit && hit != start_ && !tooLong(start_, hit)) {
            const auto p = where(hit);
            endX_ = pixels(p[0]);
            endY_ = pixels(p[1]);
        } else {
            endX_ = x;
            endY_ = y;
        }
        return;
    }
    if (choosing_ && lineAt(x, y) < 0) {
        chosen_ = -1;
        choosing_ = false;
    }
}

void LineLoop::lift(float x, float y) {
    // 02107924.
    const std::size_t before = lines_.size();
    if (drawing_) {
        adjust(x, y, true);
        const std::uint32_t hit = placeAt(x, y);
        const bool usable =
            hit && hit != start_ && !tooLong(start_, hit) && !alongEdge(start_, hit);
        if (usable) {
            const int existing = find(start_, hit);
            if (lines_.size() < maxLines_ || existing >= 0) {
                if (existing < 0) {
                    lines_.push_back({start_, hit});
                    services_.effect(addSound_);
                } else {
                    remove(existing);
                }
            } else {
                // Too many lines: the message, everything else holds still
                // until a tap after 15 frames.
                errorTime_ = kErrorFrames;
                showError(true);
                error_ = true;
                scene_.pushSwitches();
                for (Chara* o : scene_.objects()) {
                    o->setSwitch(Switches::Touch, false);
                }
                for (Message* m : scene_.messages()) {
                    m->switches.set(Switches::Touch, false);
                }
                services_.effect(removeSound_);
            }
            start_ = 0;
        } else {
            // Back to where it started.
            const auto s = where(start_);
            shrinkA_.set(s);
            shrinkA_.move(s, kShrinkFrames, kShrinkCurve);
            shrinkB_.set({fx(x), fx(y)});
            shrinkB_.move(s, kShrinkFrames, kShrinkCurve);
            start_ = 0;
            shrinking_ = true;
        }
        drawing_ = false;
    } else if (choosing_) {
        remove(chosen_);
        choosing_ = false;
        chosen_ = -1;
    }
    if (lines_.empty() && before != 0) {
        noLinesFlag(true);
    } else if (!lines_.empty() && before == 0) {
        noLinesFlag(false);
    }
}

void LineLoop::remove(int index) {
    // 0210828c: both ends shrink to the middle.
    if (index < 0 || std::size_t(index) >= lines_.size()) {
        return;
    }
    const Line l = lines_[std::size_t(index)];
    const auto a = where(l.a), b = where(l.b);
    const TweenPoint::Point middle{std::int32_t(float(a[0] + b[0]) / 2.0f),
                                   std::int32_t(float(a[1] + b[1]) / 2.0f)};
    shrinkA_.set(a);
    shrinkA_.move(middle, kShrinkFrames, kShrinkCurve);
    shrinkB_.set(b);
    shrinkB_.move(middle, kShrinkFrames, kShrinkCurve);
    lines_.erase(lines_.begin() + index);
    shrinking_ = true;
    services_.effect(removeSound_);
}

void LineLoop::afterPen() {
    // 021066b4.
    if (services_.data.flags[text(kLockedFlag) + "#0"] != 0) {
        return;
    }
    if (error_) {
        if (errorTime_ <= 0 && services_.touch.state == 1) {
            showError(false);
            scene_.popSwitches();
            error_ = false;
        }
        return;
    }
    if (shrinking_ && !shrinkA_.moving()) {
        shrinking_ = false;
    }
    const float x = float(services_.touch.x), y = float(services_.touch.y);
    const bool inside = x <= kRightEdge - width_;
    switch (services_.touch.state) {
    case 1:
        press(x, y, inside);
        break;
    case 2:
        drag(x, y, inside);
        break;
    case 3:
        lift(x, y);
        break;
    default:
        break;
    }
}

void LineLoop::afterUpdate() {
    // 0210689c.
    shrinkA_.update(kOne);
    shrinkB_.update(kOne);
    if (error_) {
        errorTime_ = std::max(0, errorTime_ - kOne);
    }
}

void LineLoop::endFrame() {
    // 02106958 (drawn this frame): every line in its colour, the chosen
    // one in the drawing colour, the line being drawn and a shrinking one;
    // square dots on their ends unless the puzzle has none.
    auto& shapes = scene_.loopShapes;
    shapes.clear();
    const std::int32_t width = fx(width_);
    const std::int32_t side = fx(2.0f * width_), half = fx(width_);
    auto line = [&](std::array<std::int32_t, 2> a, std::array<std::int32_t, 2> b,
                    std::uint16_t colour) {
        shapes.push_back({false, a, b, width, colour});
        if (!noDots_) {
            shapes.push_back({true, {a[0] - half, a[1] - half}, {side, side}, 0, colour});
            shapes.push_back({true, {b[0] - half, b[1] - half}, {side, side}, 0, colour});
        }
    };
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        line(where(lines_[i].a), where(lines_[i].b), int(i) == chosen_ ? current_ : other_);
    }
    if (drawing_) {
        line(where(start_), {fx(endX_), fx(endY_)}, current_);
    }
    if (shrinking_) {
        line(shrinkA_.value(), shrinkB_.value(), current_);
    }
}

} // namespace picarat::adventure4
