#pragma once
#include "bruce_lee_c64/game/game.hpp"
#include "c64/text_frame.hpp"
#include <array>
#include <cstdint>
#include <random>
#include <vector>

// The optional visual upgrades: a presentation layer that draws the play
// screen again from the game's state. Smooth motion lets the actors and
// room 18's fireballs glide over the three frames between the game's
// steps; particle effects add sparks where blows land, a burst of light
// when a lantern is taken, the pieces of a crumbling floor and embers behind
// the fireballs. Everything stays on the C64's pixel grid and in its 16
// colours, and only shows what the game itself does: the rules, the
// collisions and the sounds are the original's (the game keeps its own
// picture for its collision checks).
namespace bruce_lee::enhanced {

struct Settings {
    bool smooth{};
    bool particles{};
};

class Effects {
public:
    // Once per frame, after the game's tick.
    void update(const game::Game&, Settings);
    const c64::Picture& picture() const { return picture_; }

private:
    struct Glide {
        bool valid{};
        double x{}, y{};          // shown place (pixel pairs, lines)
        double vx{}, vy{};
        int frames_left{};
        std::uint8_t target_x{}, target_y{};
    };
    struct Particle {
        double x{}, y{};          // picture pixels
        double vx{}, vy{}, gravity{};
        int age{}, life{};
        const std::uint8_t* colours{}; // fading colours, over the particle's life
        int colour_count{};
    };
    void glide(Glide&, bool present, std::uint8_t x, std::uint8_t y, bool snap);
    void events(const game::World&);
    void spark(double x, double y, int count);
    void burst(double x, double y);
    void fragments(double x, double y, std::uint8_t colour);
    void ember(double x, double y);
    void step_particles();
    void draw_particles();
    double random(double low, double high);

    std::array<Glide, 3> actors_{};
    std::array<Glide, 2> fireballs_{};
    std::vector<Particle> particles_;
    std::mt19937 rng_{1984};

    // What the last frame showed, for spotting the game's events.
    bool known_{};
    int room_{-1};
    std::vector<bool> lanterns_;
    std::array<std::uint8_t, 3> moves_{};
    std::array<std::uint8_t, 3> crumble_timers_{};
    std::uint8_t fragment_colour_{};

    c64::Picture picture_;
};

} // namespace bruce_lee::enhanced
