// The challenges: five categories of three extra puzzles each, opened by
// finishing the photo hunt, the herb teas, the hamster's training, the
// story, and solving puzzles 1..150. A category lists its puzzles; choosing
// one plays its introduction event (or the replay variant once solved).
// Leaving the list saves to the loaded slot.
//
// ARM9 overlay 24, run by ov2 scene 24: constructor 020efda0, run
// 020f04d4, destructor 020f0224; drawing 020f0370, page setup 020f0984,
// the "puzzles are now selectable" notice 020f0b28.
#pragma once
#include "story/a2_game_state.hpp"
#include "system/a2_save_file.hpp"
#include "system/scene_kit.hpp"
#include <array>

namespace picarat {
class ChallengeMenu {
public:
    ChallengeMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                  GameState& state, SaveFile* file);
    void run();

private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    SaveFile* file_;
    std::array<SpriteButton, 5> categories_; // +0x00
    std::array<SpriteButton, 3> rows_;       // +0x14: spriteless 256x23
    SpriteButton back_;                      // +0x20
    SpriteObject complete_, icon_;           // +0x24 / +0x28
    SpriteObject window_, cursor_;           // +0x2c / +0x30
    int page_ = 0;                           // +0x34: 0 the list, 1..5 a category
    std::array<std::string, 3> rowTexts_;    // +0x38: 64 bytes each
    std::array<unsigned, 3> rowStatus_{};    // +0xf8
    void draw();                             // 020f0370
    void setup();                            // 020f0984
    void notice();                           // 020f0b28
    int frames(int id);                      // 0207d30c
    bool categorySolved(int category) const;
};
} // namespace picarat
