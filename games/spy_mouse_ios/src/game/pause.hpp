// The pause menu over a level (ScenePause, layout guis/pause.gui; showScene @0x14e51c, update
// @0x14d474, guiEvent @0x14debc, hideScene @0x14e3d8): the card and the settings/help corner
// bounce in, the level stops (only its HUD items and the ambience go on) and darkens. Resume
// sends both frames out and closes the menu once they have gone; Retry and Exit close the iris
// on the mouse first and act once it is shut. The ribbons show the goal info popup.
#pragma once

#include "game/goal_popup.hpp"
#include "game/results.hpp"

#include <array>

namespace doc::game {

class PauseMenu {
public:
    enum class Action { None, Resume, Retry, Exit };

    // showScene: the frames come in, the medals show the level's goals (gold when won before).
    void open(float popupHeight, const std::array<ResultsGoal, 3>& goals);
    bool active() const { return active_; }
    void close() { active_ = false; }

    // A button released (guiEvent, while the iris is still): the frames go out and the popup
    // drops; nothing more is taken until the action has been carried out.
    bool press(Action action);
    void pressGoal(int i);
    bool busy() const { return pending_ != Action::None; }

    // Scene::update: the frames and the popup.
    void updateItems(int dtMs);
    // update's step after the level's paused update: the action that is due now (its frames
    // have gone; Retry and Exit also wait for a still iris), else None.
    Action due(bool irisMoving);
    void done() { pending_ = Action::None; }
    // A frame squashing for the first time plays the wobble (drawn after the updates).
    int takeWobbles();

    const BounceFrame& card() const { return card_; }
    const BounceFrame& corner() const { return corner_; }  // FRAME_HELPOPTIONS
    const GoalPopup& popup() const { return popup_; }
    const ResultsGoal& goal(int i) const { return goals_[std::size_t(i)]; }

private:
    bool active_ = false;
    Action pending_ = Action::None;  // +0xdc
    BounceFrame card_, corner_;      // +0xb8, +0xbc
    GoalPopup popup_;                // +0xcc
    std::array<ResultsGoal, 3> goals_{};
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(active_, pending_, card_, corner_, popup_, goals_); }
};

}  // namespace doc::game
