#pragma once
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// The shack where puzzles missed during the story can still be played.
//
// The puzzle shack, ARM9 overlay 25 (load 020efda0, ROM
// 0xa0600+0x8dc), run by ov2 scene 44 (0206a970): constructor 020efda0, run
// 020f0304, destructor 020f00f0. It lists the puzzles passed on to the shack
// (puzzle-byte bit 6, set by script 0x76) that are not solved, six per page;
// choosing one and confirming starts it with following scene 44.
class NazobaMenu {
public:
    NazobaMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state);
    void run(); // 020f0304; leaves state.scene at 1 (back) or 10 (puzzle)
private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    SpriteButton back_, previous_, next_; // +0x00 modoru, +0x04 mae, +0x08 tsugi
    std::array<SpriteButton, 6> entries_; // +0x14: invisible 200x21 rows
    SpriteButton yes_, no_;               // +0x0c / +0x10
    SpriteObject digits_, window_;        // +0x2c nazoba_btn.spr ("%d"), +0x30 nazoba_win.spr
    std::vector<int> puzzles_;            // +0x34 (up to 153 numbers), count +0x298
    int pages_ = 1;                       // +0x29c
    std::string format_;                  // +0x2a0: tx 0xcf
    void collect();                       // 020f077c
    void draw(int page, bool confirm);    // 020f0224
    void digits(int page);                // 020f0510
    void list(int page);                  // 020f066c
    bool confirm(int page, int number);   // 020f0800
};
} // namespace picarat
