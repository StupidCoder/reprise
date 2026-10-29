// Pictures of the 3DS adventures decoded to RGBA. See a56_picture.hpp.
#include "graphics/a56_picture.hpp"
#include <algorithm>
#include <stdexcept>

namespace picarat::adventure56 {

namespace {

using import::adventure56::Picture;

// Position (x, y) of pixel `i` inside an 8x8 tile: y on bits 0, 2, 4 and x
// on bits 1, 3, 5.
void zOrder(unsigned i, unsigned& x, unsigned& y) {
    y = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4);
    x = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4);
}

std::uint8_t expand4(unsigned v) {
    return std::uint8_t(v << 4 | v);
}

std::uint8_t expand5(unsigned v) {
    return std::uint8_t(v << 3 | v >> 2);
}

std::uint8_t expand6(unsigned v) {
    return std::uint8_t(v << 2 | v >> 4);
}

std::uint64_t load64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = v << 8 | p[i];
    }
    return v;
}

// One ETC1 block: 16 colours, index = x * 4 + y.
void etc1Block(std::uint64_t block, platform::Color out[16]) {
    static const int kModifiers[8][2] = {{2, 8},   {5, 17},  {9, 29},  {13, 42},
                                         {18, 60}, {24, 80}, {33, 106}, {47, 183}};
    const std::uint32_t high = std::uint32_t(block >> 32), low = std::uint32_t(block);
    const bool differential = (high >> 1) & 1, flip = high & 1;
    int base[2][3];
    if (differential) {
        for (int c = 0; c < 3; ++c) {
            const int shift = 27 - c * 8;
            const int v = int(high >> shift) & 31;
            int delta = int(high >> (shift - 3)) & 7;
            if (delta & 4) {
                delta -= 8;
            }
            base[0][c] = expand5(unsigned(v));
            base[1][c] = expand5(unsigned((v + delta) & 31));
        }
    } else {
        for (int c = 0; c < 3; ++c) {
            base[0][c] = expand4((high >> (28 - c * 8)) & 15);
            base[1][c] = expand4((high >> (24 - c * 8)) & 15);
        }
    }
    const unsigned table[2] = {(high >> 5) & 7, (high >> 2) & 7};
    for (unsigned x = 0; x < 4; ++x) {
        for (unsigned y = 0; y < 4; ++y) {
            const unsigned k = x * 4 + y;
            const int half = flip ? (y >= 2) : (x >= 2);
            const int magnitude = kModifiers[table[half]][(low >> k) & 1];
            const int modifier = (low >> (16 + k)) & 1 ? -magnitude : magnitude;
            platform::Color& c = out[k];
            c.r = std::uint8_t(std::clamp(base[half][0] + modifier, 0, 255));
            c.g = std::uint8_t(std::clamp(base[half][1] + modifier, 0, 255));
            c.b = std::uint8_t(std::clamp(base[half][2] + modifier, 0, 255));
            c.a = 255;
        }
    }
}

// Pixels of one uncompressed tile pixel.
platform::Color pixel(const Picture& picture, const std::uint8_t* p) {
    switch (picture.format) {
    case 0:
        return {p[3], p[2], p[1], p[0]};
    case 3:
        return {p[2], p[1], p[0], 255};
    case 1: {
        const unsigned v = unsigned(p[0] | p[1] << 8);
        return {expand4(v >> 12), expand4((v >> 8) & 15), expand4((v >> 4) & 15),
                expand4(v & 15)};
    }
    case 2: {
        const unsigned v = unsigned(p[0] | p[1] << 8);
        return {expand5(v >> 11), expand5((v >> 6) & 31), expand5((v >> 1) & 31),
                std::uint8_t(v & 1 ? 255 : 0)};
    }
    case 4: {
        const unsigned v = unsigned(p[0] | p[1] << 8);
        return {expand5(v >> 11), expand6((v >> 5) & 63), expand5(v & 31), 255};
    }
    case 12:
        return {p[0], p[0], p[0], 255};
    default:
        throw std::runtime_error("unknown IMGC format");
    }
}

// The first map cell of a level.
std::size_t firstCell(const Picture& picture, unsigned level) {
    std::size_t cell = 0;
    for (unsigned l = 0; l < level; ++l) {
        const unsigned w = std::max(1u, picture.width >> l), h = std::max(1u, picture.height >> l);
        cell += std::size_t((w + 7) / 8) * ((h + 7) / 8);
    }
    return cell;
}

} // namespace

platform::Image decodePicture(const Picture& picture, unsigned level) {
    if (level >= picture.levels) {
        throw std::runtime_error("IMGC level out of range");
    }
    const unsigned w = std::max(1u, picture.width >> level),
                   h = std::max(1u, picture.height >> level);
    platform::Image image{int(w), int(h)};
    for (auto& c : image.pixels) {
        c = {0, 0, 0, 0};
    }
    const unsigned columns = (w + 7) / 8, rows = (h + 7) / 8;
    const std::size_t first = firstCell(picture, level);
    const std::size_t tileBytes = std::size_t(picture.bitsPerPixel) * 8;
    for (unsigned cell = 0; cell < columns * rows; ++cell) {
        if (first + cell >= picture.map.size()) {
            break;
        }
        const std::uint16_t tile = picture.map[first + cell];
        if (tile == 0xffff || (tile + 1u) * tileBytes > picture.tiles.size()) {
            continue;
        }
        const std::uint8_t* data = picture.tiles.data() + tile * tileBytes;
        const int cx = int(cell % columns) * 8, cy = int(cell / columns) * 8;
        if (picture.format == 27 || picture.format == 28) {
            const std::size_t blockBytes = picture.format == 27 ? 8 : 16;
            for (unsigned b = 0; b < 4; ++b) {
                const std::uint8_t* block = data + b * blockBytes;
                std::uint64_t alpha = ~0ull;
                if (picture.format == 28) {
                    alpha = load64(block);
                    block += 8;
                }
                platform::Color colours[16];
                etc1Block(load64(block), colours);
                const int bx = int(b >> 1) * 4, by = int(b & 1) * 4;
                for (unsigned k = 0; k < 16; ++k) {
                    platform::Color c = colours[k];
                    c.a = expand4(unsigned(alpha >> (4 * k)) & 15);
                    // The block's index runs down columns; the tile's
                    // blocks are stored transposed to it.
                    image.put(cx + bx + int(k % 4), cy + by + int(k / 4), c);
                }
            }
            continue;
        }
        const std::size_t bytes = picture.bitsPerPixel / 8;
        for (unsigned i = 0; i < 64; ++i) {
            unsigned x, y;
            zOrder(i, x, y);
            image.put(cx + int(x), cy + int(y), pixel(picture, data + i * bytes));
        }
    }
    return image;
}

std::uint16_t rawPixel16(const Picture& picture, unsigned x, unsigned y) {
    if (picture.bitsPerPixel != 16 || x >= picture.width || y >= picture.height) {
        return 0;
    }
    const std::uint16_t tile = picture.map[(y / 8) * ((picture.width + 7) / 8) + x / 8];
    if (tile == 0xffff) {
        return 0;
    }
    unsigned inside = 0;
    for (unsigned bit = 0; bit < 3; ++bit) {
        inside |= ((y % 8 >> bit) & 1) << (2 * bit) | ((x % 8 >> bit) & 1) << (2 * bit + 1);
    }
    const std::size_t at = (tile * 64u + inside) * 2;
    return std::uint16_t(picture.tiles[at] | picture.tiles[at + 1] << 8);
}

} // namespace picarat::adventure56
