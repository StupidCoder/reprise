#include "wizball_c64/data/preview.hpp"
#include "c64/palette.hpp"

namespace wizball::data {
using platform::Image;

namespace {
constexpr int level_columns = 45, strip_rows = 4, tile_size = 5;
constexpr platform::Color gap{0x20, 0x20, 0x20};

// The playfield charset as the game has it in a place: the common part
// and the place's characters at $A0-$FE.
Charset charset_for(const Assets& assets, int place) {
    Charset charset = assets.play_charset;
    const auto& chars = assets.places[std::size_t(place)].characters;
    for (std::size_t c = 0; c < chars.size(); ++c) {
        charset[0xA0 + c] = chars[c];
    }
    return charset;
}

// A multicolour character (cell colour bit 3 set) or a single-colour one,
// background black ($D021 is 0 in play).
void draw_glyph(Image& image, const Glyph& glyph, int x, int y, const PlayColours& colours) {
    for (int row = 0; row < 8; ++row) {
        for (int px = 0; px < 8; ++px) {
            int colour;
            if (colours.character & 8) {
                const int pair = glyph[std::size_t(row)] >> (6 - 2 * (px / 2)) & 3;
                colour = pair == 0 ? 0 : pair == 1 ? colours.shared1 : pair == 2 ? colours.shared2 : colours.character & 7;
            } else {
                colour = glyph[std::size_t(row)] >> (7 - px) & 1 ? colours.character : 0;
            }
            image.put(x + px, y + row, c64::colour(colour));
        }
    }
}

void draw_tile(Image& image, const Assets& assets, const Charset& charset, int tile, int x, int y,
               const PlayColours& colours) {
    const auto& t = assets.tiles[std::size_t(tile)];
    for (int ty = 0; ty < tile_size; ++ty) {
        for (int tx = 0; tx < tile_size; ++tx) {
            draw_glyph(image, charset[t[std::size_t(ty * tile_size + tx)]], x + tx * 8, y + ty * 8, colours);
        }
    }
}

void fill(Image& image, platform::Color colour) {
    for (auto& p : image.pixels) p = colour;
}
} // namespace

Image level_image(const Assets& assets, int level, bool painted, int lines) {
    const auto& place = assets.places[std::size_t(level)];
    const auto charset = charset_for(assets, level);
    const auto& colours = painted ? place.painted : assets.grey;
    const int per_line = (level_columns + lines - 1) / lines;
    const int line_height = strip_rows * tile_size * 8;
    Image image(per_line * tile_size * 8, lines * line_height + (lines - 1) * 4);
    fill(image, gap);
    for (int column = 0; column < level_columns; ++column) {
        const int line = column / per_line, x = (column % per_line) * tile_size * 8;
        for (int row = 0; row < strip_rows; ++row) {
            const int tile = assets.map.at(place.first_row + row, place.first_column + column);
            draw_tile(image, assets, charset, tile, x, line * (line_height + 4) + row * tile_size * 8, colours);
        }
    }
    return image;
}

Image place_image(const Assets& assets, int place_index) {
    const auto& place = assets.places[std::size_t(place_index)];
    const auto charset = charset_for(assets, place_index);
    constexpr int first = 90, count = LevelMap::columns - first;
    Image image(count * tile_size * 8, strip_rows * tile_size * 8);
    for (int column = 0; column < count; ++column) {
        for (int row = 0; row < strip_rows; ++row) {
            const int tile = assets.map.at(place.first_row + row, first + column);
            draw_tile(image, assets, charset, tile, column * tile_size * 8, row * tile_size * 8, assets.grey);
        }
    }
    return image;
}

Image tile_sheet(const Assets& assets, int level) {
    const auto charset = charset_for(assets, level);
    const int count = int(assets.tiles.size()), per_row = 16, cell = tile_size * 8 + 2;
    Image image(per_row * cell, (count + per_row - 1) / per_row * cell);
    fill(image, gap);
    for (int t = 0; t < count; ++t) {
        draw_tile(image, assets, charset, t, t % per_row * cell, t / per_row * cell, assets.grey);
    }
    return image;
}

Image play_charset_sheet(const Assets& assets, int level) {
    const auto charset = charset_for(assets, level);
    Image image(16 * 8, 16 * 8);
    for (int c = 0; c < 255; ++c) { // $FF is not part of the file
        draw_glyph(image, charset[std::size_t(c)], c % 16 * 8, c / 16 * 8, assets.grey);
    }
    return image;
}

Image front_charset_sheet(const Assets& assets) {
    Image image(16 * 8, 16 * 8);
    for (int c = 0; c < 256; ++c) {
        draw_glyph(image, assets.front_charset[std::size_t(c)], c % 16 * 8, c / 16 * 8, {0, 0, 1});
    }
    return image;
}

Image sprite_sheet(const Assets& assets) {
    constexpr int per_row = 16, cell_w = 26, cell_h = 23;
    const int count = int(assets.sprites.size());
    Image image(per_row * cell_w, (count + per_row - 1) / per_row * cell_h);
    fill(image, gap);
    for (int s = 0; s < count; ++s) {
        const auto& sprite = assets.sprites[std::size_t(s)];
        const int ox = s % per_row * cell_w, oy = s / per_row * cell_h;
        for (int y = 0; y < 21; ++y) {
            for (int x = 0; x < 24; ++x) {
                const auto byte = sprite.rows[std::size_t(y * 3 + x / 8)];
                const int pair = byte >> (6 - (x & 6)) & 3;
                const int colour = pair == 0 ? 0 : pair == 1 ? 0x0B : pair == 2 ? sprite.extra & 15 : 0x0D;
                image.put(ox + x, oy + y, c64::colour(colour));
            }
        }
    }
    return image;
}

} // namespace wizball::data
