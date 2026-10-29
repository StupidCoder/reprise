// The library screen's minigames: a card for each of a game's minigames with
// its progress in free play (minigames.hpp). See library_screen.hpp; the app
// plays the chosen minigame. Minigames that are not reconstructed yet are
// greyed out.
//
// Two columns in a landscape window, one in a portrait one; the page scrolls
// when it does not fit.
#include "shell/library_screen.hpp"
#include "shell/widgets.hpp"
#include <algorithm>

namespace picarat::shell {

using platform::Rect;
using namespace ui;

void LibraryScreen::showMinigames(int card) {
    const int slot = slotOf(card);
    if (!games_[slot].library.ready || games_[slot].minigames.empty()) {
        return;
    }
    if (view_ != View::Minigames || slot != game_) {
        pageScroll_ = 0;
    }
    game_ = slot;
    view_ = View::Minigames;
}

LibraryScreen::Action LibraryScreen::clickMinigames(float x, float y) {
    if (minigamesBack_.contains(x, y)) {
        showHub(cardOf(game_));
        return {};
    }
    for (int i = 0; i < int(minigameCells_.size()); ++i) {
        if (minigameCells_[std::size_t(i)].contains(x, y) &&
            game().minigames[std::size_t(i)].available) {
            Action action{Request::PlayMinigame};
            action.card = cardOf(game_);
            action.slot = i;
            return action;
        }
    }
    return {};
}

void LibraryScreen::drawMinigames(style::Canvas& c) {
    const bool tall = portrait();
    const float m = margin(), contentW = width_ - 2 * m;
    const Card& card = cards_[cardOf(game_)];
    footerTop_ = height_;
    float y = -pageScroll_;

    // The bar: back to the hub, "Minigames" and the game.
    minigamesBack_ = {m, y + 24, 150, 36};
    button(c, minigamesBack_, std::string(style::kIconLeft) + " Back", ButtonKind::Plain, true,
           minigamesBack_.contains(mouseX_, mouseY_) && clickable(mouseY_));
    c.textRight(card.title, width_ - m, y + 24 + (36 - c.capHeight(1.5f)) / 2, 1.5f, kMuted);
    if (tall) {
        c.text("Minigames", m, y + 80, 3, kInk);
        y += 124;
    } else {
        c.textCentred("Minigames", width_ / 2, y + 24 + (36 - c.capHeight(3)) / 2, 3, kInk);
        c.rect(m, y + 76, contentW, 1, kGold);
        y += 96;
    }

    // The cards: the minigame's name, its progress and a bar; a tick once
    // it is complete.
    const int columns = tall ? 1 : 2;
    const float gap = 16, cellW = (contentW - (columns - 1) * gap) / columns, cellH = 136;
    const auto& list = game().minigames;
    minigameCells_.assign(list.size(), Rect{});
    for (std::size_t i = 0; i < list.size(); ++i) {
        const MinigameStatus& g = list[i];
        const Rect r{m + float(i % columns) * (cellW + gap), y + float(i / columns) * (cellH + gap),
                     cellW, cellH};
        minigameCells_[i] = r;
        const bool hover = g.available && r.contains(mouseX_, mouseY_) && clickable(mouseY_);
        if (g.available) {
            c.rounded(r.x, r.y + 3, r.w, r.h, 8, Color{40, 28, 16, 26});
            c.frame(r.x, r.y, r.w, r.h, 8, hover ? 3 : 2, kGold, hover ? kWhite : kCard);
        } else {
            c.rounded(r.x, r.y, r.w, r.h, 8, style::mix(kPaper, kDisabled, 140));
        }
        const Color ink = g.available ? kInk : kDisabledInk;
        c.text(style::kIconMinigame, r.x + 22, r.y + 26, 4, g.available ? kGold : kDisabledInk);
        const float textX = r.x + 92, textW = r.x + r.w - 22 - textX;
        c.text(c.ellipsize(g.name, textW - 30, 3), textX, r.y + 24, 3, ink);
        if (g.complete) {
            c.textRight(style::kIconCheck, r.x + r.w - 22, r.y + 24, 3, kGold);
        }
        c.text(c.ellipsize(g.progress, textW, 2), textX, r.y + 62, 2, g.available ? kMuted : ink);
        if (g.available) {
            c.rounded(textX, r.y + 98, textW, 8, 4, kLine);
            if (g.fraction > 0) { // an empty bar shows no gold at all
                c.rounded(textX, r.y + 98, std::max(8.0f, textW * g.fraction), 8, 4, kGold);
            }
        }
    }
    const float end = y + float((list.size() + columns - 1) / columns) * (cellH + gap) + 8;
    pageScrollMax_ = std::max(0.0f, end + pageScroll_ - height_);
    pageScroll_ = std::min(pageScroll_, pageScrollMax_);
}

} // namespace picarat::shell
