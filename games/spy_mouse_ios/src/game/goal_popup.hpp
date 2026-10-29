// The goal info popup of the results and pause screens (GuiPopupGoalInfo, queueGoalInfo
// @0x226830, update @0x2268bc, popOut @0x226814): a tapped medal's line slides up from below
// the screen's bottom edge, stays 3 s (half that while two or more lines wait) and slides away;
// the next waiting line follows.
#pragma once

#include <array>
#include <deque>
#include <string>

namespace doc::game {

class GoalPopup {
public:
    // GuiPopupGoalInfo::init @0x226d78: hidden, its picture's height below the edge.
    void reset(float height, const std::array<std::string, 3>& lines);
    // A medal tapped: its line joins the queue; tapped again while its line shows, it stays up.
    void press(int goal);
    void popOut() { timerMs_ = 0; }  // the line goes now
    void update(int dtMs);

    int goal() const { return goal_; }  // the medal whose line it shows, -1 before the first
    float y() const { return y_; }      // above the screen's bottom edge (up to 2)

private:
    std::array<std::string, 3> lines_;
    float height_ = 0.0f;
    std::deque<int> queue_;
    int goal_ = -1;
    int timerMs_ = 0;   // +0xa0
    float y_ = 0.0f;    // +0xcc (+0xd0 shown: 2, +0xd4 hidden: minus its height)
};

}  // namespace doc::game
