#pragma once
#include "platform/image.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// The contract between the BlueStreak shell and a game. The shell knows
// nothing about any game's objects or cartridge addresses; a game module runs
// one fixed-length update at a time and renders its frame on request.
namespace shell {

// Controller state for one update. `jump` is the first action button (Game
// Gear button 1, GBA A), `action` the second (Game Gear button 2, GBA B).
// The Game Gear games jump with either one.
struct GameInput {
    bool left{}, right{}, jump{}, down{}, up{}, start{}, action{};
};

// The optional enhancements, each switched independently. All off is the
// original game.
struct Enhancements {
    bool widescreen{};          // A view larger than the Game Gear screen
    bool shield_transparency{}; // A translucent shield instead of flicker
    bool water{};               // Water refraction, splashes and waterfalls
    bool rings{};               // Scattered rings that bounce
    bool lcd_screen{};          // The Game Gear LCD look (applied by the host)
};

// One switchable enhancement, as listed in a game's enhancements menu.
enum class Enhancement { shield_transparency, widescreen, water, rings, lcd_screen };

// The setting behind an enhancement.
inline bool& setting(Enhancements& e, Enhancement which) {
    switch (which) {
    case Enhancement::shield_transparency:
        return e.shield_transparency;
    case Enhancement::widescreen:
        return e.widescreen;
    case Enhancement::water:
        return e.water;
    case Enhancement::rings:
        return e.rings;
    case Enhancement::lcd_screen:
        break;
    }
    return e.lcd_screen;
}

// The settings with everything a game does not offer switched off.
inline Enhancements offered(Enhancements e, const std::vector<Enhancement>& available) {
    Enhancements result;
    for (const auto which : available) {
        setting(result, which) = setting(e, which);
    }
    return result;
}

class GameModule {
public:
    virtual ~GameModule() = default;

    // Length of one update in seconds (one video frame of the original).
    virtual double step_seconds() const = 0;
    // One update; returns its audio as interleaved stereo samples.
    virtual std::vector<float> tick(GameInput, const Enhancements&) = 0;
    // The current frame, sized for the canvas when widescreen is on.
    virtual platform::Image render(const Enhancements&, int canvas_width,
                                   int canvas_height) const = 0;
    // Presented view for the next updates. A widescreen view changes object
    // activation and some limits, so the module must know it before ticking,
    // not only when rendering.
    virtual void set_view(bool widescreen, int canvas_width, int canvas_height) = 0;

    // True while an act is being played (not menus, demos or cut scenes).
    virtual bool gameplay() const = 0;
    // A short description of what is running, for the pause menu.
    virtual std::string status() const = 0;

    // Why the game stopped (a part not reconstructed yet), or empty while it
    // runs; the shell shows it at once.
    virtual std::string stopped() const { return {}; }

    virtual void restart() = 0;
    virtual std::vector<std::uint8_t> save_state() = 0;
    virtual std::uint64_t ticks() const = 0;
};

} // namespace shell
