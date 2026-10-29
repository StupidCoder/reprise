// Composition of a gameplay frame, back to front.

#include "sonic1_gg/enhancements/basin_water.hpp"
#include "sonic1_gg/enhancements/ring_effects.hpp"
#include "sonic1_gg/enhancements/water_effects.hpp"
#include "sonic1_gg/enhancements/widescreen.hpp"
#include "sonic1_gg/render/scene.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace sonic1_gg {

gg::Screen render_game_view(const Game& game, bool expanded, bool shield_transparency,
                            bool enhanced_water, const WaterEffects* effects, int canvas_width,
                            int canvas_height, const RingEffects* rings, const BasinWater* basins,
                            const ArenaBarrier* barrier, const ObjectFades* fades) {
    // The waterfall/basin enhancement only exists for Green Hill and Jungle.
    std::optional<BasinWater> local_basins;
    if (enhanced_water && !basins && (game.level.scene == 0 || game.level.scene == 2)) {
        local_basins.emplace(game.level);
        basins = &*local_basins;
    }
    if (!enhanced_water || (basins && basins->empty())) {
        basins = nullptr;
    }

    const auto size =
        expanded ? expanded_view_size(canvas_width, canvas_height) : std::array{160, 144};
    gg::Screen image(size[0], size[1]);
    // The LCD shows a crop of the VDP's 256x192 picture. Larger views expand
    // around that crop, clamped to the area the LCD could ever show
    // (widescreen.cpp); the game receives the same size for its margins.
    const auto view = view_rect(game, image.width, image.height);
    const int x = view.x;
    const int y = view.y;

    // 1. Background: Labyrinth's mid-frame water palette, then the level tiles.
    configure_water_palette(image, game, y);
    draw_level(image, game.level, x, y, basins);
    if (basins) {
        basins->draw_falls(image, game, x, y, false);
    }
    draw_flowers(image, game, x, y);

    // 2. Sonic. While the lost rings are animating he blinks. With a shield,
    //    even updates show the shield instead of Sonic (4EA7 -> 5487); the
    //    translucent-shield enhancement shows both every update.
    if (!game.ending.sonic_hidden && !(game.sonic.lost_ring_guard && (game.ticks & 1))) {
        const int px = game.sonic.x / 256 - x;
        const int py = game.sonic.y / 256 - y + 1;
        const bool shield = game.sonic.shield && !game.sonic.lost_ring_guard;
        if (!shield || shield_transparency || (game.ticks & 1)) {
            draw_player(image, game.level, game.sonic.frame, game.sonic.facing_left(), px, py);
        }
        if (shield && (shield_transparency || !(game.ticks & 1))) {
            draw_shield(image, game, px, py, shield_transparency);
        }
    }

    // 3. Sprites. Stars and the ring sparkle are queued during Sonic's update,
    //    ahead of his own sprites, so they win overlapping pixels.
    draw_player_effects(image, game, x, y);
    draw_objects(image, game, x, y, enhanced_water && has_enhanced_water(game), rings != nullptr,
                 fades);
    if (rings) {
        rings->draw(image, game, x, y);
    }
    if (enhanced_water) {
        draw_water_surface(image, game, x, y);
    }

    // 4. Foreground: background tiles with the priority bit cover sprites.
    draw_foreground(image, game.level, x, y, basins);
    if (basins) {
        basins->draw_falls(image, game, x, y, true);
        basins->draw(image, game, x, y);
    }

    // 5. Enhancement overlays, then the HUD on top.
    if (enhanced_water) {
        refract_water(image, game, x, y);
        if (effects) {
            effects->draw(image, x, y);
        }
    }
    if (barrier) {
        barrier->draw(image, game, x, y);
    }
    draw_hud(image, game);
    return image;
}

} // namespace sonic1_gg
