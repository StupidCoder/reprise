#pragma once
#include "platform/image.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// The host platform: window, input, audio output, GPU presentation and file
// dialogs, implemented with SDL 3 (host_sdl.cpp). Nothing here knows about any
// particular game.
namespace platform {

// Converts UTF-8 text (SDL paths, command-line arguments) to a filesystem path.
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// Input for one frame: held game buttons plus edge-triggered UI events.
// Keyboard: arrows/WASD, Space/Z = button 1, X = button 2, Enter = Start,
// Esc = menu/back, Ctrl+S/Ctrl+L (Cmd on macOS) = quick save/load, R =
// restart, M = mute, Tab = sound-test focus, Q/E = previous/next generation.
// While Ctrl or Cmd is held, letters act only as shortcuts (no WASD).
// Gamepads map likewise.
// The shortcut modifier's name for UI texts ("Cmd+S").
#ifdef __APPLE__
inline constexpr const char* shortcut_modifier = "Cmd";
#else
inline constexpr const char* shortcut_modifier = "Ctrl";
#endif

struct Controls {
    // Held this frame (game controls).
    bool left{}, right{}, up{}, down{}, button1{}, button2{}, start{};
    // Pressed this frame (menus and shortcuts).
    bool menu{}, accept{}, back{}, start_pressed{}, click{};
    bool nav_left{}, nav_right{}, nav_up{}, nav_down{};
    bool quick_save{}, quick_load{}, space_pressed{}, stop_playback{};
    bool prev_group{}, next_group{};
    bool quit{}, reset{}, toggle_view{}, toggle_mute{};
    // Mouse position of a click, in the 960x640 UI design space.
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
    // Window size in logical (point) units.
    std::array<int, 2> canvas_size() const;

    // A 960x640 UI screen, letterboxed into the window.
    void present(const Image& image, std::string_view title);

    // A game frame at its own small resolution, scaled to the window with
    // nearest-neighbour sampling, plus independent overlay layers (menu, badge).
    // `fill` crops the frame's rounding margin to fill the window exactly
    // (widescreen view); `paused` dims the frame. `lcd` applies the optional
    // Game Gear LCD look (colour response, persistence and subpixel mask), a
    // presentation effect only; see gamegear/lcd_filter.hpp.
    void present_game(const Image& frame, bool fill, bool paused, const Image* overlay,
                      const Image* badge, bool lcd = false);

    // Queues stereo samples. clear_audio() drops queued audio (after loading a
    // save, for example); pause_audio() stops and restarts the output device.
    void audio(std::span<const float> stereo_samples);
    void clear_audio();
    void mute(bool muted);
    void pause_audio(bool paused);

    // The per-user folder for imported content, settings and saves.
    std::filesystem::path data_directory() const;

    // Opens an asynchronous file picker for a ROM; poll take_file_choice().
    void choose_rom();

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

} // namespace platform
