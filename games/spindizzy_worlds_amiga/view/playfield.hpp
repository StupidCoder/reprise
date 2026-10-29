#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/tile_map.hpp"
#include <cstdint>
#include <stdexcept>
#include <vector>

// Drawing the playfield from its tile map into an indexed picture of any
// size: the camera is a pixel position on the 1024 × 1024 map, so a wider
// screen simply shows more of it.
namespace spindizzy::view {

struct Canvas {
    int width{}, height{};
    std::vector<std::uint8_t> pixels; // colour indices 0-15

    Canvas(int w, int h) : width(w), height(h), pixels(std::size_t(w * h), 0) {}
    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y * width + x)]; }
    template <class A> void persist(A& ar) {
        ar(width, height, pixels);
        if (pixels.size() != std::size_t(width) * std::size_t(height)) throw std::runtime_error("Spindizzy Worlds: a picture in the save has the wrong size.");
    }
};

// The map pixel at (x, y): $120CC composes a cell from its farthest layer
// (copied whole) and the nearer ones blitted through their masks (minterm
// $AC: where the mask bit is set, what is behind stays). Outside the map
// and in empty cells: colour 0.
std::uint8_t playfield_pixel(const TileMap&, const data::Graphics&, int x, int y);

// The map region with its top-left corner at (left, top) into the canvas.
// The original shows left = $2570(a5) − 16, top = $2572(a5) (one frame
// checked).
void draw_playfield(Canvas&, const TileMap&, const data::Graphics&, int left, int top);

} // namespace spindizzy::view
