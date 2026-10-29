// The challenges (overlay 24). See a2_challenge_menu.hpp.
#include "menus/a2_challenge_menu.hpp"
#include "menus/a2_file_dialog.hpp"
#include <cmath>
#include <cstdio>

namespace picarat {
namespace {
constexpr int kFirstPuzzle = 139; // three per category: 139 + 3 (category - 1) + row
// 020f0d80: the "complete" stamps over the category buttons.
constexpr int kStampX[5] = {10, 94, 178, 52, 138}, kStampY[5] = {39, 39, 39, 123, 123};
} // namespace

ChallengeMenu::ChallengeMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                             GameState& state, SaveFile* file)
    : kit_(assets, std::move(present), audio, SceneKind::Challenge), state_(state), file_(file) {
    // 020efda0: display reset; the page from +0x82bc (020f091c). Category
    // buttons (sound 3) for what was completed: photos (4, 4), herb teas
    // (88, 4), the hamster (172, 4), the story (46, 88; 020823c8) and
    // puzzles 1..150 all solved (130, 88). Three spriteless rows at
    // (0, 36 + 28 i), 256x23, sound 3; back (secret_modoru "pos_o"); the
    // solved icon, the complete stamp, the notice window ("pos") and its
    // waiting cursor (229, 121).
    kit_.resetDisplay();
    page_ = state_.challengePage;
    const auto path = kit_.localized("menu/secret/?/challenge_btn.spr");
    auto category = [&](int c, int x, int y, const char* animation) {
        auto& b = categories_[std::size_t(c)];
        b = kit_.button(path, x, y, 3, "");
        kit_.setButtonAnimations(b, animation, animation, animation);
        b.pressX = b.pressY = 1;
    };
    bool photos = true;
    for (unsigned i = 0; i < 9; ++i) {
        photos &= state_.progress.photoComplete(i); // 0208592c
    }
    if (photos) {
        category(0, 4, 4, "4");
    }
    if (state_.teaComplete()) {
        category(1, 88, 4, "2");
    }
    if (state_.progress.bytes[0x283] == 6) { // 020859f4
        category(2, 172, 4, "3");
    }
    if (file_ ? file_->clearLevel(state_.saveSlot) != 0 : state_.cleared) {
        category(3, 46, 88, "1");
    }
    bool all = true;
    for (int n = 1; n <= 150 && all; ++n) {
        all = state_.puzzleStatus(n) == 2;
    }
    if (all) {
        category(4, 130, 88, "5");
    }
    for (int i = 0; i < 3; ++i) {
        auto& r = rows_[std::size_t(i)];
        r.exists = true;
        r.x = 0;
        r.y = 36 + 28 * i;
        r.button = Button({0, r.y, 256, 23}, 3);
    }
    back_ = kit_.button(kit_.localized("menu/secret/?/secret_modoru.spr"), 0, 0, 4, "pos_o");
    icon_ = kit_.load("menu/secret/challenge_icon.spr", 0, 0, 1);
    complete_ = kit_.load(kit_.localized("menu/secret/?/complite.spr"), 0, 0, 1);
    window_ = kit_.load("system/prize_window2.spr", 0, 0, 1);
    kit_.variable(window_, "pos", window_.x, window_.y); // 02075bc8
    cursor_ = kit_.load("cursor_wait.spr", 229, 121, 1);
    cursor_.x = 229;
    cursor_.y = 121;
    // The first visit after solving puzzles 1..150: the notice, remembered
    // by the event bit 999 (02082578 / 02082598).
    if (all && !state_.progress.eventViewed(999)) {
        notice();
        state_.progress.setEventViewed(999, true);
    }
    kit_.upperBackground = kit_.localized("menu/secret/?/challenge_sub");
    if (auto s = kit_.sound()) {
        s->roomSound(20, true);
    }
}

int ChallengeMenu::frames(int id) {
    auto s = kit_.sound();
    return s ? int(std::lround(s->duration(id) * 60.0f)) : 0;
}

bool ChallengeMenu::categorySolved(int c) const {
    for (int i = 0; i < 3; ++i) {
        if (state_.puzzleStatus(kFirstPuzzle + 3 * c + i) != 2) {
            return false;
        }
    }
    return true;
}

void ChallengeMenu::draw() {
    // 020f0370: back; on the list the category buttons and, for a fully
    // solved category, its stamp (moved by one pixel while its button is
    // held); in a category the rows and the solved icon (18, 41 + 28 i).
    kit_.updateButton(back_);
    kit_.drawButton(back_);
    if (page_ == 0) {
        for (int c = 0; c < 5; ++c) {
            auto& b = categories_[std::size_t(c)];
            kit_.updateButton(b);
            kit_.drawButton(b);
            if (!categorySolved(c)) {
                continue;
            }
            const bool held = b.exists && b.button.state == Button::State::Armed && b.button.inside;
            complete_.x = kStampX[c] + (held ? 1 : 0);
            complete_.y = kStampY[c] + (held ? 1 : 0);
            kit_.draw(complete_);
        }
        return;
    }
    for (int i = 0; i < 3; ++i) {
        kit_.updateButton(rows_[std::size_t(i)]);
        if (rowStatus_[std::size_t(i)] == 2) {
            icon_.x = 18;
            icon_.y = 41 + 28 * i;
            kit_.draw(icon_);
        }
    }
}

void ChallengeMenu::setup() {
    // 020f0984: the lower text is cleared. The list: challenge_bg, one frame,
    // fade in. A category: its icon animation, each row "Nr. %.3d: %s"
    // (tx_400 with the puzzle's number and title) at (36, 42 + 28 i) and
    // its status, background challenge_<page>, one frame.
    kit_.clearText(SceneKit::Lower);
    if (page_ == 0) {
        kit_.lowerBackground = kit_.localized("menu/secret/?/challenge_bg");
        kit_.poll();
        draw();
        kit_.present();
        kit_.fadeIn();
        return;
    }
    kit_.select(icon_, unsigned(page_));
    const auto format = kit_.systemText(400);
    for (int i = 0; i < 3; ++i) {
        const auto puzzle = state_.puzzleByNumber(kFirstPuzzle + 3 * (page_ - 1) + i);
        const int number = puzzle ? puzzle->number : 0;
        const auto title = puzzle ? kit_.text.expand(puzzle->title) : std::string();
        char line[64];
        std::snprintf(line, sizeof line, format.c_str(), number, title.c_str());
        rowTexts_[std::size_t(i)] = line;
        kit_.textAt(36, 42 + 28 * i, line, SceneKit::Lower);
        rowStatus_[std::size_t(i)] = state_.puzzleStatus(number);
    }
    kit_.lowerBackground = kit_.localized("menu/secret/?/challenge_" + std::to_string(page_));
    kit_.poll();
    draw();
    kit_.present();
}

void ChallengeMenu::notice() {
    // 020f0b28: map/main0, main-screen fade in; the music dims (time 210)
    // for frames(211), stream 5 plays; the window fades in (alpha 1..31 by
    // 2); tx_1201 centred at y 92 (extra 5); after frames(212) the waiting
    // cursor fades in until a release; the text goes, the window fades
    // out, the music returns (time 213), frames(214), a frame, 2 frames,
    // main-screen fade out.
    kit_.lowerBackground = "map/main0";
    kit_.present();
    kit_.fadeInLower();
    auto s = kit_.sound();
    if (s) {
        s->bgmVolume(0.0f, s->duration(210));
    }
    kit_.wait(frames(211));
    if (s) {
        s->playStream(0, 5, -1, 1.0f, 0.0f, false);
    }
    for (int alpha = 1; alpha < 32; alpha += 2) {
        kit_.poll();
        kit_.draw(window_, alpha);
        kit_.present();
    }
    kit_.textCentered(92, kit_.systemText(1201), 5, 128, SceneKit::Lower);
    int shown = 0, cursorAlpha = 2;
    for (;;) {
        kit_.poll();
        kit_.draw(window_, 31);
        if (shown < frames(212)) {
            ++shown;
            kit_.present();
            continue;
        }
        kit_.draw(cursor_, cursorAlpha);
        if (cursorAlpha < 31) {
            ++cursorAlpha;
        }
        kit_.present();
        if (kit_.released() || SceneKit::buttonA()) {
            break;
        }
    }
    kit_.clearText(SceneKit::Lower);
    for (int alpha = 31; alpha > 1; alpha -= 2) {
        kit_.poll();
        kit_.draw(window_, alpha);
        kit_.present();
    }
    if (s) {
        s->bgmVolume(1.0f, s->duration(213));
    }
    kit_.wait(frames(214));
    kit_.poll();
    kit_.present();
    kit_.wait(2);
    kit_.fadeOutLower();
}

void ChallengeMenu::run() {
    // 020f04d4.
    setup();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        if (page_ == 0) {
            for (int c = 0; c < 5; ++c) {
                if (kit_.clicked(categories_[std::size_t(c)])) {
                    page_ = c + 1;
                    kit_.fadeOutLower();
                    setup();
                    kit_.fadeInLower();
                }
            }
            if (kit_.clicked(back_)) {
                // Saved to the slot (FileDialog::quietSave), then the secret
                // menu with the page reset.
                kit_.fadeOut();
                FileDialog::quietSave(kit_, state_, file_);
                state_.challengePage = 0;
                state_.scene = 28;
                return;
            }
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            auto& r = rows_[std::size_t(i)];
            // A held row is drawn red ('#R', 020726cc), otherwise black.
            const bool held = r.button.state == Button::State::Armed && r.button.inside;
            kit_.textAt(36, 42 + 28 * i, (held ? "#R" : "#X") + rowTexts_[std::size_t(i)],
                        SceneKit::Lower);
            if (!kit_.clicked(r)) {
                continue;
            }
            // Event 26000 + 10 (n - 139): + 2 once solved (replay: +0x8214
            // cleared, +0x821f set), + 1 once its introduction was seen
            // (+0x7ec1 set).
            const int number = kFirstPuzzle + 3 * (page_ - 1) + i;
            int event = 26000 + 10 * (number - kFirstPuzzle);
            if (state_.puzzleStatus(number) == 2) {
                state_.indexReplay = 0;
                state_.resultAdjustA = true;
                event += 2;
            } else if (state_.eventViewed(std::uint32_t(event))) {
                event += 1;
                state_.retryVariant = true;
            }
            kit_.fadeOut();
            state_.challengePage = page_;
            state_.following = scene::Challenge;
            state_.eventId = std::uint16_t(event);
            state_.scene = scene::DramaEvent;
            return;
        }
        if (kit_.clicked(back_)) {
            page_ = 0;
            kit_.fadeOutLower();
            setup();
            kit_.fadeInLower();
        }
    }
}
} // namespace picarat
