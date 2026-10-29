#pragma once
#include "gamegear/video.hpp"
#include "gamegear/water_splash.hpp"
#include "sonic1_gg/game/game.hpp"

namespace sonic1_gg {
class BasinWater;

// Cosmetic state only. Never serialized into the original game's state or RNG.
class WaterEffects {
public:
    void reset();
    void tick(const Game* game, bool enabled, const BasinWater* basins = nullptr);
    void draw(gg::Screen&, int camera_x, int camera_y) const;

private:
    gg::WaterSplash splash_;
    bool primed_{}, wet_{};
    unsigned act_{};
    int x_{}, y_{}, water_{}, deaths_{};
    int previous_dy_{};
    unsigned ticks_{};
};

bool has_enhanced_water(const Game&);
void refract_water(gg::Screen&, const Game&, int camera_x, int camera_y);
void draw_water_surface(gg::Screen&, const Game&, int camera_x, int camera_y);
} // namespace sonic1_gg
