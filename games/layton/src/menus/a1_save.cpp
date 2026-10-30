// The first adventure's save and load screens. See a1_save.hpp.
#include "menus/a1_save.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"

namespace picarat::adventure1 {

namespace {
// 0200c1ac: 8-bit components to the 3D engine's 15-bit colour.
std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

// The slots' layout tables of the program (three words each).
int table(const Story& story, std::uint32_t address, int slot) {
    return int(std::int32_t(story.programWord(address + 4 * std::uint32_t(slot))));
}
} // namespace

SlotList::SlotList(Story& s) : story(s), kit(s.kit) {
    // The owners' constructors (event 02025d48, title 0202ce14): the slots
    // are plain rectangles 0x30 apart, the close button sits at its "pos"
    // variable.
    for (int n = 0; n < 3; ++n) {
        slots_[n].setRect(4, 0x24 + 0x30 * n, 0xf8, 0x29);
    }
    back_.load(kit, "?/load_btn.spr", 4);
    numbers_ = kit.load("file_numbers.spr", 0, 0, 1);
}

void SlotList::enter(const std::string& title, int titleY) {
    kit.clearText(SceneKit::Lower);
    kit.lowerPriority = std::array<int, 4>{2, 1, 0, 3};
    message_ = kit.load(title, 0, titleY, 1);
    message_.x = (0x100 - kit.frameWidth(message_)) >> 1;
}

SaveScreen::SaveScreen(Story& s, From f, SceneKit::SpriteObject& window)
    : SlotList(s), from(f), window_(window) {
    yes_.setSprite(kit, 0x28, 0x68, "?/room_buttons.spr", "yes");
    yes_.visibleIdle = true;
    yes_.setPressedAnimation(kit, "yesp");
    yes_.sound = 0xb;
    no_.setSprite(kit, 0x88, 0x68, "?/room_buttons.spr", "no");
    no_.visibleIdle = true;
    no_.setPressedAnimation(kit, "nop");
    no_.sound = 4;
}

void SlotList::writeSlotTexts() {
    // 0200b968: a slot in use shows its player's name (row 020bbf04) and
    // place (row 020bbee0) centred on x 0x60; an empty one sys_600.txt.
    kit.clearText(SceneKit::Lower);
    const SaveImage& saves = story.saves();
    for (int n = 0; n < 3; ++n) {
        const auto info = saves.slot(n);
        if (!info.used) {
            drawCentredText(kit, SceneKit::Lower, 0x34 + 0x30 * n,
                            story.text("stext", "stext", "sys_600.txt"), true, -1, 0x60);
        } else {
            drawCentredLine(kit, SceneKit::Lower, table(story, 0x020bbf04, n), info.name, 0x60);
            drawCentredLine(kit, SceneKit::Lower, table(story, 0x020bbee0, n), info.place, 0x60);
        }
    }
}

void SlotList::draw() {
    // 02028318: per slot the "cleared" mark, the play time (hours modulo
    // 100 and minutes, two digits each), the puzzles solved and found (up
    // to three digits, leading zeros left out); the title; the slots'
    // backings (0200bb00); the close button.
    if (drawOwner) {
        drawOwner();
    }
    const SaveImage& saves = story.saves();
    auto digit = [&](int value, int x, int y) {
        kit.select(numbers_, unsigned(value + 1));
        numbers_.x = x;
        numbers_.y = y;
        kit.draw(numbers_);
    };
    for (int n = 0; n < 3; ++n) {
        const auto info = saves.slot(n);
        if (info.cleared) {
            kit.select(numbers_, "clear");
            numbers_.x = 0xa8;
            numbers_.y = table(story, 0x020bc1b4, n);
            kit.draw(numbers_);
        }
        const int hours = int(info.hours), minutes = int(info.minutes);
        const int hx = table(story, 0x020bc190, n), hy = table(story, 0x020bc19c, n);
        digit(hours % 100 / 10, hx, hy);
        digit(hours % 10, hx + 8, hy);
        const int mx = table(story, 0x020bc1a8, n), my = table(story, 0x020bc1cc, n);
        digit(minutes % 60 / 10, mx, my);
        digit(minutes % 10, mx + 8, my);
        const int py = table(story, 0x020bc1c0, n);
        auto count = [&](int value, int x) {
            if (value % 1000 / 100 != 0) {
                digit(value % 1000 / 100, x, py);
            }
            if (value % 100 / 10 != 0 || value > 99) {
                digit(value % 100 / 10, x + 8, py);
            }
            digit(value % 10, x + 0x10, py);
        };
        count(int(info.solved), 0xc4);
        count(int(info.seen), 0xe2);
    }
    if (message_.loaded()) {
        kit.draw(message_);
    }
    for (int n = 0; n < 3; ++n) {
        const int x = table(story, 0x020bbeec, n), y = table(story, 0x020bbef8, n);
        if (!saves.slot(n).used) {
            kit.rectangle(x, y, 0x88, 0x29, rgb(0xb9, 0xb9, 0xaa), 0x1f);
        } else {
            kit.rectangle(x, y, 0x88, 0x14, rgb(0xf8, 200, 0x58), 0x1f);
            kit.rectangle(x, y + 0x14, 0x88, 1, rgb(0x31, 0x1c, 0x10), 0x1f);
            kit.rectangle(x, y + 0x15, 0x88, 0x14, rgb(0xff, 0xfb, 0xe7), 0x1f);
        }
    }
    back_.update(kit, story.touch);
}

bool SaveScreen::confirm() {
    // 02028b94: the question window with yes and no; "yes" itself plays no
    // effect, the music fades out (over 30 frames) instead.
    auto frame = [&] {
        story.poll();
        draw();
        kit.draw(window_, 0x1f);
        yes_.update(kit, story.touch);
        no_.update(kit, story.touch);
        story.present();
    };
    yes_.cancel();
    no_.cancel();
    frame();
    for (;;) {
        frame();
        if (no_.clicked(kit, story.touch)) {
            story.poll();
            kit.clearText(SceneKit::Lower);
            return false;
        }
        if (yes_.clicked(kit, story.touch, false)) {
            break;
        }
    }
    if (story.sound) {
        story.sound->stopMusicSoftly();
    }
    kit.clearText(SceneKit::Lower);
    return true;
}

bool SaveScreen::run() {
    Adventure1Sound* sound = story.sound;
    auto wait = [this] {
        story.stall(1);
    };
    fadeOut(kit, Screens::Lower, 1);
    if (from == From::Trunk && sound) {
        sound->setupScene(4, wait); // 02004b08(4)
    }
    enter("?/save_mes.spr", 9);
    writeSlotTexts();
    story.poll();
    // 02009d34 remembers the picture's name; 02009d60 loads it again,
    // which also drops a tint.
    const std::string oldBackground = kit.lowerBackground;
    draw();
    kit.lowerBackground = "filesave_bg";
    kit.lowerTint = {};
    story.present();
    fadeIn(kit, Screens::Lower, 1);

    bool saved = false;
    while (!saved) {
        story.poll();
        draw();
        story.present();
        if (back_.clicked()) {
            break;
        }
        for (auto& slot : slots_) {
            slot.update(kit, story.touch);
        }
        if (!story.touch.released) {
            continue;
        }
        for (int n = 0; n < 3; ++n) {
            if (!slots_[n].clicked(kit, story.touch)) {
                continue;
            }
            const auto info = story.saves().slot(n);
            if (info.used) {
                // sys_100.txt names the slot's player.
                if (sound) {
                    sound->effect(3);
                }
                kit.clearText(SceneKit::Lower);
                std::string question = story.text("stext", "stext", "sys_100.txt");
                if (const auto at = question.find("%s"); at != question.npos) {
                    question.replace(at, 2, info.name);
                }
                drawCentredText(kit, SceneKit::Lower, 0x4a, question, false, 3, 0x80);
                if (!confirm()) {
                    writeSlotTexts();
                    break;
                }
            }
            // 0201abec writes the backup; a write error shows
            // save_writeerror.bgx and stops the game (0200b590).
            if (sound) {
                sound->moveMusicVolume(0, 0x14);
            }
            story.poll();
            draw();
            if (sound) {
                sound->effect(5);
            }
            kit.lowerOverlay = kit.localized("?/filesaving_bg");
            writeSlotTexts();
            story.present();
            story.stall(0x3c);
            // The trunk saves with the scene "room" (3), so that loading
            // returns to the room rather than to the trunk.
            const auto scene = story.state.scene;
            if (from == From::Trunk) {
                story.state.scene = scene::Room;
            }
            const bool written = story.storeGame(n);
            story.state.scene = scene;
            if (!written) {
                story.saveWriteError();
            }
            saved = true;
            kit.lowerOverlay.clear(); // 02009650
            story.poll();
            draw();
            writeSlotTexts();
            story.present();
            story.stall(0x3c);
            break;
        }
    }

    fadeOut(kit, Screens::Lower, 1);
    // 020042dc / 02023340's end: the owner's priorities.
    kit.lowerPriority =
        from == From::Trunk ? std::array<int, 4>{2, 3, 1, 3} : std::array<int, 4>{1, 0, 2, 3};
    kit.clearText(SceneKit::Lower);
    story.poll();
    kit.lowerBackground = oldBackground;
    if (drawOwner) {
        drawOwner();
    }
    story.present();
    if (sound) {
        sound->fadeInMusic(); // 02005144
    }
    fadeIn(kit, Screens::Lower, 1);
    if (from == From::Trunk && sound) {
        sound->setupScene(3, wait);
    }
    return saved;
}

bool LoadScreen::run() {
    // 0202dd5c (the bonus section's: 0202e050).
    fadeOut(kit, Screens::Lower, 1);
    enter(bonus_ ? "?/secret_mes.spr" : "?/load_mes.spr", bonus_ ? 9 : 5);
    writeSlotTexts();
    story.poll();
    draw();
    // The title's pictures give way to the screen's (02009650, 020096b4).
    kit.lowerOverlay.clear();
    kit.lowerAutoScroll = {};
    kit.lowerOverlayAutoScroll = {};
    kit.lowerScrollX = kit.lowerOverlayScrollX = 0;
    kit.lowerBackground = bonus_ ? "filesecret_bg" : "fileload_bg";
    if (story.sound) {
        story.sound->setupScene(4, [this] { story.stall(1); });
    }
    draw();
    story.present();
    fadeIn(kit, Screens::Lower, 1);
    bool loaded = false;
    for (;;) {
        story.poll();
        draw();
        story.present();
        if (back_.clicked()) {
            break;
        }
        for (auto& slot : slots_) {
            slot.update(kit, story.touch);
        }
        for (int n = 0; n < 3 && !loaded; ++n) {
            if (!slots_[n].clicked(kit, story.touch) || !story.saves().slot(n).used) {
                continue;
            }
            // A "loading" picture over the slot for half a second.
            if (story.sound) {
                story.sound->effect(6);
            }
            story.poll();
            draw();
            kit.lowerOverlay = kit.localized("?/fileloading_bg" + std::to_string(n + 1));
            story.present();
            loaded = story.loadGame(n);
            story.stall(0x1e);
            story.poll();
            draw();
            kit.lowerOverlay.clear();
            story.present();
        }
        if (loaded) {
            break;
        }
    }
    fadeOut(kit, loaded ? Screens::Both : Screens::Lower, 1);
    kit.clearText(SceneKit::Lower);
    return loaded;
}

} // namespace picarat::adventure1
