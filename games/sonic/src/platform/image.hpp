#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform {

// One RGBA pixel. The byte order matches SDL_PIXELFORMAT_RGBA32 on every host,
// so an Image can be uploaded to a texture without conversion.
struct Color {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
};

// A plain RGBA pixel buffer. Every game module renders into one of these, and the
// host scales and presents it. Console-specific concepts such as palettes or raster
// effects belong to the console layer (see gamegear/video.hpp), not here.
struct Image {
    int width;
    int height;
    std::vector<Color> pixels;

    Image(int w, int h) : width(w), height(h), pixels(std::size_t(w) * std::size_t(h)) {}

    // Writes one pixel; coordinates outside the image are silently clipped.
    void put(int x, int y, Color color) {
        if (x >= 0 && y >= 0 && x < width && y < height) {
            pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] = color;
        }
    }
};

} // namespace platform
