#include "fort_apocalypse_c64/enhanced/world.hpp"
#include "c64/palette.hpp"

namespace fort_apocalypse::enhanced {
int wrap_world_x(int x) { return (x % WorldScene::width + WorldScene::width) % WorldScene::width; }

platform::Image world_image(const WorldScene& scene) {
    platform::Image image(WorldScene::width, WorldScene::height);
    std::vector<bool> foreground(image.pixels.size());
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto& cell = scene.cells[std::size_t((y / 8) * WorldScene::columns + x / 8)];
            const int pair = (cell.glyph[std::size_t(y % 8)] >> (6 - (x % 8 / 2) * 2)) & 3;
            const auto at = std::size_t(y * image.width + x);
            image.pixels[at] = c64::colour(scene.colours[std::size_t(pair)]);
            foreground[at] = pair >= 2; // original multicolour sprite priority
        }
    }
    // Original sprite priority: the player's craft is in front of other sprites.
    for (auto it = scene.sprites.rbegin(); it != scene.sprites.rend(); ++it) {
        const auto& sprite = *it;
        for (int y = 0; y < sprite.height; ++y) {
            const int target_y = sprite.y + y;
            if (target_y < 0 || target_y >= image.height) continue;
            for (int x = 0; x < sprite.width; ++x) {
                if (!sprite.mask[std::size_t(y * sprite.width + x)]) continue;
                const auto at = std::size_t(target_y * image.width + wrap_world_x(sprite.x + x));
                if (!foreground[at]) image.pixels[at] = c64::colour(sprite.colour);
            }
        }
    }
    return image;
}
} // namespace fort_apocalypse::enhanced
