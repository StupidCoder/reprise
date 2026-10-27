#pragma once
#include "cannon_fodder_amiga/data/assets.hpp"
#include "platform/image.hpp"
#include <cstdint>
#include <vector>

// The play view's background: the mission map seen through the camera
// (.analysis/cannon_fodder_amiga/game.md "View and scrolling").
//
// The original keeps the visible part of the map in a 320 x 256 pixel ring
// buffer and points the display into it (copper list at $3134, updated by
// $86624/$86956), with a fine-scroll delay of 15 - (x & 15). What reaches the
// screen is simple: line l of the 224-line window shows map row
// camera.y + l, and column c shows map column camera.x + c - 31. Columns
// 0-30 show the end of the ring's previous line (map row - 1, columns
// camera.x + 289 ...); the left panel covers columns 0-47 anyway.
// Checked against the original's display on map 2 (camera 431, 255).
namespace fodder::game {

// Map pixel at the view's reference point: the high words of $814AE (x) and
// $814B2 (y).
struct Camera {
    int x{}, y{};
};

constexpr int view_width = 320;
constexpr int view_height = 224;
constexpr int view_column_of_camera = 31; // screen column showing camera.x

// Camera limits ($8682A): x <= map width - 305, y <= map height - 225, both
// >= 0 ($869CE). A map no wider and higher than one screen (19 x 15 tiles,
// $82144) never scrolls.
Camera clamp_camera(Camera camera, int map_width_pixels, int map_height_pixels);
bool map_fits_screen(const data::MissionMap& map);

// The map's pixels as colour indices (base and sub tile sets joined).
class MapPicture {
public:
    MapPicture(const data::MissionMap& map, const std::vector<data::Tile>& base_tiles,
               const std::vector<data::Tile>& sub_tiles);

    int width() const { return width_; }
    int height() const { return height_; }
    // Colour index at a map pixel; 0 outside the map (what the original's
    // ring holds there is not known yet).
    std::uint8_t at(int x, int y) const;

private:
    int width_{}, height_{};
    std::vector<std::uint8_t> pixels_;
};

// The 320 x 224 window as colour indices, row by row.
std::vector<std::uint8_t> view_indices(const MapPicture& map, Camera camera);
platform::Image view_image(const MapPicture& map, Camera camera, const data::Palette& palette);

} // namespace fodder::game
