#pragma once
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// The protagonist's notebook: notes that the story adds as it goes on.
//
// The protagonist's notes (bag menu "notes"), ARM9 overlay 18 (load 020efda0), run
// by ov2 scene 23 (02069234): constructor 020efda0, run 020f0294, destructor
// 020f00c8. 57 notes (MemoFlag, set by script 0x70) in pages of six; the
// page (1..10) persists in progress byte 0x318 (state +0x3e0). Touching a
// note's row shows its text d_%i on the upper screen and clears its
// MemoNewFlag; MenuNewFlag 0 (the bag badge) follows the remaining new notes.
class NotesMenu {
public:
    NotesMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state);
    void run(); // 020f0294; leaves scene 17 (bag)
private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    std::array<SpriteButton, 6> rows_;  // +0x00: spriteless 240x20 rows (sound 3)
    SpriteButton back_, next_, close_;  // +0x18 / +0x1c / +0x20
    SpriteObject icon_, digits_;        // +0x24 memo_gfx.spr, +0x28 system_num.spr
    int count_ = 0;                     // +0x2c found notes (not displayed)
    int page_ = 1;                      // +0x30
    int selected_ = 0;                  // +0x34 note number on the upper screen
    bool upperLoaded_ = false;          // +0x38 memo_bg loaded
    void draw();                        // 020f0208
    void list(bool redraw, bool clear); // 020f0588
    void detail(int number);            // 020f076c
    void updateMenuNew();               // 020f085c / destructor
};
} // namespace picarat
