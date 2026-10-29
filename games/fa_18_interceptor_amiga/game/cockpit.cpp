#include "fa_18_interceptor_amiga/game/cockpit.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>

namespace interceptor::game {
namespace {

constexpr int panel_top = 145, shield_left = 16, shield_top = 133;
constexpr int display_green = 4, glass_grey = 0xA, glass_edge = 8, black = 0, fuel_box = 8;

void blit(IndexedImage& image, const amiga::IlbmPicture& picture, int left, int top, bool transparent) {
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            const int c = picture.pixels[std::size_t(y * picture.width + x)];
            if (transparent && c == 0) continue;
            image.put(left + x, top + y, c);
        }
    }
}

// `value` in `count` digits, up to `blank` leading zeros as spaces (the
// original's BCD strings).
std::string digits(int value, int count, int blank) {
    std::string t(std::size_t(count), '0');
    value = std::max(0, value);
    for (int i = count - 1; i >= 0; --i, value /= 10) t[std::size_t(i)] = char('0' + value % 10);
    for (int i = 0; i < blank && t[std::size_t(i)] == '0'; ++i) t[std::size_t(i)] = ' ';
    return t;
}

// A character position table: pairs of a byte offset and a blitter shift.
std::vector<int> positions(const amiga::LoadedImage& m, std::uint32_t at, int count) {
    std::vector<int> xs;
    for (int i = 0; i < count; ++i) xs.push_back(m.s16(at + std::uint32_t(4 * i)) * 8 + (m.u16(at + std::uint32_t(4 * i + 2)) >> 12));
    return xs;
}

} // namespace

CockpitReadings read_cockpit(const Aircraft& a) {
    CockpitReadings r;
    r.feet = int((a.y >> 10) * 5);                       // $126C16
    r.knots = (a.state & 0x8000) ? 0 : std::abs(int(a.airspeed)) / 12; // $126B48
    r.thrust_percent = a.throttle * 100 / 120;
    r.fuel_pounds = int(a.fuel >> 8);                     // $126D26
    r.heading80 = a.attitude.heading;
    r.gear = a.gear & 0x7F;
    // $126960: (pitch rate + 1/8 of it + the bank turn's share) / 8, from 1 G.
    int pull = a.pitch_rate + (a.pitch_rate >> 3);
    if (std::abs(int(a.bank_pitch)) > 1) pull += a.bank_pitch;
    pull >>= 3;
    // ($1043F2: inverted when one of pitch and roll is past 90 degrees.)
    const auto past = [](int a80) { const int t = (a80 & 0xFFFF) >> 3; return t >= 900 && t < 2700; };
    r.inverted = past(a.attitude.pitch) != past(a.attitude.roll);
    r.g_display = r.inverted ? pull + 10 : pull - 10;
    r.f16 = a.type == 0x10;
    r.north_tenths = ((a.z - 0x10000000) >> 8) / 0x7000 + 0x177; // $126DCE
    r.west_tenths = ((a.x - 0x0F000000) >> 8) / 0x5999 + 0x4C4;  // $126E5C
    return r;
}

Cockpit::Cockpit(const data::Assets& assets) : font_(assets.small_font), messages_(assets.messages) {
    const auto& m = assets.program;
    positions_ = {positions(m, 0x127E6C, 6), positions(m, 0x127E88, 2), positions(m, 0x127E90, 4),
                  positions(m, 0x127EA4, 2), positions(m, 0x126514, 1), positions(m, 0x126508, 3),
                  positions(m, 0x1264FC, 3), positions(m, 0x127E54, 3), positions(m, 0x127E60, 3), {}};
    // `$128612`: the heading labels' two characters for offsets -22 .. 22
    // (first characters, then second ones).
    for (int k = 0; k < 2; ++k)
        for (int p = -22; p <= 22; ++p) {
            const std::uint32_t at = 0x128612 + std::uint32_t(8 * p + 4 * k);
            positions_.heading.push_back(m.s16(at) * 8 + (m.u16(at + 2) >> 12));
        }
    for (std::size_t k = 0; k < gear_images_.size(); ++k) {
        const std::uint32_t at = assets.program.u32(0x12591E + std::uint32_t(4 * k)); // $125998's table
        for (std::uint32_t i = 0; i < gear_images_[k].size(); ++i) gear_images_[k][i] = assets.program.u8(at + i);
    }
    strip_.emplace();
    for (std::uint32_t i = 0; i < strip_->size(); ++i) (*strip_)[i] = assets.program.u8(0x132D9C + i);
    for (const auto& p : assets.pictures) {
        if (p.file == "pix/inst5") panel_ = &p.picture;
        if (p.file == "pix/frnt5") shield_ = &p.picture;
    }
}

void Cockpit::text(IndexedImage& image, int x, int y, const std::string& t, int colour) const {
    for (std::size_t i = 0; i < t.size(); ++i) {
        const auto* glyph = font_.glyph(t[i]);
        if (!glyph) continue;
        for (int yy = 0; yy < font_.rows && yy < int(glyph->size()); ++yy)
            for (int xx = 0; xx < 4; ++xx)
                if ((*glyph)[std::size_t(yy)] & (0x80 >> xx)) image.put(x + 4 * int(i) + xx, y + yy, colour);
    }
}

// The head-up display ($127EB8): speed and height (as numbers, or as the
// F-16's tapes), the load factor, the heading tape with its index, the
// waterline marks ($128D42). The numbers' characters sit where the
// original's tables put them (`positions_`).
void Cockpit::hud(IndexedImage& image, int left, const CockpitReadings& r) const {
    constexpr int glass = 13, marks = 10;
    // $1276A0: characters at a table's positions from column `x0`.
    const auto cells = [&](int x0, int y, const std::vector<int>& xs, const std::string& t) {
        for (std::size_t i = 0; i < t.size() && i < xs.size(); ++i)
            if (t[i] != ' ') text(image, left + x0 + xs[i], y, std::string(1, t[i]), glass);
    };
    const auto dot = [&](int x, int y) { image.put(left + x, y, glass); };
    // The load factor ($126960): (pitch rate + 1/8) / 8 from 1 G (the other
    // way round when inverted), its tenths in two digits (one leading zero
    // blank) after a sign, the point a dot of its own.
    {
        const std::string load = std::string(r.g_display >= 0 ? "-" : " ") + digits(std::abs(r.g_display), 2, 1);
        if (r.f16) {
            cells(96, 61, positions_.g_f16, load);
            dot(0x72, 0x41);
        } else {
            cells(80, 121, positions_.g_letter, "G");
            dot(0x64, 0x7D);
            cells(80, 121, positions_.g_fa18, load);
        }
    }
    if (!r.f16) {
        // $127FB8, $1281DA: feet in six digits, knots in four.
        cells(192, 84, positions_.feet, digits(r.feet, 6, 5));
        cells(208, 91, positions_.feet_label, "FT");
        cells(80, 84, positions_.knots, digits(r.knots, 4, 3));
        cells(96, 91, positions_.knots_label, "KT");
    } else {
        // $128016 / $128240: the F-16's tapes - height in tens of feet on
        // the right, knots on the left: a label every 50 (500 feet / 50
        // knots), 15 rows apart, the nearest one on the row for the value
        // (a third of a row a unit), dots every 3 rows between.
        const auto tape = [&](int value, bool height) {
            const int last = value % 100;
            int label = (value - last) / 10, below = last; // label in tenths of a hundred
            if (last > 70) {
                label += 10;
                below -= 100;
            } else if (last > 20) {
                label += 5;
                below -= 50;
            }
            const int row = 0x5B + below / 3;       // the label's index row ($13AC7C)
            const int text_x = height ? 208 : 80, top = row - 2;
            const auto& xs = height ? positions_.height_tape : positions_.speed_tape;
            const auto show = [&](int l, int y) {
                // (Heights keep their zeros and get a point; speeds blank up to two.)
                cells(text_x, y, xs, height ? digits(l, 3, 0) : digits(l, 3, 2));
                if (height) dot(0xDF, y + 4);
            };
            show(label, top);
            for (int k = 1; top - 15 * k >= 70; ++k) show(label + 5 * k, top - 15 * k);
            for (int k = 1, l = label; l != 0 && top + 15 * k <= 110; ++k) show(l -= 5, top + 15 * k);
            // $128BB0: the scale's dots, long ones at the labels.
            const int x = height ? 0xD4 : 0x69, longer = height ? 1 : 0;
            const auto mark = [&](int y, bool long_mark) {
                if (long_mark) {
                    dot(x + longer - 1, y);
                    dot(x + longer, y);
                } else {
                    dot(x, y);
                }
            };
            mark(row, true);
            for (int k = 0, y = row - 3; y > 0x47; ++k, y -= 3) mark(y, k == 4 || k == 9);
            for (int k = 0, y = row + 3; y < 0x70; ++k, y += 3) mark(y, k == 4 || k == 9);
            // The index ($1286D2, $128702): five pixels on row 91.
            for (int i = 0; i < 5; ++i) dot((height ? 0xCE : 0x6B) + i, 91);
        };
        tape(r.feet / 10, true);
        tape(r.knots, false);
        // $12838A: the bracket by the speed's index.
        for (const auto& [x, y] : {std::pair{109, 86}, {110, 86}, {108, 87}, {108, 88}, {109, 89}, {110, 89}}) dot(x, y);
    }
    // The heading tape ($1283F8): tens of degrees every 20 pixels on row 53
    // (both digits, from the table `$128612` by the label's offset), the
    // nearest label 1/5 pixel a tenth of a degree from the middle (the next
    // ten from 4.1 degrees on); ticks every 10 pixels on row 61 between
    // columns 133 and 185, two rows tall at the labels ($128C62).
    {
        const int tenths = (r.heading80 & 0xFFFF) >> 3;
        int label = tenths / 100 * 10, past = tenths % 100;
        if (past > 40) {
            label += 10;
            past -= 100;
        }
        if (label >= 360) label = 0;
        const int shift = -(past / 5), centre = 0x9F + shift;
        const auto show = [&](int value, int p) {
            const std::string t = digits(value / 10, 2, 0);
            text(image, left + 128 + positions_.heading[std::size_t(p + 22)], 53, t.substr(0, 1), glass);
            text(image, left + 128 + positions_.heading[std::size_t(p + 22) + 45], 53, t.substr(1, 1), glass);
        };
        show(label, shift);
        for (int p = shift + 20, l = label; p <= 22; p += 20) show(l = (l + 10) % 360, p);
        for (int p = shift - 20, l = label; p >= -22; p -= 20) show(l = (l <= 0 ? 360 : l) - 10, p);
        dot(centre, 61);
        dot(centre, 60);
        for (int k = 0, x = centre + 10; x < 0xB9; ++k, x += 10) {
            dot(x, 61);
            if (k == 1 || k == 3) dot(x, 60);
        }
        for (int k = 0, x = centre - 10; x > 0x85; ++k, x -= 10) {
            dot(x, 61);
            if (k == 1 || k == 3) dot(x, 60);
        }
    }
    for (int y = 63; y <= 65; ++y) image.put(left + 159, y, glass);
    for (int x = 141; x <= 145; ++x) image.put(left + x, 91, marks);
    for (int x = 173; x <= 177; ++x) image.put(left + x, 91, marks);
    // The weapon's line under the load factor ($12685C, from column 92 at
    // four pixels a character): "GUN" and the rounds in three digits (up
    // to two leading zeros blank), or " SW" / " AM" and the count in one
    // (a 0 blank).
    if (r.weapon == 1) {
        text(image, left + 92, 129, "GUN", glass);
        const auto t = std::to_string(std::clamp(r.weapon_count, 0, 999));
        const std::string digits = std::string(3 - t.size(), ' ') + t;
        text(image, left + 108, 129, digits, glass);
    } else if (r.weapon >= 2) {
        text(image, left + 92, 129, r.weapon == 2 ? " SW" : " AM", glass);
        if (r.weapon_count > 0) text(image, left + 108, 129, std::to_string(r.weapon_count % 10), glass);
    }
    // The gun's aiming circle ($128734 with $12919C): a quarter circle's
    // 20 points (`$129548`) mirrored into the four quarters, colour 10,
    // and its centre's dot; only well inside the glass.
    if (r.pipper) {
        static constexpr int quarter[20][2] = {{0, 12}, {1, 12}, {2, 12}, {3, 12}, {4, 12}, {5, 11}, {6, 11},
                                               {7, 11}, {8, 10}, {9, 10}, {10, 9},  {11, 8}, {12, 7}, {13, 6},
                                               {13, 5}, {14, 4}, {14, 3}, {14, 2}, {14, 1}, {14, 0}};
        const auto [cx, cy] = *r.pipper;
        if (cx > 0) image.put(left + cx, cy, glass);
        if (cx > 0x63 && cx < 0xDB && cy > 0x39 && cy < 0x84) {
            for (const auto& p : quarter) {
                image.put(left + cx + p[0], cy - p[1], marks);
                image.put(left + cx + p[0], cy + p[1] - 1, marks);
                image.put(left + cx - p[0], cy + p[1] - 1, marks);
                image.put(left + cx - p[0], cy - p[1], marks);
            }
        }
    }
}

// The stores ($1255FC and $1274A4, in the panel's left display): the gun's
// mark at the nose, the missiles on their stations (empty ones black, the
// one to go next red while its weapon is selected), "ARM" (or "NO" with
// none left) and the weapon's name.
void Cockpit::stores(IndexedImage& image, int left, const CockpitReadings& r) const {
    constexpr int red = 1;
    image.put(left + 68, 163, r.weapon == 1 ? red : black);
    image.put(left + 68, 164, r.weapon == 1 ? red : display_green);
    struct Stations {
        std::array<std::array<int, 2>, 4> at;
        int count, loaded, weapon;
    };
    // `$1255DE` (Sidewinders) and `$1255E8` (AMRAAMs).
    const Stations lists[2] = {{{{{85, 172}, {51, 172}}}, 2, r.sidewinders, 2},
                               {{{{74, 159}, {62, 159}, {81, 167}, {55, 167}}}, 4, r.amraams, 3}};
    for (const auto& list : lists) {
        for (int k = 0; k < list.count; ++k) {
            int colour = black;
            if (k < list.loaded) colour = (r.weapon == list.weapon && k == list.loaded - 1) ? red : display_green;
            const auto [x, y] = list.at[std::size_t(k)];
            for (int yy = y + 1; yy <= y + 6; ++yy) image.put(left + x, yy, colour);
            image.put(left + x - 1, y + 6, colour);
            image.put(left + x + 1, y + 6, colour);
        }
    }
    if (r.weapon == 0) return;
    text(image, left + 61, 176, r.weapon_count > 0 ? "ARM" : " NO", display_green);
    text(image, left + 61, 184, r.weapon == 1 ? "GUN" : r.weapon == 2 ? " SW" : " AM", display_green);
}

void Cockpit::draw(IndexedImage& image, int left, const CockpitReadings& r) const {
    // The head-up display's glass: a grey top edge with bevelled corners and
    // black side posts down to the glare shield.
    for (int x = 93; x <= 224; ++x) image.put(left + x, 37, glass_grey);
    for (int k = 0; k <= 6; ++k) {
        image.put(left + 92 - k, 38 + k, glass_edge);
        image.put(left + 225 + k, 37 + k, glass_edge);
    }
    image.put(left + 232, 44, glass_edge);
    for (int y = 45; y <= 144; ++y) {
        image.put(left + 85, y, black);
        image.put(left + 233, y, black);
    }
    // The flight path cross in the middle of the glass.
    for (int d = -2; d <= 2; ++d) {
        image.put(left + 159 + d, 73, glass_grey);
        image.put(left + 159, 73 + d, glass_grey);
    }
    if (shield_) blit(image, *shield_, left + shield_left, shield_top, true);
    if (panel_) blit(image, *panel_, left, panel_top, false);

    // Numbers: right-aligned digits of the 5-row font, four pixels apart,
    // leading zeros left out (the original blanks them).
    const auto number = [&](int value, int last_x, int top, int digits, int colour, int background) {
        const std::string text = std::to_string(std::max(0, value));
        for (int i = 0; i < digits; ++i) {
            const int x = last_x - 4 * i;
            for (int yy = 0; yy < font_.rows; ++yy)
                for (int xx = 0; xx < 3; ++xx) image.put(left + x + xx, top + yy, background);
            if (i >= int(text.size())) continue;
            const auto* glyph = font_.glyph(text[text.size() - 1 - std::size_t(i)]);
            if (!glyph) continue;
            for (int yy = 0; yy < font_.rows && yy < int(glyph->size()); ++yy) {
                for (int xx = 0; xx < 8; ++xx) {
                    if ((*glyph)[std::size_t(yy)] & (0x80 >> xx)) image.put(left + x + xx, top + yy, colour);
                }
            }
        }
    };
    if (r.hud) hud(image, left, r);
    // Latitude and longitude ($126DCE, $126E5C) in black on the blue boxes
    // (the N and W are the picture's): hundreds, tens, units, point, tenth.
    const auto degrees = [&](int tenths, int y) {
        static constexpr int cells[5] = {292, 295, 299, 302, 305};
        const std::string t = std::to_string(std::clamp(tenths, 0, 9999));
        const std::string digits = std::string(4 - std::min<std::size_t>(4, t.size()), ' ') + t;
        const char chars[5] = {digits[0], digits[1], digits[2], '.', digits[3]};
        for (int i = 0; i < 5; ++i)
            if (chars[i] != ' ' && !(i == 0 && chars[0] == '0')) text(image, left + cells[i], y, std::string(1, chars[i]), black);
    };
    degrees(r.north_tenths, 180);
    degrees(r.west_tenths, 188);
    // The fuel bar ($125514): 22 rows from row 180 up, lit (colour 4) for
    // (fuel & $7FFF) / 1024 - 1 of them, columns 111-114.
    {
        int lit = ((r.fuel_pounds & 0x7FFF) >> 10) - 1;
        for (int k = 0; k < 22; ++k, --lit)
            for (int x = 111; x <= 114; ++x) image.put(left + x, 180 - k, lit > 0 ? display_green : black);
    }
    // The gear's symbol in the GEAR box ($125988): one of four 32 x 7
    // images by the gear's travel, black on the box, from row 189.
    {
        const auto& image_bits = gear_images_[std::size_t((r.gear >> 5) & 3)];
        for (int row = 0; row < 7; ++row)
            for (int x = 0; x < 32; ++x)
                if (image_bits[std::size_t(row * 4 + x / 8)] & (0x80 >> (x % 8))) image.put(left + x, 189 + row, black);
    }
    // The radar display ($125E22): the scan's two edges (from $125E86,
    // colour 3; drawn from 163 up to 148) and the own aircraft's mark
    // ($125F0E, colour 12). (Contacts need the other objects.)
    for (int k = 0; k <= 15; ++k) {
        image.put(left + 154 - k, 163 - k, 3);
        image.put(left + 162 + k, 163 - k, 3);
    }
    for (int x = 157; x <= 159; ++x) image.put(left + x, 168, 12);
    image.put(left + 158, 167, 12);
    // The compass strip ($125B74): the chip data's "W N E S" strip
    // (`$132D9C`: 7 rows of 96 pixels, a letter every 12, 7.5 degrees a
    // pixel) seen through the window at 192-225, the red index at 208.
    if (strip_) {
        const int shift = r.heading80 / 600;
        for (int y = 150; y <= 157; ++y)
            for (int x = 192; x <= 225; ++x) image.put(left + x, y, 3);
        for (int row = 1; row <= 5; ++row) {
            for (int x = 192; x <= 225; ++x) {
                const int col = ((x - 207 + shift - 36) % 48 + 48) % 48;
                const auto byte = (*strip_)[std::size_t(row * 12 + col / 8)];
                if (byte & (0x80 >> (col % 8))) image.put(left + x, 151 + row, 2);
            }
        }
        for (int y = 151; y <= 157; ++y) image.put(left + 208, y, 1);
    }
    number(r.feet, 262, 158, 6, display_green, black);
    number(r.knots, 256, 166, 4, display_green, black);
    number(r.thrust_percent, 248, 175, 3, display_green, black);
    number(r.zoom, 243, 184, 1, display_green, black);
    number(r.fuel_pounds, 120, 183, 5, black, fuel_box);
    number(r.radar_miles, 152, 184, 2, display_green, black);
    stores(image, left, r);

    // The message line ($127126, $127390): 26 characters four pixels apart
    // from column 108 on row 192, in the message's colour on the black box.
    if (r.message > 0 && std::size_t(r.message) < messages_.size()) {
        const auto& text = messages_[std::size_t(r.message)];
        for (std::size_t i = 0; i < text.size() && i < 26; ++i) {
            const auto* glyph = font_.glyph(text[i]);
            if (!glyph) continue;
            const int x = left + 108 + 4 * int(i);
            for (int yy = 0; yy < font_.rows && yy < int(glyph->size()); ++yy) {
                for (int xx = 0; xx < 3; ++xx) {
                    if ((*glyph)[std::size_t(yy)] & (0x80 >> xx)) image.put(x + xx, 192 + yy, r.message_colour);
                }
            }
        }
    }
}

void Cockpit::draw_outside(IndexedImage& image, int left, const CockpitReadings& r, int top, bool numbers) const {
    constexpr int row = 184, grey = 13;
    for (int y = top; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) image.put(x, y, 3); // ($111, as on the original's screen)
    if (!numbers) return;
    const auto right_aligned = [&](int value, const std::vector<int>& cells) {
        const auto t = std::to_string(std::max(0, value));
        for (std::size_t i = 0; i < t.size() && i < cells.size(); ++i)
            text(image, left + cells[cells.size() - 1 - i], row, std::string(1, t[t.size() - 1 - i]), grey);
    };
    text(image, left + 84, row, "HDG", grey);
    {
        const int h = r.heading80 / 80; // three digits with zeros ("HDG 028")
        const char t[4] = {char('0' + h / 100 % 10), char('0' + h / 10 % 10), char('0' + h % 10), 0};
        text(image, left + 100, row, t, grey);
    }
    right_aligned(r.knots, {147, 151, 155, 159});
    text(image, left + 168, row, "KTS", grey);
    right_aligned(r.feet, {199, 203, 209, 215, 219, 223});
    text(image, left + 232, row, "FT", grey);
}

} // namespace interceptor::game
