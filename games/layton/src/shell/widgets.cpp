// The shell's controls. See widgets.hpp.
#include "shell/widgets.hpp"
#include <algorithm>

namespace picarat::shell::ui {

using platform::Event;

void ornament(style::Canvas& c, float x, float y, float width) {
    c.rect(x, y, width, 1, kLine);
    c.rect(x, y, 32, 2, kBrass);
    c.rect(x + width - 32, y, 32, 2, kBrass);
}

void paper(style::Canvas& c, float width, float height) {
    c.fill(kPaper);
    c.rect(0, 0, 6, height, kToolbar);
    c.rect(6, 0, 2, height, kBrass);
    c.rect(width - 1, 0, 1, height, kLine);
}

void button(style::Canvas& c, const Rect& r, std::string_view label, ButtonKind kind, bool enabled,
            bool hover, float textSize) {
    Color face, edge, ink;
    switch (kind) {
    case ButtonKind::Primary:
        face = kGold;
        edge = style::mix(kGold, kInk, 90);
        ink = kWhite;
        break;
    case ButtonKind::Toolbar:
        face = style::mix(kToolbar, kWhite, 30);
        edge = style::mix(kToolbar, kWhite, 70);
        ink = kCard;
        break;
    case ButtonKind::Plain:
        face = kWhite;
        edge = kLine;
        ink = kInk;
        break;
    }
    if (!enabled) {
        face = kind == ButtonKind::Toolbar ? kToolbar : kDisabled;
        edge = kind == ButtonKind::Toolbar ? style::mix(kToolbar, kWhite, 40) : kDisabled;
        ink = kind == ButtonKind::Toolbar ? style::mix(kToolbar, kWhite, 90) : kDisabledInk;
    } else if (hover) {
        face = style::mix(face, kWhite, kind == ButtonKind::Plain ? 0 : 40);
        edge = kind == ButtonKind::Plain ? kGold : edge;
    }
    // A soft drop shadow, the outline, then the face.
    if (kind != ButtonKind::Toolbar) {
        c.rounded(r.x, r.y + 2, r.w, r.h, 4, Color{40, 28, 16, 18});
    }
    c.frame(r.x, r.y, r.w, r.h, 4, hover && enabled ? 2 : 1, edge, face);
    if (enabled && kind == ButtonKind::Primary) {
        c.rect(r.x + 10, r.y + r.h - 3, r.w - 20, 1, kBrass);
    }
    if (label.size() == 1 && static_cast<unsigned char>(label.front()) < 32) {
        const float side = std::min(24.f, std::min(r.w, r.h) - 12);
        c.icon(label, r.x + (r.w - side) / 2, r.y + (r.h - side) / 2, side, ink);
        return;
    }
    // A label too long for the button gets smaller print before it is cut.
    if (c.textWidth(label, textSize) > r.w - 12 && textSize > 1.5f) {
        textSize = std::max(1.5f, textSize * 0.75f);
    }
    const float y = r.y + (r.h - c.capHeight(textSize)) / 2;
    c.textCentred(c.ellipsize(label, r.w - 12, textSize), r.x + r.w / 2, y, textSize, ink);
}

void Dialog::open(int id, std::string title, std::string message,
                  std::vector<std::string> buttons) {
    open_ = true;
    id_ = id;
    title_ = std::move(title);
    message_ = std::move(message);
    buttons_ = std::move(buttons);
    buttonRects_.clear();
}

Dialog::Answer Dialog::handle(const Event& e) {
    Answer answer{id_, -1};
    if (!open_) {
        return answer;
    }
    if (e.type == Event::Type::MouseMove) {
        mouseX_ = e.x;
        mouseY_ = e.y;
    }
    if (e.type == Event::Type::MouseDown && e.button == 1) {
        for (std::size_t i = 0; i < buttonRects_.size(); ++i) {
            if (buttonRects_[i].contains(e.x, e.y)) {
                answer.button = int(i);
            }
        }
    }
    if (e.type == Event::Type::KeyDown && !e.command) {
        if (e.key == platform::Key::Return) {
            answer.button = 0;
        } else if (e.key == platform::Key::Escape) {
            answer.button = int(buttons_.size()) - 1;
        }
    }
    if (answer.button >= 0) {
        open_ = false;
    }
    return answer;
}

void Dialog::draw(style::Canvas& c, float width, float height) {
    if (!open_) {
        return;
    }
    // Dim everything behind the box.
    c.rect(0, 0, width, height, Color{20, 14, 8, 120});
    const float boxW = std::min(480.0f, width - 32), padding = 24;
    // Paragraphs are separated by "\n"; each is wrapped to the box.
    std::vector<std::string_view> lines;
    std::string_view rest = message_;
    while (true) {
        const auto end = rest.find('\n');
        const auto paragraph = rest.substr(0, end);
        const auto wrapped = c.lines(paragraph, boxW - 2 * padding, 2);
        if (wrapped.empty()) {
            lines.push_back({});
        }
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        if (end == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(end + 1);
    }
    const float lineHeight = c.lineHeight(2);
    const auto titleLines = c.lines(title_, boxW - 2 * padding, 3.0f);
    const float titleH = titleLines.size() * c.lineHeight(3.0f) + 6;
    float buttonsWidth = 0;
    for (const auto& label : buttons_) {
        buttonsWidth += std::max(110.0f, c.textWidth(label, 2) + 32) + 12;
    }
    const bool stacked = buttonsWidth - 12 > boxW - 2 * padding;
    const float controlsH = stacked ? buttons_.size() * 52.0f - 8 : 44;
    const float boxH = padding + titleH + lines.size() * lineHeight + 16 + controlsH + padding;
    const float x = (width - boxW) / 2, y = std::max(16.0f, (height - boxH) / 2);
    c.rounded(x + 4, y + 6, boxW, boxH, 10, Color{20, 14, 8, 70});
    c.frame(x, y, boxW, boxH, 10, 2, kGold, kCard);
    for (std::size_t i = 0; i < titleLines.size(); ++i) {
        c.text(titleLines[i], x + padding, y + padding + i * c.lineHeight(3.0f), 3.0f, kInk);
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        c.text(lines[i], x + padding, y + padding + titleH + i * lineHeight, 2, kMuted);
    }
    // Buttons right-aligned, the first (default) one rightmost.
    buttonRects_.assign(buttons_.size(), Rect{});
    float right = x + boxW - padding;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const float w = std::max(110.0f, c.textWidth(buttons_[i], 2) + 32);
        buttonRects_[i] = stacked ? Rect{x + padding, y + boxH - padding - controlsH + i * 52.0f,
                                         boxW - 2 * padding, 44}
                                  : Rect{right - w, y + boxH - padding - 44, w, 44};
        right -= w + 12;
        button(c, buttonRects_[i], buttons_[i], i == 0 ? ButtonKind::Primary : ButtonKind::Plain,
               true, buttonRects_[i].contains(mouseX_, mouseY_));
    }
}

} // namespace picarat::shell::ui
