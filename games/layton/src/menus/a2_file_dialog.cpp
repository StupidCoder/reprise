// The save and load screens with their three file slots. See a2_file_dialog.hpp.
#include "menus/a2_file_dialog.hpp"
#include "story/a2_save_slots.hpp"
#include <cstdio>

namespace picarat {
namespace {
std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10); // 0206b114
}

std::string replaceFirst(std::string text, const std::string& token, const std::string& value) {
    const auto p = text.find(token);
    if (p != text.npos) {
        text.replace(p, token.size(), value);
    }
    return text;
}
} // namespace

FileDialog::FileDialog(SceneKit& kit, GameState& state, SaveFile& file,
                       std::function<void()> ownerDraw, bool ownerSlides)
    : kit_(kit), state_(state), file_(file), ownerDraw_(std::move(ownerDraw)),
      ownerSlides_(ownerSlides) {
    // 02085b50: cancel (sound 4); three spriteless slot buttons at
    // (3, 31 + 46 i), 250x43, sound 3; the digits; the save/load windows;
    // the question window (actor-type sprite at (20, 56), animation
    // "window"); yes (no sound) and no (sound 4) at "win_p".
    cancel_ = kit_.button("system/btn/?/cancel.spr", 0, 0, 4);
    for (int i = 0; i < 3; ++i) {
        auto& b = slots_[std::size_t(i)];
        b.exists = true;
        b.x = 3;
        b.y = 31 + 46 * i;
        b.button = Button({3, 31 + 46 * i, 250, 43}, 3);
    }
    numbers_ = kit_.load("system/file_numbers.spr", 0, 0, 1);
    saveWindow_ = kit_.load("menu/save/?/save_window.spr", 0, 0, 1);
    loadWindow_ = kit_.load("menu/save/?/load_window.spr", 0, 0, 1);
    window_ = kit_.load("system/prize_window.spr", 20, 56, 1);
    kit_.select(window_, "window");
    window_.x = 20;
    window_.y = 56;
    yes_ = kit_.button("system/btn/?/yes.spr", 0, 0, -1, "win_p");
    kit_.setButtonAnimations(yes_, "on", "off", "click");
    no_ = kit_.button("system/btn/?/no.spr", 0, 0, 4, "win_p");
    kit_.setButtonAnimations(no_, "on", "off", "click");
}

// The slot buttons, the summaries and the owner's parts (02085f50).
void FileDialog::draw(bool active) {
    if (active) {
        kit_.updateButton(cancel_);
    }
    kit_.drawButton(cancel_);
    for (auto& s : slots_) {
        if (active) {
            kit_.updateButton(s);
        }
    }
    summaries();
    if (ownerDraw_) {
        ownerDraw_();
    }
}

// Each slot's name and place, or "no data" (02085fbc).
void FileDialog::slotTexts() {
    for (int s = 0; s < 3; ++s) {
        if (!file_.used(s)) {
            // tx 501 centred by a fixed 7 px per character on x 138.
            const auto text = kit_.text.expand(kit_.systemText(0x1f5));
            kit_.textAt(138 - int(text.size()) * 7 / 2, 47 + 46 * s, text, SceneKit::Lower);
            continue;
        }
        kit_.textTitle(37 + 46 * s, file_.name(s), 0, 91, SceneKit::Lower);
        // The place label from jiten.plz; place 92 is shown as place 38.
        int place = file_.place(s);
        if (place == 92) {
            place = 38;
        }
        const auto label =
            kit_.text.expand(kit_.member("jiten", "p_" + std::to_string(place) + ".txt"));
        kit_.textTitle(57 + 46 * s, label, 0, 91, SceneKit::Lower, true);
    }
}

// Digits from x to the left in 8 px steps (02086140): hours and minutes
// always two (the minutes' tens digit counts to 6), counts as many as needed.
void FileDialog::digits(int x, int y, unsigned value, int count, bool minutes) {
    for (int i = 0; i < count; ++i) {
        const unsigned digit = minutes && i == 1 ? value % 6 : value % 10;
        kit_.select(numbers_, std::to_string(digit));
        numbers_.x = x;
        numbers_.y = y;
        kit_.draw(numbers_);
        value /= 10;
        x -= 8;
        if (count == 3 && value == 0) {
            break;
        }
    }
}

// The summaries (02086140; coordinate and colour tables 02087210..0208727c).
void FileDialog::summaries() {
    static constexpr int clearY[3]{35, 81, 127};
    static constexpr int r[3]{224, 144, 216}, g[3]{152, 176, 136}, b[3]{88, 184, 112};
    for (int s = 0; s < 3; ++s) {
        const int top = 46 * s;
        if (const unsigned level = file_.clearLevel(s)) {
            kit_.select(numbers_, level == 1 ? "clear" : level == 2 ? "clear2" : "clear3");
            numbers_.x = 154;
            numbers_.y = clearY[s];
            kit_.draw(numbers_);
        }
        const std::uint32_t seconds = file_.seconds(s);
        digits(179, 38 + top, seconds / 3600, 2, false);
        digits(220, 38 + top, seconds / 60, 2, true);
        digits(210, 57 + top, file_.solved(s), 3, false);
        digits(240, 57 + top, file_.found(s), 3, false);
        if (file_.used(s)) {
            kit_.rectangle(29, 34 + top, 121, 17, rgb(r[s], g[s], b[s]), 31);
            kit_.rectangle(29, 54 + top, 121, 17, rgb(255, 242, 216), 31);
        } else {
            kit_.rectangle(29, 34 + top, 221, 37, rgb(248, 216, 168), 31);
        }
    }
}

// "Overwrite <name>?" with yes/no (02086d64).
bool FileDialog::confirm(int slot) {
    kit_.poll();
    kit_.clearTextRect(20, 56, 200, 80);
    draw(false);
    kit_.draw(window_);
    kit_.updateButton(yes_);
    kit_.drawButton(yes_);
    kit_.updateButton(no_);
    kit_.drawButton(no_);
    const auto question =
        replaceFirst(kit_.text.expand(kit_.systemText(500)), "%s", file_.name(slot));
    kit_.textCentered(76, question, 0, 128, SceneKit::Lower);
    kit_.present();
    for (;;) {
        kit_.poll();
        draw(false);
        kit_.draw(window_);
        kit_.updateButton(yes_);
        kit_.drawButton(yes_);
        kit_.updateButton(no_);
        kit_.drawButton(no_);
        kit_.present();
        if (kit_.clicked(yes_)) {
            no_.button.state = Button::State::Idle; // 02080204(no, 0)
            return true;
        }
        if (kit_.clicked(no_)) {
            yes_.button.state = Button::State::Idle;
            return false;
        }
    }
}

// The window over the chosen slot: animation slot + 1 at variable "pos%i".
void FileDialog::showWindow(SpriteObject& window, int slot) {
    kit_.select(window, unsigned(slot + 1));
    int x = window.x, y = window.y;
    if (kit_.variable(window, "pos" + std::to_string(slot + 1), x, y)) {
        window.x = x;
        window.y = y;
    }
    kit_.draw(window);
}

bool FileDialog::save(int resumeEvent, const std::function<std::uint32_t()>& fold,
                      const std::string& name) {
    // 020866d0: the screen fades out (02072260), room sound 6 off, the
    // display set up for blending (0206df84), the save background.
    kit_.fadeOutLower();
    if (auto s = kit_.sound()) {
        s->roomSound(6, false);
    }
    kit_.resetDisplay();
    kit_.poll();
    kit_.lowerBackground = kit_.localized("menu/bag/?/filesave_bg");
    kit_.clearText(SceneKit::Lower);
    slotTexts();
    draw(false);
    kit_.present();
    kit_.fadeInLower();
    bool saved = false;
    for (bool done = false; !done;) {
        kit_.poll();
        draw(true);
        kit_.present();
        if (kit_.clicked(cancel_)) {
            for (auto& s : slots_) {
                s.button.state = Button::State::Idle;
            }
            break;
        }
        for (int slot = 0; slot < 3; ++slot) {
            if (!kit_.clickedNow(slots_[std::size_t(slot)])) {
                continue;
            }
            cancel_.button.state = Button::State::Idle;
            if (file_.used(slot)) {
                kit_.effect(3);
                const bool overwrite = confirm(slot);
                kit_.poll();
                draw(true);
                kit_.present();
                kit_.clearTextRect(0, 56, 256, 80);
                slotTexts();
                if (!overwrite) {
                    break;
                }
            }
            // Saving: the music fades out (time 1200), SE 205, the slot's
            // text cleared, the "saving" window over the slot, one second.
            if (auto s = kit_.sound()) {
                s->bgmVolume(0.0f, s->duration(1200));
            }
            kit_.effect(205);
            kit_.clearTextRect(56, 40 + 46 * slot, 164, 26);
            kit_.poll();
            draw(false);
            showWindow(saveWindow_, slot);
            kit_.present();
            kit_.wait(60);
            if (resumeEvent > 0) {
                state_.progress.setPendingEvent(std::int16_t(resumeEvent)); // +0x3da
            }
            const std::uint32_t seconds = fold ? fold() : 0;
            state_.saveSlot = slot;
            saveToSlot(file_, state_, slot, true, name, seconds); // 02083638(slot, 1)
            saved = true;
            kit_.poll();
            draw(false);
            kit_.present();
            kit_.clearTextRect(29, 34 + 46 * slot, 164, 37);
            slotTexts();
            kit_.wait(60);
            done = true;
            break;
        }
    }
    // The music returns (time 1201); the bag slides out with 02072260,
    // other callers fade both screens.
    if (auto s = kit_.sound()) {
        s->bgmVolume(1.0f, s->duration(1201));
    }
    if (ownerSlides_) {
        kit_.fadeOutLower();
    } else {
        kit_.fadeOut();
    }
    kit_.clearText(SceneKit::Lower);
    state_.progress.setPendingEvent(-1);
    return saved;
}

int FileDialog::load(int mode, std::string& name) {
    // 02086ad4.
    kit_.fadeOutLower();
    if (auto s = kit_.sound()) {
        s->roomSound(6, false);
    }
    kit_.resetDisplay();
    kit_.poll();
    kit_.lowerBackground = kit_.localized("menu/bag/?/fileload_bg" + std::to_string(mode + 1));
    kit_.clearText(SceneKit::Lower);
    slotTexts();
    draw(false);
    kit_.present();
    kit_.fadeInLower();
    int loaded = -1;
    while (loaded < 0) {
        kit_.poll();
        draw(true);
        kit_.present();
        if (kit_.clicked(cancel_)) {
            break;
        }
        for (int slot = 0; slot < 3; ++slot) {
            if (!kit_.clicked(slots_[std::size_t(slot)]) || !file_.used(slot)) {
                continue;
            }
            // The music fades (time 0x3e9), SE 206, the "loading" window,
            // one second, then a fresh state (02083368) gets the slot.
            if (auto s = kit_.sound()) {
                s->fadeOutBgm(s->duration(0x3e9));
            }
            kit_.poll();
            kit_.effect(206);
            draw(false);
            kit_.clearTextRect(56, 40 + 46 * slot, 164, 26);
            showWindow(loadWindow_, slot);
            kit_.present();
            kit_.wait(60);
            state_.newGame();
            loadFromSlot(file_, state_, slot, name);
            state_.saveSlot = slot; // +0x8210
            kit_.wait(30);
            loaded = slot;
            break;
        }
    }
    if (loaded >= 0) {
        kit_.fadeOut();
    } else {
        kit_.fadeOutLower();
    }
    kit_.clearText(SceneKit::Lower);
    return loaded;
}

void FileDialog::quietSave(SceneKit& kit, GameState& state, SaveFile* file) {
    if (auto s = kit.sound()) {
        s->roomSound(6, false);
    }
    kit.lowerBackground = "map/main0";
    auto window = kit.load("menu/wifi/?/wifi_window3.spr", 0, 0, 1);
    kit.poll();
    kit.draw(window);
    kit.present();
    kit.fadeInLower();
    kit.poll();
    kit.draw(window);
    kit.present();
    if (file) {
        saveToSlot(*file, state, state.saveSlot, false, {}, 0);
    }
    if (auto s = kit.sound()) {
        s->bgmVolume(0.0f, s->duration(1200));
    }
    kit.wait(20);
    kit.effect(205);
    kit.wait(60);
    if (auto s = kit.sound()) {
        s->bgmVolume(1.0f, s->duration(1201));
    }
    kit.fadeOutLower();
}
} // namespace picarat
