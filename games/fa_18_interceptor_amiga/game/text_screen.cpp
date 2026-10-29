#include "fa_18_interceptor_amiga/game/text_screen.hpp"
#include <algorithm>

namespace interceptor::game {
namespace {

constexpr int line_spacing = 11; // $13AA2E = $1B8 bytes

} // namespace

TextScreen::TextScreen(const data::Assets& assets, int block) : assets_(assets) {
    const auto found = std::find_if(assets.texts.begin(), assets.texts.end(),
                                    [&](const data::TextBlock& b) { return b.number == block; });
    if (found != assets.texts.end()) block_ = &*found;
}

bool TextScreen::next_character() {
    const auto& line = block_->lines[std::size_t(line_)];
    while (shown_ < line.text.size() && (line.flags & 2) && line.text[shown_] == ' ') ++shown_;
    return shown_ < line.text.size();
}

bool TextScreen::tick() {
    if (finished()) return false;
    if (wait_ > 0 && --wait_ > 0) return false;
    if (pausing_) {
        pausing_ = false;
        ++line_;
        shown_ = 0;
        if (finished()) return false;
    }
    const auto& line = block_->lines[std::size_t(line_)];
    if (!next_character()) {
        // The line's end: its pause (at least the frame), then the next line.
        pausing_ = true;
        wait_ = std::max(1, line.delay * 4);
        return false;
    }
    ++shown_;
    wait_ = std::max(1, line.speed);
    return true;
}

void TextScreen::draw(IndexedImage& image, int left) const {
    if (!block_) return;
    const auto& font = assets_.text_font;
    int y = block_->row;
    for (int l = 0; l <= std::min(line_, lines() - 1); ++l) {
        const auto& line = block_->lines[std::size_t(l)];
        const std::size_t count = l < line_ ? line.text.size() : shown_;
        for (std::size_t i = 0; i < count && i < assets_.text_columns.size(); ++i) {
            const auto* glyph = font.glyph(line.text[i]);
            if (!glyph) continue;
            const int x0 = left + block_->column * 8 + assets_.text_columns[i];
            for (int r = 0; r < font.rows && r < int(glyph->size()); ++r) {
                for (int b = 0; b < 8; ++b) {
                    if ((*glyph)[std::size_t(r)] & (0x80 >> b)) image.put(x0 + b, y + r, line.colour);
                }
            }
        }
        y += line_spacing;
    }
}

} // namespace interceptor::game
