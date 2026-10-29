// The third adventure's text boxes. See a3_text.hpp.
#include "graphics/a3_text.hpp"
#include "graphics/text.hpp"
#include <algorithm>

namespace picarat::adventure3 {
namespace {
// One laid-out character: position and colour.
struct Placed {
    unsigned code;
    int x, y;
    bool emphasis;
};

// Lays out a text box: lines split at '\n', <CR>..</C> switching to the
// emphasis colour (the markup takes no space), advances = glyph advance +
// character spacing, line height = font height + line spacing.
std::vector<Placed> layout(const TextBox& box) {
    std::vector<Placed> out;
    if (!box.font) {
        return out;
    }
    std::vector<std::vector<Placed>> lines(1);
    std::vector<int> widths(1, 0);
    bool emphasis = false;
    int x = 0;
    for (size_t i = 0; i < box.text.size(); ++i) {
        const char c = box.text[i];
        if (c == '<') {
            const size_t end = box.text.find('>', i);
            if (end != std::string::npos) {
                const std::string tag = box.text.substr(i + 1, end - i - 1);
                if (tag == "CR") {
                    emphasis = true;
                } else if (tag == "/C") {
                    emphasis = false;
                }
                i = end;
                continue;
            }
        }
        if (c == '\n') {
            lines.emplace_back();
            widths.push_back(0);
            x = 0;
            continue;
        }
        const unsigned code = std::uint8_t(c);
        lines.back().push_back({code, x, 0, emphasis});
        x += textAdvance(*box.font, code) + box.characterSpacing;
        widths.back() = x - box.characterSpacing;
    }
    const int lineHeight = box.fontHeight + box.lineSpacing;
    const int total = int(lines.size()) * lineHeight - box.lineSpacing;
    for (size_t l = 0; l < lines.size(); ++l) {
        const int dx = box.centerX ? -(widths[l] - widths[l] / 2) : 0;
        const int dy = (box.centerY ? -(total - total / 2) : 0) + int(l) * lineHeight;
        for (auto p : lines[l]) {
            p.x += dx;
            p.y = dy;
            out.push_back(p);
        }
    }
    return out;
}
} // namespace

int TextBox::visibleCount() const {
    return int(layout(*this).size());
}

bool TextBox::complete() const {
    return revealRate == 0 || shown >= visibleCount();
}

void drawTextBox(Surface& image, const TextBox& box, int x, int y, int alpha) {
    if (alpha <= 0) {
        return;
    }
    const auto placed = layout(box);
    const size_t count = box.revealRate == 0
                             ? placed.size()
                             : std::min(placed.size(), size_t(std::max(0, box.shown)));
    for (size_t i = 0; i < count; ++i) {
        const auto& p = placed[i];
        textGlyph(image, *box.font, p.code, x + p.x, y + p.y,
                  std::uint16_t((p.emphasis ? box.emphasis : box.color) | 0x8000));
    }
}

} // namespace picarat::adventure3
