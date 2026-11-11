#pragma once
#include "c64/text_frame.hpp"
#include <array>
#include <vector>

namespace fort_apocalypse::enhanced {

// Owned presentation data. No references into the simulation, no XR resources.
// Map-backed actors are already in cells and must not be drawn a second time.
struct WorldScene {
    static constexpr int columns = 215, rows = 40, width = columns * 8, height = rows * 8;
    struct Cell { c64::Glyph glyph{}; };
    struct Sprite {
        int x{}, y{}, width{}, height{};
        std::uint8_t colour{};
        std::vector<bool> mask;
    };
    std::array<Cell, columns * rows> cells{};
    std::array<std::uint8_t, 4> colours{};
    std::vector<Sprite> sprites;
    double focus_x{}; // normalized horizontal position, [0,1)
    int level{}, alias_disagreements{};
};

int wrap_world_x(int x);
platform::Image world_image(const WorldScene&);

} // namespace fort_apocalypse::enhanced
