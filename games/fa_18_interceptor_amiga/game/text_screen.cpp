#include "fa_18_interceptor_amiga/game/text_screen.hpp"
#include <algorithm>

namespace interceptor::game {

void TextRunner::queue(int block) {
    const auto found = std::find_if(assets_.texts.begin(), assets_.texts.end(),
                                    [&](const data::TextBlock& b) { return b.number == block; });
    if (found != assets_.texts.end()) queue(*found);
}

void TextRunner::queue(data::TextBlock block) {
    queued_.push_back(std::move(block));
    idle_frames_ = 0;
}

void TextRunner::clear() {
    blocks_.clear();
    queued_.clear();
    current_ = 0;
    idle_frames_ = 0;
    spacing_ = 11;
    input_ = Input::off;
    text_.clear();
}

void TextRunner::begin_input(int length) {
    input_ = Input::armed;
    input_length_ = length;
    text_.clear();
}

bool TextRunner::type(char c) {
    if (input_ != Input::typing || int(text_.size()) >= input_length_) return false;
    if (!assets_.text_font.glyph(c)) return false;
    text_.push_back(c);
    return true;
}

void TextRunner::backspace() {
    if (input_ == Input::typing && !text_.empty()) text_.pop_back();
}

void TextRunner::enter() {
    if (input_ != Input::typing) return;
    input_ = Input::ended;
    // The field's line is done: the block goes on with its next line.
    auto& r = blocks_[std::size_t(current_)];
    r.block.lines[0].text = text_; // (the field keeps the call sign)
    r.line = 1;
    r.shown = 0;
    r.wait = 1;
    r.pausing = false;
}

bool TextRunner::step(Running& r) {
    if (r.finished()) return false;
    if (r.wait > 0 && --r.wait > 0) return false;
    if (r.pausing) {
        r.pausing = false;
        ++r.line;
        r.shown = 0;
        if (r.finished()) return false;
    }
    const auto& line = r.block.lines[std::size_t(r.line)];
    const bool typed = line.flags & 1;
    bool clicked = false;
    do {
        while (r.shown < line.text.size() && (line.flags & 2) && line.text[r.shown] == ' ') ++r.shown;
        if (r.shown >= line.text.size()) {
            // The line's end: its pause (at least the frame), then the next.
            r.pausing = true;
            r.wait = std::max(1, line.delay * 4);
            return clicked;
        }
        ++r.shown;
        clicked = typed;
    } while (!typed);
    r.wait = std::max(1, line.speed);
    return clicked;
}

bool TextRunner::tick() {
    if (current_ >= int(blocks_.size())) {
        if (queued_.empty()) {
            ++idle_frames_;
            return false;
        }
        blocks_.push_back({std::move(queued_.front())});
        queued_.pop_front();
        if (input_ == Input::armed) input_ = Input::typing;
    }
    auto& r = blocks_[std::size_t(current_)];
    if (input_ == Input::typing && r.line == 0) return false; // the field waits for Return
    if (input_ == Input::ended) input_ = Input::off;
    const bool clicked = step(r);
    if (r.finished()) ++current_;
    return clicked;
}

void TextRunner::draw(IndexedImage& image, int left) const {
    const auto& font = assets_.text_font;
    const auto glyph_at = [&](char c, int x0, int y, int colour) {
        const auto* glyph = font.glyph(c);
        if (!glyph) return;
        for (int row = 0; row < font.rows && row < int(glyph->size()); ++row)
            for (int b = 0; b < 8; ++b)
                if ((*glyph)[std::size_t(row)] & (0x80 >> b)) image.put(x0 + b, y + row, colour);
    };
    for (std::size_t n = 0; n < blocks_.size(); ++n) {
        const auto& r = blocks_[n];
        const auto& block = r.block;
        int y = block.row;
        const int last = std::min(r.line, int(block.lines.size()) - 1);
        for (int l = 0; l <= last; ++l) {
            const auto& line = block.lines[std::size_t(l)];
            const bool field = input_ == Input::typing && l == 0 && int(n) == current_;
            const std::string& text = field ? text_ : line.text;
            const std::size_t count = field || l < r.line || r.finished() ? text.size() : r.shown;
            for (std::size_t i = 0; i < count && i < text.size() && i < assets_.text_columns.size(); ++i)
                glyph_at(text[i], left + block.column * 8 + assets_.text_columns[i], y, line.colour);
            y += spacing_;
        }
    }
}

std::optional<std::string> TextRunner::line_at(int x, int y, int left) const {
    for (const auto& r : blocks_) {
        const auto& block = r.block;
        const int last = std::min(r.line, int(block.lines.size()) - 1);
        for (int l = 0; l <= last; ++l) {
            const int top = block.row + l * spacing_;
            const auto& text = block.lines[std::size_t(l)].text;
            const int from = left + block.column * 8;
            const int to = from + (text.empty() || text.size() > assets_.text_columns.size()
                                       ? 320 : assets_.text_columns[text.size() - 1] + 8);
            if (y >= top - 2 && y < top + std::max(spacing_, 10) - 2 && x >= from - 8 && x < to + 8) return text;
        }
    }
    return std::nullopt;
}

} // namespace interceptor::game
