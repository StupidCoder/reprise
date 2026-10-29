#pragma once
#include "story/a2_game_state.hpp"
#include "system/a2_save_file.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// The bag: the menu opened from rooms, with the player's status and the
// way into records, minigames and saving.
//
// Bag menu (the protagonist's case), ARM9 overlay 17 (load 020efda0), run
// by ov2 scene 17 (0206937c): constructor 020efda0, run 020f0bdc,
// destructor 020f050c. It shows the status (puzzles, picarats, hint coins,
// play time, place) and routes to the subgames and records:
//   memo 23, mysteries (ov20, inline), puzzle index 19, save (file dialog),
//   camera 14 / 27 (first visit event 18410), hamster 16 (18420),
//   herb tea 15 (18400), diary 43, close -> room, reset -> title.
class BagMenu {
public:
    BagMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state,
            std::uint32_t& random, std::uint32_t playSeconds);
    void run();
    std::function<void()> mysteries; // ov2 02069294 (ov20) from the bag
    // The save button's file dialog (ov2 020866d0): the save image, the
    // player's name and the play-time fold (02083ff4) come from the story.
    SaveFile* saveFile = nullptr;
    std::function<std::uint32_t()> foldTime;
    std::string playerName;

    std::function<HostInput(unsigned, const HostInput&)>& researchPoll() {
        return kit_.researchPoll;
    } // research only
private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    std::uint32_t& random_;
    std::uint32_t playSeconds_;
    SpriteObject bigDigits_, smallDigits_, walker_, medal_;    // +0x00..+0x0c (2D)
    SpriteObject resetWindow_, resetFont_;                     // +0x10 / +0x14
    SpriteButton close_, reset_, yes_, no_;                    // +0x18 / +0x1c / +0x20 / +0x24
    SpriteObject window_;                                      // +0x28 tobj/window
    SpriteButton memo_, mysteriesButton_, index_, saveButton_; // +0x2c..+0x38
    SpriteButton camera_, hamster_, tea_, diary_;              // +0x3c..+0x48
    SpriteObject items_, noCamera_, noHamster_, noTea_, newMark_, help_; // +0x4c..+0x64
    std::string text_;                                                   // +0x68 buffer
    bool diaryPresent_ = false;                                          // +0x6c
    void createItems();                                                  // 020f17e8
    void draw(bool update);                                              // 020f0784
    void resetOthers(int keep);                                          // 020f0af8
    void status();                                                       // 020f126c
    void bigNumber(int x, int y, int value, int digits);                 // 020f142c
    void smallNumber(int x, int y, int value, int digits, bool all);     // 020f14bc
    void minutes(int x, int y, int value);                               // 020f1558
    void resetPrompt();                                                  // 020f1600
    void resetFrame(int alpha);                                          // 020f1744
    void leaveFor(std::uint16_t firstEvent, std::uint8_t scene);         // camera/ham/tea branches
};
} // namespace picarat
