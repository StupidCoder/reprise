#include "tower_of_babel_amiga/view/overlay.hpp"

namespace babel::view {
namespace {

constexpr std::uint32_t glyphs = 0x238C8;  // main data
constexpr std::uint32_t slash = 0x23A58;   // the "/" glyph
constexpr std::uint32_t sights = 0xE184;   // program: per spider 5 rows of 2 words, then the stem

// One 16-pixel word of glyph rows: the left half from glyph `a`, the right
// from glyph `b` (each word's other half kept), as $15238 / $15284 / $152AA.
void glyph_word(Canvas& canvas, int x0, int y0, const data::Memory& main, std::uint32_t a, std::uint32_t b, bool right) {
    for (int r = 0; r < 10; ++r) {
        const unsigned am = main.u16(a + std::uint32_t(r) * 4), ad = main.u16(a + std::uint32_t(r) * 4 + 2);
        unsigned keep, set;
        if (right) {
            const unsigned bm = main.u16(b + std::uint32_t(r) * 4), bd = main.u16(b + std::uint32_t(r) * 4 + 2);
            keep = (0xFF00u | bm) & (0x00FFu | am);
            set = (0x00FFu & bd) | (0xFF00u & ad);
        } else {
            keep = 0x00FFu | am;
            set = 0xFF00u & ad;
        }
        const int y = y0 + r;
        if (y < 0 || y >= canvas.height) continue;
        for (int bit = 0; bit < 16; ++bit) {
            const int x = x0 + bit;
            if (x < 0 || x >= canvas.width) continue;
            const unsigned m = 0x8000u >> bit;
            int c = (keep & m) ? canvas.at(x, y) : 0;
            if (set & m) c = 15;
            canvas.set(x, y, std::uint8_t(c));
        }
    }
}

std::uint32_t glyph(int i) { return glyphs + std::uint32_t(i) * 0x28; }

} // namespace

void draw_overlay(Canvas& canvas, const OverlayState& o, const data::Memory& program, const data::Memory& main, int left) {
    // $151CC: READY, blinking.
    if (o.ready && o.blink < 5) {
        const int y = o.info == Corner::bottom ? 118 : 18;
        for (int i = 0; i < 4; ++i)
            glyph_word(canvas, left + 128 + 16 * i, y, main, glyph(0xF + i), glyph(0xF + i), true);
    }
    // $150BC: compass, then "n/" "m" for Klondikes and for objects; a
    // counter that has reached its target blinks.
    if (o.info != Corner::off) {
        const int y = o.info == Corner::bottom ? 118 : 18;
        int x = left + 224;
        glyph_word(canvas, x, y, main, glyph(o.heading + 11), 0, false);
        x += 16;
        if (!(o.klondikes == o.klondikes_needed && o.blink >= 5)) {
            glyph_word(canvas, x, y, main, glyph(o.klondikes), slash, true);
            glyph_word(canvas, x + 16, y, main, glyph(o.klondikes_needed), 0, false);
        }
        x += 32;
        if (!(o.destroyed == o.destroy_needed && o.blink >= 5)) {
            glyph_word(canvas, x, y, main, glyph(o.destroyed), slash, true);
            glyph_word(canvas, x + 16, y, main, glyph(o.destroy_needed), 0, false);
        }
    }
    // $15152: the timer, 4 BCD digits, blinking under 30 seconds.
    if (o.time_limit && o.timer != Corner::off && (o.time_left >= 0x30 || o.blink < 5)) {
        const int y = o.timer == Corner::top ? 18 : 118;
        const int hi = o.time_left >> 8 & 0xFF, lo = o.time_left & 0xFF;
        glyph_word(canvas, left + 32, y, main, glyph(hi >> 4 & 15), glyph(hi & 15), true);
        glyph_word(canvas, left + 48, y, main, glyph(lo >> 4 & 15), glyph(lo & 15), true);
    }
    // $E0F6: the sights, ORed in colour 15 at the view's centre.
    if (o.sights && o.spider >= 0) {
        const auto orw = [&](int x0, int y, unsigned w) {
            for (int bit = 0; bit < 16; ++bit)
                if (w & (0x8000u >> bit)) canvas.set(x0 + bit, y, 15);
        };
        int y = 59;
        std::uint32_t at = sights + std::uint32_t(o.spider) * 20;
        for (int r = 0; r < 5; ++r, ++y, at += 4) {
            orw(left + 144, y, program.u16(at));
            orw(left + 160, y, program.u16(at + 2));
        }
        at = sights + 60;
        for (int g = 0; g < 3; ++g, at += 6) {
            const int rows = program.u16(at) + 1;
            for (int r = 0; r < rows; ++r, ++y) {
                orw(left + 144, y, program.u16(at + 2));
                orw(left + 160, y, program.u16(at + 4));
            }
        }
    }
}

} // namespace babel::view
