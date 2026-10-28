#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The picture the 3D view draws into: colour indices 0-15 (the original's
// four bitplanes), and the pens it fills with.
namespace babel::view {

struct Canvas {
    int width{}, height{};
    std::vector<std::uint8_t> pixels;

    Canvas(int w, int h) : width(w), height(h), pixels(std::size_t(w) * std::size_t(h)) {}
    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
    void set(int x, int y, std::uint8_t c) { pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)] = c; }
};

// A pen: a 16 x 2 pattern of colour indices, aligned to the screen (even
// lines use the first row, odd lines the second). The original stores each
// row as four bitplane words; 2 x 2 checks of two colours give its dithers.
struct Pen {
    std::array<std::array<std::uint8_t, 16>, 2> rows{};
    std::uint8_t colour(int x, int y) const { return rows[std::size_t(y & 1)][std::size_t(x & 15)]; }
};

// Reads a pen from its 16 bytes (row 0: planes 0-3 words, row 1 likewise).
Pen read_pen(std::span<const std::uint8_t, 16> bytes);

// The rectangle the view may draw in (inclusive bounds).
struct Window {
    int left{}, right{}, top{}, bottom{};
};

} // namespace babel::view
