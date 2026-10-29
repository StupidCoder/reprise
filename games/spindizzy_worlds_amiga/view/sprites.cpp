#include "spindizzy_worlds_amiga/view/sprites.hpp"

namespace spindizzy::view {

SpritePlace project(const WorldPosition& p) {
    const auto x = std::int16_t(p.x), y = std::int16_t(p.y), z = std::int16_t(p.z);
    // The key uses whole cells for x and y and half blocks for z (clr.b,
    // and.b #$80); 16-bit wraparound as in the original.
    const int cells = (x & 0xFF00) + (y & 0xFF00);
    const auto key = std::uint16_t(2 * cells + (z & 0xFF80) + 0x4000);
    const int screen_x = 0x200 + (std::int16_t(y - x) >> 4);
    const int screen_y = 0x48 + (std::int16_t(x + y - z) >> 5);
    return {screen_x, screen_y, key};
}

int sort_value(const WorldPosition& p) {
    const int cells = (p.x & 0xFF00) + (p.y & 0xFF00);
    const int height = std::int16_t((std::int16_t(p.z) & 0xFF80) + 0x4000) >> 4;
    return std::int16_t(cells + height);
}

std::uint32_t explorer_frame(int counter, bool flash_phase, bool special_state) {
    std::uint32_t frame = explorer_frames + std::uint32_t(counter & 0xC) * 0x28;
    if (special_state) return frame + 0xC80;
    if (flash_phase) frame += 0x280;
    return frame;
}

SpritePlace explorer_place(const WorldPosition& p) {
    auto place = project(p);
    place.x -= 8;
    place.y -= 16;
    return place;
}

bool sprite_visible(const TileMap& map, const data::Graphics& graphics, int map_x, int map_y, std::uint16_t key) {
    if (map_x < 0 || map_y < 0) return true;
    const auto layers = map.layers(map_x / data::tile_width, (map_y / data::tile_height) & (map_rows - 1));
    for (const auto& layer : layers) {
        const auto layer_key = std::uint16_t(layer.depth << 8 | layer.mask);
        if (key >= layer_key) break; // the rest are behind the sprite
        if (layer.mask == 0) return false;
        const auto& mask = graphics.masks[std::size_t(layer.mask)];
        if (!(mask[std::size_t(map_y % data::tile_height)] >> (15 - map_x % data::tile_width) & 1)) return false;
    }
    return true;
}

void draw_sprite(Canvas& canvas, const data::SpriteFrame& frame, const SpritePlace& place, const TileMap& map,
                 const data::Graphics& graphics, int left, int top) {
    for (int y = 0; y < frame.height; ++y) {
        const int cy = place.y + y - top;
        if (cy < 0 || cy >= canvas.height) continue;
        for (int x = 0; x < frame.width; ++x) {
            const int cx = place.x + x - left;
            if (cx < 0 || cx >= canvas.width) continue;
            const auto at = std::size_t(y * frame.width + x);
            if (!frame.opaque[at]) continue;
            const int mask_y = place.y + y + place.mask_dy;
            const int mask_x = place.x + x + place.mask_dx;
            if (!sprite_visible(map, graphics, mask_x, mask_y, place.key_at(mask_x, mask_y))) continue;
            canvas.pixels[std::size_t(cy * canvas.width + cx)] = frame.pixels[at];
        }
    }
}

} // namespace spindizzy::view
