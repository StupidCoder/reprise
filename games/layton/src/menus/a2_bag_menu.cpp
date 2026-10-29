// The bag (overlay 17). See a2_bag_menu.hpp.
#include "menus/a2_bag_menu.hpp"
#include "menus/a2_file_dialog.hpp"
#include "puzzles/judgment.hpp"

namespace picarat {
BagMenu::BagMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                 GameState& state, std::uint32_t& random, std::uint32_t playSeconds)
    : kit_(assets, std::move(present), audio, SceneKind::Bag), state_(state), random_(random),
      playSeconds_(playSeconds) {
    // 020efda0. Room sound of the current room event (+0x81d2), ambience stopped.
    if (auto s = kit_.sound()) {
        s->roomSound(state_.roomSoundEvent, true);
        s->stopUniqueEffect(0);
    }
    diaryPresent_ = false;
    createItems();
    resetWindow_ = kit_.load("menu/bag/reset_window.spr", 0, 0);
    resetFont_ = kit_.load("menu/bag/%s/reset_font.spr", 0, 0);
    close_ = kit_.button("menu/bag/%s/tojiru.spr", 0, 0, 0x7a);
    reset_ = kit_.button("menu/bag/reset.spr", 0, 0, 3);
    yes_ = kit_.button("system/btn/%s/yes.spr", 0, 0, 3, "reset_p");
    no_ = kit_.button("system/btn/%s/no.spr", 0, 0, 4, "reset_p");
    memo_ = kit_.button("menu/bag/%s/memo.spr", 0, 0, 3);
    mysteriesButton_ = kit_.button("menu/bag/%s/hukamaru.spr", 0, 0, 3);
    index_ = kit_.button("menu/bag/%s/jiten.spr", 0, 0, 3);
    saveButton_ = kit_.button("menu/bag/%s/save.spr", 0, 0, 3);
    window_ = kit_.load("tobj/window.spr", 0, 0);
    newMark_ = kit_.load("system/btn/?/new_button.spr", 0, 0);
    bigDigits_ = kit_.load("menu/bag/status_number2.sbj", 0, 0);
    smallDigits_ = kit_.load("menu/bag/status_number.sbj", 0, 0);
    // Medal by hint coins (+0x2f4): above 30/50/80/150/229 coins.
    medal_ = kit_.load("menu/bag/medal_icon.sbj", 0x6c, 0x4c);
    const unsigned coins = state_.progress.hintCoins();
    unsigned medal = 1;
    if (coins > 0x1e) {
        medal = 2;
    }
    if (coins > 0x32) {
        medal = 3;
    }
    if (coins > 0x50) {
        medal = 4;
    }
    if (coins > 0x96) {
        medal = 5;
    }
    if (coins > 0xe5) {
        medal = 6;
    }
    kit_.select(medal_, medal);
    walker_ = kit_.load("menu/bag/dot_protagonist_walk.sbj", 0x14, 0x98);
    if (playSeconds_ >= 0x57e3f) {
        kit_.select(walker_, 2u); // 99:59:59 and more
    }
    items_ = kit_.load("menu/bag/item_icon.spr", 0, 0);
    kit_.lowerBackground = "menu/bag/ge/bag_bg";
    kit_.upperBackground = "menu/bag/ge/bag_sub";
    // Bag-menu tea requests (state +0x8238) once the story step exceeds 169:
    // The protagonist first, then the assistant; each comes with a 21 % roll that, once won,
    // stays pending (+0x387) until the tea is served (ov14 020f16dc).
    int tutorial = 0;
    if (state_.progress.storyStep() > 0xa9) {
        if (!state_.progress.hintCoin(127, 0)) {
            tutorial = state_.progress.teaRequestPending();
            if (!tutorial && int(gameRandom(random_)) % 100 < 0x15) {
                tutorial = 1;
                state_.progress.setTeaRequestPending(true);
            }
        } else if (!state_.progress.hintCoin(127, 1)) {
            if (!state_.progress.teaRequestPending()) {
                if (int(gameRandom(random_)) % 100 < 0x15) {
                    tutorial = 2;
                    state_.progress.setTeaRequestPending(true);
                }
            } else {
                tutorial = 2;
            }
        }
    }
    state_.teaTutorial = tutorial;
    help_ = kit_.load("menu/bag/?/help_mes.spr", 0, 0);
    state_.following = scene::Menu;
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.wait(1);
        }
    }
}

// Loads the bag's buttons, showing only the features the story has unlocked.
void BagMenu::createItems() {
    // 020f17e8: diary, camera, hamster and tea; with the diary present the
    // other three use their "pos2" positions.
    std::string position = "pos";
    bool diary = false;
    for (unsigned i = 0; i < 16; ++i) {
        diary |= state_.progress.bit(0x314, 16, i); // 02083040
    }
    if (diary) {
        diaryPresent_ = true;
        diary_ = kit_.button("menu/bag/%s/diary.spr", 0, 0, 3);
        position = "pos2";
    }
    if (!state_.progress.u16(0x24c)) {
        noCamera_ = kit_.load("menu/bag/no_camera.spr", 0, 0); // 02082914
    } else {
        camera_ = kit_.button(state_.progress.bit(0x24c, 16, 10) ? "menu/bag/%s/camera_fix.spr"
                                                                 : "menu/bag/%s/camera.spr",
                              0, 0, 3, position); // CameraSolved
    }
    if (!state_.progress.bytes[0x283]) {
        noHamster_ = kit_.load("menu/bag/no_ham.spr", 0, 0); // 02082ab0
    } else {
        hamster_ = kit_.button(state_.progress.bytes[0x283] == 6 ? "menu/bag/%s/ham_fix.spr"
                                                                 : "menu/bag/%s/ham.spr",
                               0, 0, 3, position); // 020859f4
    }
    if (state_.progress.anyHerb()) {
        tea_ = kit_.button(state_.teaComplete() ? "menu/bag/%s/tea_fix.spr" : "menu/bag/%s/tea.spr",
                           0, 0, 3, position);
    } else {
        noTea_ = kit_.load("menu/bag/no_tea.spr", 0, 0);
    }
}

// Status numbers in the large and small digit sprites.
void BagMenu::bigNumber(int x, int y, int value, int digits) {
    // 020f142c: right to left, 0x11 apart, without leading zeros.
    for (int i = 0; i < digits; ++i) {
        kit_.select(bigDigits_, unsigned(value % 10 + 1));
        bigDigits_.x = x;
        bigDigits_.y = y;
        kit_.draw(bigDigits_);
        x -= 0x11;
        value /= 10;
        if (value == 0) {
            return;
        }
    }
}

void BagMenu::smallNumber(int x, int y, int value, int digits, bool all) {
    // 020f14bc: 6 apart; `all` keeps leading zeros.
    for (int i = 0; i < digits; ++i) {
        kit_.select(smallDigits_, unsigned(value % 10 + 1));
        smallDigits_.x = x;
        smallDigits_.y = y;
        kit_.draw(smallDigits_);
        value /= 10;
        x -= 6;
        if (!all && value == 0) {
            return;
        }
    }
}

// Play time as hours and minutes.
void BagMenu::minutes(int x, int y, int value) {
    // 020f1558: minutes of the hour, two digits (value % 10, value / 10 % 6).
    for (int i = 0; i < 2; ++i) {
        kit_.select(smallDigits_, unsigned((i == 0 ? value % 10 : value % 6) + 1));
        smallDigits_.x = x;
        smallDigits_.y = y;
        kit_.draw(smallDigits_);
        x -= 6;
        value /= 10;
    }
}

// The status panel: puzzles found and solved, picarats, hint coins, play
// time and the current place.
void BagMenu::status() {
    // 020f126c: upper-screen status.
    bigNumber(0x3f, 0x2d, int(state_.solvedCount()), 3);
    int found = 0;
    for (int i = 1; i < 0x8b; ++i) {
        if (state_.puzzleStatus(i) - 1u < 2) {
            ++found; // 02083e40
        }
    }
    bigNumber(0x3f, 0x71, found, 3);
    bigNumber(0xbf, 0x20, state_.progress.picarats(), 4);
    kit_.draw(medal_);
    smallNumber(0xce, 0x55, state_.progress.hintCoins(), 3, false);
    smallNumber(0xe8, 0x55, state_.progress.hintCoinsFound(), 3, false);
    kit_.draw(walker_);
    kit_.textCentered(0xa1, state_.placeName, 0, 0xa4, SceneKit::Upper);
    smallNumber(0xa7, 0x83, int(playSeconds_ / 3600), 2, true);
    minutes(0xd2, 0x83, int(playSeconds_ / 60));
}

// Draws the bag (and, with `update`, runs its buttons).
void BagMenu::draw(bool update) {
    // 020f0784: buttons, "new" marks and the status.
    auto button = [&](SpriteButton& b) {
        if (update) {
            kit_.updateButton(b);
        }
        kit_.drawButton(b);
    };
    auto mark = [&](int x, int y) {
        newMark_.x = x;
        newMark_.y = y;
        kit_.draw(newMark_);
    };
    button(memo_);
    if (state_.progress.menuNew(0)) {
        mark(0xd, 0x26);
    }
    button(mysteriesButton_);
    if (state_.progress.menuNew(1)) {
        mark(0x49, 0x16);
    }
    button(index_);
    button(saveButton_);
    if (!state_.progress.u16(0x24c)) {
        kit_.draw(noCamera_);
    } else {
        button(camera_);
        if (state_.progress.menuNew(2)) {
            mark(diaryPresent_ ? 0x47 : 0x24, 0x5e);
        }
    }
    if (!state_.progress.bytes[0x283]) {
        kit_.draw(noHamster_);
    } else {
        button(hamster_);
        if (state_.progress.menuNew(3)) {
            mark(diaryPresent_ ? 0x82 : 0x66, 100);
        }
    }
    if (!state_.progress.anyHerb()) {
        kit_.draw(noTea_);
    } else {
        button(tea_);
        if (state_.teaTutorial == 0) {
            if (state_.progress.menuNew(4)) {
                mark(diaryPresent_ ? 0xbe : 0xab, 0x5f);
            }
        } else {
            help_.x = diaryPresent_ ? 0xbe : 0xab;
            help_.y = 0x5f;
            kit_.draw(help_);
        }
    }
    if (diary_.exists) {
        button(diary_);
        if (state_.progress.menuNew(6)) {
            mark(0xd, 0x62);
        }
    }
    // Up to three story items.
    for (int i = 0, shown = 0, x = 0x66; i < 8; ++i) {
        if (!state_.progress.storyItem(std::uint32_t(i))) {
            continue;
        }
        kit_.select(items_, unsigned(i + 1));
        items_.x = x;
        items_.y = 0x9e;
        kit_.draw(items_);
        x += 0x1c;
        if (++shown > 2) {
            break;
        }
    }
    button(close_);
    button(reset_);
    status();
}

// Releases every button but one.
void BagMenu::resetOthers(int keep) {
    // 020f0af8: every other button returns to idle (02080204(b, 0)).
    SpriteButton* buttons[]{&close_, &reset_,      &memo_,   &mysteriesButton_,
                            &index_, &saveButton_, &camera_, &hamster_,
                            &tea_,   &diary_};
    for (int i = 0; i < 10; ++i) {
        if (i != keep && buttons[i]->exists) {
            buttons[i]->button.state = Button::State::Idle;
            kit_.select(buttons[i]->sprite, buttons[i]->idleAnim);
        }
    }
}

// One frame of the "back to the title?" window fading in or out.
void BagMenu::resetFrame(int alpha) {
    // 020f1744.
    kit_.poll();
    kit_.drawButton(close_);
    kit_.draw(resetWindow_, alpha);
    kit_.draw(resetFont_, alpha);
    if (alpha == 0x1f) {
        kit_.textTitle(0x3c, text_, 0, 0x80, SceneKit::Lower);
        kit_.updateButton(yes_);
        kit_.drawButton(yes_);
        kit_.updateButton(no_);
        kit_.drawButton(no_);
    }
    status();
    kit_.present();
}

// "Back to the title?" Yes leaves the game for the title screen.
void BagMenu::resetPrompt() {
    // 020f1600: "return to the title?" (tx 100); yes resets the game.
    text_ = kit_.systemText(100);
    for (int alpha = 1; alpha < 0x1f; alpha += 2) {
        resetFrame(alpha);
    }
    for (;;) {
        resetFrame(0x1f);
        if (kit_.clicked(yes_)) {
            if (auto s = kit_.sound()) {
                s->stopUniqueEffect(s->duration(0x3f3));
                s->stopStream(s->duration(0x3f3));
                s->stopBgm(s->duration(0x3f3));
            }
            throw RestartGame{}; // 02071b80 fade, 0207f5c8, 0201fe14(0x12) soft reset
        }
        if (kit_.clicked(no_)) {
            break;
        }
    }
    no_.button.state = Button::State::Idle;
    kit_.clearText(SceneKit::Lower);
    for (int alpha = 0x1e; alpha > 1; alpha -= 2) {
        resetFrame(alpha);
    }
}

// Leaves the bag for a feature; the first visit plays its introduction
// event first.
void BagMenu::leaveFor(std::uint16_t firstEvent, std::uint8_t scene) {
    // Camera/hamster/tea: a first visit plays its introduction event.
    if (!state_.eventViewed(firstEvent)) {
        state_.eventId = firstEvent;
        state_.setEventViewed(firstEvent, true);
        state_.scene = scene::DramaEvent;
    } else {
        state_.scene = scene;
    }
    if (auto s = kit_.sound()) {
        state_.savedBgm = s->currentBgm();
        s->fadeOutBgm(s->duration(0x3e9));
    } // +0x7e84 = 0207fa88
    kit_.fadeOut();
}

// Runs the bag until the player closes it or picks a feature.
void BagMenu::run() {
    // 020f0bdc.
    kit_.poll();
    draw(true);
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw(true);
        kit_.present();
        bool indexEmpty = false;
        if (kit_.clicked(close_)) {
            resetOthers(0);
            state_.scene = scene::Room;
            kit_.fadeOut();
            return;
        }
        if (kit_.clicked(reset_)) {
            resetOthers(1);
            resetPrompt();
        } else if (kit_.clicked(memo_)) {
            resetOthers(2);
            kit_.fadeOut();
            state_.scene = scene::Notes;
            return;
        } else if (kit_.clicked(mysteriesButton_)) {
            resetOthers(3);
            kit_.fadeOut();
            if (mysteries) {
                mysteries(); // 02069294 (ov20)
            }
            kit_.poll();
            draw(true);
            kit_.present();
            kit_.fadeIn();
        } else if (kit_.clicked(index_)) {
            resetOthers(4);
            int found = 0;
            for (int i = 1; i < 0x8b; ++i) {
                if (state_.puzzleStatus(i) - 1u < 2) {
                    ++found;
                }
            }
            if (found) {
                if (auto s = kit_.sound()) {
                    state_.savedBgm = s->currentBgm();
                }
                state_.scene = scene::PuzzleIndex;
                kit_.fadeOut();
                return;
            }
            indexEmpty = true;
        } else if (kit_.clicked(saveButton_)) {
            // 020f0de0: the file dialog (constructed with the bag as owner,
            // which keeps drawing its status), no resume event; then the
            // bag's background again and the main screen fades in.
            resetOthers(5);
            if (saveFile) {
                FileDialog dialog(kit_, state_, *saveFile, [this] { status(); }, true);
                dialog.save(-1, foldTime, playerName);
            }
            kit_.poll();
            kit_.lowerBackground = kit_.localized("menu/bag/?/bag_bg");
            draw(true);
            kit_.present();
            kit_.fadeInLower();
        } else if (camera_.exists && kit_.clicked(camera_)) {
            resetOthers(6);
            state_.progress.setMenuNew(2, false);
            if (!state_.progress.bit(0x24c, 16, 10)) {
                leaveFor(0x47ea, scene::SubCamera);
            } else {
                state_.scene = scene::PhotoAlbum;
                if (auto s = kit_.sound()) {
                    state_.savedBgm = s->currentBgm();
                    s->fadeOutBgm(s->duration(0x3e9));
                }
                kit_.fadeOut();
            }
            return;
        } else if (hamster_.exists && kit_.clicked(hamster_)) {
            resetOthers(7);
            state_.progress.setMenuNew(3, false);
            leaveFor(0x47f4, scene::SubHam);
            return;
        } else if (tea_.exists && kit_.clicked(tea_)) {
            resetOthers(8);
            state_.progress.setMenuNew(4, false);
            leaveFor(0x47e0, scene::SubHerb);
            return;
        } else if (diary_.exists && kit_.clicked(diary_)) {
            resetOthers(9);
            if (auto s = kit_.sound()) {
                state_.savedBgm = s->currentBgm();
                s->fadeOutBgm(s->duration(0x3e9));
            }
            kit_.fadeOut();
            state_.scene = scene::Diary;
            return;
        }
        if (indexEmpty) {
            // No puzzle found yet: tx 0x3e9 in the window until a release.
            text_ = kit_.systemText(0x3e9);
            do {
                kit_.poll();
                draw(false);
                kit_.draw(window_);
                kit_.textTitle(0x54, text_, 0, 0x80, SceneKit::Lower);
                kit_.present();
            } while (!kit_.released());
            kit_.effect(4);
            kit_.wait(1);
            kit_.clearText(SceneKit::Lower);
        }
    }
}
} // namespace picarat
