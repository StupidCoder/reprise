// The in-game menu. See game_menu.hpp.
#include "shell/game_menu.hpp"
#include "shell/game_screen.hpp"
#include "shell/widgets.hpp"
#include <algorithm>

namespace picarat::shell {

using platform::Event;
using platform::Key;
using platform::Rect;
using namespace ui;

void GameMenu::open(std::string title, bool canSkip, std::optional<bool> touchAreas) {
    open_ = true;
    title_ = std::move(title);
    touchAreas_ = touchAreas;
    items_.clear();
    items_.push_back(Item::Resume);
    if (canSkip) {
        items_.push_back(Item::Skip);
    }
    if (touchAreas) {
        items_.push_back(Item::TouchAreas);
    }
    items_.push_back(Item::Quit);
    rects_.assign(items_.size(), Rect{});
    selected_ = 0;
    mouseX_ = mouseY_ = -1;
}

GameMenu::Action GameMenu::handle(const Event& e) {
    if (!open_) {
        return Action::None;
    }
    const int count = int(items_.size());
    auto choose = [&](int index) {
        switch (items_[std::size_t(index)]) {
        case Item::Resume:
            open_ = false;
            return Action::Resume;
        case Item::Skip:
            open_ = false;
            return Action::Skip;
        case Item::TouchAreas:
            touchAreas_ = !touchAreas();
            return Action::TouchAreas;
        case Item::Quit:
            break;
        }
        open_ = false;
        return Action::Quit;
    };
    switch (e.type) {
    case Event::Type::MouseMove:
        mouseX_ = e.x;
        mouseY_ = e.y;
        for (int i = 0; i < count; ++i) {
            if (rects_[std::size_t(i)].contains(e.x, e.y)) {
                selected_ = i;
            }
        }
        break;
    case Event::Type::MouseDown:
        if (e.button != 1) {
            break;
        }
        for (int i = 0; i < count; ++i) {
            if (rects_[std::size_t(i)].contains(e.x, e.y)) {
                return choose(i);
            }
        }
        // A click (or tap) beside the panel goes back to the game.
        if (!panel_.contains(e.x, e.y)) {
            open_ = false;
            return Action::Resume;
        }
        break;
    case Event::Type::KeyDown:
        if (e.command) {
            break;
        }
        switch (e.key) {
        case Key::Escape:
            open_ = false;
            return Action::Resume;
        case Key::Up:
            selected_ = (selected_ + count - 1) % count;
            break;
        case Key::Down:
            selected_ = (selected_ + 1) % count;
            break;
        case Key::Return:
            return choose(selected_);
        default:
            break;
        }
        break;
    default:
        break;
    }
    return Action::None;
}

void GameMenu::draw(style::Canvas& c, float width, float height) {
    if (!open_) {
        return;
    }
    // A compact two-column sheet on landscape phones, a reading card elsewhere.
    c.rect(0, 0, width, height, Color{15, 31, 28, 160});
    const bool compact = width > height && height < 480;
    const float w = std::min(compact ? 620.0f : 420.0f, width - 24);
    const float padding = compact ? 16 : 24, itemH = 44, gap = 10;
    const int columns = compact ? 2 : 1;
    const int rows = (int(items_.size()) + columns - 1) / columns;
    const float legendH = touchAreas_ ? (compact ? 26 : 48) : 0;
    const float h = 90 + rows * (itemH + gap) + legendH + 34;
    const float x = std::floor((width - w) / 2), y = std::floor(std::max(8.0f, (height - h) / 2));
    panel_ = {x, y, w, h};
    c.rounded(x + 3, y + 5, w, h, 8, Color{15, 31, 28, 60});
    c.frame(x, y, w, h, 8, 1, kBrass, kCard);
    c.rounded(x + 1, y + 1, w - 2, 64, 7, kToolbar);
    c.rect(x + 1, y + 48, w - 2, 18, kToolbar);
    c.text("Paused", x + padding, y + 18, 3, kCard);
    c.textRight("PICARAT", x + w - padding, y + 22, 1.5f, kLine);
    c.text(c.ellipsize(title_, w - 2 * padding, 1.5f), x + padding, y + 46, 1.5f, kLine);
    const float itemW = (w - 2 * padding - gap * (columns - 1)) / columns;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const float ix = x + padding + (i % columns) * (itemW + gap);
        const float iy = y + 82 + (i / columns) * (itemH + gap);
        rects_[i] = {ix, iy, itemW, itemH};
        const char* label = items_[i] == Item::Resume ? "Continue playing"
                            : items_[i] == Item::Skip ? "Skip for now"
                            : items_[i] == Item::Quit ? "Return to collection"
                            : touchAreas()            ? "Touch areas: On"
                                                      : "Touch areas: Off";
        button(c, rects_[i], label, int(i) == selected_ ? ButtonKind::Primary : ButtonKind::Plain,
               true, rects_[i].contains(mouseX_, mouseY_), 2);
    }
    if (touchAreas_) {
        const TouchArea::Kind kinds[] = {TouchArea::Kind::Text, TouchArea::Kind::Dialog,
                                         TouchArea::Kind::Puzzle, TouchArea::Kind::HintCoin};
        const int perRow = compact ? 4 : 2;
        const float lw = (w - 2 * padding) / perRow;
        for (int i = 0; i < 4; ++i) {
            const float lx = x + padding + (i % perRow) * lw,
                        ly = y + 82 + rows * (itemH + gap) + (i / perRow) * 22;
            c.rounded(lx, ly, 9, 9, 2, GameScreen::touchAreaColour(kinds[i]));
            c.text(GameScreen::touchAreaName(kinds[i]), lx + 15, ly, 1.5f, kMuted);
        }
    }
    c.textCentred("Esc or Back to continue", x + w / 2, y + h - 22, 1.5f, kMuted);
}

} // namespace picarat::shell
