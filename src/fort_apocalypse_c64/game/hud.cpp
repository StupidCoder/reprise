#include "fort_apocalypse_c64/game/hud.hpp"

namespace fort_apocalypse::game {
namespace {
const c64::Glyph empty_glyph{};

// One counter ($A9D3): `digits` decimal digits from `column` on row `row`,
// two cells per digit (code $10 + d, then d; a 0 in the second cell is
// written as $0A because code 0 is the empty character). Leading zeros are
// blank (codes $80 and $00), except the last digit.
void draw_counter(c64::TextFrame& frame, const data::Assets& assets, int value, int digits,
                  int column, int row) {
    bool leading = true;
    for (int i = digits - 1; i >= 0; --i) {
        int divisor = 1;
        for (int k = 0; k < i; ++k) {
            divisor *= 10;
        }
        const int d = value / divisor % 10;
        std::uint8_t left, right;
        if (leading && d == 0 && i > 0) {
            left = 0x80;
            right = 0x00;
        } else {
            leading = false;
            left = std::uint8_t(0x10 + d);
            right = std::uint8_t(d == 0 ? 0x0A : d);
        }
        const int at = column + 2 * (digits - 1 - i);
        frame.set_glyph(at, row, hud_glyph(assets, left));
        frame.set_glyph(at + 1, row, hud_glyph(assets, right));
    }
}
} // namespace

const c64::Glyph* hud_glyph(const data::Assets& assets, std::uint8_t code) {
    return code < assets.hud_charset.size() ? &assets.hud_charset[code] : &empty_glyph;
}

int draw_text(c64::TextFrame& frame, const data::Assets& assets, const data::Text& text,
              int column, int row) {
    for (const auto code : text) {
        frame.set_glyph(column++, row, hud_glyph(assets, code));
        if (code != 0) {
            frame.set_glyph(column++, row, hud_glyph(assets, std::uint8_t(code + 0x20)));
        }
    }
    return column;
}

void draw_hud(c64::TextFrame& frame, const data::Assets& assets, const Counters& counters,
              bool with_counters) {
    for (int row = 0; row < data::HudScreen::rows; ++row) {
        for (int column = 0; column < data::HudScreen::columns; ++column) {
            frame.set_glyph(column, row,
                            hud_glyph(assets, assets.hud.cells[std::size_t(row)][std::size_t(column)]));
        }
    }
    if (with_counters) {
        draw_counter(frame, assets, counters.score, 6, 19, 0); // $4413
        draw_counter(frame, assets, counters.bonus, 4, 29, 3); // $4495
        draw_counter(frame, assets, counters.fuel, 4, 2, 3);   // $447A
    }
}

} // namespace fort_apocalypse::game
