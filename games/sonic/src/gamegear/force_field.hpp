#pragma once
#include "gamegear/video.hpp"
#include <vector>

// Widescreen enhancement shared by the Game Gear games: a force field drawn
// where a locked arena's invisible walls fall inside a view larger than the
// LCD. On the LCD those walls are the screen's edges; in a larger view the
// player would otherwise stop against nothing. Purely cosmetic: each game
// reports where its walls are, and nothing here changes them.
namespace gg {

class ForceField {
public:
    struct Wall {
        int x{};         // World X of the boundary line.
        bool right{};    // Arena lies to the left of a right wall.
        int strength{};  // Fade-in, 0..fade_ticks.
        int release{-1}; // Updates since release, or -1 while held.
        int flare{};     // Remaining contact-flare updates.
        int flare_y{};   // World Y of the contact.
    };

    static constexpr int fade_ticks = 24, release_ticks = 56, flare_ticks = 22;

    void clear() { walls_.clear(); }

    // Once per update, before the walls are reported: animation time,
    // flares and releases advance.
    void advance();
    // The wall on one side: held at `x` while `lock`, otherwise released
    // with an outward ripple once its line moves away. Returns the held
    // wall (or nullptr) for touch().
    Wall* side(bool right, bool lock, int x);
    // A contact (the player pushing, a boss crossing) ripples the field at
    // world Y `y`.
    static void touch(Wall& wall, int y);
    // Draws the field over `image`, whose top-left shows world (view_x,
    // view_y).
    void draw(Screen& image, int view_x, int view_y) const;

    // Calls f on each part of the state (for saving and loading).
    template <class F>
    void for_each_state(F&& f) {
        f(walls_);
        f(ticks_);
    }

private:
    std::vector<Wall> walls_;
    unsigned ticks_{};
};

} // namespace gg
