#include "spindizzy_worlds_amiga/view/playfield.hpp"

namespace spindizzy::view {

std::uint8_t playfield_pixel(const TileMap& map, const data::Graphics& graphics, int x, int y) {
    if (x < 0 || y < 0 || x >= map_width || y >= map_height) return 0;
    const auto layers = map.layers(x / data::tile_width, y / data::tile_height);
    if (layers.empty()) return 0;
    const int tx = x % data::tile_width, ty = y % data::tile_height;
    const auto pixel = [&](const TileLayer& layer) {
        return graphics.tiles[std::size_t(layer.tile)][std::size_t(ty * data::tile_width + tx)];
    };
    // From the nearest layer back: the first whose mask lets it show wins;
    // the farthest layer is drawn whole.
    for (std::size_t i = 0; i + 1 < layers.size(); ++i) {
        if (layers[i].mask == 0) return pixel(layers[i]); // opaque
        const auto& mask = graphics.masks[std::size_t(layers[i].mask)];
        if (!(mask[std::size_t(ty)] >> (15 - tx) & 1)) return pixel(layers[i]);
    }
    return pixel(layers.back());
}

void draw_playfield(Canvas& canvas, const TileMap& map, const data::Graphics& graphics, int left, int top) {
    for (int y = 0; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            canvas.pixels[std::size_t(y * canvas.width + x)] = playfield_pixel(map, graphics, left + x, top + y);
        }
    }
}

} // namespace spindizzy::view
