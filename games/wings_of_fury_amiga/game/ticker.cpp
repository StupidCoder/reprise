#include "wings_of_fury_amiga/game/ticker.hpp"
#include <stdexcept>

namespace wings::game {

TickerFont read_ticker_font(const std::vector<std::uint8_t>& file) {
    if (file.size() < 4) throw std::runtime_error("newarmyfont: too short");
    TickerFont f;
    f.height = file[0] << 8 | file[1];
    f.first = file[2];
    f.last = file[3];
    const int count = f.last - f.first + 1;
    std::size_t at = 4 + std::size_t((count + 1) & ~1);
    for (int i = 0; i < count; ++i) {
        const int w = file.at(4 + std::size_t(i));
        f.widths.push_back(w);
        const std::size_t words = std::size_t((w + 15) >> 4) * std::size_t(f.height);
        std::vector<std::uint16_t> glyph;
        for (std::size_t k = 0; k < words; ++k, at += 2) {
            if (at + 1 >= file.size()) throw std::runtime_error("newarmyfont: glyphs cut short");
            glyph.push_back(std::uint16_t(file[at] << 8 | file[at + 1]));
        }
        f.glyphs.push_back(std::move(glyph));
    }
    return f;
}

void Ticker::step(const TickerFont& font) {
    // $10184E: the strip moves a pixel left (each row's bits roll in from
    // the right, the row's last byte taking in zeros).
    if (scroll_ != 0) {
        --scroll_;
        for (int y = 0; y < rows; ++y) {
            std::uint8_t* row = strip_.data() + y * row_bytes;
            for (int i = 0; i < row_bytes; ++i) {
                const int carry = i + 1 < row_bytes ? row[i + 1] >> 7 : 0;
                row[i] = std::uint8_t(row[i] << 1 | carry);
            }
        }
    }
    // $1018C4: the next character once the last has come in.
    if (wait_ != 0 && --wait_ != 0) return;
    if (!running_) return;
    const int c = next_ < text_.size() ? std::uint8_t(text_[next_]) : 0;
    ++next_;
    if (c == 0) {
        running_ = false;
        wait_ = 0;
        return;
    }
    scroll_ = 0x2A0;
    const int index = c - font.first;
    if (index < 0 || index >= int(font.widths.size())) {
        wait_ = 0xA; // (outside the font: its width byte would be read from elsewhere)
        return;
    }
    const int w = font.widths[std::size_t(index)];
    if (w == 0) {
        wait_ = 0xA;
        return;
    }
    wait_ = w + 1;
    // The glyph's words go to byte $50 of each row.
    const int words = (w + 15) >> 4;
    const auto& glyph = font.glyphs[std::size_t(index)];
    for (int y = 0; y < font.height && y < rows; ++y) {
        for (int k = 0; k < words; ++k) {
            const std::uint16_t v = glyph[std::size_t(y * words + k)];
            const std::size_t at = std::size_t(y * row_bytes + 0x50 + k * 2);
            if (at + 1 >= strip_.size()) break;
            strip_[at] = std::uint8_t(v >> 8);
            strip_[at + 1] = std::uint8_t(v);
        }
    }
}

} // namespace wings::game
