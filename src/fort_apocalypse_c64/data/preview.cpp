#include "fort_apocalypse_c64/data/preview.hpp"
#include "c64/palette.hpp"
#include <vector>

namespace fort_apocalypse::data {
using platform::Image;

namespace {
// Colour RAM of the HUD rows during play, per row; row 3 also holds two
// yellow ($0F) stretches and row 0 one. Observed in the emulator run.
int hud_cell_colour(int column, int row) {
    if ((row == 0 && column >= 19 && column <= 30) ||
        (row == 3 && (column <= 11 || (column >= 27 && column <= 38)))) {
        return 0x0F;
    }
    return row == 0 || row == 5 ? 0x0B : 0x0D;
}

void draw_glyph(Image& image, const Glyph& glyph, int x, int y, const Colours& colours,
                int cell) {
    const bool multicolour = cell & 8;
    for (int row = 0; row < 8; ++row) {
        for (int px = 0; px < 8; ++px) {
            int colour;
            if (multicolour) {
                const int pair = glyph[std::size_t(row)] >> (6 - 2 * (px / 2)) & 3;
                const int by_pair[] = {colours.background, colours.shared1, colours.shared2,
                                       cell & 7};
                colour = by_pair[pair];
            } else {
                colour = glyph[std::size_t(row)] >> (7 - px) & 1 ? cell : colours.background;
            }
            image.put(x + px, y + row, c64::colour(colour));
        }
    }
}
} // namespace

Colours play_colours(const Assets& assets, int level) {
    return {0, assets.levels[std::size_t(level)].multicolour, 1, 0x0D};
}

Image charset_sheet(const Charset& charset, const Colours& colours) {
    Image image(16 * 8, 8 * 8);
    for (int c = 0; c < 128; ++c) {
        draw_glyph(image, charset[std::size_t(c)], c % 16 * 8, c / 16 * 8, colours,
                   colours.cell | 8);
    }
    return image;
}

Image helicopter_sheet(const Assets& assets, int colour) {
    // Distinct poses in tilt order: each is a pair of rotor frames.
    std::vector<std::pair<int, int>> poses;
    for (std::size_t i = 0; i + 1 < assets.helicopter_tilt.size(); i += 2) {
        const std::pair pose{int(assets.helicopter_tilt[i]), int(assets.helicopter_tilt[i + 1])};
        if (poses.empty() || poses.back() != pose) {
            poses.push_back(pose);
        }
    }
    constexpr int cell_w = 2 * SpriteShape::width + 8, cell_h = SpriteShape::height + 4;
    Image image(2 * cell_w, int(poses.size()) * cell_h);
    for (std::size_t p = 0; p < poses.size(); ++p) {
        for (int frame = 0; frame < 2; ++frame) {
            const auto& shape = assets.helicopter[std::size_t(frame ? poses[p].second
                                                                     : poses[p].first)];
            for (int y = 0; y < SpriteShape::height; ++y) {
                for (int x = 0; x < SpriteShape::width; ++x) {
                    const auto c = c64::colour(shape.pixel(x, y) ? colour : 0);
                    image.put(frame * cell_w + 2 * x, int(p) * cell_h + y, c);
                    image.put(frame * cell_w + 2 * x + 1, int(p) * cell_h + y, c);
                }
            }
        }
    }
    return image;
}

Image terrain_image(const Assets& assets, int level) {
    const auto& terrain = assets.levels[std::size_t(level)].terrain;
    const auto colours = play_colours(assets, level);
    Image image(TerrainMap::columns * 8, TerrainMap::rows * 8);
    for (int row = 0; row < TerrainMap::rows; ++row) {
        for (int column = 0; column < TerrainMap::columns; ++column) {
            draw_glyph(image, assets.playfield_charset[terrain.at(column, row)], column * 8,
                       row * 8, colours, colours.cell);
        }
    }
    return image;
}

Image radar_image(const Assets& assets, int level) {
    const auto& radar = assets.levels[std::size_t(level)].radar;
    const auto colours = play_colours(assets, level);
    const int by_pair[] = {colours.background, colours.shared1, colours.shared2,
                           colours.cell & 7};
    Image image(RadarImage::bytes_per_line * 8, RadarImage::lines);
    for (int y = 0; y < RadarImage::lines; ++y) {
        for (int x = 0; x < RadarImage::bytes_per_line * 4; ++x) {
            const auto c = c64::colour(by_pair[radar.pair(x, y)]);
            image.put(2 * x, y, c);
            image.put(2 * x + 1, y, c);
        }
    }
    return image;
}

Image hud_image(const Assets& assets, int level) {
    const auto colours = play_colours(assets, level);
    Image image(HudScreen::columns * 8, HudScreen::rows * 8);
    for (int row = 0; row < HudScreen::rows; ++row) {
        for (int column = 0; column < HudScreen::columns; ++column) {
            draw_glyph(image, assets.hud_charset[assets.hud.cells[std::size_t(row)][std::size_t(column)]],
                       column * 8, row * 8, colours, hud_cell_colour(column, row));
        }
    }
    return image;
}

} // namespace fort_apocalypse::data
