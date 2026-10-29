#pragma once
// The platform layer: one window, input events, drawing images, sound output
// and a few operating-system services (file picker, clipboard, folders).
//
// Everything above this layer (src/shell, the game in src/story and below) is
// portable C++. The only implementation, host_sdl.cpp, uses SDL 3, which runs
// on macOS, Linux, Windows and Android. The shell draws its own controls into
// Images (see shell/style.hpp); the host only puts pixels on the screen, so
// no native UI toolkit is involved.
//
// Coordinates: the window is measured in points (logical pixels). On a
// high-density display one point covers several device pixels; the host
// converts, so callers never deal with device pixels.
#include "platform/image.hpp"
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

namespace picarat::platform {

// Keys the shell reacts to by name. Printable keys arrive as Event::symbol
// (the character of the key in the current keyboard layout) instead.
enum class Key {
    None,
    Return,
    Escape,
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    Character, // a printable key; see Event::symbol
};

struct Event {
    enum class Type {
        Quit,           // the window was closed or the app asked to quit
        KeyDown,        // key, symbol, command, shift, repeat
        KeyUp,          // key, symbol
        Text,           // typed text (UTF-8), after keyboard layout and dead keys
        MouseDown,      // x, y, button, clicks
        MouseUp,        // x, y, button
        MouseMove,      // x, y, buttons held
        Wheel,          // wheel: positive scrolls up
        DropFile,       // file
        Sleep,          // the window was minimised or hidden, or the app went to the background
        Wake,           // ... and is back
        FocusLost,      // keys held now will not report their release
        KeyboardHidden, // the user closed the on-screen keyboard (touch devices)
        Expose,         // the window's picture was lost: draw the next frame in full
        // Fingers on a touch screen, each on its own (finger, x, y); only
        // for gestures. The first finger also arrives as the mouse (with
        // `touch` set), which is what taps and drags use.
        TouchDown,
        TouchMove,
        TouchUp,
    };

    Type type = Type::Quit;
    Key key = Key::None;
    char32_t symbol = 0;  // printable key: its lower-case character ('b', '1', '.')
    bool command = false; // the shortcut modifier: Command on macOS, Control elsewhere
    bool shift = false;
    bool repeat = false; // key held down (auto-repeat)
    std::string text;
    float x = 0, y = 0;       // mouse position in window points
    int button = 0;           // 1 left, 2 middle, 3 right; MouseMove: 1 if the left button is held
    int clicks = 0;           // 2 for a double click
    bool touch = false;       // a mouse event made from a finger on a touch screen
    std::uint64_t finger = 0; // Touch*: which finger
    float wheel = 0;
    std::filesystem::path file;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

class Host;

// An image on the graphics card. Create it with Host::createTexture and
// refresh its pixels with update() whenever the Image changes.
class Texture {
public:
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void update(const Image& image);

    int width() const { return width_; }

    int height() const { return height_; }

private:
    friend class Host;
    Texture() = default;
    struct Handle;
    Handle* handle_ = nullptr;
    int width_ = 0, height_ = 0;
};

class Host {
public:
    // Opens the window (size in points). Throws std::runtime_error when the
    // platform has no display.
    Host(std::string_view title, int width, int height, int minimumWidth, int minimumHeight);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Collects the events since the last call. With waitMilliseconds > 0 it
    // first waits up to that long for one (saves power on idle screens).
    std::vector<Event> poll(int waitMilliseconds = 0);

    // Window size in points, and device pixels per point (2 on most
    // high-density displays). On phones this is the safe area, clear of
    // system bars and camera cut-outs; event positions and drawing are
    // relative to it, and beginFrame's background colour fills the rest.
    std::array<float, 2> windowSize() const;
    float pixelDensity() const;
    void setTitle(std::string_view title);

    // Back from the app's first screen: on Android the app goes to the
    // background, as the back gesture does in other apps. Nothing elsewhere.
    void leaveToSystem();

    // Typed text (Event::Text) is only delivered while text input is on. On
    // touch devices `screenKeyboard` also shows the on-screen keyboard; it
    // comes back each time screenKeyboard turns on again. Without it, only
    // hardware keyboards type (for screens that draw their own keyboard).
    void setTextInput(bool on, bool screenKeyboard);
    std::string clipboardText() const;

    // Drawing one frame: clear, draw textures back to front, present. The
    // frame is shown at the display's refresh rate (vertical sync).
    void beginFrame(Color background);
    std::unique_ptr<Texture> createTexture(int width, int height, bool smooth);
    // Draws the texture's `source` part (all of it when null) into `target`
    // (window points), with `alpha` 0..255 as overall opacity.
    void draw(const Texture& texture, const Rect* source, const Rect& target,
              std::uint8_t alpha = 255);
    void endFrame();

    // Seconds since the host started, for animation and frame pacing.
    double seconds() const;

    // The device's accelerometer while `wanted` (opened on the first such
    // call, closed again by a call without): the direction away from the
    // earth in G (1 at rest), in the window's axes (x right, y up, z out of
    // the screen) whichever way the display is turned. Nothing on devices
    // without one (desktops).
    std::optional<std::array<float, 3>> motion(bool wanted);

    // The open-file dialog. It runs asynchronously; poll takeFileChoice()
    // until it returns a result (a file, or nothing when cancelled).
    struct FileChoice {
        std::optional<std::filesystem::path> file;
        std::string error;
    };

    void chooseFile(std::string_view filterName, std::string_view extensions);
    std::optional<FileChoice> takeFileChoice();

    // Where the app's own read-only files (artwork) are: the app bundle's
    // resources on macOS, the APK's assets on Android (an empty path: files
    // are named relative to the assets), the folder of the executable elsewhere.
    static std::filesystem::path resourceDirectory();
    // A per-user writable folder for the app's data, created if missing.
    static std::filesystem::path dataDirectory(std::string_view appName);
    // The user's preferred language as an ISO 639 code with an optional
    // region ("de", "en_GB"); empty when the system does not say.
    static std::string preferredLanguage();
    // Reads a whole file of at most maxBytes bytes. The path may also be what
    // the file picker returned on Android (a content:// document). Throws
    // std::runtime_error with a readable message on failure.
    static std::vector<std::uint8_t> readFile(const std::filesystem::path& path,
                                              std::uint64_t maxBytes);

    // Opens a file for random access without reading it whole (the 3DS ROM
    // images are 1 GiB). The same paths as readFile. Throws
    // std::runtime_error with a readable message on failure.
    class FileReader {
    public:
        explicit FileReader(const std::filesystem::path& path);
        ~FileReader();
        FileReader(const FileReader&) = delete;
        FileReader& operator=(const FileReader&) = delete;

        std::uint64_t size() const { return size_; }

        // Fills `out` from `offset`; throws std::runtime_error if it cannot.
        void read(std::uint64_t offset, std::span<std::uint8_t> out);

    private:
        void* io_ = nullptr;
        std::uint64_t size_ = 0;
    };

    // Reads a PNG file into an RGBA image.
    static std::optional<Image> loadPng(const std::filesystem::path& path);

private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
};

// Sound output. The callback runs on the audio thread and must fill `frames`
// stereo frames of 16-bit samples (left, right, left, ...). Sound plays from
// construction until destruction. Independent of Host, so a game thread can
// own it.
class AudioStream {
public:
    using Fill = std::function<void(std::int16_t* stereo, int frames)>;
    AudioStream(int sampleRate, Fill fill);
    ~AudioStream();
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
};

} // namespace picarat::platform
