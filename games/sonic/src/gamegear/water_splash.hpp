#pragma once
#include "gamegear/video.hpp"
#include <cstdint>
#include <vector>

// Water enhancement shared by the Game Gear games: droplet bursts where the
// player enters or leaves water. Each game detects the crossing and reports
// it with burst(); this class only animates and draws the droplets. Purely
// cosmetic, with its own deterministic randomness, never a game's generator.
namespace gg {

class WaterSplash {
public:
    struct Particle {
        float x, y, vx, vy;
        unsigned age;
        unsigned delay; // ticks before this droplet leaves the surface
        unsigned life;  // total ticks including delay
        unsigned size;  // 0 = single pixel, 1 = small drop, 2 = full drop
        float surface;  // falling droplets rejoin the water here
    };

    // Droplets emitted per crossing; maximum lifetime stays below 40 ticks.
    static constexpr unsigned burst_size = 16;

    void clear() { particles_.clear(); }

    // Once per update: the droplets move, fall and expire.
    void step();
    // A crossing at world X `x` through the surface at world Y `surface`:
    // `wet` when entering the water, `drift` the player's horizontal motion
    // in pixels this update (skews the spray), `ticks` the game's update
    // counter (varies the burst).
    void burst(int x, int surface, int drift, bool wet, std::uint32_t ticks);
    // Draws the droplets over `image`, whose top-left shows world (view_x,
    // view_y).
    void draw(Screen& image, int view_x, int view_y) const;

    // Calls f on each part of the state (for saving and loading).
    template <class F>
    void for_each_state(F&& f) {
        f(particles_);
    }

private:
    std::vector<Particle> particles_;
};

} // namespace gg
