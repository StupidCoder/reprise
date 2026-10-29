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

} // namespace

CockpitReadings read_cockpit(const Aircraft& a) {
    CockpitReadings r;
    r.feet = int((a.y >> 10) * 5);                       // $126C16
    r.knots = (a.state & 0x8000) ? 0 : std::abs(int(a.airspeed)) / 12; // $126B48
    r.thrust_percent = a.throttle * 100 / 120;
    r.fuel_pounds = int(a.fuel >> 8);                     // $126D26
    r.heading80 = a.attitude.heading;
    r.gear = a.gear & 0x7F;
    // $126960: (pitch rate + 1/8 of it) / 8, from 1 G; the bank turn's
    // share ($13AC36) is left out.
    const int pull = (a.pitch_rate + (a.pitch_rate >> 3)) >> 3;
    r.g_tenths = -(pull - 10);
    r.north_tenths = ((a.z - 0x10000000) >> 8) / 0x7000 + 0x177; // $126DCE
    r.west_tenths = ((a.x - 0x0F000000) >> 8) / 0x5999 + 0x4C4;  // $126E5C
    return r;
}

Cockpit::Cockpit(const data::Assets& assets) : font_(assets.small_font), messages_(assets.messages) {
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

// The head-up display ($127EB8): speed and height with their labels, the
// load factor, the heading tape with its index, the waterline marks
// ($128D42). Rules from the code, positions from the original's screen
// (`work/ref/cap/009.png`).
void Cockpit::hud(IndexedImage& image, int left, const CockpitReadings& r) const {
    constexpr int glass = 13, marks = 10;
    const auto digits_at = [&](int value, const int* xs, int count, int y) {
        const auto t = std::to_string(std::max(0, value));
        for (int i = 0; i < count && i < int(t.size()); ++i)
            text(image, left + xs[count - 1 - i], y, std::string(1, t[t.size() - 1 - std::size_t(i)]), glass);
    };
    static constexpr int speed_x[4] = {90, 94, 98, 102};             // table $127E90
    static constexpr int height_x[6] = {205, 209, 213, 219, 223, 227}; // table $127E6C (a gap at the thousands)
    digits_at(r.knots, speed_x, 4, 84);
    text(image, left + 96, 91, "KT", glass);
    digits_at(r.feet, height_x, 6, 84);
    text(image, left + 226, 91, "FT", glass);
    // "1.4 G" (a minus sign when pushed over).
    const int g = std::abs(r.g_tenths);
    std::string load = std::to_string(g / 10) + "." + std::to_string(g % 10) + " G";
    if (r.g_tenths < 0) load = "-" + load;
    text(image, left + 98 - 4 * int(load.size() - 3), 121, load, glass);
    // The heading tape: a tick every 5 degrees 2 pixels apart (every 10
    // degrees two rows tall, with its tens of degrees above it), the index
    // under the middle.
    for (int k = -8; k <= 8; ++k) {
        const int step = r.heading80 / 400 + k;            // 5-degree steps
        const int x = 159 - (r.heading80 - step * 400) * 2 / 80;
        if (x < 126 || x > 185) continue;
        const int deg = ((step * 5) % 360 + 360) % 360;
        image.put(left + x, 61, glass);
        if (deg % 10 == 0) {
            image.put(left + x, 60, glass);
            const int tens = deg / 10;
            char label[3] = {char('0' + tens / 10), char('0' + tens % 10), 0};
            text(image, left + x + 3, 53, label, glass);
        }
    }
    for (int y = 63; y <= 65; ++y) image.put(left + 159, y, glass);
    for (int x = 141; x <= 145; ++x) image.put(left + x, 90, marks);
    for (int x = 173; x <= 177; ++x) image.put(left + x, 90, marks);
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
