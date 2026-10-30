// Notices the original shows over a level before play (GameBoot::bootLevel @0x1f5142): a
// tutorial popup (ScenePopupTutorial, description in res/popup_tutorials/<name>.xml.brd, layout
// guis/popup_tutorial.gui). While it is up only the popup and the ambience run; the level waits.
// In 1.1.1 the popup is a bouncing card with a title, a picture and a scrolling text; 1.0.5's
// posed mouse (and the random draws its eyes made) is gone: ScenePopupTutorial::load @0x225208
// still reads <mouse pose flip> but uses neither.
// So far only the "carry more" notice is reconstructed (SceneGame::initGame @0x10639e raises the
// profile's strength to the level's trail length and asks for new_cheese_slot0<strength-1>).
#pragma once

#include "game/random.hpp"
#include "game/results.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace doc::game {

// The mouse drawn in some layouts (GuiPosedMouse @0x224608.., 1.1.1: the main menu's and the
// store reminder's; 1.0.5's tutorial popups had one too): a pose picture with eyes that
// look around and blink at random times. Every change of look draws random numbers, so it is
// part of the game's random sequence.
class PosedMouse {
public:
    // Which eye pictures each pose can show (g_vEyeMapping, 8 poses x 5, from the executable).
    using EyeMapping = std::array<std::uint8_t, 40>;

    void setPose(int pose, bool flip, GameRandom& random);
    void update(int dtMs, GameRandom& random, const EyeMapping& eyes);

    int pose() const { return pose_; }
    bool flipped() const { return flip_; }
    int eyes() const { return eyes_; }        // eye picture 0..3 (1: looking ahead)
    int blinkMs() const { return blinkMs_; }  // how long the current blink lasts
    // The eyes are shut while a blink lasts (GuiPosedMouse::render @0x224708).
    bool eyesShut() const { return timerMs_ < blinkMs_; }

private:
    int pose_ = 0;
    bool flip_ = false;
    int timerMs_ = 0, nextMs_ = 120;
    int eyes_ = 1, blinkMs_ = 0;
    int looks_ = 0, looksBeforeChange_ = 0;
};

// One popup over the level.
struct LevelNotice {
    std::string layout;  // popup_tutorials/<name>
    int pose = 1;        // <mouse pose=".." flip=".."/>
    bool flip = false;
    std::string titleText, bodyText, image;  // text ids and picture
    std::string bodyExtraText;  // <body_text_extra>: a second text in place of the picture
    bool wifiHint = false;      // <wifi_hint>: the Bluetooth/Wi-Fi line is shown
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(layout, pose, flip, titleText, bodyText, image, bodyExtraText, wifiHint); }
};

class NoticePopup {
public:
    // Opens the popup: its layout is made, so its card starts bouncing in.
    void open(const LevelNotice& notice);
    bool active() const { return active_; }
    // The popup's button was released (ScenePopupTutorial::guiEvent @0x22582c): popScene at
    // once, no fade.
    void dismiss() { active_ = false; }
    // The scene has no update of its own in 1.1.1: Scene::update runs its items (the card's
    // bounce, the scroll frame's easing).
    void update(int dtMs);

    const LevelNotice& notice() const { return notice_; }
    const BounceFrame& card() const { return card_; }  // the layout's GuiBounceFrameCM
    // Updates since it opened (its scroll frame eases once per update: GuiScrollFrame::update).
    int updates() const { return updates_; }

private:
    LevelNotice notice_;
    BounceFrame card_;
    int updates_ = 0;
    bool active_ = false;
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(notice_, card_, updates_, active_); }
};

}  // namespace doc::game
