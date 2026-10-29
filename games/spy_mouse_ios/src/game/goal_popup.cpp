#include "game/goal_popup.hpp"

#include <algorithm>

namespace doc::game {

namespace {
constexpr float kShownY = 2.0f;
constexpr float kSpeed = 0.25f;  // per ms
}  // namespace

void GoalPopup::reset(float height, const std::array<std::string, 3>& lines) {
    *this = GoalPopup{};
    lines_ = lines;
    height_ = height;
    y_ = -height;
}

void GoalPopup::press(int goal) {
    if (goal < 0 || goal > 2) return;
    const float off = y_ - kShownY;
    if (timerMs_ <= 0 && off > -0.00100000005f && off < 0.00100000005f) {
        queue_.push_back(goal);
    } else if (goal_ >= 0 && lines_[std::size_t(goal_)] == lines_[std::size_t(goal)]) {
        timerMs_ = 3000;  // the same line again: it stays up longer
    } else {
        timerMs_ = 0;  // the shown line goes, the new one follows
        queue_.push_back(goal);
    }
}

void GoalPopup::update(int dtMs) {
    if (timerMs_ >= 1) {
        timerMs_ -= dtMs;
        y_ = std::min(kShownY, y_ + float(dtMs) * kSpeed);
    } else if (y_ > -height_) {
        y_ = std::max(-height_, y_ - float(dtMs) * kSpeed);
    } else if (!queue_.empty()) {
        goal_ = queue_.front();
        queue_.pop_front();
        timerMs_ = 3000;
        if (queue_.size() >= 2) timerMs_ >>= 1;
    }
}

}  // namespace doc::game
