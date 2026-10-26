#include "bruce_lee_c64/game/front.hpp"

namespace bruce_lee::game {
namespace {

constexpr std::uint8_t fill_code = 0xDC;       // the empty big-letter cell ($0613, $0EB6, $18FE)
constexpr std::uint8_t blue = 0x06, grey = 0x0F, white = 0x01, big_letters = 0x0F; // colours
constexpr int split_line = 80;                 // the title interrupt at raster $82 switches to the text font

// The upper rows use the $8000 charset, multicolour, 40 columns; below
// raster $82 the title interrupt ($158C) switches to the text font with
// $D016 = $16 (38 columns, fine scroll 6).
void set_front_lines(c64::TextFrame& frame, bool text_below) {
    c64::LineSetup upper;
    upper.background = blue;
    upper.border = blue;
    upper.shared1 = upper.shared2 = grey;
    upper.multicolour = true;
    upper.yscroll = 3;
    upper.sprites = 0x01;
    frame.set_lines(upper);
    if (text_below) {
        c64::LineSetup lower = upper;
        lower.narrow = true;
        lower.xscroll = 6;
        frame.set_lines(split_line, c64::Picture::height, lower);
    }
}

void put_big(c64::TextFrame& frame, const data::Assets& assets, int column, int row, std::uint8_t code) {
    frame.set(column, row, &assets.title_charset[code], big_letters);
}

void put_player_label(c64::TextFrame& frame, const data::Assets& assets, int column, int row, int player) {
    for (int i = 0; i < 16; ++i) {
        std::uint8_t upper = assets.player_label[0][std::size_t(i)];
        std::uint8_t lower = assets.player_label[1][std::size_t(i)];
        if (i == 14 || i == 15) { // $0ED0: the player's digit
            upper = assets.player_digit[std::size_t(player)][std::size_t(i - 14)];
            lower = assets.player_digit[std::size_t(player)][std::size_t(i - 12)];
        }
        put_big(frame, assets, column + i, row, std::uint8_t(upper | 0x80));
        put_big(frame, assets, column + i, row + 1, std::uint8_t(lower | 0x80));
    }
}

} // namespace

void TitleText::print(const data::Assets& assets, int text, int row_index, int column) {
    const auto& chars = assets.strings[std::size_t(text)];
    const int top = row_index == 0 ? 0 : 2 * row_index - 1;
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const int at = top * Playfield::columns + column + int(i);
        if (at >= Playfield::columns * Playfield::rows) {
            break;
        }
        rows.at(at % Playfield::columns, at / Playfield::columns).code = std::uint8_t(chars[i] & 0x7F);
    }
}

void TitleText::build(const data::Assets& assets, bool alternate, bool yamo_player) {
    rows = Playfield{};
    print(assets, 0x4F, 6, 0x31);
    print(assets, 0x50, 7, 0x2B);
    print(assets, 0x51, 8, 0x31);
    auto invert = [&](int column, int row) { rows.at(column, row).code = std::uint8_t(rows.at(column, row).code + 0x40); };
    for (int x = 9; x >= 0; --x) {
        invert(16 + x, 12);
        invert(16 + x, 14);
        if (x < 2) {
            invert(11 + x, 12);
            invert(11 + x, 14);
            invert(11 + x, 16);
        }
    }
    if (alternate) {
        toggle_alternate(true);
    }
    if (yamo_player) {
        toggle_yamo_player(true);
    }
}

// $0E59 / $0E40 / $0E6F: the choice's line inverted (but its dashes, code
// $4D), the brackets moved to the chosen word.
static void toggle_line(Playfield& rows, int row, bool now_on) {
    for (int x = 19; x >= 0; --x) {
        auto& code = rows.at(16 + x, row).code;
        if (code != 0x4D) {
            code ^= 0x40;
        }
    }
    const std::uint8_t a = now_on ? 0x00 : 0x42, b = now_on ? 0x42 : 0x40, c = now_on ? 0x40 : 0x56,
                       d = now_on ? 0x56 : 0x00;
    rows.at(15, row).code = a;
    rows.at(24, row).code = b;
    rows.at(26, row).code = c;
    rows.at(36, row).code = d;
}

void TitleText::toggle_alternate(bool now_on) {
    toggle_line(rows, 12, now_on);
}

void TitleText::toggle_yamo_player(bool now_on) {
    toggle_line(rows, 14, now_on);
}

// $0584-$069F
c64::TextFrame title_frame(const data::Assets& assets, const TitleText& text) {
    c64::TextFrame frame;
    for (int row = 0; row < 10; ++row) {
        for (int column = 0; column < 40; ++column) {
            put_big(frame, assets, column, row, fill_code);
        }
    }
    constexpr int title_screen_rows[4] = {2, 5, 6, 9};
    for (int r = 0; r < 4; ++r) {
        for (int column = 0; column < 40; ++column) {
            put_big(frame, assets, column, title_screen_rows[r], std::uint8_t(assets.title_rows[std::size_t(r)][std::size_t(column)] | 0x80));
        }
    }
    for (int row = 10; row < Playfield::rows; ++row) {
        for (int column = 0; column < 40; ++column) {
            const std::uint8_t code = text.rows.at(column, row).code;
            frame.set(column, row, &assets.text[code & 0x7F], row < 12 ? big_letters : white);
        }
    }
    set_front_lines(frame, true);
    // $05A5: sprite 0, x-expanded, at x $10A, y $56: the trademark sign.
    c64::Sprite mark;
    mark.shown = true;
    mark.expand_x = true;
    mark.colour = grey;
    mark.x = 0x10A;
    mark.y = 0x56;
    for (std::size_t i = 0; i < assets.trademark.size(); ++i) {
        mark.data[12 + i] = assets.trademark[i];
    }
    frame.set_sprite(0, mark);
    return frame;
}

// $0E9B: "PLAYER n" on a screen of empty big-letter cells; the interrupt
// at raster $63 ($14BC) changes $D022 line by line from raster $6E,
// shading the letters.
c64::TextFrame player_frame(const data::Assets& assets, int player) {
    c64::TextFrame frame;
    for (int row = 0; row < Playfield::rows; ++row) {
        for (int column = 0; column < 40; ++column) {
            put_big(frame, assets, column, row, fill_code);
        }
    }
    put_player_label(frame, assets, 12, 7, player);
    set_front_lines(frame, false);
    c64::LineSetup line = frame.line(0);
    line.sprites = 0;
    frame.set_lines(line);
    struct Band {
        int raster;
        std::uint8_t colour;
    };
    // Each delay call takes one raster line and each write a few cycles
    // more, so the later writes slip a line: the lines as the original
    // shows them (compared on its pictures); $FB before and after.
    constexpr Band bands[] = {{0x70, 0x0C}, {0x72, 0x0F}, {0x75, 0x01},
                              {0x76, 0x0F}, {0x78, 0x0C}, {0x7A, 0x0B}};
    line.shared1 = 0x0B;
    frame.set_lines(line);
    for (const auto& band : bands) {
        line.shared1 = band.colour;
        frame.set_lines(band.raster - 51, c64::Picture::height, line);
    }
    return frame;
}

// $18A6: "GAME OVER", the player and the score in big letters.
c64::TextFrame game_over_frame(const data::Assets& assets, int player, std::uint32_t score) {
    c64::TextFrame frame;
    for (int row = 0; row < Playfield::rows; ++row) {
        for (int column = 0; column < 40; ++column) {
            if (row < 10) {
                put_big(frame, assets, column, row, fill_code);
            } else {
                frame.set(column, row, &assets.text[0], row < 12 ? big_letters : white);
            }
        }
    }
    for (int i = 0; i < 18; ++i) {
        put_big(frame, assets, 11 + i, 1, std::uint8_t(assets.game_over_label[0][std::size_t(i)] | 0x80));
        put_big(frame, assets, 11 + i, 2, std::uint8_t(assets.game_over_label[1][std::size_t(i)] | 0x80));
    }
    put_player_label(frame, assets, 12, 5, player);
    // $1948: six decimal digits, each two big cells wide ($199F: glyphs
    // $34 + 4 * digit: upper left, lower left, upper right, lower right).
    std::uint32_t value = score;
    for (int i = 5; i >= 0; --i) {
        const auto digit = std::uint8_t(value % 10);
        value /= 10;
        const auto first = std::uint8_t((0x34 + 4 * digit) | 0x80);
        put_big(frame, assets, 14 + 2 * i, 8, first);
        put_big(frame, assets, 14 + 2 * i, 9, std::uint8_t(first + 1));
        put_big(frame, assets, 15 + 2 * i, 8, std::uint8_t(first + 2));
        put_big(frame, assets, 15 + 2 * i, 9, std::uint8_t(first + 3));
    }
    set_front_lines(frame, true);
    c64::LineSetup line = frame.line(0);
    line.sprites = 0;
    frame.set_lines(0, split_line, line);
    line = frame.line(split_line);
    line.sprites = 0;
    frame.set_lines(split_line, c64::Picture::height, line);
    return frame;
}

} // namespace bruce_lee::game
