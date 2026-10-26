#pragma once
#include "platform/image.hpp"
#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

// The contract between the Joyport launcher and a running game. The launcher
// knows nothing about a game's objects; a module runs one frame at a time
// and shows its picture on request.
namespace app {

// The player's controls for one frame: the joystick plus the keys the
// collection's games use (C64 function keys and space bar for now).
struct GameInput {
    bool up{}, down{}, left{}, right{}, fire{};
    bool f1{}, f3{}, f5{}, f7{}, space{};
    // A second joystick (a second gamepad or the numeric keypad), for games
    // with a second player at the same time.
    bool up2{}, down2{}, left2{}, right2{}, fire2{};
};

// A setting the player can change in the pause menu (a fix or an
// enhancement), with its choices; 0 is always the original behaviour.
struct GameOption {
    std::string name;
    std::vector<std::string> choices;
    int value{};
    std::string description; // shown under the list
    // Options with a group (such as "Cheats") are listed on a page of their
    // own, reached from the options page.
    std::string group{};
};

class GameModule {
public:
    virtual ~GameModule() = default;

    // Length of one frame of the original, in seconds.
    virtual double step_seconds() const = 0;
    // Runs one frame.
    virtual void tick(const GameInput&) = 0;
    // The picture of the current frame, borders included.
    virtual platform::Image picture() const = 0;
    // Displayed width / height of one picture pixel.
    virtual double pixel_aspect() const = 0;
    // The window's drawing area, for a game whose view can widen to it.
    virtual void set_canvas(int /*width*/, int /*height*/) {}

    // Interleaved stereo samples for the time of the last tick.
    virtual void audio(std::span<float> stereo, int sample_rate) {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        (void)sample_rate;
    }

    // Quick save and load (kept while the game runs).
    virtual bool save() { return false; }
    virtual bool load() { return false; }
    virtual bool has_save() const { return false; }

    virtual std::vector<GameOption> options() const { return {}; }
    virtual void set_option(std::size_t index, int value) {
        (void)index;
        (void)value;
    }

    // Why the game stopped (a part not rebuilt yet), or empty while it runs.
    virtual std::string stopped() const { return {}; }
};

} // namespace app
