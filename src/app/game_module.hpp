#pragma once
#include "platform/image.hpp"
#include <string>

// The contract between the Joyport launcher and a running game. The launcher
// knows nothing about a game's objects; a module runs one frame at a time
// and shows its picture on request.
namespace app {

// The player's controls for one frame: the joystick plus the keys the
// collection's games use (C64 function keys and space bar for now).
struct GameInput {
    bool up{}, down{}, left{}, right{}, fire{};
    bool f1{}, f3{}, f5{}, f7{}, space{};
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

    // Why the game stopped (a part not rebuilt yet), or empty while it runs.
    virtual std::string stopped() const { return {}; }
};

} // namespace app
