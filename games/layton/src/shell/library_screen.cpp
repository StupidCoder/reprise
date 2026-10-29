// The library screen: game cards and the puzzle collection. See
// library_screen.hpp.
#include "shell/library_screen.hpp"
#include "shell/widgets.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace picarat::shell {

using platform::Event;
using platform::Key;
using platform::Rect;
using namespace ui;

namespace {

// The importable cards, in the order of the per-card rectangles.
constexpr int kImportable[LibraryScreen::kImportableGames] = {
    LibraryScreen::kAdventure2Card, LibraryScreen::kAdventure3Card, LibraryScreen::kAdventure1Card,
    LibraryScreen::kAdventure4Card, LibraryScreen::kAdventure5Card, LibraryScreen::kAdventure6Card};

// Games view: the illustration band's height, and the card around it.
float artHeight(bool portrait) {
    return portrait ? 170 : 156;
}

float cardHeight(bool portrait) {
    return artHeight(portrait) + 70;
}

// Collection view: list metrics and column titles.
constexpr float kHeaderHeight = 30, kRowHeight = 32, kScrollbarWidth = 10;
constexpr int kColumns = 3;
constexpr const char* kColumnTitles[kColumns] = {"No.", "Puzzle", "Type"};

int columnAt(const float (&columnX)[kColumns], float x) {
    for (int i = kColumns - 1; i >= 0; --i) {
        if (x >= columnX[i]) {
            return i;
        }
    }
    return 0;
}

// Text vertically centred in a band starting at y.
float centred(const style::Canvas& c, float y, float height, float size) {
    return y + (height - c.capHeight(size)) / 2;
}

} // namespace

LibraryScreen::LibraryScreen(const std::filesystem::path& resources) {
    const struct {
        const char* title;
        const char* platform;
        int year; // first release (Japan)
        const char* art;
    } cards[] = {
        {"Curious Village", "Nintendo DS", 2007, "village.png"},
        {"Pandora's Box", "Nintendo DS", 2007, "train.png"},
        {"Lost Future", "Nintendo DS", 2008, "clock.png"},
        {"Spectre's Call", "Nintendo DS", 2009, "canal.png"},
        {"Miracle Mask", "Nintendo 3DS", 2011, "mask.png"},
        {"Azran Legacy", "Nintendo 3DS", 2013, "sanctuary.png"},
    };

    for (const auto& card : cards) {
        cards_.push_back({card.title,
                          card.platform,
                          platform::Host::loadPng(resources / "art" / card.art),
                          {},
                          {},
                          {},
                          card.year});
    }
    logo_ = platform::Host::loadPng(resources / "art" / "logo.png");
}

LibraryScreen::Viewport LibraryScreen::viewport(float width, float height) {
    if (height > width) {
        // Portrait: fill the window, as tall as it is.
        const float zoom = std::max(0.01f, width / kPortraitWidth);
        return {{0, 0, width, height}, kPortraitWidth, height / zoom, zoom};
    }
    const float zoom = std::max(0.01f, std::min(width / kWidth, height / kHeight));
    const float w = kWidth * zoom, h = kHeight * zoom;
    return {
        {std::floor((width - w) / 2), std::floor((height - h) / 2), w, h}, kWidth, kHeight, zoom};
}

void LibraryScreen::resize(float width, float height) {
    width_ = width;
    height_ = height;
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
}

void LibraryScreen::setLibrary(int card, const Library& library) {
    const int slot = slotOf(card);
    games_[slot].library = library;
    if (slot == game_) {
        updateKinds();
        filter();
        if (!library.ready) {
            view_ = View::Games;
        }
    }
}

void LibraryScreen::setCheckpoint(int card, std::optional<StorySave> checkpoint) {
    games_[slotOf(card)].checkpoint = std::move(checkpoint);
}

void LibraryScreen::setMusicTracks(int card, int count) {
    games_[slotOf(card)].musicTracks = count;
}

void LibraryScreen::setMinigames(int card, std::vector<MinigameStatus> minigames, bool saved) {
    auto& game = games_[slotOf(card)];
    game.minigames = std::move(minigames);
    game.minigamesSaved = saved;
}

void LibraryScreen::setMovies(int card, std::vector<MovieEntry> movies) {
    auto& game = games_[slotOf(card)];
    game.movies = std::move(movies);
    game.thumbnails.assign(game.movies.size(), {});
}

void LibraryScreen::setLanguage(int card, LanguageChoice choice) {
    games_[slotOf(card)].language = choice;
}

void LibraryScreen::setMusic(MusicStatus music) {
    music_ = std::move(music);
}

void LibraryScreen::setSolved(int card, std::set<unsigned> solved) {
    games_[slotOf(card)].solved = std::move(solved);
}

int LibraryScreen::cardOf(int slot) const {
    return kImportable[slot];
}

// The type filter offers the kinds present in the shown game, alphabetically.
void LibraryScreen::updateKinds() {
    const std::string previous = kind_ < int(kinds_.size()) ? kinds_[kind_] : "";
    std::set<std::string> kinds;
    for (const auto& row : collectionLibrary().puzzles) {
        kinds.insert(row.kind);
    }
    kinds_ = {"All puzzle types"};
    kinds_.insert(kinds_.end(), kinds.begin(), kinds.end());
    const auto again = std::find(kinds_.begin(), kinds_.end(), previous);
    kind_ = again == kinds_.end() ? 0 : int(again - kinds_.begin());
}

void LibraryScreen::setStatus(const Status& status) {
    status_ = status;
}

void LibraryScreen::showGames() {
    if (view_ != View::Games) {
        pageScroll_ = 0;
    }
    view_ = View::Games;
    kindsOpen_ = false;
}

void LibraryScreen::showHub(int card) {
    const int slot = slotOf(card);
    if (!games_[slot].library.ready) {
        showGames();
        return;
    }
    if (view_ != View::Hub || slot != game_) {
        pageScroll_ = 0;
    }
    game_ = slot;
    view_ = View::Hub;
    kindsOpen_ = false;
}

void LibraryScreen::showCollection(int card) {
    const int slot = slotOf(card);
    if (!games_[slot].library.ready) {
        return;
    }
    if (slot != game_) {
        game_ = slot;
        kind_ = 0;
        scroll_ = 0;
    }
    updateKinds();
    view_ = View::Collection;
    filter();
}

LibraryScreen::Action LibraryScreen::handle(const Event& e) {
    if (e.type == Event::Type::MouseMove || e.type == Event::Type::MouseDown) {
        mouseX_ = e.x;
        mouseY_ = e.y;
    }
    if (e.type == Event::Type::MouseDown && e.button == 1 && fling_.moving) {
        catchFling(e);
        return {};
    }
    switch (view_) {
    case View::Collection:
        return handleCollection(e);
    case View::Music:
        return handleMusic(e);
    default:
        return handlePage(e);
    }
}

void LibraryScreen::draw(style::Canvas& c) {
    paper(c, width_, height_);
    switch (view_) {
    case View::Games:
        drawGames(c);
        break;
    case View::Hub:
        drawHub(c);
        break;
    case View::Collection:
        drawCollection(c);
        break;
    case View::Music:
        drawMusic(c);
        break;
    case View::Movies:
        drawMovies(c);
        break;
    case View::Minigames:
        drawMinigames(c);
        break;
    }
}

// --- Scrolling pages ---------------------------------------------------------

LibraryScreen::Action LibraryScreen::handlePage(const Event& e) {
    const bool hub = view_ == View::Hub;
    auto click = [&](float x, float y) {
        return hub                        ? clickHub(x, y)
               : view_ == View::Movies    ? clickMovies(x, y)
               : view_ == View::Minigames ? clickMinigames(x, y)
                                          : clickGames(x, y);
    };
    switch (e.type) {
    case Event::Type::MouseDown:
        if (e.button == 1) {
            pressed_ = true;
            panning_ = false;
            pressX_ = e.x;
            pressY_ = e.y;
            pressScroll_ = pageScroll_;
        }
        break;
    case Event::Type::MouseMove:
        // A press that moves becomes a drag of the page (touch scrolling).
        if (pressed_ && e.button == 1 && pageScrollMax_ > 0 &&
            (panning_ || std::abs(e.y - pressY_) > 8)) {
            panning_ = true;
            pageScroll_ = std::clamp(pressScroll_ - (e.y - pressY_), 0.0f, pageScrollMax_);
        }
        break;
    case Event::Type::MouseUp:
        if (e.button == 1 && pressed_) {
            pressed_ = false;
            const bool panned = panning_;
            panning_ = false;
            if (!panned) {
                return click(pressX_, pressY_);
            }
        }
        break;
    case Event::Type::Wheel:
        scrollPage(-e.wheel * 40);
        break;
    case Event::Type::KeyDown:
        if (e.command) {
            break;
        }
        switch (e.key) {
        case Key::Return:
            if (hub) {
                return playStory();
            }
            if (view_ == View::Movies || view_ == View::Minigames) {
                break;
            }
            if (!status_.importing) {
                // The first imported game, else the first one to import.
                int slot = 0;
                for (int s = 0; s < kImportableGames; ++s) {
                    if (games_[s].library.ready) {
                        slot = s;
                        break;
                    }
                }
                return clickGames(cardArea_[slot].x + 1, cardArea_[slot].y + 1);
            }
            break;
        case Key::Escape:
            if (view_ == View::Movies || view_ == View::Minigames) {
                showHub(cardOf(game_));
            } else if (hub) {
                showGames();
            } else if (status_.importing) {
                return {Request::CancelImport};
            } else {
                return {Request::Leave};
            }
            break;
        case Key::Up:
            scrollPage(-40);
            break;
        case Key::Down:
            scrollPage(40);
            break;
        case Key::PageUp:
            scrollPage(-(footerTop_ - 40));
            break;
        case Key::PageDown:
            scrollPage(footerTop_ - 40);
            break;
        case Key::Home:
            scrollPage(-pageScrollMax_);
            break;
        case Key::End:
            scrollPage(pageScrollMax_);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return {};
}

void LibraryScreen::scrollPage(float delta) {
    pageScroll_ = std::clamp(pageScroll_ + delta, 0.0f, pageScrollMax_);
}

// --- Swipe momentum ----------------------------------------------------------

float* LibraryScreen::scrollTarget(float& max) {
    switch (view_) {
    case View::Collection:
        max = maxScroll();
        return &scroll_;
    case View::Music:
        max = musicListMax();
        return &musicScroll_;
    default:
        max = pageScrollMax_;
        return &pageScroll_;
    }
}

void LibraryScreen::catchFling(const Event& e) {
    // The press holds the page where it is and can drag it on; releasing it
    // does not click.
    fling_.moving = false;
    switch (view_) {
    case View::Collection:
        listPressed_ = listPanning_ = true;
        listPressY_ = e.y;
        listPressScroll_ = scroll_;
        break;
    case View::Music:
        musicPressed_ = musicPanning_ = true;
        musicPressY_ = e.y;
        musicPressScroll_ = musicScroll_;
        break;
    default:
        pressed_ = panning_ = true;
        pressX_ = e.x;
        pressY_ = e.y;
        pressScroll_ = pageScroll_;
        break;
    }
}

bool LibraryScreen::animate(double now) {
    // Slowing down: the speed falls by e^-kFriction per second; slower
    // swipes than kMinimum do not carry on.
    constexpr float kFriction = 2.5f, kMinimum = 150, kStop = 15;
    float max = 0;
    float* scroll = scrollTarget(max);
    if (dragging()) {
        if (!fling_.dragging || fling_.view != view_) {
            fling_ = {};
            fling_.view = view_;
            fling_.dragging = true;
            fling_.lastScroll = *scroll;
            fling_.lastMove = now;
            return false;
        }
        // The finger's speed, smoothed over the last movements.
        if (*scroll != fling_.lastScroll && now > fling_.lastMove) {
            const float speed = float((*scroll - fling_.lastScroll) / (now - fling_.lastMove));
            fling_.velocity = fling_.velocity == 0 ? speed : (speed + fling_.velocity) / 2;
            fling_.lastScroll = *scroll;
            fling_.lastMove = now;
        }
        return false;
    }
    if (fling_.dragging) {
        // The finger lifted: carry on if it was still moving fast.
        fling_.dragging = false;
        fling_.moving = fling_.view == view_ && now - fling_.lastMove < 0.1 &&
                        std::abs(fling_.velocity) >= kMinimum;
        fling_.time = now;
        return false;
    }
    if (!fling_.moving || fling_.view != view_) {
        fling_.moving = false;
        return false;
    }
    const float dt = float(std::min(now - fling_.time, 0.05));
    fling_.time = now;
    *scroll = std::clamp(*scroll + fling_.velocity * dt, 0.0f, max);
    fling_.velocity *= std::exp(-kFriction * dt);
    if (std::abs(fling_.velocity) < kStop || *scroll <= 0 || *scroll >= max) {
        fling_.moving = false;
    }
    return true;
}

// --- Games view ------------------------------------------------------------

LibraryScreen::Action LibraryScreen::clickGames(float x, float y) {
    if (status_.importing) {
        return cancelButton_.contains(x, y) ? Action{Request::CancelImport} : Action{};
    }
    if (y >= footerTop_) {
        return {}; // the footer covers the end of the scrolled list
    }
    // A card (or its "Add game..." button) imports its game, or opens the
    // game's hub once imported.
    for (int slot = 0; slot < kImportableGames; ++slot) {
        if (cardArea_[slot].contains(x, y)) {
            if (games_[slot].library.ready) {
                showHub(cardOf(slot));
                return {};
            }
            Action action{Request::Import};
            action.card = cardOf(slot);
            return action;
        }
    }
    return {};
}

void LibraryScreen::drawGames(style::Canvas& c) {
    const bool tall = portrait();
    const float m = margin(), contentW = width_ - 2 * m;
    footerTop_ = height_ - footerHeight(c);
    float y = -pageScroll_;

    // The logo, then the tagline under it.
    const float logoH = tall ? 54 : 60;
    if (logo_ && logo_->height > 0) {
        const float logoW = logoH * logo_->width / logo_->height;
        const int w = c.px(logoW), h = c.px(logoH); // the same at any scroll position
        if (logoScaled_.width != w || logoScaled_.height != h) {
            logoScaled_ = style::scaled(*logo_, w, h);
        }
        c.blend(logoScaled_, m - 4, y + 16);
    }
    y += 16 + logoH + 10;
    const char* tagline = "Choose an adventure, or revisit a favourite puzzle.";
    y += c.wrapped(tagline, m + 2, y, contentW, 3, 2, kMuted) * c.lineHeight(2) + 8;
    ornament(c, m, y, contentW);
    y += 16;

    // Two sections of three cards: rows of three side by side, or a column.
    cancelButton_ = {};
    std::fill(std::begin(cardArea_), std::end(cardArea_), Rect{});
    const float cardH = cardHeight(tall);
    const float cardW = tall ? contentW : (contentW - 40) / 3;
    const char* sections[2] = {"Original Trilogy", "Prequel Trilogy"};
    for (int section = 0; section < 2; ++section) {
        c.text(sections[section], m + 1, y, 2.5f, kInk);
        y += 26;
        for (int k = 0; k < 3; ++k) {
            const int i = section * 3 + k;
            const Rect r = tall ? Rect{m, y + k * (cardH + 16), cardW, cardH}
                                : Rect{m + k * (cardW + 20), y, cardW, cardH};
            drawCard(c, i, r);
        }
        y += tall ? 3 * (cardH + 16) : cardH + 11;
    }
    // Everything above was laid out from the scrolled top.
    pageScrollMax_ = tall ? std::max(0.0f, y + pageScroll_ - footerTop_) : 0;
    if (pageScroll_ > pageScrollMax_) {
        pageScroll_ = pageScrollMax_; // the window grew; the next draw catches up
    }
    drawGamesFooter(c);
}

void LibraryScreen::drawCard(style::Canvas& c, int i, const Rect& r) {
    auto& card = cards_[i];
    const bool active = i >= 0 && i < kImportableGames; // every card can be imported
    const int slot = active ? slotOf(i) : -1;
    const bool importingHere = status_.importing && status_.card == i;
    const bool hover =
        active && r.contains(mouseX_, mouseY_) && clickable(mouseY_) && !status_.importing;
    c.rounded(r.x, r.y + 4, r.w, r.h, 8, Color{40, 28, 16, 26});
    if (active) {
        c.frame(r.x, r.y, r.w, r.h, 8, hover ? 2 : 1, hover ? kGold : kLine,
                hover ? kWhite : kCard);
    } else {
        c.rounded(r.x, r.y, r.w, r.h, 8, kCard);
    }
    // The illustration, cropped to a wide band; planned games are faded.
    const float ax = r.x + 8, ay = r.y + 8, aw = r.w - 16, ah = artHeight(portrait());
    if (card.art) {
        const int w = c.px(aw), h = c.px(ah); // the same at any scroll position
        if (card.scaled.width != w || card.scaled.height != h) {
            card.scaled = platform::Image(w, h);
            style::picture(card.scaled, *card.art, 0, 0, w, h, 255);
        }
        c.picture(card.scaled, ax, ay, aw, ah, active ? 255 : 160);
    } else {
        c.rect(ax, ay, aw, ah, kLine);
    }
    // Below the band: the title, the release year and platform, and on the
    // right an arrow button (or the import's Cancel); planned games say so.
    const float base = ay + ah;
    const float right = r.x + r.w - 16;
    const std::string facts = std::to_string(card.year) + " · " + card.platform;
    if (!active) {
        c.text(card.title, r.x + 16, base + 12, 2.5f, kMuted);
        c.text(facts, r.x + 16, base + 40, 1.5f, kMuted);
        c.textRight("Planned", right, base + 40, 1.5f, kMuted);
        return;
    }
    cardArea_[slot] = r;
    if (importingHere) {
        cancelButton_ = {right - 80, base + 10, 80, 30};
        c.text(c.ellipsize(card.title, cancelButton_.x - 12 - (r.x + 16), 2.5f), r.x + 16,
               base + 12, 2.5f, kInk);
        button(c, cancelButton_, "Cancel", ButtonKind::Plain, true,
               cancelButton_.contains(mouseX_, mouseY_), 1.5f);
        c.text(c.ellipsize(status_.message, r.w - 32, 1.5f), r.x + 16, base + 40, 1.5f, kMuted);
        const float barW = r.w - 32;
        c.rounded(r.x + 16, base + 55, barW, 6, 3, kLine);
        c.rounded(r.x + 16, base + 55,
                  std::max(6.0f, float(barW * std::clamp(status_.progress, 0.0, 1.0))), 6, 3,
                  kGold);
        return;
    }
    if (!games_[slot].library.ready) {
        // Not imported yet: an "Add game..." button across the
        // picture; the whole card imports as well.
        const float bw = std::min(aw - 32, 200.0f), bh = 44;
        const Rect add{std::floor(ax + (aw - bw) / 2), std::floor(ay + (ah - bh) / 2), bw, bh};
        button(c, add, "Add game…", ButtonKind::Primary, !status_.importing, hover, 2);
        c.text(c.ellipsize(card.title, r.w - 32, 2.5f), r.x + 16, base + 12, 2.5f, kMuted);
        c.text(facts, r.x + 16, base + 40, 1.5f, kMuted);
        return;
    }
    const Rect arrow{right - 34, base + 14, 34, 34};
    c.text(c.ellipsize(card.title, arrow.x - 12 - (r.x + 16), 2.5f), r.x + 16, base + 12, 2.5f,
           kInk);
    c.text(facts, r.x + 16, base + 40, 1.5f, kMuted);
    // The arrow opens the hub; the whole card does the same.
    if (hover) {
        c.rounded(arrow.x, arrow.y, arrow.w, arrow.h, 6, kGold);
    } else {
        c.frame(arrow.x, arrow.y, arrow.w, arrow.h, 6, 2, kGold, kWhite);
    }
    c.icon(style::kIconRight, arrow.x + (arrow.w - 22) / 2, arrow.y + (arrow.h - 22) / 2, 22,
           hover ? kWhite : kGold);
}

// The footer: a hint on the left and, on the right, the import's outcome or
// the note that the app has no game data. Portrait windows stack the two.
std::string LibraryScreen::footerHint() const {
    return games_[0].library.ready || games_[1].library.ready || games_[2].library.ready
               ? "Choose a game for its story, puzzles and more."
               : "Choose \"Add game…\" on a card to import your ROM.";
}

float LibraryScreen::footerHeight(const style::Canvas& c) const {
    if (!portrait()) {
        return 52;
    }
    const auto hint = c.lines(footerHint(), width_ - 2 * margin(), 1.5f);
    return 14 + (hint.size() + 1) * c.lineHeight(1.5f) + 16;
}

void LibraryScreen::drawGamesFooter(style::Canvas& c) {
    const float m = margin();
    const std::string hint = footerHint();
    // The import's step shows here until the ROM is known and has a card.
    const bool footMessage = status_.importing ? status_.card < 0 : !status_.message.empty();
    const std::string note =
        footMessage ? status_.message : "No game data is included with the app.";
    if (portrait()) {
        // Over the scrolled list: paper, then two lines of small print.
        c.rect(0, footerTop_, width_, height_ - footerTop_, kPaper);
        c.rect(m, footerTop_, width_ - 2 * m, 1, kGold);
        const float y = footerTop_ + 14;
        const int lines = c.wrapped(hint, m + 1, y, width_ - 2 * m, 3, 1.5f, kInk);
        const std::string shown = c.ellipsize(note, width_ - 2 * m, 1.5f);
        c.textRight(shown, width_ - m, y + lines * c.lineHeight(1.5f), 1.5f, kMuted);
        return;
    }
    // Landscape: one line, the note's small print on the hint's baseline.
    const float y = height_ - 52;
    const float hintEnd = c.text(hint, m + 1, y, 2, kInk);
    const float noteY = y + c.capHeight(2) - c.capHeight(1.5f);
    const std::string shown = c.ellipsize(note, width_ - m - hintEnd - 24, 1.5f);
    c.textRight(shown, width_ - m, noteY, 1.5f, kMuted);
}

// --- Collection view -------------------------------------------------------

void LibraryScreen::filter() {
    const auto& rows = collectionLibrary().puzzles;
    shown_.clear();
    for (int i = 0; i < int(rows.size()); ++i) {
        const auto& row = rows[i];
        if (kind_ == 0 || row.kind == kinds_[kind_]) {
            shown_.push_back(i);
        }
    }
    // Sort by the chosen column; equal entries keep the puzzle order.
    auto key = [&](int i) -> std::string {
        const auto& row = rows[i];
        switch (sortColumn_) {
        case 1:
            return style::foldCase(row.title);
        case 2:
            return row.kind;
        default:
            return {};
        }
    };
    std::stable_sort(shown_.begin(), shown_.end(), [&](int a, int b) {
        const std::string ka = key(a), kb = key(b);
        if (ka != kb) {
            return ascending_ ? ka < kb : kb < ka;
        }
        return ascending_ || sortColumn_ != 0 ? rows[a].order < rows[b].order
                                              : rows[b].order < rows[a].order;
    });
    selected_ = -1;
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
}

// The list fills the space between the type filter and the footer.
LibraryScreen::Table LibraryScreen::table() const {
    const float m = margin(), w = width_ - 2 * m;
    Table t{};
    const float top = portrait() ? 170 : 148, bottom = height_ - (portrait() ? 104 : 86);
    t.frame = {m, top, w, std::max(kHeaderHeight + kRowHeight, bottom - top)};
    t.bodyTop = top + kHeaderHeight;
    t.bodyHeight = t.frame.h - kHeaderHeight;
    const float typeWidth = std::min(230.0f, w * 0.36f);
    t.columnX[0] = m;
    t.columnX[1] = m + (portrait() ? 80 : 84);
    t.columnX[2] = m + w - typeWidth;
    t.columnsEnd = m + w - kScrollbarWidth;
    t.pageRows = std::max(1, int(t.bodyHeight / kRowHeight) - 1);
    return t;
}

float LibraryScreen::maxScroll() const {
    const Table t = table();
    return std::max(0.0f, shown_.size() * kRowHeight - t.bodyHeight);
}

void LibraryScreen::select(int row) {
    if (shown_.empty()) {
        selected_ = -1;
        return;
    }
    selected_ = std::clamp(row, 0, int(shown_.size()) - 1);
    scrollTo(selected_);
}

void LibraryScreen::scrollTo(int row) {
    const Table t = table();
    const float top = row * kRowHeight, bottom = top + kRowHeight;
    if (top < scroll_) {
        scroll_ = top;
    } else if (bottom > scroll_ + t.bodyHeight) {
        scroll_ = bottom - t.bodyHeight;
    }
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
}

int LibraryScreen::rowAt(float x, float y) const {
    const Table t = table();
    if (x < t.frame.x || x >= t.frame.x + t.frame.w - kScrollbarWidth || y < t.bodyTop ||
        y >= t.bodyTop + t.bodyHeight) {
        return -1;
    }
    const int row = int((y - t.bodyTop + scroll_) / kRowHeight);
    return row < int(shown_.size()) ? row : -1;
}

LibraryScreen::Action LibraryScreen::launchSelection() const {
    if (selected_ >= 0 && selected_ < int(shown_.size())) {
        const auto& row = collectionLibrary().puzzles[shown_[selected_]];
        if (row.supported) {
            return {Request::Play, int(row.id), cardOf(game_)};
        }
    }
    return {};
}

LibraryScreen::Action LibraryScreen::handleCollection(const Event& e) {
    const Table t = table();
    switch (e.type) {
    case Event::Type::KeyDown:
        if (e.command) {
            break;
        }
        switch (e.key) {
        case Key::Up:
            select(selected_ < 0 ? 0 : selected_ - 1);
            break;
        case Key::Down:
            select(selected_ + 1);
            break;
        case Key::PageUp:
            select(std::max(0, selected_) - t.pageRows);
            break;
        case Key::PageDown:
            select(std::max(0, selected_) + t.pageRows);
            break;
        case Key::Home:
            select(0);
            break;
        case Key::End:
            select(int(shown_.size()) - 1);
            break;
        case Key::Return:
            return launchSelection();
        case Key::Escape:
            if (kindsOpen_) {
                kindsOpen_ = false;
            } else {
                showHub(cardOf(game_));
            }
            break;
        default:
            break;
        }
        break;
    case Event::Type::Wheel:
        scroll_ = std::clamp(scroll_ - e.wheel * kRowHeight, 0.0f, maxScroll());
        break;
    case Event::Type::MouseMove:
        // A press on the rows that moves scrolls the list (touch scrolling).
        if (listPressed_ && e.button == 1 && (listPanning_ || std::abs(e.y - listPressY_) > 8)) {
            listPanning_ = true;
            scroll_ = std::clamp(listPressScroll_ - (e.y - listPressY_), 0.0f, maxScroll());
        }
        if (draggingThumb_) {
            const float trackRange =
                t.bodyHeight -
                std::max(30.0f, t.bodyHeight * t.bodyHeight / (shown_.size() * kRowHeight));
            if (trackRange > 0) {
                scroll_ = std::clamp(dragScroll_ + (e.y - dragStart_) * maxScroll() / trackRange,
                                     0.0f, maxScroll());
            }
        }
        break;
    case Event::Type::MouseUp:
        draggingThumb_ = false;
        if (e.button == 1 && listPressed_) {
            listPressed_ = false;
            const bool panned = listPanning_;
            listPanning_ = false;
            if (!panned && listPressRow_ < int(shown_.size())) {
                selected_ = listPressRow_;
                if (listPressClicks_ >= 2) {
                    return launchSelection();
                }
            }
        }
        break;
    case Event::Type::MouseDown: {
        if (e.button != 1) {
            break;
        }
        if (kindsOpen_) {
            // A click anywhere closes the menu; on an item it also chooses it.
            for (int i = 0; i < int(kindItems_.size()); ++i) {
                if (kindItems_[i].contains(e.x, e.y)) {
                    kind_ = i;
                    filter();
                }
            }
            kindsOpen_ = false;
            break;
        }
        if (kindButton_.contains(e.x, e.y)) {
            kindsOpen_ = true;
            break;
        }
        if (backButton_.contains(e.x, e.y)) {
            showHub(cardOf(game_));
            break;
        }
        if (playButton_.contains(e.x, e.y)) {
            return launchSelection();
        }
        // The column headers sort; a second click reverses the order.
        if (Rect{t.frame.x, t.frame.y, t.frame.w, kHeaderHeight}.contains(e.x, e.y)) {
            const int column = columnAt(t.columnX, e.x);
            ascending_ = column == sortColumn_ ? !ascending_ : true;
            sortColumn_ = column;
            filter();
            break;
        }
        // The scroll bar: drag the thumb, or click the track to page.
        if (maxScroll() > 0 &&
            Rect{t.frame.x + t.frame.w - kScrollbarWidth, t.bodyTop, kScrollbarWidth, t.bodyHeight}
                .contains(e.x, e.y)) {
            const float thumbH =
                std::max(30.0f, t.bodyHeight * t.bodyHeight / (shown_.size() * kRowHeight));
            const float thumbY = t.bodyTop + (t.bodyHeight - thumbH) * scroll_ / maxScroll();
            if (e.y >= thumbY && e.y < thumbY + thumbH) {
                draggingThumb_ = true;
                dragStart_ = e.y;
                dragScroll_ = scroll_;
            } else {
                const float page = (e.y < thumbY ? -1 : 1) * t.bodyHeight;
                scroll_ = std::clamp(scroll_ + page, 0.0f, maxScroll());
            }
            break;
        }
        const int row = rowAt(e.x, e.y);
        if (row >= 0) {
            listPressed_ = true;
            listPanning_ = false;
            listPressRow_ = row;
            listPressClicks_ = e.clicks;
            listPressY_ = e.y;
            listPressScroll_ = scroll_;
        }
        break;
    }
    default:
        break;
    }
    return {};
}

void LibraryScreen::drawCollection(style::Canvas& c) {
    const auto& rows = collectionLibrary().puzzles;
    const Table t = table();
    const bool tall = portrait();
    const float m = margin();

    // The list first: rows that scroll out of the body are covered below.
    for (int i = std::max(0, int(scroll_ / kRowHeight));
         i < int(shown_.size()) && i * kRowHeight < scroll_ + t.bodyHeight; ++i) {
        const auto& row = rows[shown_[i]];
        const float y = t.bodyTop + i * kRowHeight - scroll_;
        const bool selected = i == selected_;
        const bool hover = rowAt(mouseX_, mouseY_) == i && !kindsOpen_ && !listPanning_;
        Color background = i % 2 ? kRowAlternate : kCard;
        if (selected) {
            background = kGold;
        } else if (hover) {
            background = style::mix(background, kGold, 40);
        }
        c.rect(t.frame.x, y, t.frame.w, kRowHeight, background);
        const Color ink = selected ? kWhite : row.supported ? kInk : kDisabledInk;
        const float ty = centred(c, y, kRowHeight, 2);
        c.text(row.number, t.columnX[0] + 14, ty, 2, ink);
        if (game().solved.count(row.id)) {
            c.textRight(style::kIconCheck, t.columnX[1] - 6, ty, 2, selected ? kWhite : kGold);
        }
        c.text(c.ellipsize(row.title, t.columnX[2] - t.columnX[1] - 16, 2), t.columnX[1] + 8, ty, 2,
               ink);
        c.text(c.ellipsize(row.kind, t.columnsEnd - t.columnX[2] - 16, 2), t.columnX[2] + 8, ty, 2,
               ink);
    }
    if (shown_.empty()) {
        c.rect(t.frame.x, t.bodyTop, t.frame.w, t.bodyHeight, kCard);
        c.textCentred("No puzzle matches.", t.frame.x + t.frame.w / 2, t.bodyTop + 40, 2, kMuted);
    } else if (shown_.size() * kRowHeight < t.bodyHeight) {
        const float end = t.bodyTop + shown_.size() * kRowHeight;
        c.rect(t.frame.x, end, t.frame.w, t.bodyTop + t.bodyHeight - end, kCard);
    }
    // Paper over rows that scrolled past the body's edges.
    c.rect(0, t.bodyTop - kRowHeight, width_, kRowHeight, kPaper);
    c.rect(0, t.bodyTop + t.bodyHeight, width_, kRowHeight, kPaper);

    // Header with sort arrows, and the frame around the list.
    c.rect(t.frame.x, t.frame.y, t.frame.w, kHeaderHeight, kLine);
    for (int i = 0; i < kColumns; ++i) {
        std::string title = kColumnTitles[i];
        if (i == sortColumn_) {
            title += " ";
            title += ascending_ ? style::kIconUp : style::kIconDown;
        }
        c.text(title, t.columnX[i] + (i == 0 ? 14 : 8), centred(c, t.frame.y, kHeaderHeight, 2), 2,
               kInk);
    }
    const Color edge = style::mix(kLine, kInk, 60);
    c.rect(t.frame.x, t.frame.y, t.frame.w, 1, edge);
    c.rect(t.frame.x, t.frame.y + t.frame.h, t.frame.w, 1, edge);
    c.rect(t.frame.x, t.frame.y, 1, t.frame.h, edge);
    c.rect(t.frame.x + t.frame.w - 1, t.frame.y, 1, t.frame.h, edge);
    c.rect(t.frame.x, t.bodyTop - 1, t.frame.w, 1, edge);

    // Scroll bar.
    if (maxScroll() > 0) {
        const float x = t.frame.x + t.frame.w - kScrollbarWidth;
        const float thumbH =
            std::max(30.0f, t.bodyHeight * t.bodyHeight / (shown_.size() * kRowHeight));
        const float thumbY = t.bodyTop + (t.bodyHeight - thumbH) * scroll_ / maxScroll();
        c.rect(x, t.bodyTop, kScrollbarWidth - 1, t.bodyHeight, kRowAlternate);
        c.rounded(x + 2, thumbY + 2, kScrollbarWidth - 5, thumbH - 4, 3,
                  draggingThumb_ ? kGold : style::mix(kLine, kInk, 70));
    }

    // The bar, as on the other pages: back to the hub, "Puzzles" and the game.
    backButton_ = {m, 24, 150, 36};
    button(c, backButton_, std::string(style::kIconLeft) + " Back", ButtonKind::Plain, true,
           backButton_.contains(mouseX_, mouseY_) && !kindsOpen_);
    c.textRight(cards_[cardOf(game_)].title, width_ - m, 24 + (36 - c.capHeight(1.5f)) / 2, 1.5f,
                kMuted);
    if (tall) {
        c.text("Puzzles", m, 80, 3, kInk);
    } else {
        c.textCentred("Puzzles", width_ / 2, 24 + (36 - c.capHeight(3)) / 2, 3, kInk);
        c.rect(m, 76, width_ - 2 * m, 1, kGold);
    }

    // The puzzle type filter, above the list.
    kindButton_ = tall ? Rect{m, 120, width_ - 2 * m, 36} : Rect{m, 96, 300, 36};

    // Puzzle type filter.
    const bool kindHover = kindButton_.contains(mouseX_, mouseY_);
    const float ky = centred(c, kindButton_.y, kindButton_.h, 2);
    c.frame(kindButton_.x, kindButton_.y, kindButton_.w, kindButton_.h, 6, kindHover ? 2 : 1,
            kindHover || kindsOpen_ ? kGold : style::mix(kLine, kInk, 80), kWhite);
    c.text(c.ellipsize(kinds_.empty() ? "" : kinds_[kind_], kindButton_.w - 50, 2),
           kindButton_.x + 12, ky, 2, kInk);
    c.text(style::kIconDown, kindButton_.x + kindButton_.w - 26, ky, 2, kMuted);

    // Footer: the count and play. A portrait window puts the count above
    // the button.
    const float buttonY = height_ - (tall ? 60 : 64), buttonW = tall ? 150 : 170;
    playButton_ = {width_ - m - buttonW, buttonY, buttonW, 36};
    const bool canPlay = launchSelection().request == Request::Play;
    button(c, playButton_, "Play puzzle", ButtonKind::Primary, canPlay,
           playButton_.contains(mouseX_, mouseY_) && !kindsOpen_);
    const std::string count = std::to_string(shown_.size()) + " shown, " +
                              std::to_string(game().solved.size()) +
                              " solved. Later puzzles may reveal parts of the story.";
    if (tall) {
        c.textCentred(c.ellipsize(count, width_ - 2 * m, 1.5f), width_ / 2, buttonY - 30, 1.5f,
                      kMuted);
    } else {
        c.text(c.ellipsize(count, playButton_.x - 24 - m, 1.5f), m + 1,
               centred(c, buttonY, 36, 1.5f), 1.5f, kMuted);
    }

    // The open type menu goes over everything else.
    kindItems_.clear();
    if (kindsOpen_) {
        // Long lists get two columns, so the menu stays inside the window.
        const int count = int(kinds_.size()), columns = count > 12 ? 2 : 1;
        const int perColumn = (count + columns - 1) / columns;
        const float itemH = 28, columnW = std::min(kindButton_.w, (width_ - 2 * m) / columns);
        const float w = columns * columnW, h = perColumn * itemH + 8;
        const float x = std::max(m, kindButton_.x + kindButton_.w - w);
        const float y = kindButton_.y + kindButton_.h + 4;
        c.rounded(x + 3, y + 5, w, h, 8, Color{40, 28, 16, 50});
        c.frame(x, y, w, h, 8, 1, style::mix(kLine, kInk, 80), kWhite);
        for (int i = 0; i < count; ++i) {
            const Rect item{x + 4 + (i / perColumn) * columnW, y + 4 + (i % perColumn) * itemH,
                            columnW - 8, itemH};
            kindItems_.push_back(item);
            const bool hot = item.contains(mouseX_, mouseY_);
            if (hot || i == kind_) {
                c.rounded(item.x, item.y, item.w, item.h, 5, hot ? kGold : kRowAlternate);
            }
            c.text(c.ellipsize(kinds_[i], item.w - 20, 2), item.x + 10,
                   centred(c, item.y, itemH, 2), 2, hot ? kWhite : kInk);
        }
    }
}

} // namespace picarat::shell
