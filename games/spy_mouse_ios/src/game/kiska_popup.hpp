// Kiska's offer over a level (SceneLevelSkip, layout guis/level_skip.gui (1.0.5: levelSkip.gui)): a cat who "can help
// you pass levels" by taking out the other cats once an hour, sold in the original's store.
// The level intro pushes it as it starts to fade (SceneLevelIntro::update @0x14a390; when, see
// Session::offerKiska): Kiska's picture and the offer's card bounce in over the level, which
// waits under them (SceneLevelSkip::update @0x1faabc runs only SceneGame::updatePaused and the
// particles). "Later" sends both frames out and the popup leaves at its next update; "Okay"
// leaves at once and opens the store (makeCloudcellStorePurchase(0x21), not part of the port).
// With Kiska bought the card asks instead whether she should skip this level (FRAME_SKIP);
// the port never owns her, so that part (state 1..3 of the update) is not reconstructed.
#pragma once

#include "data/gui_layout.hpp"
#include "game/geometry.hpp"
#include "game/results.hpp"

#include <string>

namespace doc::game {

class KiskaPopup {
public:
    // SceneLevelSkip::showScene @0x1fb994: both frames go in; the card shows the offer.
    void open();
    bool active() const { return active_; }
    // SceneLevelSkip::guiEvent @0x1fb54c. Returns whether the button belongs to the popup.
    bool press(const std::string& button);
    // SceneLevelSkip::update: its frames (Scene::update), then, once a button has sent it
    // away (state 4), the popup leaves (Engine::popScene). True in the update it leaves.
    bool update(int dtMs);

    // Bounce frames from the layout (GuiBounceFrameCM made by App::loadUserItem @0x183800 with
    // the wobble sound off, +0xb2; only the results scene turns it on: run l28_later).
    const BounceFrame& card() const { return card_; }    // FRAME
    const BounceFrame& kiska() const { return kiska_; }  // FRAME_KISKA
    // The card's button under a screen point (y down) in the layout guis/level_skip.gui (1.0.5: levelSkip.gui), or ""
    // (GuiItem::mouseEvent; the frame without Kiska bought, FRAME_SKIP, is hidden).
    static std::string buttonAt(const data::GuiNode& root, Vec2 screen);

private:
    bool active_ = false;
    bool leaving_ = false;  // +0xb8 = 4
    BounceFrame card_, kiska_;
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(active_, leaving_, card_, kiska_); }
};

}  // namespace doc::game
