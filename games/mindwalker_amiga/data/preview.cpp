#include "mindwalker_amiga/data/preview.hpp"
#include <algorithm>
#include <array>

namespace mindwalker::data {
namespace {

constexpr platform::Color backdrop{40, 40, 56, 255};
constexpr std::array<platform::Color, 4> grey_ramp = {
    platform::Color{0, 0, 0, 255}, platform::Color{250, 250, 250, 255}, platform::Color{170, 170, 170, 255},
    platform::Color{90, 90, 90, 255}};

void fill(platform::Image& image, platform::Color colour) {
    std::fill(image.pixels.begin(), image.pixels.end(), colour);
}

} // namespace

platform::Color to_color(Rgb4 colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17),
            255};
}

platform::Image ham_image(const HamPicture& picture) {
    const auto& codes = picture.codes;
    platform::Image image(codes.width, codes.height);
    for (int y = 0; y < codes.height; ++y) {
        // Each line starts from the background colour (colour 0).
        Rgb4 colour = picture.palette[0];
        for (int x = 0; x < codes.width; ++x) {
            const int code = codes.at(x, y), value = code & 15;
            switch (code >> 4) {
            case 0: colour = picture.palette[std::size_t(value)]; break;
            case 1: colour = Rgb4((colour & 0xFF0) | value); break;
            case 2: colour = Rgb4((colour & 0x0FF) | value << 8); break;
            default: colour = Rgb4((colour & 0xF0F) | value << 4); break;
            }
            image.put(x, y, to_color(colour));
        }
    }
    return image;
}

platform::Image bitmap_image(const Bitmap& bitmap, const Palette& palette) {
    platform::Image image(bitmap.width, bitmap.height);
    for (int y = 0; y < bitmap.height; ++y) {
        for (int x = 0; x < bitmap.width; ++x) {
            const auto index = bitmap.at(x, y);
            image.put(x, y, index < palette.size() ? to_color(palette[index]) : platform::Color{255, 0, 255, 255});
        }
    }
    return image;
}

platform::Image portrait_image(const Bitmap& bitmap) {
    return bitmap_image(bitmap, {0x000, 0xFFF});
}

platform::Image frame_sheet(const std::vector<Bitmap>& frames, const Palette& palette, int width) {
    // Lay the frames out in rows, left to right, 2 pixels apart.
    int x = 2, y = 2, row_height = 0, widest = 0;
    struct Place {
        int x, y;
    };
    std::vector<Place> places;
    for (const auto& frame : frames) {
        if (x + frame.width + 2 > width && x > 2) {
            x = 2;
            y += row_height + 2;
            row_height = 0;
        }
        places.push_back({x, y});
        x += frame.width + 2;
        widest = std::max(widest, x);
        row_height = std::max(row_height, frame.height);
    }
    platform::Image image(std::max(widest, 4), y + row_height + 2);
    fill(image, backdrop);
    for (std::size_t f = 0; f < frames.size(); ++f) {
        const auto& frame = frames[f];
        for (int fy = 0; fy < frame.height; ++fy) {
            for (int fx = 0; fx < frame.width; ++fx) {
                const auto index = frame.at(fx, fy);
                const auto colour = palette.empty() ? grey_ramp[index & 3]
                                    : index < palette.size() ? to_color(palette[index])
                                                             : platform::Color{255, 0, 255, 255};
                image.put(places[f].x + fx, places[f].y + fy, colour);
            }
        }
    }
    return image;
}

Palette hands_palette(const Assets& assets) {
    Palette palette{assets.tunnel_palette[0]};
    for (std::size_t v = 1; v < 8; ++v) {
        palette.push_back(assets.tunnel_palette[8 + v]);
    }
    return palette;
}

platform::Image palette_image(const Palette& palette) {
    constexpr int size = 12, per_row = 16;
    const int rows = (int(palette.size()) + per_row - 1) / per_row;
    platform::Image image(per_row * size, rows * size);
    fill(image, backdrop);
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const int x0 = int(i % per_row) * size, y0 = int(i / per_row) * size;
        for (int y = 1; y < size - 1; ++y) {
            for (int x = 1; x < size - 1; ++x) {
                image.put(x0 + x, y0 + y, to_color(palette[i]));
            }
        }
    }
    return image;
}

platform::Image sample_image(const Sample& sample) {
    constexpr int height = 129;
    const int per_column = std::max<int>(1, int(sample.data.size() + 319) / 320);
    const int width = int(sample.data.size() + std::size_t(per_column) - 1) / per_column;
    platform::Image image(std::max(width, 1), height);
    fill(image, backdrop);
    for (int x = 0; x < width; ++x) {
        int low = 127, high = -128;
        for (int i = x * per_column; i < std::min<int>(int(sample.data.size()), (x + 1) * per_column); ++i) {
            low = std::min<int>(low, sample.data[std::size_t(i)]);
            high = std::max<int>(high, sample.data[std::size_t(i)]);
        }
        for (int v = low; v <= high; ++v) {
            image.put(x, 64 - v / 2, platform::Color{120, 220, 140, 255});
        }
        image.put(x, 64, platform::Color{90, 90, 110, 255});
    }
    return image;
}

} // namespace mindwalker::data
