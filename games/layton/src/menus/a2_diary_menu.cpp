// The old diary (overlay 21). See a2_diary_menu.hpp.
#include "menus/a2_diary_menu.hpp"

namespace picarat {
DiaryMenu::DiaryMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                     GameState& state)
    : kit_(assets, std::move(present), audio, SceneKind::Diary), state_(state) {
    // 020efda0: room sound 14 with music (0207f780), display reset, twelve
    // icon buttons (no sound), the back button, backgrounds.
    if (auto s = kit_.sound()) {
        s->roomSound(14, true);
    }
    kit_.resetDisplay();
    for (auto& p : pages_) {
        p = kit_.button("menu/bag/diary_icon.spr", 0, 0, -1);
    }
    layout();
    back_ = kit_.button("menu/secret/?/secret_modoru.spr", 0, 0, 4, "pos_o");
    kit_.setButtonAnimations(back_, "on", "off", "click");
    kit_.lowerBackground = kit_.localized("menu/bag/?/diary_bg");
    kit_.upperBackground = kit_.localized("menu/bag/?/diary_sub2");
    first_ = true;
    selected_ = -1;
    // Until the BGM controller has settled (0207fa78): 0206d950 + one frame.
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.clearSubmissions();
            kit_.wait(1);
        }
    }
}

// Places the page icons on the 4x3 grid.
void DiaryMenu::layout() {
    // 020f0038: count the found pages; icons at (6 + 62 (i % 4), 12 + 54 (i / 4))
    // (02080184). Of the first `count` icons, new pages use pressed2/gfx2/
    // gfx5, read ones pressed3/gfx3/gfx3; the rest show gfx1.
    count_ = 0;
    for (int i = 0; i < 12; ++i) {
        if (found(i)) {
            ++count_;
        }
    }
    for (int i = 0; i < 12; ++i) {
        auto& p = pages_[std::size_t(i)];
        p.x = 6 + 62 * (i % 4);
        p.y = 12 + 54 * (i / 4);
        p.sprite.x = p.x;
        p.sprite.y = p.y;
        p.button.bounds.x = p.x;
        p.button.bounds.y = p.y;
        if (i < count_) {
            if (fresh(i)) {
                kit_.setButtonAnimations(p, "pressed2", "gfx2", "gfx5");
            } else {
                kit_.setButtonAnimations(p, "pressed3", "gfx3", "gfx3");
            }
        } else {
            kit_.setButtonAnimations(p, "gfx1", "gfx1", "gfx1");
        }
    }
}

// Draws the page icons (marking new pages).
void DiaryMenu::draw(bool active) {
    // 020f017c: the first `count` icons updated while active, the others only
    // drawn; the back button always updated and drawn.
    for (int i = 0; i < 12; ++i) {
        auto& p = pages_[std::size_t(i)];
        if (active && i < count_) {
            kit_.updateButton(p);
        }
        kit_.drawButton(p);
    }
    kit_.updateButton(back_);
    kit_.drawButton(back_);
}

// Types a page's text on the upper screen.
void DiaryMenu::show(int index) {
    // 020f03a8: the first time the upper BG becomes diary_sub (with one
    // frame), then diary_%i typed from (17, 10), all characters.
    if (first_) {
        kit_.upperBackground = "menu/bag/diary_sub";
        kit_.poll();
        draw(false);
        kit_.present();
        first_ = false;
    }
    const auto text = kit_.member("", "diary_" + std::to_string(index + 1) + ".txt");
    kit_.clearText(SceneKit::Upper);
    kit_.textTyped(17, 10, kit_.text.expand(text), -1, 0, -1);
}

// Runs the diary until the player goes back.
void DiaryMenu::run() {
    kit_.poll();
    draw(false);
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw(true);
        kit_.present();
        bool picked = false, jingle = false;
        for (int i = 0; i < 12; ++i) {
            if (selected_ == i || !kit_.clickedNow(pages_[std::size_t(i)])) {
                continue;
            }
            // New page: SE 0x1ea; read page: SE 120. The icon turns gfx4; the
            // previous selection reverts to pressed3/gfx3/gfx4.
            if (fresh(i)) {
                kit_.effect(0x1ea);
                jingle = true;
            } else {
                kit_.effect(120);
            }
            kit_.setButtonAnimations(pages_[std::size_t(i)], "gfx4", "gfx4", "gfx4");
            picked = true;
            if (selected_ != -1) {
                kit_.setButtonAnimations(pages_[std::size_t(selected_)], "pressed3", "gfx3",
                                         "gfx4");
            }
            selected_ = i;
            state_.progress.setBit(0x316, 16, std::uint32_t(i), false); // 02083014
            break;
        }
        if (picked) {
            if (jingle) {
                kit_.wait(12);
            }
            show(selected_);
        }
        if (kit_.clicked(back_)) {
            kit_.fadeOut();
            state_.scene = scene::Menu;
            // Destructor: MenuNewFlag 6 = any page still new.
            bool any = false;
            for (int i = 0; i < 12; ++i) {
                any |= fresh(i);
            }
            state_.progress.setBit(0x2f2, 16, 6, any);
            return;
        }
    }
}
} // namespace picarat
