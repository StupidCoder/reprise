#include "layton/reprise/dual_screen.hpp"
#include <algorithm>
#include <cmath>

namespace layton_reprise {
namespace embed = picarat::embed;

namespace {
constexpr double max_zoom = 4;
constexpr long hold_back_ticks = 9;  // 150 ms: a resting finger waits that long for a second one
constexpr long shortest_tap_ticks = 2;
// Off every screen: where a cancelled stylus slides to and lifts (the standalone app's point).
constexpr int off_screen = 100000;

embed::Rect zoomed(const embed::Rect& r, double zoom, double x, double y) {
    return {r.x * zoom + x, r.y * zoom + y, r.width * zoom, r.height * zoom};
}

bool inside(const embed::Rect& r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
} // namespace

void DualScreen::clamp(int width, int height, double& x, double& y) const {
    // Zoomed in, the screens cover the frame where they can (no empty band opens beside them);
    // where they are smaller than the frame they stay centred as without zoom.
    const auto l = embed::layout(width, height, screens_);
    const double x0 = std::min(l.top.x, l.bottom.x), y0 = std::min(l.top.y, l.bottom.y);
    const double x1 = std::max(l.top.x + l.top.width, l.bottom.x + l.bottom.width);
    const double y1 = std::max(l.top.y + l.top.height, l.bottom.y + l.bottom.height);
    auto axis = [&](double& offset, double lo, double hi, double window) {
        const double size = (hi - lo) * zoom_;
        if (size <= window) {
            offset = (window - size) / 2 - lo * zoom_;
        } else {
            offset = std::clamp(offset, window - hi * zoom_, -lo * zoom_);
        }
    };
    axis(x, x0, x1, width);
    axis(y, y0, y1, height);
}

DualScreen::Placement DualScreen::place(int width, int height) const {
    const auto l = embed::layout(std::max(1, width), std::max(1, height), screens_);
    double x = x_, y = y_;
    clamp(std::max(1, width), std::max(1, height), x, y);
    return {zoomed(l.top, zoom_, x, y), zoomed(l.bottom, zoom_, x, y), l.sideBySide, width, height};
}

void DualScreen::show(int width, int height) {
    clamp(std::max(1, width), std::max(1, height), x_, y_);
    const auto now = place(width, height);
    const auto same = [](const embed::Rect& a, const embed::Rect& b) {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    };
    if (now.width != shown_.width || now.height != shown_.height || !same(now.top, shown_.top) ||
        !same(now.bottom, shown_.bottom)) {
        ++generation_;
    }
    shown_ = now;
}

std::array<int, 2> DualScreen::to_lower(float x, float y) const {
    const auto& b = shown_.bottom;
    if (b.width <= 0 || b.height <= 0) {
        return {off_screen, off_screen};
    }
    return {int(std::floor((x - b.x) * screens_.bottom.width / b.width)),
            int(std::floor((y - b.y) * screens_.bottom.height / b.height))};
}

bool DualScreen::on_lower(float x, float y) const {
    const auto at = to_lower(x, y);
    return at[0] >= 0 && at[1] >= 0 && at[0] < screens_.bottom.width && at[1] < screens_.bottom.height;
}

void DualScreen::pinch_start(double x, double y, double distance) {
    pinch_x_ = x;
    pinch_y_ = y;
    pinch_distance_ = std::max(1.0, distance);
}

void DualScreen::pinch_to(double x, double y, double distance) {
    // The point of the screens under the fingers' middle stays under it: spreading zooms in
    // around it, moving both fingers moves the screens.
    const double zoom = std::clamp(zoom_ * distance / std::max(1.0, pinch_distance_), 1.0, max_zoom);
    const double px = (pinch_x_ - x_) / zoom_, py = (pinch_y_ - y_) / zoom_;
    zoom_ = zoom;
    x_ = x - px * zoom;
    y_ = y - py * zoom;
    pinch_start(x, y, distance);
}

void DualScreen::pinch_end() {
    if (zoom_ < 1.05) {
        zoom_ = 1; // nearly out: all the way
    }
}

// --- the stylus ---------------------------------------------------------------------------------

void StylusRouter::slide_off(std::vector<embed::Touch>& out) {
    if (stylus_) {
        out.push_back({embed::Touch::Kind::Move, off_screen, off_screen});
        out.push_back({embed::Touch::Kind::Up, off_screen, off_screen});
        stylus_ = false;
        ignoring_ = true; // (until the pointer's own release)
    }
}

void StylusRouter::cancel(std::vector<embed::Touch>& out) {
    held_.reset();
    release_.reset();
    slide_off(out);
    blowing_ = false;
    movie_gesture_ = false;
}

void StylusRouter::press(float x, float y, const DualScreen& screens, bool movie,
                         const std::optional<embed::Rect>& blow, std::vector<embed::Touch>& out) {
    ignoring_ = false;
    if (blow && inside(*blow, x, y)) {
        blowing_ = true;
        return;
    }
    if (movie) {
        movie_gesture_ = true;
        return;
    }
    // Presses on the upper screen or beside the screens do not become stylus contacts.
    if (!screens.on_lower(x, y)) {
        return;
    }
    const auto at = screens.to_lower(x, y);
    stylus_ = true;
    press_generation_ = screens.generation();
    out.push_back({embed::Touch::Kind::Down, at[0], at[1]});
}

void StylusRouter::move(float x, float y, const DualScreen& screens, std::vector<embed::Touch>& out) {
    if (stylus_) {
        // Dragging off the lower screen keeps the stylus down (a DS button notices it left).
        const auto at = screens.to_lower(x, y);
        out.push_back({embed::Touch::Kind::Move, at[0], at[1]});
    }
}

void StylusRouter::release(float x, float y, const DualScreen& screens, std::vector<embed::Touch>& out) {
    if (blowing_) {
        blowing_ = false;
    } else if (movie_gesture_) {
        movie_gesture_ = false;
    } else if (stylus_) {
        const auto at = screens.to_lower(x, y);
        out.push_back({embed::Touch::Kind::Up, at[0], at[1]});
        stylus_ = false;
    }
    ignoring_ = false;
}

StylusRouter::Frame StylusRouter::route(const std::vector<platform::PointerEvent>& events,
                                        const std::vector<platform::Finger>& fingers, DualScreen& screens,
                                        bool movie, std::optional<embed::Rect> blow) {
    ++tick_;
    Frame frame;
    auto& out = frame.touches;
    // A new layout under a press: it slides off and lifts (the finger no longer points where it
    // did).
    if (stylus_ && screens.generation() != press_generation_) {
        slide_off(out);
    }
    // Fingers: two make a pinch, until all have lifted.
    const bool had_two = fingers_.size() >= 2;
    fingers_.clear();
    for (const auto& f : fingers) {
        fingers_[f.id] = {f.x, f.y};
    }
    if (fingers_.size() >= 2) {
        const auto& a = fingers_.begin()->second;
        const auto& b = std::next(fingers_.begin())->second;
        const double mx = (a[0] + b[0]) / 2.0, my = (a[1] + b[1]) / 2.0;
        const double distance = std::hypot(a[0] - b[0], a[1] - b[1]);
        if (!pinching_ || !had_two) {
            // A held press is dropped; a tap just before ends as a tap; a stylus down slides off.
            pinching_ = true;
            held_.reset();
            if (release_) {
                release(release_->x, release_->y, screens, out);
                release_.reset();
            }
            slide_off(out);
            screens.pinch_start(mx, my, distance);
        } else {
            screens.pinch_to(mx, my, distance);
        }
    } else if (fingers_.empty() && pinching_) {
        pinching_ = false;
        screens.pinch_end();
    }
    const float slop = std::max(8.0f, float(std::min(screens.shown().width, screens.shown().height)) / 100.0f);
    for (const auto& e : events) {
        if (e.kind == platform::PointerEvent::Kind::down) {
            ignoring_ = false; // (a new press)
        }
        if (e.touch && pinching_) {
            if (e.kind == platform::PointerEvent::Kind::up) {
                ignoring_ = false;
            }
            continue;
        }
        if (ignoring_ && e.kind != platform::PointerEvent::Kind::down) {
            if (e.kind == platform::PointerEvent::Kind::up) {
                ignoring_ = false;
            }
            continue;
        }
        if (e.touch) {
            // A finger: a press waits until it is clearly not a pinch's first finger.
            if (e.kind == platform::PointerEvent::Kind::down) {
                if (release_) {
                    release(release_->x, release_->y, screens, out); // a tap before still lifts first
                    release_.reset();
                }
                if (blow && inside(*blow, e.x, e.y)) {
                    press(e.x, e.y, screens, movie, blow, out);
                } else {
                    held_ = Held{e.x, e.y, tick_};
                }
                continue;
            }
            if (held_ && e.kind == platform::PointerEvent::Kind::move) {
                if (std::hypot(e.x - held_->x, e.y - held_->y) < slop) {
                    continue;
                }
                press(held_->x, held_->y, screens, movie, blow, out); // a stroke: the press, then this move
                held_.reset();
            }
            if (held_ && e.kind == platform::PointerEvent::Kind::up) {
                // A tap: the press now, the release once the tap's own length has passed again.
                const long length = std::max(shortest_tap_ticks, tick_ - held_->tick);
                press(held_->x, held_->y, screens, movie, blow, out);
                held_.reset();
                release_ = Held{e.x, e.y, tick_ + length};
                continue;
            }
        }
        switch (e.kind) {
        case platform::PointerEvent::Kind::down:
            press(e.x, e.y, screens, movie, blow, out);
            break;
        case platform::PointerEvent::Kind::move:
            move(e.x, e.y, screens, out);
            break;
        case platform::PointerEvent::Kind::up:
            release(e.x, e.y, screens, out);
            break;
        }
    }
    if (held_ && tick_ - held_->tick >= hold_back_ticks) {
        press(held_->x, held_->y, screens, movie, blow, out);
        held_.reset();
    }
    if (release_ && tick_ >= release_->tick) {
        release(release_->x, release_->y, screens, out);
        release_.reset();
    }
    frame.blowing = blowing_;
    return frame;
}

} // namespace layton_reprise
