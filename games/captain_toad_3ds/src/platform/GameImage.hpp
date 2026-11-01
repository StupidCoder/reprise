// =============================================================================================
// Image.hpp - a plain RGBA pixel buffer
// =============================================================================================
//
// The shell draws its screens (launcher, help bar) into Images; the host uploads and presents
// them. Captures of the 3D view are read back into Images and saved as PNG.
// =============================================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace captain_toad::platform {

// One RGBA pixel, byte order R, G, B, A in memory.
struct Color {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    Image() = default;
    Image(int w, int h) : width(w), height(h), pixels(std::size_t(w) * std::size_t(h)) {}

    // Writes one pixel; coordinates outside the image are silently clipped.
    void put(int x, int y, Color color) {
        if (x >= 0 && y >= 0 && x < width && y < height) {
            pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] = color;
        }
    }

    Color at(int x, int y) const { return pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
};

}   // namespace captain_toad::platform
