#pragma once
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// An old diary found in the story; its pages appear as they are found.
//
// The diary screen is ARM9 overlay 21 (load 020efda0), run by ov2 scene 43 (020692f4): constructor
// 020efda0, run 020f01e4, destructor 020eff78; afterwards the runner restarts the BGM saved by the
// bag (+0x7e84, 0207faa4). Twelve pages (diary page flags, progress 0x314;
// "new page" flags 0x316) as icons on a 4x3 grid; touching one types
// diary_%i on the upper screen. MenuNewFlag 6 follows the remaining new pages.
class DiaryMenu {
public:
    DiaryMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state);
    void run(); // 020f01e4; leaves scene 17 (bag)
private:
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    std::array<SpriteButton, 12> pages_; // +0x00 diary_icon.spr
    SpriteButton back_;                  // +0x30 secret_modoru.spr ("pos_o", sound 4)
    bool first_ = true;                  // +0x34 upper BG not yet switched
    int selected_ = -1;                  // +0x38
    int count_ = 0;                      // +0x3c found pages (+0x40 their indices)

    bool found(int i) const { return state_.progress.bit(0x314, 16, std::uint32_t(i)); } // 02082fa8

    bool fresh(int i) const { return state_.progress.bit(0x316, 16, std::uint32_t(i)); } // 02082ff0

    void layout();          // 020f0038
    void draw(bool active); // 020f017c
    void show(int index);   // 020f03a8
};
} // namespace picarat
