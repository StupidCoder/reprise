#include "game/hand_tutorial.hpp"

namespace doc::game {

namespace {
constexpr int kRouteLineType = 142;
const Vec2 kOffScreen{-9999.99f, -9999.99f};
}  // namespace

std::vector<HandTutorial::Sample> HandTutorial::route(const data::MapFile& map) {
    // The lines in map order; a negative wait flips the finger (it starts lifted).
    std::vector<Sample> samples;
    bool pressed = false;
    for (const data::MapLine& line : map.lines) {
        if (line.type != kRouteLineType) continue;
        if (line.a < 0 || std::size_t(line.a) >= map.vertices.size()) continue;
        const data::MapVertex& v = map.vertices[std::size_t(line.a)];
        if (v.d <= -1) pressed = !pressed;
        samples.push_back({{v.x, v.y}, pressed, v.d < 0 ? -v.d : v.d});
    }
    return samples;
}

HandTutorial::HandTutorial(std::vector<Sample> route, SheetSize rippleSheet, float rippleFrameWidth)
    : route_(std::move(route)), active_(route_.size() >= 2) {
    ripple_.setSheet(rippleSheet.width, rippleSheet.height, rippleFrameWidth);
}

void HandTutorial::startOver() {
    velocity_ = {};
    waitMs_ = 0;
    playing_ = true;
    index_ = 0;
    position_ = route_.front().position;
    rippleState_ = 0;
}

void HandTutorial::update(int dtMs, int pathPoints) {
    if (!active_) return;
    if (pathPoints >= 1) {  // the player has started a path: the scene pops
        active_ = false;
        return;
    }
    if (delayMs_ >= 1) {
        delayMs_ -= dtMs;
        if (delayMs_ <= 0) startOver();
        return;
    }
    if (rippleState_ == 1) {
        ripple_.update(dtMs);
        if (ripple_.finished() || ripple_.mode() == SpriteAnim::Hold) ++rippleState_;
    }
    if (!playing_) {
        position_ = kOffScreen;
        return;
    }

    // Steer towards the next sample: the velocity turns a little towards it each tick and keeps
    // a speed in proportion to the length of the leg (each leg takes about as long); close to
    // the sample the finger slows down and waits there for the sample's time.
    const Sample& next = route_[std::size_t(index_) + 1];
    Vec2 toward = next.position - position_;
    const Vec2 leg = next.position - route_[std::size_t(index_)].position;
    const float dt = float(dtMs);
    const float legLength = leg.length();
    const float distance = toward.normalise(dt * 0.00400000019f);
    if (waitMs_ <= 0) {
        velocity_ += toward;
        velocity_.normalise(legLength * 0.00579999993f);
    }
    if (distance < 25.0f) {
        if (waitMs_ >= next.waitMs) {
            waitMs_ = 0;
            ++index_;
        } else {
            velocity_.normalise(velocity_.length() * 0.550000012f);
            waitMs_ += dtMs;
        }
    }
    position_ = position_ + (velocity_ * dt) * 0.600000024f;

    if (index_ == int(route_.size()) - 1) {
        // Off the end of the route: hidden for 7 s, then from the start again.
        playing_ = false;
        position_ = kOffScreen;
        index_ = 0;
        waitMs_ = 0;
        rippleState_ = 0;
        delayMs_ = 7000;
        return;
    }
    // The finger sinks onto its shadow while pressed; touching down shows the ripple once.
    lift_ = (lift_ * 3.0f + (route_[std::size_t(index_)].pressed ? 5.0f : 25.0f)) * 0.25f;
    if (rippleState_ == 0 && lift_ <= 6.0f) {
        rippleState_ = 1;
        ripple_.setAnim(0, ripple_.frameCount() - 1, SpriteAnim::Once, 40);
    }
}

}  // namespace doc::game
