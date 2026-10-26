#include "bruce_lee_c64/data/preview.hpp"
#include "c64/palette.hpp"

namespace bruce_lee::data {
using platform::Image;

namespace {
// Draws a character in multicolour (cell colour 8-15) or single colour.
// `shared` holds $D021-$D023.
void draw_glyph(Image& image, const Glyph& glyph, int x, int y, std::array<int, 3> shared, int cell) {
    for (int row = 0; row < 8; ++row) {
        for (int px = 0; px < 8; ++px) {
            int colour;
            if (cell & 8) {
                const int pair = glyph[std::size_t(row)] >> (6 - 2 * (px / 2)) & 3;
                colour = pair == 3 ? cell & 7 : shared[std::size_t(pair)];
            } else {
                colour = glyph[std::size_t(row)] >> (7 - px) & 1 ? cell : shared[0];
            }
            image.put(x + px, y + row, c64::colour(colour));
        }
    }
}

std::array<int, 3> shared_colours(const Assets& assets) {
    return {assets.play_colours[1] & 15, assets.play_colours[2] & 15, assets.play_colours[3] & 15};
}
} // namespace

Image room_image(const Assets& assets, int room) {
    const auto& r = assets.rooms[std::size_t(room)];
    const auto& charset = assets.charsets[std::size_t(r.charset)];
    Image image(Room::columns * 8, Room::rows * 16);
    for (int row = 0; row < Room::rows; ++row) {
        for (int column = 0; column < Room::columns; ++column) {
            const auto cell = r.at(column, row);
            const int colour = cell & 0x80 ? 0xF8 : r.colour; // $0B51
            const int code = cell & 0x7F;
            draw_glyph(image, charset[std::size_t(code)], column * 8, row * 16, shared_colours(assets), colour & 15);
            draw_glyph(image, charset[std::size_t(code | 0x80)], column * 8, row * 16 + 8, shared_colours(assets),
                       colour & 15);
        }
    }
    return image;
}

Image charset_sheet(const Assets& assets, int set) {
    Image image(16 * 8, 16 * 8);
    for (int c = 0; c < 256; ++c) {
        draw_glyph(image, assets.charsets[std::size_t(set)][std::size_t(c)], c % 16 * 8, c / 16 * 8,
                   shared_colours(assets), 0x0E);
    }
    return image;
}

Image text_sheet(const Assets& assets) {
    Image image(16 * 8, 8 * 8);
    for (std::size_t c = 0; c < assets.text.size(); ++c) {
        draw_glyph(image, assets.text[c], int(c % 16) * 8, int(c / 16) * 8, {6, 0, 0}, 1);
    }
    return image;
}

// Observed in the emulator while the picture is shown: multicolour bitmap,
// background 0, screen RAM $CE (pairs 01 = 12, 10 = 14), colour RAM 1.
Image loading_image(const Assets& assets) {
    Image image(320, 200);
    for (int cy = 0; cy < 25; ++cy) {
        for (int cx = 0; cx < 40; ++cx) {
            for (int row = 0; row < 8; ++row) {
                const auto byte = assets.loading.bitmap[std::size_t((cy * 40 + cx) * 8 + row)];
                for (int pair = 0; pair < 4; ++pair) {
                    const int bits = byte >> (6 - 2 * pair) & 3;
                    const int colour = bits == 0 ? 0 : bits == 1 ? 12 : bits == 2 ? 14 : 1;
                    image.put(cx * 8 + 2 * pair, cy * 8 + row, c64::colour(colour));
                    image.put(cx * 8 + 2 * pair + 1, cy * 8 + row, c64::colour(colour));
                }
            }
        }
    }
    return image;
}

} // namespace bruce_lee::data
