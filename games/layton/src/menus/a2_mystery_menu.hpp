#pragma once
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// The list of the story's mysteries: new ones are revealed and solved ones
// stamped as the story goes on.
//
// The mysteries list ("Geheimnisse"), ARM9 overlay 20 (load 020f4860), run
// by ov2 02069294 from the bag (ov17 020f0d0c) and from scripts 0x71/0x7d
// (ov8 020f1170 / 020f2d18): constructor 020f4860, run 020f4e40, destructor
// 020f4adc. State +0x8218 selects the entry: -1 plain list, 0..9 reveals a
// new mystery first (020f4fdc), 10..19 stamps one solved (020f52e8).
// Ten entries on a 5x2 grid; touching one shows its title and text on the
// upper screen. Progress bits (SaveProgress +0x2e3 / +0x2e5 / +0x2e7) are the
// "FukamaruFlag", "FukamaruNewFlag" and "FukamaruEndFlag" arrays.
class MysteryMenu {
public:
    MysteryMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state,
                std::uint32_t& random);
    void run(); // 020f4e40
private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    SpriteButton ok_;                      // +0x00: cancel.spr (bag) / fukamaru_ok.spr (script)
    std::array<SpriteButton, 10> entries_; // +0x04: info_mode_chr.spr, animation "%i"
    SpriteObject badge_;                   // +0x34: new_button.spr
    int current_ = -1;                     // +0x38: entry on the upper screen

    bool found(int i) const { return state_.progress.bit(0x2e3, 16, std::uint32_t(i)); } // 02082c40

    bool fresh(int i) const { return state_.progress.bit(0x2e5, 16, std::uint32_t(i)); } // 02082c90

    bool solved(int i) const {
        return state_.progress.bit(0x2e7, 16, std::uint32_t(i));
    } // 02082ce0

    void show(int number);  // 020f4b88
    void draw(bool active); // 020f4d70
    void clearMenuNew();    // 020f4f7c
    void reveal();          // 020f4fdc
    void stamp();           // 020f52e8
    void frames(int count); // poll, draw(0), present
};
} // namespace picarat
