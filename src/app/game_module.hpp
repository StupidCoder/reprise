#pragma once
#include "platform/image.hpp"
#include "ui/vhs.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <stdexcept>
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
    // The C64 keyboard matrix, held (index row * 8 + column; see Controls).
    std::array<bool, 64> c64_keys{};
    // The same keys while Ctrl (not Cmd) is held - the letters and digits
    // except Joyport's own shortcuts Ctrl-Q, Ctrl-S and Ctrl-L - for games
    // whose keys are Ctrl combinations.
    std::array<bool, 64> ctrl_keys{};
    // The mouse, for games played with one: whether it is over the game's
    // picture, where (in pixels of the game's own picture, picture(), not of
    // the larger one shown with the launcher's effects), and the left button
    // held.
    bool pointer{};
    int pointer_x{}, pointer_y{};
    bool mouse_button{};
    bool mouse_right{}; // the right button
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
    // The picture of the current frame, borders included, at the game's own
    // resolution (the launcher adds the VHS look; see shown_picture).
    virtual platform::Image picture() const = 0;
    // The VHS look the player chose in the game's options (0 off, up to
    // ui::vhs_levels - 1); the launcher draws it over picture().
    virtual int vhs_level() const { return 0; }
    // True for a game that draws its own mouse pointer: the system's is
    // hidden over its picture, so that only one shows.
    virtual bool draws_pointer() const { return false; }
    // Displayed width / height of one picture pixel.
    virtual double pixel_aspect() const = 0;
    // The window's drawing area, for a game whose view can widen to it.
    virtual void set_canvas(int /*width*/, int /*height*/) {}
    // How far the time shown lies past the last tick, as a fraction of a
    // tick (0 .. 1), set before picture(): a game that draws motion between
    // its own frames (smooth motion) can show it at the display's rate.
    virtual void set_frame_fraction(double /*fraction*/) {}

    // Interleaved stereo samples for the time of the last tick.
    virtual void audio(std::span<float> stereo, int sample_rate) {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        (void)sample_rate;
    }

    // Quick save/load; the application wrapper persists supported modules to disk.
    // Portable state for disk saves. Empty means this module is not ready yet.
    virtual std::vector<std::uint8_t> write_state() { return {}; }
    // Called on a fresh instance; a failed decode is discarded by the host.
    virtual void read_state(std::span<const std::uint8_t>) {
        throw std::runtime_error("Persistent saves are not available for this game yet.");
    }
    virtual std::string save_error() const { return {}; }
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

// The picture the player sees: the game's own picture with the launcher's
// effects on top (the VHS look, which doubles its size). `frame` counts the
// frames run, for the look's moving noise.
struct ShownPicture {
    platform::Image image{1, 1};
    int picture_width{1}, picture_height{1}; // the game's own picture

    // A point over the shown picture (as the host reports the mouse) in
    // pixels of the game's own picture.
    std::array<int, 2> to_picture(std::array<int, 2> point) const {
        return {point[0] * picture_width / std::max(image.width, 1),
                point[1] * picture_height / std::max(image.height, 1)};
    }
};

inline ShownPicture shown_picture(const GameModule& game, long frame) {
    auto picture = game.picture();
    const int width = std::max(picture.width, 1), height = std::max(picture.height, 1);
    const int level = game.vhs_level();
    if (level > 0) picture = ui::vhs(picture, frame, level);
    return {std::move(picture), width, height};
}

} // namespace app
