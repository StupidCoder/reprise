#include "wings_of_fury_amiga/data/preview.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace wings::data {
namespace {

constexpr platform::Color backdrop{40, 40, 56, 255};
constexpr platform::Color missing{255, 0, 255, 255};

void fill(platform::Image& image, platform::Color colour) {
    std::fill(image.pixels.begin(), image.pixels.end(), colour);
}

platform::Color colour_of(const Palette& palette, int index) {
    return std::size_t(index) < palette.size() ? to_color(palette[std::size_t(index)]) : missing;
}

} // namespace

platform::Color to_color(Rgb4 colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17),
            255};
}

platform::Image picture_image(const amiga::IlbmPicture& picture) {
    platform::Image image(picture.width, picture.height);
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            image.put(x, y, colour_of(picture.palette, picture.pixels[std::size_t(y * picture.width + x)]));
        }
    }
    return image;
}

platform::Image palette_image(const Palette& palette) {
    constexpr int size = 12, per_row = 16;
    const int rows = std::max(1, (int(palette.size()) + per_row - 1) / per_row);
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

platform::Image library_sheet(const ShapeLibrary& library, const Palette& palette, int width) {
    struct Place {
        int x, y;
    };
    std::vector<Place> places;
    int x = 2, y = 2, row_height = 0, widest = 4;
    for (const auto& shape : library.shapes) {
        if (x + shape.width() + 2 > width && x > 2) {
            x = 2;
            y += row_height + 2;
            row_height = 0;
        }
        places.push_back({x, y});
        x += shape.width() + 2;
        widest = std::max(widest, x);
        row_height = std::max(row_height, shape.height);
    }
    platform::Image image(widest, y + row_height + 2);
    fill(image, backdrop);
    for (std::size_t s = 0; s < library.shapes.size(); ++s) {
        const auto& shape = library.shapes[s];
        for (int sy = 0; sy < shape.height; ++sy) {
            for (int sx = 0; sx < shape.width(); ++sx) {
                if (shape.covers(sx, sy)) {
                    image.put(places[s].x + sx, places[s].y + sy, colour_of(palette, shape.colour(sx, sy)));
                }
            }
        }
    }
    return image;
}

platform::Image sample_image(const Sample& sample) {
    constexpr int height = 129;
    const int per_column = std::max<int>(1, int(sample.data.size() + 319) / 320);
    const int width = std::max(1, int(sample.data.size() + std::size_t(per_column) - 1) / per_column);
    platform::Image image(width, height);
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

const Palette& find_palette(const Assets& assets, std::string_view name) {
    for (const auto& palette : assets.palettes) {
        if (palette.name == name) return palette.colours;
    }
    for (const auto& picture : assets.pictures) {
        if (picture.name == name) return picture.picture.palette;
    }
    throw std::runtime_error("No palette named " + std::string(name) + ".");
}

} // namespace wings::data
