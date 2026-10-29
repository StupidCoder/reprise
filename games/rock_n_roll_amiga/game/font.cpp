#include "rock_n_roll_amiga/game/font.hpp"

namespace rocknroll::game {
namespace {

constexpr int font_row_low = 600, font_row_high = 608; // $364C0: '$27'-'$4E', $36B00: '$4F' on
constexpr int high_first = 0x4F - 0x27;

} // namespace

void draw_glyph(Canvas canvas, const data::GraphicsBank& font, int x, int y, int index, int lines, int plane) {
    const bool high = index >= high_first;
    const int column = high ? index - high_first : index;
    const int row = high ? font_row_high : font_row_low;
    const auto bit_value = std::uint8_t(1 << plane);
    for (int line = 0; line < lines; ++line) {
        for (int bit = 0; bit < 8; ++bit) {
            const int px = x + bit, py = y + line;
            if (px < 0 || px >= canvas.width || py < 0 || py >= canvas.height) continue;
            const bool on = font.at(column * 8 + bit, row + line) & 1; // the font's plane 0
            auto& p = canvas.pixels[std::size_t(py) * std::size_t(canvas.width) + std::size_t(px)];
            p = std::uint8_t((p & ~bit_value) | (on ? bit_value : 0));
        }
    }
}

void print(Canvas canvas, const data::GraphicsBank& font, int x, int y, std::string_view characters, int plane) {
    for (const char ch : characters) {
        int c = std::uint8_t(ch);
        if (c == 0xFF) break;
        if (c == 0x20) c = 0x28; // a space is drawn as glyph $28
        draw_glyph(canvas, font, x, y, c - 0x27, 8, plane);
        x += 8;
    }
}

void print_at(Canvas canvas, const data::GraphicsBank& font, int origin_x, int origin_y, const ScreenText& t) {
    if (t.text.size() < 2) return;
    const int column = std::uint8_t(t.text[0]), row = std::uint8_t(t.text[1]);
    print(canvas, font, origin_x + column * 8, origin_y + row * 8, std::string_view(t.text).substr(2), t.plane);
}

ScreenText text_at(int column, int row, std::string_view characters, int plane) {
    ScreenText t;
    t.text.push_back(char(column));
    t.text.push_back(char(row));
    t.text += characters;
    t.plane = plane;
    return t;
}

ScreenText read_text(const data::Program& program, std::uint32_t address, int plane) {
    ScreenText t;
    t.text.push_back(char(program.u8(address)));
    t.text.push_back(char(program.u8(address + 1)));
    for (std::uint32_t a = address + 2; program.contains(a) && program.u8(a) != 0xFF; ++a) t.text.push_back(char(program.u8(a)));
    t.plane = plane;
    return t;
}

} // namespace rocknroll::game
