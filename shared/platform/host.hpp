#pragma once
#include "platform/image.hpp"
#include "platform/touch.hpp"
#include "platform/tilt.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The host platform: window, input, audio output, presentation and file
// dialogs, implemented with SDL 3 (host_sdl.cpp). Nothing here knows about any
// particular game.
struct SDL_Renderer;
struct SDL_Texture;

namespace platform {

// Converts UTF-8 text (SDL paths, command-line arguments) to a filesystem path.
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// The UI design space; launcher screens are drawn at this size and
// letterboxed into the window.
inline constexpr int ui_width = 960, ui_height = 640;

// The modifier of keyboard shortcuts as the player sees it.
#ifdef __APPLE__
inline constexpr const char* shortcut_modifier = "Cmd";
#else
inline constexpr const char* shortcut_modifier = "Ctrl";
#endif

// One pointer (mouse or touch) event over the game frame, in the order it happened: games that
// draw with the pointer (paths, gestures) need every point between press and release, not
// only where the pointer is once per frame. Positions are in the frame's own units (the
// pixels of present_game's frame, the logical size of present_game_gpu's), as floats, and may
// lie outside it while a press that began inside goes on.
struct PointerEvent {
    enum class Kind { down, move, up };
    Kind kind{};
    float x{}, y{};
    // Made by a finger on a touch screen (the first finger; the others are Controls::fingers),
    // not a mouse: a game can tell a touch that may become a gesture of several fingers.
    bool touch{};
};

// A finger on a touch screen, while it touches: SDL's finger id and where it is, in the units
// of PointerEvent (it may lie outside the frame).
struct Finger {
    std::uint64_t id{};
    float x{}, y{};
};

// A frame a game draws itself with the host's renderer (SDL 3's SDL_Renderer): `target` is a
// render-target texture of `width` x `height` device pixels, current while the game draws.
struct GpuFrame {
    SDL_Renderer* renderer{};
    SDL_Texture* target{};
    int width{}, height{};
};

// What a GPU-drawn game can ask of the host outside presentation: a picture of one frame
// (save previews), drawn into an offscreen target and read back once.
class GpuSurface {
public:
    virtual ~GpuSurface() = default;
    virtual Image capture(int width, int height, const std::function<void(const GpuFrame&)>& draw) = 0;
};

// A fragment shader for the host's GPU renderer (SDL's GPU renderer), in the formats Reprise
// ships: SPIR-V for Vulkan and MSL for Metal (tools/build_shaders.py). It gets the texture being
// drawn at sampler slot 0, the effect's uniforms at uniform slot 0, and SDL's vertex colour and
// texture coordinate at locations 0 and 1.
struct FragmentShader {
    const char* name{};                   // identifies it: the host compiles each one once
    std::span<const unsigned char> spirv; // entry point "main"
    std::span<const unsigned char> msl;   // entry point "main0", NUL-terminated source
};

// A presentation effect over a game frame (the host's screen filter stage, such as the VHS
// look). With one, the game draws its frame at its logical size; the effect is applied as the
// frame is shown, before the note and the overlay (pause menu, save messages) are drawn over it
// at the window's own resolution, unfiltered. Where the renderer can run `shader`, it does so
// on the GPU; elsewhere the frame is read back once and `cpu` makes the shown picture (the
// same look, slower). Presentation only: nothing here reaches the game.
struct ScreenEffect {
    // Optional fixed processing raster; zero draws the shader directly at display size.
    int raster_width{}, raster_height{};
    bool linear_output{}; // reconstruct a sampled video signal without repeating whole columns
    const FragmentShader* shader{};
    std::vector<std::uint8_t> uniforms;     // the shader's uniforms for this frame
    std::function<Image(const Image&)> cpu; // the frame's picture -> the picture to show
    // Separate pictures within one frame (the two screens of a handheld shown together, side by
    // side or stacked): each region, in the frame's logical units, is filtered as a picture of
    // its own (its own raster, uniforms and CPU form) and shown where it lies, so one screen's
    // picture never bleeds into the other and neither is stretched to the frame's shape. The
    // frame outside the regions is shown as drawn. Empty: the whole frame is one picture
    // (`uniforms` and `cpu` above).
    struct Region {
        int x{}, y{}, width{}, height{};
        int raster_width{}, raster_height{}; // each screen may have a different aspect
        std::vector<std::uint8_t> uniforms;
        std::function<Image(const Image&)> cpu;
    };
    std::vector<Region> regions;
};

// Input for one frame: held joystick state plus edge-triggered UI events.
// Keyboard: arrows/WASD = joystick, Space/Z = fire, Enter = accept/start,
// Esc = menu/back, Backspace/Delete = remove, I = info, M = mute. The C64 keys games
// use: F1/F3/F5/F7 (or 1/3/5/7) and P for the C64 space bar. Gamepads: A =
// fire, Start = F7, Y = F3, X = F5. Cmd/Ctrl+S and +L: quick save and load.
struct Controls {
    // Held this frame (the game's joystick).
    bool left{}, right{}, up{}, down{}, fire{};
    bool f1{}, f3{}, f5{}, f7{}, c64_space{};
    bool left2{}, right2{}, up2{}, down2{}, fire2{}; // the second joystick
    // A second action button and a Start button, held, for consoles with them (Game Gear
    // button 2 / GBA B, Start): X and Return on the keyboard, the gamepad's east button and Start.
    bool button2{}, start{};
    // The C64 keyboard as a game that scans it sees it, held this frame:
    // index row * 8 + column of the C64 key matrix. Letters, digits and
    // punctuation by their character, Return, Backspace = DEL, Tab = RUN/STOP,
    // ] = the up arrow.
    // (The C64 space bar stays on c64_space.)
    std::array<bool, 64> c64_keys{};
    // The same matrix while Ctrl (not Cmd) is held, without the shortcut
    // letters Q, S and L (GameInput::ctrl_keys).
    std::array<bool, 64> ctrl_keys{};
    // Pressed this frame (menus and shortcuts).
    bool accept{}, back{}, menu{}, remove{}, info{}, click{};
    bool nav_left{}, nav_right{}, nav_up{}, nav_down{};
    bool page_up{}, page_down{}; // Page Up/Down, the gamepad's shoulder buttons
    // Mouse wheel steps this frame (positive: towards the user, scrolling down).
    int wheel{};
    float wheel_precise{}; // fractional wheel motion, same direction as wheel
    bool quit{}, toggle_mute{}, quick_save{}, quick_load{};
    // Mouse position of a click, in the UI design space.
    int mouse_x{}, mouse_y{};
    // Current hover position, independent of edge-triggered clicks.
    std::optional<std::array<int, 2>> ui_pointer;
    // Files dropped onto the window this frame (several for a multi-disk game).
    std::vector<std::filesystem::path> dropped_files;
    // The mouse over the last game frame shown (present_game), in the frame's
    // pixels; unset outside it and on UI screens. The left button, held.
    std::optional<std::array<int, 2>> game_pointer;
    // Every press, move (while pressed) and release over the last game frame shown, in order.
    std::vector<PointerEvent> pointer_events;
    // The fingers touching the screen after this poll (touch screens; in the last game frame's
    // units), for games with gestures of several fingers such as a pinch.
    std::vector<Finger> fingers;
    // Touch contacts in the same logical coordinates as ui_pointer.
    std::vector<Finger> ui_fingers;
    // The device's accelerometer while a game asks for it (Host::set_motion_wanted): the
    // direction away from the earth in G (1 at rest), in the window's axes (x right, y up, z out
    // of the screen) whichever way the display is turned. Empty without one (desktops).
    std::optional<std::array<float, 3>> motion;
    bool mouse_held{};
    bool mouse_right_held{}; // the right button (games played with both)
    // The mouse's movement since the previous poll, in window points (also
    // while captured, set_mouse_captured), for games steered by movement.
    float mouse_dx{}, mouse_dy{};
    bool command_held{}, option_held{}, control_held{}; // (GameInput::command, option, control)
    bool shift_held{}; // either Shift key (GameInput::shift)
    bool gamepad_connected{}, touch_available{}, input_cancelled{}, mouse_is_touch{};
    // Console camera and face/shoulder controls, independent of legacy keyboard aliases.
    std::optional<std::array<float, 2>> camera_stick{};
    bool face_x{}, face_y{}, shoulder_left{}, shoulder_right{}, trigger_left{}, trigger_right{};
    std::optional<std::array<float, 2>> analog_stick;
    std::vector<TouchEvent> touch_events;
};

class Host final : public GpuSurface {
public:
    // Output rate for the interleaved stereo float samples passed to audio().
    static constexpr int audio_sample_rate = 48000;

    explicit Host(std::string_view title);
    ~Host() override;

    Controls poll();
    // Android policy only; desktops keep their resizable windows.
    void set_orientation(Orientation);
    TouchArea safe_area() const;
    InputCapabilities input_capabilities();
    void set_touch_overlay(std::function<void(const GpuFrame&)> draw);
    double seconds() const;

    // A UI screen (ui_width x ui_height), letterboxed into the window.
    void present(const Image& image);
    // A game frame at its own small resolution, scaled to the window with
    // nearest-neighbour sampling and an aspect-correct pixel shape
    // (`pixel_aspect` = displayed width / height of one pixel). An overlay
    // (ui_width x ui_height, with transparency) is letterboxed on top; `dim`
    // darkens the frame under it. A `note` (device pixels, with
    // transparency) is drawn unscaled in the frame's top-right corner, above
    // the game and without taking the mouse. An effect uses the shared GPU
    // stage after uploading the picture, with a CPU fallback if unavailable.
    void present_game(const Image& frame, double pixel_aspect, const Image* overlay = nullptr,
                      bool dim = false, const Image* note = nullptr, float note_opacity = 1.0f,
                      int note_offset = 0, const ScreenEffect* effect = nullptr);
    // A frame the game draws itself (GpuFrame): its logical size (`width` x `height`, the units
    // of pointer positions) is shown aspect-correct like present_game's frames; the game draws
    // into a target of the shown size in device pixels, so nothing is scaled twice or read
    // back. Overlay, dim and note as for present_game. With an `effect` (ScreenEffect) the game
    // draws at its logical size and the effect makes the picture shown; without one the frame
    // is shown exactly as drawn.
    void present_game_gpu(int width, int height, double pixel_aspect,
                          const std::function<void(const GpuFrame&)>& draw, const Image* overlay = nullptr,
                          bool dim = false, const Image* note = nullptr, float note_opacity = 1.0f,
                          int note_offset = 0, const ScreenEffect* effect = nullptr);
    // How the last screen effect was applied: "GPU shader (SPIR-V)", "GPU shader (MSL)", "CPU",
    // or empty before the first one. The renderer in use: renderer_name().
    std::string screen_effect_path() const;
    std::string renderer_name() const;
    Image capture(int width, int height, const std::function<void(const GpuFrame&)>& draw) override;
    // Writes the next frame presented (UI or game, as shown, in device pixels) to a PNG file
    // (scripted checks, app/event_script.hpp).
    void capture_next_frame(const std::filesystem::path& file);
    // Whether the game shown draws its own mouse pointer; the system's is
    // then hidden while over the game frame (not over an overlay).
    void set_game_draws_pointer(bool draws);
    // Opens the accelerometer (Controls::motion) while a game wants it; false closes it again.
    void set_motion_wanted(bool wanted);
    // Captures the mouse for a game played with its movement (relative
    // mode: the pointer is hidden and held in the window, Controls::mouse_dx
    // and mouse_dy keep counting at the screen's edges); false lets it go.
    void set_mouse_captured(bool captured);

    // Queues stereo samples; clear_audio() drops queued audio.
    void audio(std::span<const float> stereo_samples);
    void clear_audio();
    void mute(bool muted);
    void pause_audio(bool paused);
    // The window's drawing area in device pixels (width, height).
    std::array<int, 2> canvas_size() const;

    // Opens an asynchronous file picker for a game image; poll
    // take_file_choice() for the result.
    void choose_image();

    // A completed dialog: the selected files (several allowed, for games on
    // more than one disk), a cancellation (none), or an error.
    struct FileChoice {
        std::vector<std::filesystem::path> files;
        std::string error;
    };

    std::optional<FileChoice> take_file_choice();

private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
};

// The per-user folder for imported content and settings
// (~/Library/Application Support/NativeReconstruction/Joyport/ on macOS).
std::filesystem::path data_directory();

// Writes an image as PNG (headless previews and tests).
void save_png(const Image& image, const std::filesystem::path& file);

} // namespace platform
