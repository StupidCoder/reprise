#include "tower_of_babel_amiga/view/sky.hpp"
#include <algorithm>

namespace babel::view {
namespace {

// Main data addresses ($10200 + the offsets in the code).
constexpr std::uint32_t stars_table = 0x2D3C0;   // $D34E: per heading 64 x (x, y, pen)
constexpr std::uint32_t moon_images = 0x2D9C0;   // $CFBC: per type $1000, 32 frames of $80
constexpr std::uint32_t bolt_images = 0x339C0;   // $D064: 4 bolts of $230
constexpr std::uint32_t meteor_images = 0x34280; // $D1A4: 8 frames of $140
// Program tables.
constexpr std::uint32_t moon_mask = 0xCF36;      // 16 row masks (1 = keep the sky)
constexpr std::uint32_t band_lines = 0x8ACA;     // per sky type an offset to a 0-ended line list
constexpr std::uint32_t panel_palette = 0x2BF8;  // $23F2(a6): 15 colours from line $82
// Lightning's flash: which of the 18 colours turn white ($136CE).
constexpr std::uint32_t flash_mask = 0x1D448;

std::uint16_t next(std::uint16_t r, int add = 0x29) { return std::uint16_t(r * 0x5E5 + add); }

void put_word(Canvas& canvas, int x0, int y, unsigned keep, std::array<unsigned, 4> planes) {
    if (y < 0 || y >= canvas.height) return;
    for (int b = 0; b < 16; ++b) {
        const int x = x0 + b;
        if (x < 0 || x >= canvas.width) continue;
        const unsigned bit = 0x8000u >> b;
        int c = (keep & bit) ? canvas.at(x, y) : 0;
        for (int p = 0; p < 4; ++p)
            if (planes[std::size_t(p)] & bit) c |= 1 << p;
        canvas.set(x, y, std::uint8_t(c));
    }
}

} // namespace

SkySettings sky_settings(const data::Tower& t) {
    return {t.settings[1], t.settings[3], t.settings[4], t.settings[5], t.settings[6], t.settings[7], t.settings[8],
            !t.flags[6], !t.flags[7]};
}

void SkyEffects::advance(SkyState& s, const SkySettings& sky, int heading, bool even_frame) {
    s.bolt_column.reset();
    s.bolt_image.reset();
    s.flash = s.thunder = s.crack = false;
    s.meteor_visible = false;

    // $D120: meteors.
    if (sky.meteors) {
        if (s.meteor_wait != 0) {
            if (--s.meteor_wait == 0) { // a new meteor
                s.meteor_heading = heading;
                std::uint16_t r = next(next(s.random));
                int x = r & 0x1FF;
                if (x != 0x190) x >>= 1;
                s.meteor_x = x;
                s.meteor_y = -15;
                s.random = r;
            }
        } else {
            s.meteor_x -= 4;
            s.meteor_y += 2;
            if (s.meteor_y >= 0x38) { // gone: wait $F-$4E frames
                s.random = next(s.random);
                s.meteor_wait = (s.random & 0x3F) + 0x0F;
            } else if (s.meteor_heading == heading) {
                ++s.meteor_frame;
                s.meteor_visible = true;
            }
        }
    }

    // $CFFC: lightning, on even frames only.
    if (sky.lightning && even_frame) {
        if (--s.lightning_wait != 0) {
            if (--s.thunder_wait == 0) s.thunder = true;
            return;
        }
        std::uint16_t r = s.random;
        if (s.lightning_heading == heading) {
            r = next(r, 0x2A);
            s.bolt_column = r & 15;
            r = next(r);
            s.bolt_image = r & 3;
        }
        r = next(r);
        if (r & 0x800) { // a long wait, and the next flash shows where the camera looks now
            r = next(r);
            s.lightning_wait = ((r & 0x1F) + 0x20) & ~1;
            s.lightning_heading = heading;
        } else {
            r = next(r);
            s.lightning_wait = (r & 7) + 6;
        }
        s.random = r;
        s.thunder_wait = (r & 3) + 4;
        s.crack = true;
        s.flash = true;
    }
}

void draw_sky(Canvas& canvas, const SkySettings& sky, SkyState& s, int heading, const data::Memory& program,
              const data::Memory& main, const std::array<Pen, 80>& pens, int left) {
    const int dx = left - 16; // shift for wider views
    // $D33A: stars, 21 per step of density, plus one.
    if (sky.stars != 0) {
        const std::uint32_t table = stars_table + std::uint32_t(heading & 3) * 0x180;
        for (int i = 0; i <= sky.stars * 21; ++i) {
            const std::uint32_t e = table + std::uint32_t(i) * 6;
            const int x = std::int16_t(main.u16(e)), y = std::int16_t(main.u16(e + 2)), pen = std::int16_t(main.u16(e + 4));
            // $14F88: the pen's first row decides the pixel.
            if (y >= 0 && y < canvas.height && x + dx >= 0 && x + dx < canvas.width)
                canvas.set(x + dx, y, pens[std::size_t(std::clamp(pen, 0, 79))].colour(x, 0));
        }
    }
    // $CF56: the moon, when the camera faces it.
    if (heading == sky.moon_heading && sky.moon_y <= 0x37) {
        const int rows = std::min(0x37 - sky.moon_y, 15) + 1;
        s.moon_frame += sky.moon_rotation - 1;
        const std::uint32_t image = moon_images + std::uint32_t(sky.moon_type) * 0x1000 +
                                    std::uint32_t(s.moon_frame & 0x1F) * 0x80;
        for (int r = 0; r < rows; ++r) {
            const std::uint32_t row = image + std::uint32_t(r) * 8;
            put_word(canvas, 16 + sky.moon_x * 16 + dx, 16 + sky.moon_y + r, program.u16(moon_mask + std::uint32_t(r) * 2),
                     {main.u16(row), main.u16(row + 2), main.u16(row + 4), main.u16(row + 6)});
        }
    }
    // $D1DE: the meteor, a 32-pixel masked image moving down the top 56 lines.
    if (s.meteor_visible) {
        const std::uint32_t image = meteor_images + std::uint32_t(s.meteor_frame & 7) * 0x140;
        for (int r = 0; r < 16; ++r) {
            const int y = s.meteor_y + r;
            if (y < 0 || y >= 0x38) continue;
            const std::uint32_t row = image + std::uint32_t(r) * 20;
            const std::uint32_t mask = main.u32(row);
            std::array<std::uint32_t, 4> planes{main.u32(row + 4), main.u32(row + 8), main.u32(row + 12), main.u32(row + 16)};
            for (int b = 0; b < 32; ++b) {
                const std::uint32_t bit = 0x80000000u >> b;
                if (!(mask & bit)) continue;
                const int x = s.meteor_x + 1 + b;
                // Only the 18 word columns of the view take pixels.
                if (x < 0 || (x >> 4) >= 0x12) continue;
                int c = 0;
                for (int p = 0; p < 4; ++p)
                    if (planes[std::size_t(p)] & bit) c |= 1 << p;
                // The original's bug ($D2B4): in the first, partial word (the
                // image's first 15 - (x & 15) pixels) plane 3 gets plane 0's bits.
                if (b < 15 - (s.meteor_x & 15)) c = (c & 7) | ((c & 1) << 3);
                const int sx = 16 + x + dx, sy = 16 + y;
                if (sx >= 0 && sx < canvas.width) canvas.set(sx, sy, std::uint8_t(c));
            }
        }
    }
    // $D028: the bolt, 16 pixels wide from the top of the view, 56 lines.
    if (s.bolt_column && s.bolt_image) {
        const std::uint32_t image = bolt_images + std::uint32_t(*s.bolt_image) * 0x230;
        for (int r = 0; r < 0x38; ++r) {
            const std::uint32_t row = image + std::uint32_t(r) * 10;
            put_word(canvas, 32 + *s.bolt_column * 16 + dx, 16 + r, main.u16(row),
                     {main.u16(row + 2), main.u16(row + 4), main.u16(row + 6), main.u16(row + 8)});
        }
    }
}

FramePalette frame_palette(const data::Tower& tower, const data::Memory& program, bool flash) {
    // The 48 colours the copper reads: the tower's 18, the sky gradient
    // (15), the panel's 15.
    std::array<data::Colour, 48> all{};
    for (std::size_t i = 0; i < 18; ++i) all[i] = tower.colours[i];
    const int sky_type = tower.settings[3];
    const auto rgb = [](data::Colour c) { return std::array<int, 3>{c >> 8 & 15, c >> 4 & 15, c & 15}; };
    const auto top = rgb(tower.colours[7]), bottom = rgb(tower.colours[8]);
    if (sky_type == 2) { // stripes: bottom, top, bottom, ...
        for (int i = 0; i < 15; ++i) all[std::size_t(18 + i)] = (i & 1) ? tower.colours[7] : tower.colours[8];
    } else { // $12392: 15 steps, 8.8 fixed point from the top colour towards the bottom
        std::array<int, 3> value{}, step{};
        for (int k = 0; k < 3; ++k) {
            value[std::size_t(k)] = (top[std::size_t(k)] << 8) | 0x80;
            step[std::size_t(k)] = int(std::int16_t(std::int16_t((bottom[std::size_t(k)] - top[std::size_t(k)]) << 8) / 15));
        }
        for (int i = 0; i < 15; ++i) {
            data::Colour c = 0;
            for (int k = 0; k < 3; ++k) {
                value[std::size_t(k)] = std::int16_t(value[std::size_t(k)] + step[std::size_t(k)]);
                c = data::Colour(c << 4 | ((std::int16_t(value[std::size_t(k)]) >> 8) & 15));
            }
            all[std::size_t(18 + i)] = c;
        }
    }
    for (std::size_t i = 0; i < 15; ++i) all[33 + i] = program.u16(panel_palette + std::uint32_t(i) * 2);

    // Lightning's palette ($136C2): some of the 18 turn white.
    std::array<data::Colour, 48> base = all;
    if (flash) {
        std::uint32_t mask = flash_mask;
        for (std::size_t i = 0; i < 18; ++i, mask >>= 1)
            if (mask & 1) base[i] = data::Colour(base[i] | 0xFFF);
    }
    FramePalette p;
    for (std::size_t i = 0; i < 8; ++i) p.colours[i] = base[i];
    p.colours[8] = base[17]; // colour 8: the floor's unlit colour (ground shadows)
    for (std::size_t i = 9; i < 16; ++i) p.colours[i] = base[i];
    // Colour 7 from the listed lines on: gradient entries (read on past the
    // gradient when a list is longer), then the ground at the horizon.
    const std::uint32_t list = band_lines + program.u16(band_lines + std::uint32_t(sky_type & 3) * 2);
    std::size_t entry = 18;
    for (std::uint32_t at = list; program.u16(at) != 0 && entry < all.size(); at += 2)
        p.colour7.push_back({program.u16(at), all[entry++]});
    p.colour7.push_back({0x48, base[16]});
    return p;
}

} // namespace babel::view
