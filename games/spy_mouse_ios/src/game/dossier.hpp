// (1.1.1) Dossiers: 21 pages about the game's characters and gadgets. A dossier pickup in a
// level (object type 431, ObjControllerPickups) opens its page once touched (SceneDossier,
// layout guis/dossier.gui); the backpack's dossier menu opens found ones again
// (SceneMenuDossier). The first find also explains what dossiers are (SceneMsgBox).
#pragma once

#include "game/geometry.hpp"

#include <array>
#include <vector>

namespace doc::game {

// A message box (SceneMsgBox, layout guis/msgbox.gui): a bounce frame with a title, a text
// and an "Okay" button (type 0) or "Yes"/"No" (type 1). A press closes it at once
// (guiEvent @0x1bd1d8: Engine::popScene, then the owner's guiEvent 7 for OK, 8 yes, 9 no).
struct MessageBox {
    bool active = false;
    int type = 0;
    int title = -1, text = -1, okText = -1;  // string ids
    void open(int boxType, int titleId, int textId) {
        active = true;
        type = boxType;
        title = titleId;
        text = textId;
        okText = -1;
    }
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(active, type, title, text, okText); }
};

// The dossier page (SceneDossier): the folder slides in from the left, the paper with the
// page's text follows it, then the screenshot and the portrait drop in and turn upright; the
// back button sends everything out the way it came and the scene ends. Positions are screen
// points with y up, as the original's (×LevelState +0x60 = 1, +LevelState +0x68 = 0).
class DossierScreen {
public:
    // SceneDossier::showScene @0x28760c and setActiveDossier @0x2862f4. `fromMenu`: opened by
    // the backpack's dossier menu (the first-find note is for finds in a level only).
    void open(int dossier, bool fromMenu, int dossiersFound);
    // SceneDossier::update @0x286dc0. Returns false once the scene has ended.
    bool update(int dtMs);
    // guiEvent @0x285e2c: the back button (BTN_BACK).
    void back();
    // The first-find note's button (SceneMsgBox::guiEvent, then regainFocus @0x2869c8).
    void closeNote() { note_.active = false; }

    bool active() const { return active_; }
    int dossier() const { return dossier_; }
    float dim() const { return dim_; }                 // +0x130: the black over the level
    Vec2 folder() const { return folder_; }            // +0xdc: folder_top.png's corner
    Vec2 paper() const { return folder_ + paper_; }    // +0xe4 from the folder: paper.png
    Vec2 screenshot() const { return screenshot_; }    // +0xec
    Vec2 portrait() const { return portrait_; }        // +0xf4
    float portraitTurn() const { return portraitTurn_; }      // +0x124, degrees
    float screenshotTurn() const { return screenshotTurn_; }  // +0x128
    const MessageBox& note() const { return note_; }
    // Effects since the last call (sound table numbers).
    std::vector<int> takeSounds() { return std::move(sounds_); }

private:
    void scrollIn();   // setScrollInTargets @0x285e60
    void scrollOut();  // setScrollOutTargets @0x285d30

    bool active_ = false;
    bool fromMenu_ = false;
    bool exiting_ = false;   // m_bExitScene
    int dossier_ = -1;
    int found_ = 0;
    unsigned steps_ = 0;     // +0x12c: 1 folder moving, 2 paper, 4 pictures, 8 settled
    bool comingIn_ = true;   // +0x134
    float dim_ = 0.0f;
    Vec2 folder_, paper_, screenshot_, portrait_;
    Vec2 folderTarget_, paperTarget_, screenshotTarget_, portraitTarget_;
    float portraitTurn_ = 270.0f, screenshotTurn_ = 150.0f;
    float portraitTurnTarget_ = 0.0f, screenshotTurnTarget_ = 0.0f;
    MessageBox note_;        // +0x135 while it is up
    std::vector<int> sounds_;
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(active_, fromMenu_, exiting_, dossier_, found_, steps_, comingIn_, dim_, folder_,
                           paper_, screenshot_, portrait_, folderTarget_, paperTarget_, screenshotTarget_,
                           portraitTarget_, portraitTurn_, screenshotTurn_, portraitTurnTarget_,
                           screenshotTurnTarget_, note_, sounds_); }
};

}  // namespace doc::game
