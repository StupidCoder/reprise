// The notebook (overlay 18). See a2_notes_menu.hpp.
#include "menus/a2_notes_menu.hpp"

namespace picarat {
NotesMenu::NotesMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                     GameState& state)
    : kit_(assets, std::move(present), audio, SceneKind::Notes), state_(state) {
    // 020efda0: display reset (0206dabc, 0206d9c0, 0206ddec), fields
    // (020f0800: page from state +0x3e0, at least 1).
    kit_.resetDisplay();
    page_ = state_.progress.bytes[0x318];
    if (page_ == 0) {
        page_ = 1;
    }
    // Rows: 0208003c without a sprite at (15, 32 + 21 i), sound 3, size
    // fixed to 240 x 20 (+0x34).
    for (int i = 0; i < 6; ++i) {
        auto& r = rows_[std::size_t(i)];
        r.exists = true;
        r.x = 15;
        r.y = 32 + 21 * i;
        r.button = Button({r.x, r.y, 240, 20}, 3);
    }
    back_ = kit_.button("menu/bag/?/memo_page_buttons.spr", 0, 0, -1, "pos_b");
    kit_.setButtonAnimations(back_, "back2", "back1", "back2");
    next_ = kit_.button("menu/bag/?/memo_page_buttons.spr", 0, 0, -1, "pos_n");
    kit_.setButtonAnimations(next_, "next2", "next1", "next2");
    close_ = kit_.button("menu/bag/?/memo_close_buttons.spr", 0, 0, 121, "pos_c");
    kit_.setButtonAnimations(close_, "close2", "close1", "c_click");
    icon_ = kit_.load("menu/bag/?/memo_gfx.spr", 0, 0, 1);
    digits_ = kit_.load("system/system_num.spr", 0, 0, 1);
    digits_.x = 212;
    digits_.y = 17;
    kit_.lowerBackground = "menu/bag/memo_index";
    kit_.upperBackground = kit_.localized("menu/bag/?/memo_bg2");
    upperLoaded_ = false;
    count_ = 0;
    for (std::uint32_t i = 0; i < 57; ++i) {
        if (state_.progress.memo(i)) {
            ++count_; // 020f0540
        }
    }
}

// Draws the notebook page.
void NotesMenu::draw() {
    // 020f0208: page and close buttons updated and drawn; rows of found
    // notes updated (they have no sprite).
    for (auto* b : {&back_, &next_, &close_}) {
        kit_.updateButton(*b);
        kit_.drawButton(*b);
    }
    for (int i = 0; i < 6; ++i) {
        if (state_.progress.memo(std::uint32_t((page_ - 1) * 6 + i))) {
            kit_.updateButton(rows_[std::size_t(i)]);
        }
    }
}

// Writes the titles of the current page's notes.
void NotesMenu::list(bool redraw, bool clear) {
    // 020f0588: page number (system_num digit d = animation d + 1, right to
    // left from x 212 by 6, at most two digits) at y 17; per found note a
    // hat ("new" while unread) at (15, 32 + 22 k); with redraw, the titles
    // t_%i at (44, 35 + 22 k) on the lower text layer, the shown note red
    // (020726cc 'R', back to 'X').
    if (redraw && clear) {
        kit_.clearText(SceneKit::Lower);
    }
    int value = page_, x = 212;
    for (int k = 0; k < 2; ++k) {
        kit_.select(digits_, unsigned(value % 10 + 1));
        digits_.x = x;
        digits_.y = 17;
        kit_.draw(digits_);
        value /= 10;
        if (!value) {
            break;
        }
        x -= 6;
    }
    int y = 32;
    for (int k = 1; k <= 6; ++k, y += 22) {
        const int number = (page_ - 1) * 6 + k;
        const auto index = std::uint32_t(number - 1);
        if (!state_.progress.memo(index)) {
            continue;
        }
        kit_.select(icon_, state_.progress.memoNew(index) ? "new" : "hat");
        icon_.x = 15;
        icon_.y = y;
        kit_.draw(icon_);
        if (!redraw) {
            continue;
        }
        const auto title =
            kit_.text.expand(kit_.member("", "t_" + std::to_string(number) + ".txt"));
        kit_.textAt(44, y + 3, (selected_ == number ? "#R" : "") + title, SceneKit::Lower);
    }
}

// Shows a note's text on the upper screen.
void NotesMenu::detail(int number) {
    // 020f076c: d_%i on the upper text layer from (17, 10), all characters.
    kit_.clearText(SceneKit::Upper);
    auto text = kit_.member("", "d_" + std::to_string(number) + ".txt");
    if (text.empty()) {
        text = "NO DATA:" + std::to_string(number);
    }
    kit_.textTyped(17, 10, kit_.text.expand(text), -1, 0, -1);
}

// Updates the bag's "new" badge.
void NotesMenu::updateMenuNew() {
    // 020f085c clears MenuNewFlag 0 when no note is new; the destructor
    // clears it and sets it again for any new note: together, flag = any.
    bool any = false;
    for (std::uint32_t i = 0; i < 57; ++i) {
        any |= state_.progress.memoNew(i);
    }
    state_.progress.setBit(0x2f2, 16, 0, any);
}

// Runs the notebook until the player goes back.
void NotesMenu::run() {
    int shown = -1;
    bool redraw = false;
    kit_.poll();
    draw();
    list(true, true);
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        list(redraw, true);
        kit_.present();
        if (shown >= 0) {
            kit_.poll();
            draw();
            detail(shown);
            list(redraw, true);
            kit_.present();
        }
        redraw = false;
        shown = -1;
        if (kit_.clicked(next_)) {
            kit_.effect(120);
            redraw = true;
            if (++page_ > 10) {
                page_ = 1;
            }
        }
        if (kit_.clicked(back_)) {
            kit_.effect(120);
            redraw = true;
            if (--page_ < 1) {
                page_ = 10;
            }
        }
        for (int i = 0; i < 6; ++i) {
            const int number = (page_ - 1) * 6 + i + 1;
            if (kit_.clicked(rows_[std::size_t(i)]) && selected_ != number) {
                selected_ = number;
                state_.progress.setMemoNew(std::uint32_t(number - 1), false); // 02082c14
                shown = number;
                kit_.poll();
                draw();
                list(true, false);
                kit_.present();
            }
            if (number >= 57) {
                break;
            }
        }
        if (shown >= 0 && !upperLoaded_) {
            kit_.upperBackground = "menu/bag/memo_bg";
            upperLoaded_ = true;
        }
        if (kit_.clicked(close_)) {
            kit_.fadeOut();
            state_.progress.bytes[0x318] = std::uint8_t(page_);
            updateMenuNew();
            state_.scene = scene::Menu;
            return;
        }
    }
}
} // namespace picarat
