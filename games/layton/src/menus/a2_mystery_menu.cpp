// The mysteries list (overlay 20). See a2_mystery_menu.hpp.
#include "menus/a2_mystery_menu.hpp"
#include <cmath>

namespace picarat {
MysteryMenu::MysteryMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                         GameState& state, std::uint32_t& random)
    : kit_(assets, std::move(present), audio, SceneKind::Mysteries), state_(state) {
    kit_.random = &random;
    // 020f4860: 0206e584 / 0207556c (display and sprite managers), ten
    // entry buttons (0208003c, sound 3) at x 4 + 50 (i % 5), y 27 + 74 (i / 5),
    // animation "%i" = i + 11 when solved, else i + 1 (pressed, idle and click
    // alike), press offset (1, 1).
    for (int i = 0; i < 10; ++i) {
        auto& e = entries_[std::size_t(i)];
        e = kit_.button("menu/bag/?/info_mode_chr.spr", 4 + 50 * (i % 5), 27 + 74 * (i / 5), 3);
        const auto name = std::to_string(solved(i) ? i + 11 : i + 1);
        kit_.setButtonAnimations(e, name, name, name);
        e.pressX = e.pressY = 1;
    }
    badge_ = kit_.load("system/btn/?/new_button.spr", 0, 0, 1);
    kit_.select(badge_, 1u);
    // From the bag (+0x8218 = -1) the list has its background at once and a
    // cancel button (sound 4); from a script the OK button (sound 3) and the
    // background arrives with the reveal / stamp sequence.
    if (state_.mysteryMenu == -1) {
        kit_.lowerBackground = "menu/bag/info_mode_bg";
        ok_ = kit_.button("system/btn/?/cancel.spr", 0, 0, 4);
    } else {
        ok_ = kit_.button("menu/bag/?/fukamaru_ok.spr", 0, 0, 3);
    }
    kit_.upperBackground = kit_.localized("menu/bag/?/info_mode_sub");
    kit_.clearText(SceneKit::Upper);
    current_ = -1;
}

// Shows a mystery's title and text on the upper screen.
void MysteryMenu::show(int number) {
    // 020f4b88: title it_%i (0x80 bytes, default "NO TITLE:%i") and text
    // id_%i, or ide_%i once solved (0x200 bytes, "NO DATA:%i"), from
    // txt.plz; upper BG info_mode_sub2, title centred on 128 at y 14
    // (02072e34), text from (17, 45) (020731b8, all characters).
    auto title = kit_.member("", "it_" + std::to_string(number) + ".txt");
    if (title.empty()) {
        title = "NO TITLE:" + std::to_string(number);
    }
    auto body =
        kit_.member("", (solved(number - 1) ? "ide_" : "id_") + std::to_string(number) + ".txt");
    if (body.empty()) {
        body = "NO DATA:" + std::to_string(number);
    }
    kit_.upperBackground = "menu/bag/info_mode_sub2";
    kit_.clearText(SceneKit::Upper);
    kit_.textTitle(14, kit_.text.expand(title), 0, 128, SceneKit::Upper);
    kit_.textTyped(17, 45, kit_.text.expand(body), -1);
}

// Draws the ten entries (unknown, new, solved).
void MysteryMenu::draw(bool active) {
    // 020f4d70: found entries (02080664: updated only when active, always
    // drawn); while active a "new" badge at (x + 9, y + 14) on found, new,
    // unsolved entries; then the OK/cancel button.
    for (int i = 0; i < 10; ++i) {
        auto& e = entries_[std::size_t(i)];
        if (found(i)) {
            if (active) {
                kit_.updateButton(e);
            }
            kit_.drawButton(e);
        }
        if (active && fresh(i) && !solved(i)) {
            badge_.x = e.x + 9;
            badge_.y = e.y + 14;
            kit_.draw(badge_);
        }
    }
    if (active) {
        kit_.updateButton(ok_);
    }
    kit_.drawButton(ok_);
}

// Shows `count` frames.
void MysteryMenu::frames(int count) {
    for (int i = 0; i < count; ++i) {
        kit_.poll();
        draw(false);
        kit_.present();
    }
}

// Clears the bag's "new" badge once nothing new is left.
void MysteryMenu::clearMenuNew() {
    // 020f4f7c: with no new entry left, MenuNewFlag 1 (the bag's badge) clears.
    for (int i = 0; i < 10; ++i) {
        if (fresh(i)) {
            return;
        }
    }
    state_.progress.setBit(0x2f2, 16, 1, false); // 02082e94
}

// The animation that adds a new mystery to the list.
void MysteryMenu::reveal() {
    // 020f4fdc: a new mystery joins the list.
    const int i = state_.mysteryMenu;
    auto figure = kit_.load("menu/bag/?/info_mode_chr.spr", 0, 0, 1);
    const int x = 50 * (i % 5) + 28, y = 74 * (i / 5) + 59; // the entry's centre
    kit_.lowerBackground = "menu/bag/info_mode_bg";
    frames(1);
    kit_.fadeInLower(); // 02071d28: lower screen only
    frames(30);
    if (auto s = kit_.sound()) {
        s->playStream(0, 23, -1, 1.0f, 0.0f, false); // 0207fb84 jingle
    }
    // White flash: 0x0400006c from +16 down to 0, one present each without
    // polling (the swap leaves the 3D list empty).
    for (int level = 16; level >= 0; --level) {
        kit_.setLowerBrightness(level);
        kit_.clearSubmissions();
        draw(false);
        kit_.present();
    }
    frames(4);
    // The entry's picture (animation i + 1) shrinks from 1.2 by 0.02 per
    // frame over ten frames, centred on the entry (02076124).
    float scale = 1.2f;
    for (int k = 0; k < 10; ++k) {
        kit_.poll();
        draw(false);
        kit_.select(figure, unsigned(i + 1));
        figure.x = x;
        figure.y = y;
        kit_.drawScaled(figure, scale);
        kit_.present();
        scale -= 0.02f;
    }
    kit_.lowerShake = 20;                                      // layer manager +0x8c
    state_.progress.setBit(0x2e3, 16, std::uint32_t(i), true); // 02082c64
    frames(50);
    state_.progress.setBit(0x2e5, 16, std::uint32_t(i), true); // 02082cb4
    state_.progress.setBit(0x2f2, 16, 1, true);                // 02082e94: MenuNewFlag 1
}

// The animation that stamps a mystery as solved.
void MysteryMenu::stamp() {
    // 020f52e8: a mystery is solved; the hanko stamp lands on its entry.
    const int i = state_.mysteryMenu - 10;
    auto hanko = kit_.load("event/hukmaru_hanko.spr", 0, 0, 1);
    const int x = 50 * (i % 5), y = 74 * (i / 5) + 25;
    hanko.x = x;
    hanko.y = y;
    kit_.lowerBackground = "menu/bag/info_mode_bg";
    frames(1);
    kit_.fadeInLower();
    // 90 frames of a white 48x64 rectangle at (x + 4, y + 4) pulsing with
    // alpha f2i(15 sin t), t += 0.12 (binary32; alpha <= 0 draws nothing);
    // the jingle starts on frame 66.
    float t = 0.0f;
    for (int k = 0; k < 90; ++k) {
        const int alpha = int(15.0f * std::sin(t));
        t += 0.12f;
        kit_.poll();
        draw(false);
        kit_.rectangle(x + 4, y + 4, 48, 64, 0x7fff, alpha);
        kit_.present();
        if (k == 66) {
            if (auto s = kit_.sound()) {
                s->playStream(0, 23, -1, 1.0f, 0.0f, false);
            }
        }
    }
    // Three frames with only the stamp (animation 1) on the lower screen.
    for (int k = 0; k < 3; ++k) {
        kit_.poll();
        kit_.draw(hanko);
        kit_.present();
    }
    kit_.lowerShake = 20;
    state_.progress.setBit(0x2e7, 16, std::uint32_t(i), true); // 02082d04
    const auto name = std::to_string(i + 11);
    kit_.setButtonAnimations(entries_[std::size_t(i)], name, name, name);
    for (int k = 0; k < 20; ++k) {
        kit_.poll();
        draw(false);
        kit_.select(hanko, 3u);
        kit_.draw(hanko);
        kit_.present();
    }
}

// Runs the list (after a reveal or stamp, if one was requested).
void MysteryMenu::run() {
    const int request = state_.mysteryMenu;
    if (request >= 0 && request < 10) {
        reveal();
    }
    if (request >= 10) {
        stamp();
    }
    kit_.poll();
    draw(true);
    kit_.present();
    kit_.fadeIn(); // 02071790
    for (;;) {
        kit_.poll();
        draw(true);
        kit_.present();
        if (kit_.clicked(ok_)) {
            kit_.fadeOut();
            return;
        }
        for (int i = 0; i < 10; ++i) {
            if (!kit_.clicked(entries_[std::size_t(i)])) {
                continue;
            }
            if (current_ != i) {
                show(i + 1);
                state_.progress.setBit(0x2e5, 16, std::uint32_t(i), false); // 02082cb4(i, 0)
                clearMenuNew();
            }
            current_ = i;
        }
        // 0206ae08 (an always-false exit test) follows here.
    }
}
} // namespace picarat
