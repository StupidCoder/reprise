#pragma once
#include "app/game_entry.hpp"
#include "app/quick_save.hpp"
#include "ui/control_icons.hpp"
#include "ui/shelf.hpp"

namespace app {
struct FeatureBadge { platform::ControlIcon icon; const char* label; };
inline std::vector<FeatureBadge> feature_badges(const GameEntry& game) {
    if (game.integration_pending || !game.start) return {};
    using I = platform::ControlIcon;
    std::vector<FeatureBadge> result;
    // Stable visual order, even when modules declare features in another order.
    const std::pair<GameFeature, FeatureBadge> all[] = {
        {GameFeature::widescreen, {I::widescreen, "Widescreen"}},
        {GameFeature::high_framerate, {I::high_framerate, "High framerate"}},
        {GameFeature::cheats, {I::cheats, "Cheats"}},
        {GameFeature::extra_modes, {I::extra_modes, "Extra modes"}},
        {GameFeature::touch, {I::touch, "Touch"}},
        {GameFeature::tilt, {I::tilt, "Tilt"}},
    };
    for (const auto& [feature, badge] : all)
        if (std::find(game.features.begin(), game.features.end(), feature) != game.features.end())
            result.push_back(badge);
    if (persistent_saves(game.id)) result.push_back({I::quick_save, "Quick save"});
    return result;
}
// Informational chips (not buttons). Return the consumed height for flowing portrait text.
inline int draw_feature_badges(platform::Image& image, const GameEntry& game,
                               int x, int y, int width, platform::Color ink,
                               platform::Color background) {
    int left = x, top = y;
    const auto badges = feature_badges(game);
    for (const auto& badge : badges) {
        const int w = ui::shelf::width(badge.label, 14) + 42;
        if (left > x && left + w > x + width) { left = x; top += 38; }
        ui::rounded(image, left, top, w, 32, 8, background);
        const auto icon = ui::control_icon(badge.icon, 22, ink);
        for (int row = 0; row < 22; ++row) for (int col = 0; col < 22; ++col) {
            const auto pixel = icon.pixels[std::size_t(row * 22 + col)];
            if (pixel.a) ui::blend_rect(image, left + 7 + col, top + 5 + row, 1, 1, pixel, pixel.a);
        }
        ui::shelf::text(image, badge.label, left + 34, top + 8, 14, ink);
        left += w + 8;
    }
    return badges.empty() ? 0 : top - y + 32;
}
} // namespace app
