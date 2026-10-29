#pragma once
#include "layton_embed.hpp"
#include "platform/host.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

// The two screens in Reprise's game frame, and the stylus. The frame is the window's drawing
// area; the screens go stacked or side by side, whichever shows them larger (embed::layout,
// the standalone app's rule), and follow every change of the window's size or orientation at
// once. A pinch (two fingers) zooms and moves them, as in the standalone app. The stylus is the
// mouse or the first finger, on the lower screen only, mapped through the layout and the zoom
// as they were when the frame the pointer was over was drawn.
namespace layton_reprise {

class DualScreen {
public:
    explicit DualScreen(picarat::embed::Screens screens) : screens_(screens) {}

    // The screens' places in a frame of `width` x `height` with the current zoom.
    struct Placement {
        picarat::embed::Rect top, bottom;
        bool side_by_side{};
        int width{}, height{};
    };
    Placement place(int width, int height) const;

    // The frame drawn now (draw_gpu's size): the placement the pointer maps through. A change of
    // size (a resize, a rotation) or of the zoom counts as a new layout (generation()).
    void show(int width, int height);
    const Placement& shown() const { return shown_; }
    std::uint64_t generation() const { return generation_; }

    // A point of the shown frame in lower-screen pixels (may lie outside the screen), and
    // whether it lies on the lower screen.
    std::array<int, 2> to_lower(float x, float y) const;
    bool on_lower(float x, float y) const;

    // The pinch: two fingers' middle and distance at its start and as they move.
    void pinch_start(double x, double y, double distance);
    void pinch_to(double x, double y, double distance);
    // All fingers lifted: nearly unzoomed snaps back all the way.
    void pinch_end();
    double zoom() const { return zoom_; }

    picarat::embed::Screens screens() const { return screens_; }

private:
    void clamp(int width, int height, double& x, double& y) const;
    picarat::embed::Screens screens_;
    double zoom_ = 1, x_ = 0, y_ = 0; // a frame point p of the plain layout shows at p * zoom + (x, y)
    double pinch_x_ = 0, pinch_y_ = 0, pinch_distance_ = 1;
    Placement shown_{};
    std::uint64_t generation_ = 0;
};

// One tick's pointer events and fingers to the stylus events the game gets. Rules, as in the
// standalone app (src/shell/game_screen.cpp):
//  - presses on the upper screen or outside the screens are not stylus contacts; a press that
//    began on the lower screen goes on (and lifts) wherever it moves;
//  - a click during a movie never becomes a stylus contact in the next scene;
//  - a touch that may start a pinch is held back up to 150 ms (a stroke or a tap goes through
//    at once; a tap keeps its own length); a second finger makes a pinch the game never sees,
//    and a stylus already down slides off the screen and lifts there;
//  - a new layout (resize, rotation, zoom) during a press ends it the same way, off the screen,
//    so nothing is dropped where the finger no longer points; the rest of that press is ignored.
class StylusRouter {
public:
    struct Frame {
        std::vector<picarat::embed::Touch> touches;
        bool blowing{}; // the blow button is held
    };
    // `movie`: a movie plays; `blow_button`: where the "hold to blow" button is (frame units),
    // when the scene listens to the microphone.
    Frame route(const std::vector<platform::PointerEvent>& events, const std::vector<platform::Finger>& fingers,
                DualScreen& screens, bool movie, std::optional<picarat::embed::Rect> blow_button);
    // Lifts everything (focus lost, the pause menu, a load): an unfinished stylus press slides off.
    void cancel(std::vector<picarat::embed::Touch>& out);
    bool stylus_down() const { return stylus_; }

private:
    struct Held {
        float x, y;
        long tick;
    };
    void press(float x, float y, const DualScreen& screens, bool movie,
               const std::optional<picarat::embed::Rect>& blow, std::vector<picarat::embed::Touch>& out);
    void move(float x, float y, const DualScreen& screens, std::vector<picarat::embed::Touch>& out);
    void release(float x, float y, const DualScreen& screens, std::vector<picarat::embed::Touch>& out);
    void slide_off(std::vector<picarat::embed::Touch>& out);
    long tick_ = 0;
    bool stylus_ = false, movie_gesture_ = false, blowing_ = false, ignoring_ = false, pinching_ = false;
    std::uint64_t press_generation_ = 0;
    std::optional<Held> held_, release_;
    std::map<std::uint64_t, std::array<float, 2>> fingers_;
};

} // namespace layton_reprise
