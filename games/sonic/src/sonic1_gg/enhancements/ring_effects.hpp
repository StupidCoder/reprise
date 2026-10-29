#pragma once
#include "gamegear/video.hpp"
#include "sonic1_gg/game/game.hpp"

namespace sonic1_gg {
// Cosmetic particles: independent of original object slots, RNG and saves.
class RingEffects {
public:
    struct Ring {
        Body body;
        unsigned age{}, phase{};
    };

    void reset();
    void tick(const Game*, bool enabled);
    void draw(gg::Screen&, const Game&, int camera_x, int camera_y) const;

private:
    std::vector<Ring> rings_;
    bool primed_{};
    unsigned act_{}, ticks_{};
    int deaths_{}, x_{}, y_{};
};
} // namespace sonic1_gg
