// The shack of missed puzzles (overlay 25). See a2_nazoba_menu.hpp.
#include "menus/a2_nazoba_menu.hpp"
#include <cstdio>

namespace picarat {
namespace {
std::string threeDigits(int n) {
    char b[16];
    std::snprintf(b, sizeof b, "%.3d", n);
    return b;
}

std::string replaceFirst(std::string text, const std::string& token, const std::string& value) {
    const auto p = text.find(token);
    if (p != text.npos) {
        text.replace(p, token.size(), value);
    }
    return text;
}
} // namespace

NazobaMenu::NazobaMenu(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                       GameState& state)
    : kit_(assets, std::move(present), audio, SceneKind::Shack), state_(state) {
    // 020efda0: room-sound row 8 with music, display reset, one present.
    if (auto s = kit_.sound()) {
        s->roomSound(8, true);
    }
    kit_.resetDisplay();
    kit_.present();
    format_ = kit_.systemText(0xcf);
    kit_.lowerBackground = "menu/nazoba/nazoba_bg";
    kit_.upperBackground = kit_.localized("menu/nazoba/?/nazoba_sub");
    // Buttons (0208003c, no position variable, no click sound) and their
    // pressed/idle/click animations (0208070c).
    auto button = [&](int x, int y, const char* on, const char* off) {
        auto b = kit_.button("menu/nazoba/?/nazoba_btn.spr", x, y, -1, "");
        kit_.setButtonAnimations(b, on, off, off);
        return b;
    };
    back_ = button(0xc2, 0xa7, "modoru_on", "modoru_off");
    previous_ = button(0xf, 0xa7, "mae_on", "mae_off");
    next_ = button(100, 0xa7, "tsugi_on", "tsugi_off");
    for (int i = 0; i < 6; ++i) {
        SpriteButton& e = entries_[std::size_t(i)];
        e.exists = true;
        e.x = 0x1f;
        e.y = 0x17 + 0x16 * i;
        e.button = Button({e.x, e.y, 200, 21}, -1);
    }
    yes_ = kit_.button("system/btn/?/yes.spr", 0x30, 100, 3, "");
    kit_.setButtonAnimations(yes_, "on", "off", "click");
    no_ = kit_.button("system/btn/?/no.spr", 0x90, 100, 4, "");
    kit_.setButtonAnimations(no_, "on", "off", "click");
    window_ = kit_.load("menu/nazoba/nazoba_win.spr", 0x18, 0x16);
    kit_.select(window_, "gfx");
    digits_ = kit_.load("menu/nazoba/?/nazoba_btn.spr", 0, 0, 1);
    collect();
    kit_.present();
}

// Collects the unsolved puzzles passed on to the shack.
void NazobaMenu::collect() {
    // 020f077c: puzzles 1..153 with bit 6 set (02083dc8) and not solved;
    // pages = (count - 1) / 6 + 1.
    puzzles_.clear();
    for (int n = 1; n < 0x9a; ++n) {
        const auto index = std::uint32_t(n - 1);
        if (index < 153 && (state_.progress.puzzleByte(index) >> 6 & 1) &&
            state_.puzzleStatus(n) != 2) {
            puzzles_.push_back(n);
        }
    }
    pages_ = (int(puzzles_.size()) - 1) / 6 + 1;
}

// The page number.
void NazobaMenu::digits(int page) {
    // 020f0510: page tens/ones at (0xdc,4)/(0xe2,4), page count at (0xf0,4)/(0xf6,4).
    auto digit = [&](int value, int x) {
        digits_.x = x;
        digits_.y = 4;
        kit_.select(digits_, std::to_string(value));
        kit_.draw(digits_);
    };
    digit(page / 10, 0xdc);
    digit(page % 10, 0xe2);
    digit(pages_ / 10, 0xf0);
    digit(pages_ % 10, 0xf6);
}

// Draws a page of the list, or the confirmation window.
void NazobaMenu::draw(int page, bool confirm) {
    // 020f0224. Menu: buttons updated and drawn (02080664); page buttons only
    // with more than one page. Confirmation: those buttons drawn still, the
    // window sprite, yes/no updated.
    auto live = [&](SpriteButton& b) {
        kit_.updateButton(b);
        kit_.drawButton(b);
    };
    if (!confirm) {
        live(back_);
        if (pages_ > 1) {
            live(next_);
            live(previous_);
        }
        for (auto& e : entries_) {
            kit_.updateButton(e);
        }
    } else {
        kit_.drawButton(back_);
        if (pages_ > 1) {
            kit_.drawButton(next_);
            kit_.drawButton(previous_);
        }
        kit_.draw(window_);
        live(yes_);
        live(no_);
    }
    digits(page);
}

// Writes the puzzle names of a page.
void NazobaMenu::list(int page) {
    // 020f066c: tx 0x514 when nothing was passed on, otherwise one
    // "puzzle number: title" line each at (0x12, 0x1e + 0x16 i).
    kit_.clearText(SceneKit::Lower);
    if (puzzles_.empty()) {
        kit_.textTitle(0x4a, kit_.systemText(0x514), 6, 0x80, SceneKit::Lower);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        const std::size_t k = std::size_t((page - 1) * 6 + i);
        if (k >= puzzles_.size()) {
            return;
        }
        const auto info = state_.puzzleByNumber(puzzles_[k]);
        if (!info) {
            continue;
        }
        auto line = replaceFirst(format_, "%.3d", threeDigits(info->number));
        line = replaceFirst(line, "%s", kit_.text.expand(info->title));
        kit_.textAt(0x12, 0x1e + 0x16 * i, line, SceneKit::Lower);
    }
}

// "Play this puzzle?"
bool NazobaMenu::confirm(int page, int number) {
    // 020f0800: tx 300 (the "solve puzzle %.3d?" question) centred at y 0x39.
    kit_.poll();
    draw(page, true);
    kit_.clearText(SceneKit::Lower);
    const auto info = state_.puzzleByNumber(number);
    auto text =
        replaceFirst(kit_.systemText(300), "%.3d", threeDigits(info ? info->number : number));
    text = replaceFirst(text, "%s", info ? kit_.text.expand(info->title) : std::string());
    kit_.textTitle(0x39, text, 3, 0x80, SceneKit::Lower);
    kit_.present();
    for (;;) {
        kit_.poll();
        draw(page, true);
        kit_.present();
        if (kit_.clicked(yes_)) {
            return true;
        }
        if (kit_.clicked(no_)) {
            return false;
        }
    }
}

// Runs the shack until the player leaves or starts a puzzle.
void NazobaMenu::run() {
    int page = 1;
    bool relist = false;
    kit_.poll();
    draw(page, false);
    list(page);
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw(page, false);
        if (relist) {
            list(page);
            relist = false;
        }
        kit_.present();
        if (kit_.clicked(previous_)) {
            kit_.effect(0x78);
            if (--page < 1) {
                page = pages_;
            }
            relist = true;
            continue;
        }
        if (kit_.clicked(next_)) {
            kit_.effect(0x78);
            if (++page > pages_) {
                page = 1;
            }
            relist = true;
            continue;
        }
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const std::size_t k = std::size_t(page - 1) * 6 + i;
            const int number = k < puzzles_.size() ? puzzles_[k] : -1;
            if (!kit_.clicked(entries_[i]) || number == -1) {
                continue;
            }
            kit_.effect(3);
            if (confirm(page, number)) {
                // 02083cb0 loads the puzzle by number; following scene 44.
                state_.following = scene::Nazoba;
                if (const auto info = state_.puzzleByNumber(number)) {
                    state_.loadPuzzle(info->internal);
                }
                state_.scene = scene::Puzzle;
                kit_.fadeOut();
                return;
            }
            relist = true;
        }
        if (kit_.clicked(back_)) {
            kit_.effect(0xe7);
            state_.scene = scene::Room;
            kit_.fadeOut();
            return;
        }
    }
}
} // namespace picarat
