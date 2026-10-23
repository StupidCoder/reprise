#pragma once
#include "platform/image.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// The host platform: window, input, audio output, presentation and file
// dialogs, implemented with SDL 3 (host_sdl.cpp). Nothing here knows about any
// particular game.
namespace platform {

// Converts UTF-8 text (SDL paths, command-line arguments) to a filesystem path.
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// The UI design space; launcher screens are drawn at this size and
// letterboxed into the window.
inline constexpr int ui_width = 960, ui_height = 640;

// Input for one frame: held joystick state plus edge-triggered UI events.
// Keyboard: arrows/WASD = joystick, Space/Z = fire, Enter = accept/start,
// Esc = menu/back, Backspace/Delete = remove, I = info, M = mute. The C64 keys games
// use: F1/F3/F5/F7 (or 1/3/5/7) and P for the C64 space bar. Gamepads: A =
// fire, Start = F7, Y = F3, X = F5.
struct Controls {
    // Held this frame (the game's joystick).
    bool left{}, right{}, up{}, down{}, fire{};
    bool f1{}, f3{}, f5{}, f7{}, c64_space{};
    // Pressed this frame (menus and shortcuts).
    bool accept{}, back{}, remove{}, info{}, click{};
    bool nav_left{}, nav_right{}, nav_up{}, nav_down{};
    bool quit{}, toggle_mute{};
    // Mouse position of a click, in the UI design space.
    int mouse_x{}, mouse_y{};
    // A file dropped onto the window.
    std::optional<std::filesystem::path> dropped_file;
};

class Host {
public:
    // Output rate for the interleaved stereo float samples passed to audio().
    static constexpr int audio_sample_rate = 48000;

    explicit Host(std::string_view title);
    ~Host();

    Controls poll();
    double seconds() const;

    // A UI screen (ui_width x ui_height), letterboxed into the window.
    void present(const Image& image);
    // A game frame at its own small resolution, scaled to the window with
    // nearest-neighbour sampling and an aspect-correct pixel shape
    // (`pixel_aspect` = displayed width / height of one pixel).
    void present_game(const Image& frame, double pixel_aspect);

    // Queues stereo samples; clear_audio() drops queued audio.
    void audio(std::span<const float> stereo_samples);
    void clear_audio();
    void mute(bool muted);

    // Opens an asynchronous file picker for a game image; poll
    // take_file_choice() for the result.
    void choose_image();

    // A completed dialog: a selected file, a cancellation, or an error.
    struct FileChoice {
        std::optional<std::filesystem::path> file;
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
