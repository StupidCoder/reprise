// Notices the original shows over a level before play (GameBoot::bootLevel @0x1f5142): a
// tutorial popup (ScenePopupTutorial, layout in res/popup_tutorials/<name>.xml.brd) with a
// posed mouse. While it is up only the popup and the ambience run; the level waits.
// So far only the "carry more" notice is reconstructed (SceneGame::initGame @0x10639e raises the
// profile's strength to the level's trail length and asks for new_cheese_slot0<strength-1>).
#pragma once

#include "game/random.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace doc::game {

// The mouse drawn in popups (GuiPosedMouse @0x224608..0x203fa4): a pose picture with eyes that
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
};

class NoticePopup {
public:
    // Opens the popup: the posed mouse is created (pose 1) and then given the layout's pose.
    void open(const LevelNotice& notice, GameRandom& random);
    bool active() const { return active_; }
    // The popup's button was pressed (ScenePopupTutorial::guiEvent): it fades out and closes.
    void dismiss() { if (active_) closing_ = true; }
    // ScenePopupTutorial::update @0x204c78 (1.0.5).
    void update(int dtMs, GameRandom& random, const PosedMouse::EyeMapping& eyes);

    const LevelNotice& notice() const { return notice_; }
    const PosedMouse& mouse() const { return mouse_; }
    float alpha() const { return alpha_; }
    // Updates since it opened (its scroll frame eases once per update: GuiScrollFrame::update).
    int updates() const { return updates_; }

private:
    LevelNotice notice_;
    int updates_ = 0;
    PosedMouse mouse_;
    bool active_ = false, closing_ = false;
    float alpha_ = 1.0f;
};

}  // namespace doc::game
