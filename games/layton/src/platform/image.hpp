#pragma once
// A plain RGBA pixel buffer. The shell draws its screens (library, puzzle
// collection, toolbar, dialogs) into Images in software; the host uploads
// them to textures and puts them on the screen.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace picarat::platform {

// One pixel, byte order R, G, B, A in memory. Alpha 255 is opaque.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels; // row by row, top row first

    Image() = default;

    Image(int w, int h) : width(w), height(h), pixels(std::size_t(w) * std::size_t(h)) {}

    // Writes one pixel; coordinates outside the image are ignored.
    void put(int x, int y, Color color) {
        if (x >= 0 && y >= 0 && x < width && y < height) {
            pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] = color;
        }
    }

    Color at(int x, int y) const {
        return pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)];
    }
};

} // namespace picarat::platform
