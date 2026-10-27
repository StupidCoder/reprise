#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include <algorithm>

namespace fodder::game {

Camera clamp_camera(Camera camera, int map_width_pixels, int map_height_pixels) {
    // $8682A limits the step so the camera stays at or below these; $869CE
    // stops it at 0.
    camera.x = std::max(0, std::min(camera.x, map_width_pixels - 305));
    camera.y = std::max(0, std::min(camera.y, map_height_pixels - 225));
    return camera;
}

bool map_fits_screen(const data::MissionMap& map) {
    return map.width == 19 && map.height == 15; // the test at $8617E
}

MapPicture::MapPicture(const data::MissionMap& map, const std::vector<data::Tile>& base_tiles,
                       const std::vector<data::Tile>& sub_tiles)
    : width_(map.width * 16), height_(map.height * 16), pixels_(std::size_t(width_ * height_)) {
    for (int ty = 0; ty < map.height; ++ty) {
        for (int tx = 0; tx < map.width; ++tx) {
            const auto index = std::size_t(map.tile(tx, ty));
            const data::Tile* tile = index < base_tiles.size() ? &base_tiles[index]
                                     : index - base_tiles.size() < sub_tiles.size()
                                         ? &sub_tiles[index - base_tiles.size()]
                                         : nullptr;
            if (tile == nullptr) {
                continue; // tile 399 on map 31: one past the sub set (open)
            }
            for (int y = 0; y < 16; ++y) {
                std::copy_n(tile->pixels.begin() + y * 16, 16,
                            pixels_.begin() + std::ptrdiff_t((ty * 16 + y) * width_ + tx * 16));
            }
        }
    }
}

void MapPicture::set_tile(int tx, int ty, int tile, const std::vector<data::Tile>& base_tiles,
                          const std::vector<data::Tile>& sub_tiles) {
    const auto index = std::size_t(tile & 0x1ff);
    const data::Tile* t = index < base_tiles.size()                       ? &base_tiles[index]
                          : index - base_tiles.size() < sub_tiles.size() ? &sub_tiles[index - base_tiles.size()]
                                                                         : nullptr;
    if (t == nullptr || tx < 0 || ty < 0 || (tx + 1) * 16 > width_ || (ty + 1) * 16 > height_) return;
    for (int y = 0; y < 16; ++y) {
        std::copy_n(t->pixels.begin() + y * 16, 16, pixels_.begin() + std::ptrdiff_t((ty * 16 + y) * width_ + tx * 16));
    }
}

std::uint8_t MapPicture::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return 0;
    }
    return pixels_[std::size_t(y * width_ + x)];
}

std::vector<std::uint8_t> view_indices(const MapPicture& map, Camera camera) {
    std::vector<std::uint8_t> out(std::size_t(view_width * view_height));
    for (int line = 0; line < view_height; ++line) {
        for (int column = 0; column < view_width; ++column) {
            int x = camera.x + column - view_column_of_camera, y = camera.y + line;
            if (column < view_column_of_camera) { // the ring's previous line
                x += view_width;
                y -= 1;
            }
            out[std::size_t(line * view_width + column)] = map.at(x, y);
        }
    }
    return out;
}

platform::Image view_image(const MapPicture& map, Camera camera, const data::Palette& palette) {
    const auto indices = view_indices(map, camera);
    platform::Image image(view_width, view_height);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        image.pixels[i] = indices[i] < palette.size() ? data::to_color(palette[indices[i]]) : platform::Color{};
    }
    return image;
}

} // namespace fodder::game
