#pragma once
#include "platform/host.hpp"
#include "platform/image.hpp"
#include "ui/vhs.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
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
    // The mouse's movement since the previous tick, in window points (the
    // system's mouse units, whatever the window's size), for a game that
    // captures the mouse (GameModule::captures_mouse).
    float mouse_dx{}, mouse_dy{};
    // Command, Option/Alt or Control held on its own (no key with it
    // needed): for mouse games, more buttons on a one-button trackpad.
    bool command{}, option{}, control{};
    // Every press, move and release over the game since the previous tick, in order (in the
    // units of picture(), or of gpu_frame_size() for a GPU-drawn game). Games drawn with the
    // pointer (paths) use these instead of the once-per-frame pointer above.
    std::vector<platform::PointerEvent> pointer_events{};
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
    // A presentation preference (such as the screen filter), not part of the game: loading a
    // save keeps the player's current choice instead of the one the save was made with.
    bool presentation{};
};

// The framework's screen filter as a game option: the same name, choices and meaning in every
// game, so the pause menu, the options file and saves treat it alike. A module offering it
// returns the chosen level from vhs_level(); the host applies it (shown_picture for a picture
// game, the screen filter stage for a GPU-drawn one).
inline GameOption screen_filter_option(int level) {
    return {"VHS look",
            {"Off", "Light", "Medium", "Heavy", "Broken"},
            level,
            "A worn VHS tape played on an old TV, over the game's picture. Presentation only: the "
            "game, its sound and its saves are the same with any choice.",
            {},
            true};
}

class GameModule {
public:
    virtual ~GameModule() = default;

    // Length of one frame of the original, in seconds.
    virtual double step_seconds() const = 0;
    // Runs one frame.
    virtual void tick(const GameInput&) = 0;
    // The picture of the current frame, borders included, at the game's own
    // resolution (the launcher adds the VHS look; see shown_picture). Never
    // filtered: save pictures are taken from it.
    virtual platform::Image picture() const = 0;
    // The VHS look the player chose in the game's options (0 off, up to
    // ui::vhs_levels - 1; screen_filter_option). The launcher draws it over
    // picture(), or over a GPU-drawn game's frame (screen_effect).
    virtual int vhs_level() const { return 0; }
    // True for a game that draws its own mouse pointer: the system's is
    // hidden over its picture, so that only one shows.
    virtual bool draws_pointer() const { return false; }
    // True for a game steered by the mouse's movement (as the Amiga's mouse
    // counters are read), not its position: while it runs (not in menus) the
    // host captures the mouse and GameInput::mouse_dx/dy carry the movement.
    virtual bool captures_mouse() const { return false; }
    // Displayed width / height of one picture pixel.
    virtual double pixel_aspect() const = 0;

    // A game drawn on the GPU (its own renderer, behind the host's GpuFrame): the frame's
    // logical size, or empty for a game that gives picture() each frame. Such a game draws
    // with draw_gpu() when shown; its picture() is a one-off capture (save previews) through the
    // GpuSurface given with attach_gpu() before the first frame. Drawing must not change the
    // game's state.
    virtual std::optional<std::array<int, 2>> gpu_frame_size() const { return std::nullopt; }
    virtual void draw_gpu(const platform::GpuFrame& /*frame*/) {}
    virtual void attach_gpu(platform::GpuSurface* /*surface*/) {}
    // The player left the game through its own menu (back to the library, not an error).
    virtual bool finished() const { return false; }
    // The window's drawing area, for a game whose view can widen to it.
    virtual void set_canvas(int /*width*/, int /*height*/) {}
    // How far the time shown lies past the last tick, as a fraction of a
    // tick (0 .. 1), set before picture(): a game that draws motion between
    // its own frames (smooth motion) can show it at the display's rate.
    virtual void set_frame_fraction(double /*fraction*/) {}
    // The mouse over the picture now (in pixels of picture(), like
    // GameInput), set before picture(): a game drawing its own pointer can
    // show it where the mouse is at the display's rate. Empty: not over it.
    virtual void set_display_pointer(std::optional<std::array<int, 2>> /*at*/) {}

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
// effects on top (the VHS look, which uses a fixed virtual video raster). `frame` counts the
// frames run, for the look's moving noise.
struct ShownPicture {
    platform::Image image{1, 1};
    int picture_width{1}, picture_height{1}; // the game's own picture
    bool gpu{}; // drawn by the game itself (gpu_frame_size): `image` is unused

    // A video-space effect may replace the raster dimensions. Keep the original
    // displayed aspect ratio; pointer conversion below likewise uses both grids.
    double display_pixel_aspect(double original) const {
        return original * double(picture_width) * image.height /
               (double(std::max(picture_height, 1)) * std::max(image.width, 1));
    }

    // A point over the shown picture (as the host reports the mouse) in
    // pixels of the game's own picture.
    std::array<int, 2> to_picture(std::array<int, 2> point) const {
        if (gpu) return point; // the host reports it in the frame's own units already
        return {point[0] * picture_width / std::max(image.width, 1),
                point[1] * picture_height / std::max(image.height, 1)};
    }
};

// Runtime presentation requests an unfiltered picture for the host GPU stage.
// Offline captures may still request the deterministic CPU implementation.
inline ShownPicture shown_picture(const GameModule& game, long frame, bool cpu_filter = true) {
    if (const auto size = game.gpu_frame_size()) {
        // (Not drawn here: the host draws GPU games into its own target, with their effect from
        // screen_effect. The size maps the pointer one to one.)
        return ShownPicture{platform::Image{1, 1}, (*size)[0], (*size)[1], true};
    }
    auto picture = game.picture();
    const int width = std::max(picture.width, 1), height = std::max(picture.height, 1);
    const int level = game.vhs_level();
    if (cpu_filter && level > 0) picture = ui::vhs(picture, frame, level);
    return {std::move(picture), width, height};
}

// The screen effect over a GPU-drawn game's frame (Host::present_game_gpu's filter stage), or
// none: with the filter off the frame is shown exactly as the game drew it. Picture games get
// theirs from shown_picture instead, so nothing is filtered twice. `frame` as for
// shown_picture.
inline std::optional<platform::ScreenEffect> screen_effect(const GameModule& game, long frame) {
    const auto size = game.gpu_frame_size();
    const int level = game.vhs_level();
    if (!size || level <= 0) return std::nullopt;
    return ui::vhs_effect(frame, level, (*size)[0], (*size)[1]);
}

} // namespace app
