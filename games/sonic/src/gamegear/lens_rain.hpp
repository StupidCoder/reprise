#pragma once
#include "gamegear/video.hpp"
#include <cstdint>
#include <vector>

// Water enhancement shared by the Game Gear games: rain on the camera lens.
// Drops land on the view, cling to it, and once heavy enough slide down in
// stop-and-go runs, swallowing the drops in their way and leaving a trail of
// residue that dries up. Drops and trail refract the picture behind them at
// the game's own resolution. Each game reports how hard it rains; the drops
// live in view pixels and never touch the game. Deterministic: its own hash,
// never a game's generator.
namespace gg {

class LensRain {
public:
    struct Drop {
        float x, y;     // centre, view pixels
        float r;        // radius in pixels
        float vy;       // sliding speed, pixels per update
        unsigned cling; // updates before a heavy drop starts to slide
        unsigned age;
        std::uint32_t seed;
    };

    // Residue left behind a sliding drop: one bead per pixel row it crossed.
    struct Bead {
        float x, y;
        float w;       // half-width in pixels
        unsigned life; // updates until dry
    };

    void clear();

    bool empty() const { return drops_.empty() && beads_.empty(); }

    // Once per update. `rain` is how hard it rains (0 dry, 1 full), `width`
    // and `height` the size of the presented view (the drops move with it
    // when it changes), `time` an update counter.
    void step(float rain, int width, int height, std::uint64_t time);
    // Draws the drops over `image` (the presented view; scaled if its size
    // differs from the one given to step()).
    void draw(Screen& image) const;

    // Calls f on each part of the state (for saving and loading).
    template <class F>
    void for_each_state(F&& f) {
        f(drops_);
        f(beads_);
        f(width_);
        f(height_);
    }

private:
    std::vector<Drop> drops_;
    std::vector<Bead> beads_;
    int width_{160}, height_{144};
};

} // namespace gg
