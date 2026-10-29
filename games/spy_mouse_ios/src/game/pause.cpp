#include "game/pause.hpp"

namespace doc::game {

void PauseMenu::open(float popupHeight, const std::array<ResultsGoal, 3>& goals) {
    active_ = true;
    pending_ = Action::None;
    goals_ = goals;
    card_.goIn();
    corner_.goIn();
    popup_.reset(popupHeight, {goals[0].infoText, goals[1].infoText, goals[2].infoText});
}

bool PauseMenu::press(Action action) {
    if (!active_ || busy() || action == Action::None) return false;
    pending_ = action;
    popup_.popOut();
    card_.goOut();
    corner_.goOut();
    return true;
}

void PauseMenu::pressGoal(int i) {
    if (!active_ || busy()) return;
    popup_.press(i);
}

void PauseMenu::updateItems(int dtMs) {
    if (!active_) return;
    card_.update(dtMs);
    corner_.update(dtMs);
    popup_.update(dtMs);
}

int PauseMenu::takeWobbles() {
    if (!active_) return 0;
    return int(card_.takeWobble()) + int(corner_.takeWobble());
}

PauseMenu::Action PauseMenu::due(bool irisMoving) {
    if (!active_ || pending_ == Action::None || !card_.finished()) return Action::None;
    if ((pending_ == Action::Retry || pending_ == Action::Exit) && irisMoving) return Action::None;
    return pending_;
}

}  // namespace doc::game
