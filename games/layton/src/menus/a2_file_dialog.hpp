// The save and load screens: three file slots, each showing the player's
// name, the place, the play time, puzzles solved and found and how far the
// game was finished. Saving to a used slot asks before overwriting it.
//
// Overlay 2 file dialog: constructor 02085b50, destructor 02085e4c, drawing
// 02085f50 (slot texts 02085fbc, slot summaries 02086140), overwrite question
// 02086d64, save 020866d0 (bag, script 0x7b) and load 02086ad4 (title
// Continue / Secret). It draws on the caller's screen and lets the caller
// draw its own parts underneath (the bag's status 020f126c, the title's
// logo 020f02cc).
#pragma once
#include "story/a2_game_state.hpp"
#include "system/a2_save_file.hpp"
#include "system/scene_kit.hpp"
#include <functional>

namespace picarat {
class FileDialog {
public:
    // `ownerSlides` selects the bag's exit fade (02072260) instead of the
    // two-screen fade out (object +0x1c set).
    FileDialog(SceneKit& kit, GameState& state, SaveFile& file, std::function<void()> ownerDraw,
               bool ownerSlides);

    // 020866d0: returns whether a slot was written. `resumeEvent` (> 0)
    // is stored as the pending event while saving (progress +0x312). `fold`
    // puts the play time into the progress block (02083ff4) and returns the
    // seconds for the summary; `name` is the player's name.
    bool save(int resumeEvent, const std::function<std::uint32_t()>& fold, const std::string& name);
    // 02086ad4: returns the loaded slot or -1; `mode` 0 Continue, 1 Secret
    // (background fileload_bg1 / fileload_bg2).
    int load(int mode, std::string& name);

    // The bonus screens' save to the loaded slot without a dialog (ov23
    // 020f30a4, ov24 020f0590, ov19 020f0c5c): room sound 6 off, map/main0
    // with the "saving" window (wifi_window3), main-screen fade in, the
    // block written without refreshing the summary (02083638(slot, 0)), the
    // music faded (time 1200), 20 frames, SE 205, 60 frames, the music back
    // (time 1201), main-screen fade out (02072260).
    static void quietSave(SceneKit& kit, GameState& state, SaveFile* file);

private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit& kit_;
    GameState& state_;
    SaveFile& file_;
    std::function<void()> ownerDraw_;
    bool ownerSlides_;
    SpriteButton cancel_;                                     // +0x00
    std::array<SpriteButton, 3> slots_;                       // +0x04: spriteless 250x43
    SpriteObject numbers_, saveWindow_, loadWindow_, window_; // +0x10 +0x14 +0x18 +0x24
    SpriteButton yes_, no_;                                   // +0x28 / +0x2c
    void draw(bool active);                                   // 02085f50
    void slotTexts();                                         // 02085fbc
    void summaries();                                         // 02086140
    void digits(int x, int y, unsigned value, int count,
                bool minutes);                       // the digit loops of 02086140
    bool confirm(int slot);                          // 02086d64
    void showWindow(SpriteObject& window, int slot); // "pos%i" window over the slot
};
} // namespace picarat
