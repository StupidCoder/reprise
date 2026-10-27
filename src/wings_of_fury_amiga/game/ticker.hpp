#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The message ticker below the dashboard: one bitplane, 13 rows of $54
// bytes (672 pixels), into which the vertical-blank interrupt ($10184E)
// scrolls the message (-$5848 points at the next character) one pixel per
// blank, writing each character of `newarmyfont` just past the right edge
// (byte $50) once the last one has moved its width + 1 pixels in. After the
// last character the strip keeps scrolling for $2A0 blanks (-$5A36), so the
// text leaves on the left.
namespace wings::game {

// `newarmyfont` ($102794 reads it): a word height, the first and last
// character, a width byte per character (padded to a word), then each
// glyph's rows of whole words.
struct TickerFont {
    int height{};
    int first{}, last{};
    std::vector<int> widths;
    std::vector<std::vector<std::uint16_t>> glyphs; // by character - first: height x words
};
TickerFont read_ticker_font(const std::vector<std::uint8_t>& file);

class Ticker {
public:
    static constexpr int rows = 13;
    static constexpr int row_bytes = 0x54;
    static constexpr int width = row_bytes * 8;

    // A message to show (the game sets -$5848; the ticker reads it from the
    // start).
    void start(const std::string& text) {
        text_ = text;
        next_ = 0;
        running_ = true;
    }
    // Whether the message is still being read (-$5848 non-zero): a new one
    // waits until it has been.
    bool running() const { return running_; }
    // One vertical blank.
    void step(const TickerFont&);
    bool pixel(int x, int y) const {
        return (strip_[std::size_t(y * row_bytes + x / 8)] >> (7 - x % 8) & 1) != 0;
    }

private:
    std::array<std::uint8_t, rows * row_bytes> strip_{};
    std::string text_;
    std::size_t next_{};
    bool running_{};
    int scroll_{};  // -$5A36: blanks the strip still scrolls
    int wait_{};    // -$5A1E: blanks to the next character
};

} // namespace wings::game
