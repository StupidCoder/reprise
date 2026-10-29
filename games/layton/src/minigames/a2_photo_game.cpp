// Photo hunt (overlay 16). See a2_photo_game.hpp for how it plays.
#include "minigames/a2_photo_game.hpp"

namespace picarat {
PhotoGame::PhotoGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                     GameState& state, std::uint32_t& random)
    : kit_(assets, std::move(present), audio, SceneKind::Photo), state_(state), random_(random) {}

// Runs the photo-hunt screens one after another until one ends the scene.
void PhotoGame::run() {
    // 020efde8: scene 27 starts on the album, scene 26 (photo just taken in a
    // room) on the hunt's intro.
    int screen = state_.scene == scene::PhotoTaken ? 2 : 0;
    for (;;) {
        switch (screen) {
        case 0:
            screen = album();
            break;
        case 1:
            screen = start();
            break;
        case 2:
            screen = intro();
            break;
        case 3:
            screen = hunt();
            break;
        case 4:
        case 5:
        case 6: {
            screen = result(screen - 4);
            // All nine photos complete: event 18500 (0x4844) once.
            if (!state_.eventViewed(0x4844)) {
                int i = 0;
                while (i < 9 && state_.progress.photoComplete(std::uint32_t(i))) {
                    ++i;
                }
                if (i > 8) {
                    state_.eventId = 0x4844;
                    state_.setEventViewed(0x4844, true);
                    state_.following = scene::DramaEvent;
                    state_.scene = state_.following;
                    return;
                }
            }
            break;
        }
        default:
            state_.scene = state_.following;
            return; // 0206a8fc: +0x478 = +0x479
        }
    }
}

// ---------------------------------------------------------------------------
// Album (020f0000 / 020f04e8).

int PhotoGame::album() {
    if (auto s = kit_.sound()) {
        s->roomSound(0x21, true);
    }
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.resetDisplay(); // 0206ddec
    std::array<SpriteButton, 9> photos;
    for (int i = 0, x = 0x14, y = 0xc; i < 9; ++i) {
        if (state_.progress.photoTaken(std::uint32_t(i))) {
            photos[std::size_t(i)] = kit_.button("subgame/photo/photo_icon.spr", x, y, 3);
            const auto n = std::to_string(i + 1);
            kit_.setButtonAnimations(photos[std::size_t(i)], n, n, n);
        }
        x += 0x4c;
        if (i % 3 == 2) {
            x = 0x14;
            y += 0x36;
        }
    }
    auto numbers = kit_.load("subgame/ham/iten_num.spr", 0, 0);
    auto slash = kit_.load("subgame/photo/slash.spr", 0, 0);
    auto clear = kit_.load("subgame/photo/?/clear.spr", 0, 0);
    auto back = kit_.button("subgame/photo/%s/album_modoru.spr", 0, 0, 4);
    kit_.setButtonAnimations(back, "on", "off", "click");
    kit_.lowerBackground = "subgame/photo/album_bg";
    kit_.upperBackground = "subgame/photo/" + kit_.language + "/album_sub";
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.wait(1);
        }
    }
    auto draw = [&] { // 020f0384
        for (int i = 0; i < 9; ++i) {
            auto& b = photos[std::size_t(i)];
            if (!b.exists) {
                continue;
            }
            kit_.updateButton(b);
            kit_.drawButton(b);
            if (!state_.progress.photoComplete(std::uint32_t(i))) {
                slash.x = b.x + 0x31;
                slash.y = b.y + 0x24;
                kit_.draw(slash);
                kit_.select(numbers, "3");
                numbers.x = b.x + 0x37;
                numbers.y = b.y + 0x24;
                kit_.draw(numbers);
                int found = 0;
                for (int k = 0; k < 3; ++k) {
                    found += state_.progress.photoChecked(std::uint32_t(i * 3 + k));
                }
                kit_.select(numbers, std::to_string(found));
                numbers.x = b.x + 0x2a;
                numbers.y = b.y + 0x24;
                kit_.draw(numbers);
            } else {
                clear.x = b.x + 1;
                clear.y = b.y + 0x23;
                kit_.draw(clear);
            }
        }
        kit_.updateButton(back);
        kit_.drawButton(back);
    };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        for (int i = 0; i < 9; ++i) {
            auto& b = photos[std::size_t(i)];
            if (!b.exists || !kit_.clicked(b)) {
                continue;
            }
            kit_.fadeOut();
            state_.photo = std::uint8_t(i);
            state_.scene = scene::PhotoTaken;
            if (state_.progress.photoComplete(std::uint32_t(i))) {
                for (int k = 0; k < 3; ++k) {
                    state_.progress.setPhotoChecked(std::uint32_t(i * 3 + k), false);
                }
                return 1;
            }
            return 2;
        }
        if (kit_.clicked(back)) {
            if (auto s = kit_.sound()) {
                s->fadeOutBgm(s->duration(0x3e9));
            }
            kit_.fadeOut();
            state_.following = state_.scene = scene::Menu;
            return 7;
        }
    }
}

// ---------------------------------------------------------------------------
// Start screen of a finished photo (020f0680 / 020f095c).

int PhotoGame::start() {
    const auto* r = row();
    if (!r) {
        return 0;
    }
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.resetDisplay(); // 0206ddec
    auto again = kit_.button("subgame/photo/%s/re_chosen_btn.spr", 0, 0, 0x6f);
    auto stop = kit_.button("subgame/photo/%s/yameru_btn.spr", 0, 0, 4);
    auto icon = kit_.load("subgame/photo/%s/ayashi_icon.spr", 0, 0);
    kit_.lowerBackground = "subgame/photo/photo_bg" + std::to_string(r[2]);
    kit_.upperBackground = "subgame/photo/" + kit_.language + "/photostart_sub";
    auto draw = [&] { // 020f084c: the icon points at the photo's puzzle (solved or not).
        kit_.updateButton(stop);
        kit_.drawButton(stop);
        kit_.updateButton(again);
        kit_.drawButton(again);
        const int x = std::int16_t(r[8] | r[9] << 8), y = std::int16_t(r[10] | r[11] << 8);
        const bool solved =
            state_.puzzleStatus(state_.puzzleNumber(std::int16_t(r[6] | r[7] << 8))) == 2;
        kit_.select(icon, solved ? (x < 0x80 ? "2" : "4") : (x < 0x80 ? "1" : "3"));
        icon.x = x < 0x80 ? x : x - 0x48;
        icon.y = y - 0x1c;
        kit_.draw(icon);
    };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    int next = 0;
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        if (kit_.clicked(stop)) {
            next = 0;
            break;
        }
        if (kit_.clicked(again)) {
            next = 2;
            break;
        }
    }
    // 020f07b4: fade out, display reset, 30 frames.
    kit_.fadeOut();
    kit_.wait(0x1e);
    return next;
}

// ---------------------------------------------------------------------------
// Intro card (020f0a70 / 020f0d1c / 020f0bf0): the photo number and place on
// a card that scrolls 16 lines per frame from the lower to the upper screen
// (background scroll via 020f0c58); then the room fades in below. The ROM
// also clips both screens with WIN0 during the scroll (native: none, G52).

int PhotoGame::intro() {
    if (auto s = kit_.sound()) {
        s->fadeOutBgm(s->duration(0x3eb));
    }
    kit_.wait(0x3c);
    const auto* r = row();
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.resetDisplay(); // 0206ddec
    const auto bg = "subgame/photo/" + kit_.language + "/photostart_bg";
    kit_.lowerBackground = kit_.upperBackground = bg;
    // 020f0a70: the number on both screens, 3D lower and 2D upper sprite.
    auto title = kit_.load("nazo/system/?/nazo_title.spr", 0, 0);
    auto upperTitle = kit_.load("nazo/system/?/nazo_title.sbj", 0, 0);
    if (r) {
        int label = r[1];
        if (label == '\\') {
            label = '&';
        }
        // 02072e34: 02072d80 on the upper text layer (+0x34).
        kit_.textTitle(
            0x78, kit_.text.expand(kit_.member("jiten", "p_" + std::to_string(label) + ".txt")), 0,
            0x80, SceneKit::Upper);
    }
    auto card = [&](int lower, int upper) { // 020f0c58: scrolls, then the padded number
        kit_.lowerScrollY = lower;
        kit_.upperScrollY = upper;
        if (!r) {
            return;
        }
        kit_.drawNumber(title, unsigned(r[0] + 1), "pos", 0, -lower, true);      // 020722fc
        kit_.drawNumber(upperTitle, unsigned(r[0] + 1), "pos", 0, -upper, true); // 02072434
    };
    openWindows(); // 020f0d24
    card(0, -0xbf);
    kit_.present();
    if (auto s = kit_.sound()) {
        s->playStream(0, 10, -1, 1.0f, 0, false);
    }
    kit_.fadeIn();
    kit_.wait(0x16);
    int upper = -0xbf, lower = 0;
    for (; lower < 0xc0; lower += 0x10, upper += 0x10) {
        kit_.poll();
        card(lower, upper);
        clipWindows(lower, upper);
        kit_.present();
    }
    kit_.poll();
    card(lower, upper);
    kit_.present();
    clearWindows();
    kit_.setLowerBrightness(-16); // 020f0ea8
    kit_.poll();
    card(0, 0);
    kit_.present();
    windowsOff();
    kit_.poll();
    kit_.lowerScrollY = 0;
    kit_.upperScrollY = 0;
    if (r) {
        kit_.lowerBackground = "map/main" + std::to_string(r[2]);
        kit_.drawNumber(upperTitle, unsigned(r[0] + 1), "pos", 0, 0, true); // 02072434
    }
    kit_.present();
    kit_.fadeInLower();
    kit_.waitRelease();
    // 020f0bf0: fade out and display reset.
    kit_.fadeOut();
    return 3;
}

// ---------------------------------------------------------------------------
// Difference hunt (020f0ff4 / 020f15c8).

int PhotoGame::hunt() {
    const auto* r = row();
    if (!r) {
        return 0;
    }
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.resetDisplay(); // 0206ddec
    auto here = kit_.button("subgame/photo/%s/koko_btn.spr", 0, 0, 0x6f);
    auto erase = kit_.button("subgame/photo/%s/kesu_btn.spr", 0, 0, 0x1cd);
    auto stop = kit_.button("subgame/photo/%s/yameru_btn.spr", 0, 0, 4);
    const int count = r[3];
    std::array<bool, 3> found{true, true, true};
    for (int i = 0; i < count; ++i) {
        if (!state_.progress.photoChecked(std::uint32_t(i + state_.photo * 3))) {
            found[std::size_t(i)] = false;
        }
    }
    auto message = kit_.load("subgame/photo/%s/photo_msg.spr", 0, 0);
    int remaining = 0;
    for (int i = 0; i < count; ++i) {
        remaining += !found[std::size_t(i)];
    }
    auto counter = kit_.load("subgame/photo/photo_num.spr", 0x21, 0xb5, unsigned(remaining + 1));
    auto circle = kit_.load("subgame/photo/circle.spr", 0, 0);
    circle.x = circle.y = -0x28;
    auto ring = kit_.load("subgame/photo/o.spr", 0, 0);
    (void)ring;
    std::array<SpriteObject, 3> fails;
    for (int i = 0; i < 3; ++i) {
        fails[std::size_t(i)] = kit_.load("subgame/photo/fail" + std::to_string(r[2]) + "_" +
                                              std::to_string(i + 1) + ".spr",
                                          0, 0);
    }
    auto check = kit_.load("subgame/photo/check_icon.spr", 0, 0);
    bool marking = false, dragging = false;
    int selected = -1;
    if (auto s = kit_.sound()) {
        s->roomSound(0xf, true);
    }
    kit_.lowerBackground = "subgame/photo/photo_bg" + std::to_string(r[2]);
    kit_.upperBackground = "map/main" + std::to_string(r[2]);
    auto draw = [&] { // 020f14e0
        for (int i = 0; i < 3; ++i) {
            auto& f = fails[std::size_t(i)];
            kit_.draw(f);
            if (found[std::size_t(i)]) {
                check.x = f.x + (kit_.frameWidth(f) >> 1) - 9;
                check.y = f.y + (kit_.frameHeight(f) >> 1) - 9;
                kit_.draw(check, 0xf);
            }
        }
        kit_.updateButton(stop);
        kit_.drawButton(stop);
        kit_.draw(message);
        kit_.draw(counter);
        if (!marking) {
            return;
        }
        kit_.updateButton(here);
        kit_.drawButton(here);
        kit_.updateButton(erase);
        kit_.drawButton(erase);
        kit_.draw(circle);
    };
    auto inside =
        [&](SpriteObject& f) { // 02077698 -> 0206e224: frame rectangle at (x-2, y-2), held touch
            if (!kit_.held()) {
                return false;
            }
            int x = -1, y = -1;
            kit_.position(x, y);
            return Rect{f.x - 2, f.y - 2, kit_.frameWidth(f), kit_.frameHeight(f)}.contains(x, y);
        };
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        if (kit_.clicked(here)) {
            erase.button.state = Button::State::Idle;
            kit_.wait(0x1e);
            kit_.fadeOut();
            if (selected < 0) {
                return 4;
            }
            found[std::size_t(selected)] = true;
            state_.progress.setPhotoChecked(std::uint32_t(selected + state_.photo * 3), true);
            int left = 0;
            for (int i = 0; i < count; ++i) {
                left += !found[std::size_t(i)];
            }
            if (left == 0) {
                state_.progress.setPhotoComplete(state_.photo, true);
                return 6;
            }
            return 5;
        }
        if (kit_.clicked(erase)) {
            here.button.state = Button::State::Idle;
            circle.x = circle.y = -0x28;
            marking = false;
            selected = -1;
            continue;
        }
        if (kit_.clicked(stop)) {
            kit_.fadeOut();
            return 0;
        }
        if (here.button.state != Button::State::Idle || stop.button.state != Button::State::Idle) {
            continue; // 020802f8
        }
        int px = -1, py = -1;
        kit_.pressPosition(px, py);
        if (kit_.pressed() && py < 0xa7) {
            dragging = true;
        }
        if (kit_.held() && dragging) {
            int x = -1, y = -1;
            kit_.position(x, y);
            if (y > 0x9a) {
                y = 0x9b;
            }
            circle.x = x - 0x14;
            circle.y = y - 0x14;
            marking = true;
            selected = -1;
            for (int i = 0; i < count; ++i) {
                if (inside(fails[std::size_t(i)])) {
                    selected = i;
                    break;
                }
            }
            continue;
        }
        if (kit_.released()) {
            if (dragging) {
                kit_.effect(0x1cc);
            }
            dragging = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Result (020f1988 / 020f1a48): judgment with a random character, message.

// The card scroll (020f0d1c) and the judgment slide (0206b4fc) program the
// same windows. Lower: WIN0 (0..254, 0..190-scroll) shows BG0 and BG3, nothing
// outside. Upper: WIN0 (0..254, 0..15-scroll) and WIN1 (columns 255 and 0)
// show nothing, outside BG3 and OBJ; the text layers stay hidden meanwhile.
void PhotoGame::openWindows() {
    auto& lower = kit_.windows[SceneKit::Lower];
    auto& upper = kit_.windows[SceneKit::Upper];
    lower = SceneKit::Windows{};
    lower.enabled[0] = true;
    lower.x2[0] = 0xff;
    lower.y2[0] = 0xbf;
    lower.inside[0] = SceneKit::BG0 | SceneKit::BG3;
    lower.outside = 0;
    upper = SceneKit::Windows{};
    upper.enabled[0] = upper.enabled[1] = true;
    upper.x2[0] = 0xff;
    upper.y2[0] = 0xc0;
    upper.x1[1] = 0xff;
    upper.x2[1] = 0x01;
    upper.y2[1] = 0xc0;
    upper.outside = SceneKit::BG3 | SceneKit::OBJ;
}

void PhotoGame::clipWindows(int bottom, int top) {
    kit_.windows[SceneKit::Lower].y2[0] = (0xbf - bottom) & 0xff;
    kit_.windows[SceneKit::Upper].y2[0] = (0x10 - top) & 0xff;
}

void PhotoGame::clearWindows() { // empty lower WIN0, upper windows off
    auto& lower = kit_.windows[SceneKit::Lower];
    lower.x2[0] = lower.y2[0] = 0;
    kit_.windows[SceneKit::Upper] = SceneKit::Windows{};
}

void PhotoGame::windowsOff() {
    kit_.windows[SceneKit::Lower] = SceneKit::Windows{};
    kit_.windows[SceneKit::Upper] = SceneKit::Windows{};
}

void PhotoGame::image(const std::string& path, bool final) {
    if (final) {
        kit_.upperBackground = path;
    }
    kit_.lowerBackground = path;
}

void PhotoGame::present() {
    kit_.poll();
    kit_.present();
} // 0206b4fc polls (0206c7b4) before its presents, except 0206b8d0

// The result of a tap: a miss, a found difference, or a completed photo.
int PhotoGame::result(int mode) {
    const auto* r = row();
    if (!r) {
        return 0;
    }
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.resetDisplay(); // 0206ddec
    auto window = kit_.load("subgame/photo/photo_window.spr", 0, 0);
    kit_.poll();
    if (auto s = kit_.sound()) {
        s->stopBgm(s->duration(0x4b0));
    }
    const int character = (gameRandom(random_) & 1) == 0 ? 2 : 0;
    kit_.wait(0x1e);
    const int voiceId = (character == 2 ? 5 : 0) + 100 + int(gameRandom(random_) % 3);
    if (auto s = kit_.sound()) {
        s->playStream(0, voiceId, -1, 1.0f, 0, false);
    }
    {
        const auto table = readFile(kit_.assets.root / "puzzle/judgment.tbl");
        runJudgment(table, unsigned((mode == 0 ? 1 : 0) | character), false, gameRandom(random_),
                    *this); // 0206b4fc
    }
    kit_.poll();
    kit_.lowerBackground = "subgame/photo/photo_bg" + std::to_string(r[2]);
    kit_.present();
    kit_.fadeInLower();
    kit_.wait(0x3c); // 02071d28
    if (auto s = kit_.sound()) {
        while (!s->streamIdle()) {
            kit_.wait(1);
        }
    }
    kit_.wait(0xf);
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    bool full = false;
    if (mode == 0) {
        kit_.textCentered(0x5b, kit_.text.expand(kit_.member("cm", "msg_phtf.txt")), 5, 0x80,
                          SceneKit::Lower);
    } else {
        int found = 0;
        for (int i = 0; i < r[3]; ++i) {
            found += state_.progress.photoChecked(std::uint32_t(i + state_.photo * 3));
        }
        full = found > 2;
        kit_.textCentered(
            0x5b, kit_.text.expand(kit_.member("cm", "msg_pht" + std::to_string(found) + ".txt")),
            5, 0x80, SceneKit::Lower);
    }
    if (auto s = kit_.sound()) {
        if (!full) {
            s->playTrackedEffect(0xd);
        } else {
            s->playTrackedStream(0, 5, -1, 1.0f, 0);
        }
    }
    kit_.poll();
    window.x = 4;
    window.y = 0x34;
    kit_.draw(window);
    kit_.present();
    kit_.wait(0x3c);
    kit_.waitRelease(); // 0206cec0
    kit_.effect(3);
    kit_.wait(0xf);
    // 020f19f0: fade out, display reset, 60 frames.
    kit_.fadeOut();
    kit_.wait(0x3c);
    return mode == 2 ? 0 : 3;
}
} // namespace picarat
