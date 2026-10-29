// The library screen's game hub: one game's story and its extras.
// See library_screen.hpp.
//
// Landscape: a bar with the way back and the game's name; below it the
// story column (artwork, the story button) on the left and four
// tiles in a 2 x 2 grid filling the right half. Portrait: the same parts in
// one column, the tiles as full-width rows; the page scrolls when it does
// not fit. Tiles for features that are not supported yet are greyed out.
// With the app's checkpoint the story button continues from it ("Continue
// story", the checkpoint's progress under it) and a small square button
// beside it starts over from the title screen (after the app asks).
// A game whose cartridge holds several languages (the first adventure) has
// a language control under the story button: follow the system, as the
// console did, or one of the languages.
#include "shell/library_screen.hpp"
#include "shell/widgets.hpp"
#include <algorithm>
#include <cctype>

namespace picarat::shell {

using platform::Rect;
using namespace ui;

namespace {

struct TileInfo {
    std::string_view icon;
    const char* title;
};

constexpr TileInfo kTileInfo[] = {
    {style::kIconPuzzle, "Puzzles"},
    {style::kIconMinigame, "Minigames"},
    {style::kIconMovie, "Movies"},
    {style::kIconMusic, "Music"},
};

} // namespace

LibraryScreen::Action LibraryScreen::playStory() const {
    if (!game().library.story) {
        return {};
    }
    // The checkpoint continues; else the game's own title screen, where
    // the save slots are.
    Action action{game().checkpoint ? Request::ContinueStory : Request::Play};
    action.card = cardOf(game_);
    return action;
}

LibraryScreen::Action LibraryScreen::clickHub(float x, float y) {
    if (hubBack_.contains(x, y)) {
        showGames();
        return {};
    }
    if (hubRemove_.contains(x, y) && !status_.importing) {
        Action action{Request::RemoveGame};
        action.card = cardOf(game_);
        return action;
    }
    if (storyButton_.contains(x, y)) {
        return playStory();
    }
    if (restartButton_.contains(x, y) && game().library.story) {
        Action action{Request::RestartStory};
        action.card = cardOf(game_);
        return action;
    }
    for (int i = 0; i < int(languageButtons_.size()); ++i) {
        if (languageButtons_[i].contains(x, y)) {
            Action action{Request::SetLanguage};
            action.card = cardOf(game_);
            action.slot = i - 1;
            return action;
        }
    }
    if (resetButtons_[kPuzzles].contains(x, y)) {
        if (!game().solved.empty()) {
            Action action{Request::ResetPuzzles};
            action.card = cardOf(game_);
            return action;
        }
        return {};
    }
    if (resetButtons_[kMinigames].contains(x, y)) {
        if (game().minigamesSaved) {
            Action action{Request::ResetMinigames};
            action.card = cardOf(game_);
            return action;
        }
        return {};
    }
    if (tiles_[kPuzzles].contains(x, y) && !game().library.puzzles.empty()) {
        showCollection(cardOf(game_));
    }
    if (tiles_[kMinigames].contains(x, y) && !game().minigames.empty()) {
        showMinigames(cardOf(game_));
    }
    if (tiles_[kMovies].contains(x, y) && !game().movies.empty()) {
        Action action{Request::OpenMovies};
        action.card = cardOf(game_);
        return action;
    }
    if (tiles_[kMusic].contains(x, y) && game().musicTracks > 0) {
        Action action{Request::OpenMusic};
        action.card = cardOf(game_);
        return action;
    }
    return {};
}

void LibraryScreen::drawHub(style::Canvas& c) {
    const bool tall = portrait();
    const float m = margin(), contentW = width_ - 2 * m;
    const Card& card = cards_[cardOf(game_)];
    footerTop_ = height_;
    float y = -pageScroll_;

    // The bar: back to the library, the game's name, and removing the
    // imported game (to free the space, or to import another version).
    hubBack_ = {m, y + 24, 150, 36};
    button(c, hubBack_, std::string(style::kIconLeft) + " Library", ButtonKind::Plain, true,
           hubBack_.contains(mouseX_, mouseY_) && clickable(mouseY_));
    const float removeW = c.textWidth("Remove game", 1.5f) + 32;
    hubRemove_ = {width_ - m - removeW, y + 24, removeW, 36};
    button(c, hubRemove_, "Remove game", ButtonKind::Plain, !status_.importing,
           !status_.importing && hubRemove_.contains(mouseX_, mouseY_) && clickable(mouseY_), 1.5f);
    if (tall) {
        c.text(card.title, m, y + 80, 3, kInk);
        y += 120;
    } else {
        c.textCentred(card.title, width_ / 2, y + 24 + (36 - c.capHeight(3)) / 2, 3, kInk);
        ornament(c, m, y + 76, contentW);
        y += 96;
    }

    // The story column: artwork, the story button and the saves.
    const float leftW = tall ? contentW : (contentW - 24) / 2.15f;
    const float artH = tall ? 150 : 200;
    if (card.art) {
        auto& scaled = cards_[cardOf(game_)].hub;
        const int w = c.px(leftW), h = c.px(artH);
        if (scaled.width != w || scaled.height != h) {
            scaled = platform::Image(w, h);
            style::picture(scaled, *card.art, 0, 0, w, h, 255);
        }
        c.picture(scaled, m, y, leftW, artH);
    }
    const float storyY = y + artH + 16;
    const bool story = game().library.story;
    const auto& checkpoint = game().checkpoint;
    const bool resumable = story && checkpoint;
    // Continue and the square restart button share the story button's width.
    storyButton_ = {m, storyY, resumable ? leftW - 48 - 8 : leftW, 48};
    restartButton_ = resumable ? Rect{m + leftW - 48, storyY, 48, 48} : Rect{};
    button(c, storyButton_, resumable ? "Continue story" : "Play story", ButtonKind::Primary, story,
           story && storyButton_.contains(mouseX_, mouseY_) && clickable(mouseY_), 2.5f);
    if (resumable) {
        button(c, restartButton_, style::kIconRestart, ButtonKind::Plain, true,
               restartButton_.contains(mouseX_, mouseY_) && clickable(mouseY_), 3.5f);
    }
    const std::string storyNote = !story      ? "Imported. The story cannot be played yet."
                                  : resumable ? "Last played: " + checkpoint->detail
                                              : "Choose a save slot on the game's title screen.";
    c.text(c.ellipsize(storyNote, leftW, 1.5f), m + 1, storyY + 60, 1.5f, kMuted);
    const float languageH = drawLanguage(c, m, storyY + 92, leftW);

    // The tiles: a 2 x 2 grid on the right, or rows under the story button.
    const float tilesTop = tall ? storyY + 92 + languageH : y;
    for (int i = 0; i < kTiles; ++i) {
        Rect r;
        if (tall) {
            r = {m, tilesTop + i * 90, contentW, 80};
        } else {
            const float x0 = m + leftW + 24, w = (m + contentW - x0 - 16) / 2;
            const float h = (height_ - 48 - y - 16) / 2;
            r = {x0 + (i % 2) * (w + 16), y + (i / 2) * (h + 16), w, h};
        }
        tiles_[i] = r;
        drawTile(c, Tile(i), r, tall);
    }

    const float end = tall ? tilesTop + kTiles * 90 + 14 : height_;
    pageScrollMax_ = tall ? std::max(0.0f, end + pageScroll_ - height_) : 0;
    pageScroll_ = std::min(pageScroll_, pageScrollMax_);
}

void LibraryScreen::drawTile(style::Canvas& c, Tile tile, const Rect& r, bool rows) {
    const auto& info = kTileInfo[tile];
    const bool enabled = (tile == kPuzzles && !game().library.puzzles.empty()) ||
                         (tile == kMusic && game().musicTracks > 0) ||
                         (tile == kMovies && !game().movies.empty()) ||
                         (tile == kMinigames && !game().minigames.empty());
    const bool hover = enabled && r.contains(mouseX_, mouseY_) && clickable(mouseY_) &&
                       !resetButtons_[tile].contains(mouseX_, mouseY_);
    if (enabled) {
        c.rounded(r.x, r.y + 3, r.w, r.h, 8, Color{40, 28, 16, 26});
        c.frame(r.x, r.y, r.w, r.h, 8, hover ? 2 : 1, hover ? kGold : kLine,
                hover ? kWhite : kCard);
    } else {
        c.rounded(r.x, r.y, r.w, r.h, 8, style::mix(kPaper, kDisabled, 140));
    }
    const Color ink = enabled ? kInk : kDisabledInk;
    const Color icon = enabled ? kBrass : kDisabledInk;

    // What the tile says under its title.
    std::string status = "Not yet available";
    float progress = -1;
    if (enabled && tile == kPuzzles) {
        const std::size_t total = game().library.puzzles.size();
        const std::size_t solved = game().solved.size();
        status = std::to_string(solved) + " of " + std::to_string(total) + " solved";
        progress = total ? float(solved) / total : 0;
    } else if (enabled && tile == kMinigames) {
        int available = 0, complete = 0;
        for (const auto& m : game().minigames) {
            available += m.available;
            complete += m.available && m.complete;
        }
        status = std::to_string(complete) + " of " + std::to_string(available) + " complete";
        progress = available ? float(complete) / available : 0;
    } else if (enabled && tile == kMovies) {
        status = std::to_string(game().movies.size()) + " movies · contains spoilers";
    } else if (enabled && tile == kMusic) {
        status = std::to_string(game().musicTracks) + " tracks";
    }

    // Puzzle mode's and the minigames' progress can be reset (the app asks
    // first): a small square button with the restart arrow.
    const bool resettable = enabled && (tile == kPuzzles || tile == kMinigames);
    Rect reset{};
    if (resettable) {
        reset = rows ? Rect{r.x + r.w - 14 - 36, r.y + (r.h - 36) / 2, 36, 36}
                     : Rect{r.x + r.w - 20 - 40, r.y + r.h - 20 - 40, 40, 40};
    }

    if (rows) {
        // A row: icon, title and status, a thin bar, Reset on the right.
        c.icon(info.icon, r.x + 20, r.y + (r.h - 32) / 2, 32, icon);
        const float textX = r.x + 76, textW = (reset.w ? reset.x - 12 : r.x + r.w - 16) - textX;
        c.text(info.title, textX, r.y + 14, 2.5f, ink);
        c.text(c.ellipsize(status, textW, 1.5f), textX, r.y + 42, 1.5f, enabled ? kMuted : ink);
        if (progress >= 0) {
            c.rounded(textX, r.y + 62, textW, 6, 3, kLine);
            if (progress > 0) { // an empty bar shows no gold at all
                c.rounded(textX, r.y + 62, std::max(6.0f, textW * progress), 6, 3, kGold);
            }
        }
    } else {
        // A tile: a large icon, the title, the status, a bar, Reset below.
        c.icon(info.icon, r.x + 22, r.y + 24, 48, icon);
        c.text(info.title, r.x + 22, r.y + 96, 3, ink);
        c.wrapped(status, r.x + 22, r.y + 132, r.w - 44, 2, 2, enabled ? kMuted : ink);
        if (progress >= 0) {
            c.rounded(r.x + 22, r.y + 168, r.w - 44, 8, 4, kLine);
            if (progress > 0) {
                c.rounded(r.x + 22, r.y + 168, std::max(8.0f, (r.w - 44) * progress), 8, 4, kGold);
            }
        }
    }
    resetButtons_[tile] = reset;
    if (resettable) {
        const bool canReset = tile == kPuzzles ? !game().solved.empty() : game().minigamesSaved;
        button(c, reset, style::kIconRestart, ButtonKind::Plain, canReset,
               canReset && reset.contains(mouseX_, mouseY_) && clickable(mouseY_), 3);
    }
}

float LibraryScreen::drawLanguage(style::Canvas& c, float x, float y, float w) {
    const auto& languages = game().library.languages;
    languageButtons_.clear();
    if (languages.empty()) {
        return 0;
    }
    // "Language", a row of buttons (System, then the two-letter codes), and
    // the language in use spelled out under them.
    c.text("Language", x + 1, y, 2, kInk);
    const auto& choice = game().language;
    const int count = int(languages.size()) + 1;
    const float gap = 6, bw = (w - gap * (count - 1)) / count, by = y + 26;
    for (int i = 0; i < count; ++i) {
        const Rect r{x + i * (bw + gap), by, bw, 32};
        languageButtons_.push_back(r);
        std::string label = "System";
        if (i > 0) {
            label = languages[std::size_t(i - 1)].code;
            for (char& ch : label) {
                ch = char(std::toupper(std::uint8_t(ch)));
            }
        }
        const bool selected = choice.chosen == i - 1;
        button(c, r, label, selected ? ButtonKind::Primary : ButtonKind::Plain, true,
               r.contains(mouseX_, mouseY_) && clickable(mouseY_), 1.5f);
    }
    std::string used = "In use: ";
    if (choice.used >= 0 && choice.used < int(languages.size())) {
        used += languages[std::size_t(choice.used)].name;
    }
    if (choice.chosen < 0) {
        used += " (this computer's language)";
    }
    c.text(c.ellipsize(used, w, 1.5f), x + 1, by + 42, 1.5f, kMuted);
    return 26 + 32 + 10 + c.lineHeight(1.5f) + 18;
}

} // namespace picarat::shell
