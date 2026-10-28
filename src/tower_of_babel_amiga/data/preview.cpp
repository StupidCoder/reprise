#include "tower_of_babel_amiga/data/preview.hpp"
#include "ui/draw.hpp"
#include <string>

namespace babel::data {
namespace {

void blit(platform::Image& image, const Picture& picture, int x0, int y0, const TitleScreen& screen) {
    for (int y = 0; y < picture.height; ++y) {
        const int line = y0 + y;
        const int band = line < screen.band2_line ? 0 : line < screen.band3_line ? 1 : 2;
        for (int x = 0; x < picture.width; ++x) {
            const auto index = picture.pixels[std::size_t(y) * std::size_t(picture.width) + std::size_t(x)];
            image.put(x0 + x, line, to_color(screen.palette[std::size_t(band * 16 + index)]));
        }
    }
}

// Legend colours for the map's cell codes (the game's own colours depend on
// the view and the tower's palette; their mapping is not recovered yet).
platform::Color cell_color(std::uint8_t code) {
    switch (code) {
    case 0x00: return {24, 24, 32};
    case 0xC0: return {150, 150, 160};  // floor section
    case 0xC8: return {90, 160, 220};
    case 0xE0: return {220, 180, 80};
    case 0xE8: return {200, 90, 200};
    case 0xF0: return {90, 200, 120};
    case 0x30: return {230, 110, 80};
    default: return {255, 0, 255};
    }
}

std::string initials(const std::string& name) {
    std::string result;
    bool start = true;
    for (char c : name) {
        if (c == '\'') { start = false; continue; } // "Player's": no new word
        if (c == ' ' || c == '(') { start = true; continue; }
        if (start && result.size() < 2) result.push_back(c);
        start = false;
    }
    return result;
}

} // namespace

platform::Color to_color(Colour colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17)};
}

platform::Image title_image(const TitleScreen& screen) {
    platform::Image image(320, 200);
    ui::fill(image, to_color(screen.palette[0]));
    blit(image, screen.logo, 0, 10, screen);   // $7F0A
    blit(image, screen.globe, 80, 61, screen); // $7F28
    return image;
}

platform::Image tower_image(const Tower& tower, const Memory& program) {
    constexpr int cell = 22, gap = 12, margin = 8;
    constexpr int floor_size = 8 * cell;
    platform::Image image(margin * 2 + 4 * floor_size + 3 * gap, margin * 2 + 14 + floor_size + 10 + 16);
    ui::fill(image, {12, 12, 16});
    for (int f = 0; f < 4; ++f) {
        const int x0 = margin + f * (floor_size + gap), y0 = margin + 14;
        ui::text(image, "Floor " + std::to_string(f + 1), x0, margin, 1, {200, 200, 200});
        for (int z = 0; z < 8; ++z) {
            for (int x = 0; x < 8; ++x) {
                ui::rect(image, x0 + x * cell + 1, y0 + z * cell + 1, cell - 2, cell - 2,
                         cell_color(tower.cells[std::size_t(f)][std::size_t(z)][std::size_t(x)]));
            }
        }
    }
    for (const auto& object : tower.objects) {
        const int x = margin + object.floor * (floor_size + gap) + object.x * cell;
        const int y = margin + 14 + object.z * cell;
        ui::rect(image, x + 3, y + 3, cell - 6, cell - 6, {0, 0, 0});
        ui::text(image, initials(object_kind_name(program, object.kind)), x + 4, y + 7, 1, {255, 255, 120});
    }
    const int y = margin + 14 + floor_size + 10;
    for (std::size_t i = 0; i < tower.colours.size(); ++i) {
        ui::rect(image, margin + int(i) * 20, y, 18, 14, to_color(tower.colours[i]));
    }
    return image;
}

} // namespace babel::data
