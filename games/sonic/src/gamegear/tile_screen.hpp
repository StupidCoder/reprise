#pragma once
#include "gamegear/video.hpp"

namespace gg {
// One 8x16 sprite: position in the VDP's 256x192 picture and its first tile.
struct TileSprite {
    int x{}, y{};
    unsigned tile{};
};

// A complete Game Gear screen as the VDP would show it: tile patterns, the
// 32x28 name table (two bytes per entry), 64 bytes of palette and a sprite
// list. Used for frontend screens (logo, title, map, results); render() returns
// the 160x144 LCD area.
struct TileScreen {
    std::vector<std::uint8_t> patterns, tiles, palette;
    std::vector<TileSprite> sprites;
    bool enabled{true};
    int scroll_y{}; // VDP register 9: background rows scroll up; sprites do not.
    Screen render() const;
};
} // namespace gg
