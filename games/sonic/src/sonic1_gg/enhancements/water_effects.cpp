// Labyrinth water enhancement and water splashes.
//
// refract_water() bends the picture below the water line at the game's own low
// resolution (so pixels stay square when scaled), draw_water_surface() replaces
// the surface sprites with a translucent band, and WaterEffects emits droplet
// bursts (gamegear/water_splash) when Sonic enters or leaves water. All of it
// only observes the game; it uses its own deterministic randomness, never the
// game's generator.

#include "sonic1_gg/enhancements/water_effects.hpp"
#include "sonic1_gg/enhancements/basin_water.hpp"
#include <algorithm>
#include <cmath>

namespace sonic1_gg {
namespace {
void blend(gg::Screen& image, int x, int y, platform::Color color, unsigned alpha) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return;
    }
    auto& under = image.pixels[y * image.width + x];
    alpha = alpha * color.a / 255;
    auto channel = [&](unsigned top, unsigned bottom) {
        return std::uint8_t((top * alpha + bottom * (255 - alpha) + 127) / 255);
    };
    under = {channel(color.r, under.r), channel(color.g, under.g), channel(color.b, under.b), 255};
}
} // namespace

bool has_enhanced_water(const Game& game) {
    return game.level.scene == 3 && game.level.has_water();
}

void WaterEffects::reset() {
    splash_.clear();
    primed_ = false;
    previous_dy_ = 0;
}

void WaterEffects::tick(const Game* game, bool enabled, const BasinWater* basins) {
    if (!enabled || !game || (!has_enhanced_water(*game) && (!basins || !basins->has_basins())) ||
        game->sonic.dead || game->ending.sonic_hidden) {
        reset();
        return;
    }
    const auto& g = *game;
    int x = g.sonic.x / 256 + 8, y = g.sonic.y / 256;
    const bool basin = !has_enhanced_water(g) && basins && basins->has_basins();
    // A foot probe also detects the shallow basins, where Sonic cannot submerge
    // his head. This is cosmetic; their original water/terrain rules are untouched.
    if (basin) {
        y += g.sonic.height - 2;
    }
    bool wet = basin ? basins->contains(x, y) : (g.sonic.flags & 16) != 0;
    int surface = basin ? (wet ? basins->surface_at(x, y) : water_) : g.water.y + 13;
    // Loads/retries/warps and initial water placement must never look like a
    // physical crossing. Counter continuity also covers same-act resets.
    if (primed_ && (act_ != g.level.act || g.death_count != deaths_ || g.ticks != ticks_ + 1 ||
                    std::abs(x - x_) > 32 || std::abs(y - y_) > 32 ||
                    (!basin && std::abs(g.water.y + 13 - water_) > 8))) {
        reset();
    }
    splash_.step();
    const int dy = primed_ ? y - y_ : 0;
    // The wet flag is evaluated before player integration. A landing can stop
    // movement on the tick that reports the crossing, so retain one tick of
    // whole-pixel motion. Floor snapping preserves a cycling subpixel byte,
    // even while standing still; that byte must not count as movement.
    const bool moving_through = wet ? (dy > 0 || previous_dy_ > 0) : (dy < 0 || previous_dy_ < 0);
    const bool crossed = basin ? surface >= 0 && (dy != 0 || x != x_) : moving_through;
    if (primed_ && wet != wet_ && crossed && std::abs(y - (basin ? surface : g.water.y)) < 32) {
        splash_.burst(x, surface, x - x_, wet, g.ticks);
    }
    previous_dy_ = dy;
    primed_ = true;
    wet_ = wet;
    act_ = g.level.act;
    x_ = x;
    y_ = y;
    water_ = surface;
    deaths_ = g.death_count;
    ticks_ = g.ticks;
}

void WaterEffects::draw(gg::Screen& image, int cx, int cy) const {
    splash_.draw(image, cx, cy);
}

void refract_water(gg::Screen& image, const Game& game, int cx, int cy) {
    if (!has_enhanced_water(game)) {
        return;
    }
    // Palette transition starts at water_y+13 in the recovered IRQ service.
    const int surface = game.water.y + 13 - cy, first = std::max(0, surface);
    if (first >= image.height) {
        return;
    }
    auto original = image.pixels;
    const double phase = double(game.ticks) * 0.045;
    std::vector<float> vertical(image.width);
    for (int x = 0; x < image.width; ++x) {
        vertical[x] = 0.7f * std::sin((x + cx) * 0.085f + phase * 0.8f);
    }
    for (int y = first; y < image.height; ++y) {
        float fade = std::clamp(float(y - surface) / 5.0f, 0.0f, 1.0f);
        float dx = 1.25f * std::sin((y + cy) * 0.13f + phase) * fade;
        for (int x = 0; x < image.width; ++x) {
            float sx = std::clamp(x + dx, 0.f, float(image.width - 1));
            float sy = std::clamp(y + vertical[x] * fade, float(first), float(image.height - 1));
            int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, image.width - 1),
                y1 = std::min(y0 + 1, image.height - 1);
            float fx = sx - x0, fy = sy - y0;
            auto a = original[y0 * image.width + x0], b = original[y0 * image.width + x1],
                 c = original[y1 * image.width + x0], d = original[y1 * image.width + x1];
            auto channel = [&](auto member) {
                return std::uint8_t(std::lround((a.*member * (1 - fx) + b.*member * fx) * (1 - fy) +
                                                (c.*member * (1 - fx) + d.*member * fx) * fy));
            };
            image.put(x, y,
                      {channel(&platform::Color::r), channel(&platform::Color::g),
                       channel(&platform::Color::b), 255});
        }
    }
}

void draw_water_surface(gg::Screen& image, const Game& game, int cx, int cy) {
    if (!has_enhanced_water(game)) {
        return;
    }
    // Original 8D59 emits tiles 0/2 at water_y+9 (VDP sprite Y adds one).
    // Enhancement draws all positions each frame with alpha instead of cycling
    // two pairs through a sparse six-step hardware sprite list.
    const int y = game.water.y + 10 - cy;
    if (y >= image.height || y + 16 < 0) {
        return;
    }
    const auto palette = gg::decode_palette(game.level.palette);
    const int phase = int(game.ticks / 6) % 16;
    int start = phase - ((cx % 16 + 16) % 16);
    if (start > 0) {
        start -= 16;
    }
    for (int x = start; x < image.width; x += 16) {
        for (int py = 0; py < 16; ++py) {
            for (int px = 0; px < 16; ++px) {
                int tile = (px / 8) * 2 + py / 8;
                auto color = gg::tile_pixel(game.level.object_patterns, tile, px % 8, py % 8);
                if (color) {
                    const gg::Palette* row_palette = &palette;
                    for (const auto& change : image.palette_changes) {
                        if (y + py < change.first) {
                            break;
                        }
                        row_palette = &change.second;
                    }
                    blend(image, x + px, y + py, (*row_palette)[16 + color], 128);
                }
            }
        }
    }
}
} // namespace sonic1_gg
