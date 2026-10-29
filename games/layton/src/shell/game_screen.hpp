#pragma once
// The game screen: the two DS screens filling the window, side by side or
// stacked, whichever shows them larger. There is no toolbar; the in-game
// menu (game_menu.hpp, Escape or Back) leads out of the game.
//
// Input: the mouse is the stylus on the lower screen; the keyboard types
// the name on the name-entry screen; holding B, or the floating "Hold to
// blow" button the one microphone puzzle shows, blows into the microphone.
// A double click on the screens is reported to the app, which decides whether
// it skips (App's skip triggers).
//
// Pinch zoom (touch screens, i.e. phones): two fingers zoom the screens in
// and out and, zoomed in, move them around (from a puzzle up to its text
// and back). One finger stays the stylus, mapped through the zoom, and
// reaches the game as soon as it is clearly not the start of a pinch: once
// it moves (a stroke), lifts (a tap: press and release then keep the touch's
// own length) or has rested 150 ms. A second finger before that makes a
// pinch the game never sees - no stroke, no dialog step. Should the second
// finger come later, the stylus slides off the screen and lifts there.
//
// The touch-area help (the in-game menu's toggle) outlines what the room
// offers to touch over the lower screen, coloured by what a touch starts
// (TouchArea, system/game_types.hpp).
#include "platform/host.hpp"
#include "shell/game_session.hpp"
#include "shell/screen_layout.hpp"
#include "shell/style.hpp"
#include <map>
#include <optional>
#include <string>

namespace picarat::shell {

class GameScreen {
public:
    // Mouse coordinates are window points. Returns true for a double click
    // on the screens (a possible skip trigger).
    bool handle(const platform::Event& event, GameSession& session, float width, float height);
    // Where the two screens go (the session's screen sizes).
    static ScreenLayout layout(float width, float height, const ScreenGeometry& geometry = {});

    // The floating "Hold to blow" button (window points; empty when the
    // scene does not listen to the microphone), and drawing it into a canvas
    // of its own size.
    static platform::Rect blowButton(float width, float height, const GameSnapshot& state);
    void drawBlowButton(style::Canvas& canvas, const platform::Rect& button);
    // The held arrows to the session: the pad, or with Shift the tilt.
    void sendArrows(GameSession& session) const;

    // The touch-area help: the colour of each kind, its name (the menu's
    // legend), and the areas drawn into a canvas the size of the lower
    // screen (`scale` window points per DS pixel).
    static platform::Color touchAreaColour(TouchArea::Kind kind);
    static const char* touchAreaName(TouchArea::Kind kind);
    static void drawTouchAreas(style::Canvas& canvas, float scale,
                               const std::vector<TouchArea>& areas);

    // Forgets gestures in progress and the zoom (when a game starts).
    void reset();

    // The zoom: a window point p of the screens' layout shows at
    // p * zoom + (x, y).
    struct View {
        double zoom = 1, x = 0, y = 0;
    };

    // Where a rectangle of the layout shows in the window.
    ScreenRect zoomed(const ScreenRect& r) const;
    // Once a frame: keeps the zoomed screens inside the window.
    void update(double now, GameSession& session, float width, float height);

private:
    bool movieGesture_ = false;  // a click that started during a movie
    bool stylusGesture_ = false; // a press that started on the lower screen
    bool blowing_ = false;       // the blow button is held
    unsigned arrows_ = 0;        // arrow keys held (left, right, up, down)
    bool tilting_ = false;       // Shift held with them: they tilt the console
    float mouseX_ = -1, mouseY_ = -1;

    View view_;
    ScreenGeometry geometry_; // the session's screens (set by update / handle)
    // The fingers down (window points), and the pinch they make.
    std::map<std::uint64_t, std::pair<float, float>> fingers_;
    bool pinching_ = false; // until every finger has lifted
    double now_ = 0;
    float width_ = 0, height_ = 0; // the window at the last update

    // A finger's press not passed on yet (window point, when), and the
    // release of a tap passed on late, due when the tap's own length is over.
    struct Held {
        float x, y;
        double at;
    };

    std::optional<Held> held_;
    std::optional<Held> release_;
    void passHeld(GameSession& session, float width, float height);
    void passRelease(GameSession& session, float width, float height);
    double pinchX_ = 0, pinchY_ = 0, pinchDistance_ = 0;

    void startPinch(GameSession& session);
    void pinchTo(double x, double y, double distance);
    void clampView(float width, float height);
    platform::Rect blow_{}; // the blow button at the last event
};

} // namespace picarat::shell
