#include "fa_18_interceptor_amiga/game/cockpit.hpp"
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
    return r;
}

Cockpit::Cockpit(const data::Assets& assets) : font_(assets.small_font), messages_(assets.messages) {
    for (const auto& p : assets.pictures) {
        if (p.file == "pix/inst5") panel_ = &p.picture;
        if (p.file == "pix/frnt5") shield_ = &p.picture;
    }
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

} // namespace interceptor::game
