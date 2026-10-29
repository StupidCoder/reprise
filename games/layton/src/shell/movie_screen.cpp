// The library screen's movie theatre: a game's movies as the thumbnails its
// bonus page shows, numbered in the page's order. See library_screen.hpp; the
// app plays the chosen movie (GameSession) on one enlarged screen.
//
// The grid has six columns in a landscape window and three in a portrait
// one; the page scrolls when it does not fit.
#include "shell/library_screen.hpp"
#include "shell/widgets.hpp"
#include <algorithm>

namespace picarat::shell {

using platform::Rect;
using namespace ui;

void LibraryScreen::showMovies(int card) {
    const int slot = slotOf(card);
    if (!games_[slot].library.ready || games_[slot].movies.empty()) {
        return;
    }
    if (view_ != View::Movies || slot != game_) {
        pageScroll_ = 0;
    }
    game_ = slot;
    view_ = View::Movies;
}

LibraryScreen::Action LibraryScreen::clickMovies(float x, float y) {
    if (moviesBack_.contains(x, y)) {
        showHub(cardOf(game_));
        return {};
    }
    for (int i = 0; i < int(movieCells_.size()); ++i) {
        if (movieCells_[std::size_t(i)].contains(x, y)) {
            Action action{Request::PlayMovie};
            action.card = cardOf(game_);
            action.slot = i;
            return action;
        }
    }
    return {};
}

void LibraryScreen::drawMovies(style::Canvas& c) {
    const bool tall = portrait();
    const float m = margin(), contentW = width_ - 2 * m;
    Game& game = games_[game_];
    const Card& card = cards_[cardOf(game_)];
    footerTop_ = height_;
    float y = -pageScroll_;

    // The bar: back to the hub, "Movies" and the game.
    moviesBack_ = {m, y + 24, 150, 36};
    button(c, moviesBack_, std::string(style::kIconLeft) + " Back", ButtonKind::Plain, true,
           moviesBack_.contains(mouseX_, mouseY_) && clickable(mouseY_));
    c.textRight(card.title, width_ - m, y + 24 + (36 - c.capHeight(1.5f)) / 2, 1.5f, kMuted);
    if (tall) {
        c.text("Movies", m, y + 80, 3, kInk);
        y += 124;
    } else {
        c.textCentred("Movies", width_ / 2, y + 24 + (36 - c.capHeight(3)) / 2, 3, kInk);
        c.rect(m, y + 76, contentW, 1, kGold);
        y += 96;
    }

    // The grid: each movie's picture with its number underneath.
    const int columns = tall ? 3 : 6;
    const float gap = 16, cellW = (contentW - (columns - 1) * gap) / columns;
    const auto& first = game.movies.front().thumbnail;
    const float pictureW = cellW - 16;
    const float pictureH = first.width ? pictureW * first.height / first.width : pictureW * 0.75f;
    const float cellH = 8 + pictureH + 8 + 24;
    movieCells_.assign(game.movies.size(), Rect{});
    for (std::size_t i = 0; i < game.movies.size(); ++i) {
        const Rect r{m + float(i % columns) * (cellW + gap), y + float(i / columns) * (cellH + gap),
                     cellW, cellH};
        movieCells_[i] = r;
        if (r.y > height_ || r.y + r.h < 0) {
            continue;
        }
        const bool hover = r.contains(mouseX_, mouseY_) && clickable(mouseY_);
        c.rounded(r.x, r.y + 3, r.w, r.h, 8, Color{40, 28, 16, 26});
        c.frame(r.x, r.y, r.w, r.h, 8, hover ? 3 : 2, kGold, hover ? kWhite : kCard);
        const auto& thumbnail = game.movies[i].thumbnail;
        if (thumbnail.width) {
            // Scaled once per size; the pictures are small, so they are
            // enlarged by averaging, which keeps their pixels square.
            auto& scaled = game.thumbnails[i];
            const int w = c.px(pictureW), h = c.px(pictureH); // independent of scrolling
            if (scaled.width != w || scaled.height != h) {
                scaled = style::scaled(thumbnail, w, h);
            }
            c.blend(scaled, r.x + 8, r.y + 8);
        }
        const int number = int(i) + 1;
        c.textCentred(std::string("Movie ") + (number < 10 ? "0" : "") + std::to_string(number),
                      r.x + r.w / 2, r.y + 8 + pictureH + 12, 1.5f, hover ? kGold : kInk);
    }
    const float end = y + float((game.movies.size() + columns - 1) / columns) * (cellH + gap) + 8;
    pageScrollMax_ = std::max(0.0f, end + pageScroll_ - height_);
    pageScroll_ = std::min(pageScroll_, pageScrollMax_);
}

} // namespace picarat::shell
