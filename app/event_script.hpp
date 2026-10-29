#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Scripted input for checking the real application (window, rendering, audio, saves) without a
// person at the controls: `--events FILE` replays timed lines through SDL's own event queue, so
// the host sees them exactly as it sees the mouse and keyboard. Test support only.
//
//   SECONDS down|move|up X Y      the left mouse button / pointer, in window points
//   SECONDS rel DX DY             mouse movement only (a game holding the captured mouse)
//   SECONDS drag X1 Y1 X2 Y2 SECS a press, moves along the line every frame, a release
//   SECONDS key NAME              escape, return, left, right, up, down, m; cmd+s, cmd+l, cmd+q
//   SECONDS shot FILE             the next frame the window shows, as a PNG file
//   SECONDS drop FILE             a file dropped on the window (the rest of the line)
//   SECONDS quit                  closes the application
namespace app {

class EventScript {
public:
    // `shot` takes the file name of a picture to write (Host::capture_next_frame).
    EventScript(const std::filesystem::path& file, std::function<void(const std::filesystem::path&)> shot);
    // Pushes every event due by `seconds` since the application started.
    void pump(double seconds);
    bool done() const { return next_ >= lines_.size() && !drag_; }

private:
    struct Line {
        double at{};
        std::string kind;
        std::vector<float> numbers;
        std::string text;
    };
    std::vector<Line> lines_;
    std::function<void(const std::filesystem::path&)> shot_;
    std::size_t next_{};
    // A drag in progress.
    bool drag_{};
    Line dragging_;
    double drag_start_{};
};

} // namespace app
