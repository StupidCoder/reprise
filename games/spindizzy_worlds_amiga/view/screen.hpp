#pragma once
#include "platform/image.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include <array>
#include <cstdint>

// The whole picture as the Amiga shows it: 320 × 200 (the display window
// $2C81 … $F4C1, $10D2E), four bitplanes in colours 0-15 and the hardware
// sprites (the pillars) in colours 16-31.
namespace spindizzy::view {

struct Screen : Canvas {
    std::array<data::Colour, 32> palette{}; // Amiga 12-bit colours

    Screen() : Canvas(320, 200) {}
    Screen(int width, int height) : Canvas(width, height) {} // widescreen
    void put(int x, int y, std::uint8_t colour) {
        if (x >= 0 && y >= 0 && x < width && y < height) pixels[std::size_t(y * width + x)] = colour;
    }
    void fill(std::uint8_t colour) { pixels.assign(pixels.size(), colour); }
    // Copies a canvas into the screen with its top-left corner at (x, y).
    void paste(const Canvas&, int x, int y);
    platform::Image image() const;
    template <class A> void persist(A& ar) {
        Canvas::persist(ar);
        ar(palette);
    }
};

} // namespace spindizzy::view
