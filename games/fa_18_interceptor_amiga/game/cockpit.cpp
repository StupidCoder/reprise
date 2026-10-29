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
    // $126960: (pitch rate + 1/8 of it) / 8, from 1 G; the bank turn's
    // share ($13AC36) is left out.
    const int pull = (a.pitch_rate + (a.pitch_rate >> 3)) >> 3;
    r.g_tenths = -(pull - 10);
    r.north_tenths = ((a.z - 0x10000000) >> 8) / 0x7000 + 0x177; // $126DCE
    r.west_tenths = ((a.x - 0x0F000000) >> 8) / 0x5999 + 0x4C4;  // $126E5C
    return r;
}

Cockpit::Cockpit(const data::Assets& assets) : font_(assets.small_font), messages_(assets.messages) {
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
    number(r.feet, 262, 158, 6, display_green, black);
    number(r.knots, 256, 166, 4, display_green, black);
    number(r.thrust_percent, 248, 175, 3, display_green, black);
    number(r.zoom, 243, 184, 1, display_green, black);
    number(r.fuel_pounds, 120, 183, 5, black, fuel_box);
    number(r.radar_miles, 152, 184, 2, display_green, black);

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

void Cockpit::draw_outside(IndexedImage& image, int left, const CockpitReadings& r) const {
    constexpr int row = 184, grey = 13;
    for (int y = 0xA8; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) image.put(x, y, 0);
    const auto right_aligned = [&](int value, const std::vector<int>& cells) {
        const auto t = std::to_string(std::max(0, value));
        for (std::size_t i = 0; i < t.size() && i < cells.size(); ++i)
            text(image, left + cells[cells.size() - 1 - i], row, std::string(1, t[t.size() - 1 - i]), grey);
    };
    text(image, left + 84, row, "HDG", grey);
    right_aligned(r.heading80 / 80, {100, 104, 108});
    right_aligned(r.knots, {147, 151, 155, 159});
    text(image, left + 168, row, "KTS", grey);
    right_aligned(r.feet, {199, 203, 209, 215, 219, 223});
    text(image, left + 232, row, "FT", grey);
}

} // namespace interceptor::game
