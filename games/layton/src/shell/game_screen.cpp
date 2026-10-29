// The game screen's input. See game_screen.hpp.
#include "shell/game_screen.hpp"
#include "shell/library.hpp"
#include "shell/widgets.hpp"
#include <cmath>

namespace picarat::shell {

namespace {

unsigned arrowBit(platform::Key key) {
    switch (key) {
    case platform::Key::Left:
        return 1;
    case platform::Key::Right:
        return 2;
    case platform::Key::Up:
        return 4;
    case platform::Key::Down:
        return 8;
    default:
        return 0;
    }
}

} // namespace

using platform::Event;
using platform::Key;
using platform::Rect;

ScreenLayout GameScreen::layout(float width, float height, const ScreenGeometry& geometry) {
    return screenLayout(width, std::max(1.0f, height), geometry);
}

Rect GameScreen::blowButton(float width, float height, const GameSnapshot& state) {
    // The one puzzle that listens to the microphone (203) gets a button to
    // hold instead of the B key, for touch screens: bottom right.
    if (state.scene != SceneKind::Puzzle || state.detail != 203) {
        return {};
    }
    return {width - 16 - 150, height - 16 - 36, 150, 36};
}

void GameScreen::drawBlowButton(style::Canvas& c, const Rect& r) {
    using namespace ui;
    c.fill({0, 0, 0, 0});
    button(c, {0, 0, r.w, r.h}, blowing_ ? "Blowing…" : "Hold to blow", ButtonKind::Toolbar, true,
           blowing_ || r.contains(mouseX_, mouseY_), 1.5f);
}

platform::Color GameScreen::touchAreaColour(TouchArea::Kind kind) {
    switch (kind) {
    case TouchArea::Kind::Text:
        return {70, 140, 235, 255};
    case TouchArea::Kind::Dialog:
        return {60, 190, 90, 255};
    case TouchArea::Kind::Puzzle:
        return {230, 60, 50, 255};
    case TouchArea::Kind::HintCoin:
        return {245, 200, 40, 255};
    }
    return {255, 255, 255, 255};
}

const char* GameScreen::touchAreaName(TouchArea::Kind kind) {
    switch (kind) {
    case TouchArea::Kind::Text:
        return "Text";
    case TouchArea::Kind::Dialog:
        return "Dialog";
    case TouchArea::Kind::Puzzle:
        return "Puzzle";
    case TouchArea::Kind::HintCoin:
        return "Hint coin";
    }
    return "";
}

void GameScreen::drawTouchAreas(style::Canvas& c, float scale,
                                const std::vector<TouchArea>& areas) {
    // A light wash and a solid edge per area; puzzles and coins last, so
    // they stay visible where areas overlap.
    c.fill({0, 0, 0, 0});
    const float edge = std::max(1.0f, std::round(scale / 2));
    for (auto kind : {TouchArea::Kind::Text, TouchArea::Kind::Dialog, TouchArea::Kind::HintCoin,
                      TouchArea::Kind::Puzzle}) {
        for (const auto& a : areas) {
            if (a.kind != kind) {
                continue;
            }
            const float x = float(a.x) * scale, y = float(a.y) * scale, w = float(a.w) * scale,
                        h = float(a.h) * scale;
            platform::Color colour = touchAreaColour(kind);
            platform::Color wash = colour;
            wash.a = 70;
            c.rect(x, y, w, h, wash);
            c.rect(x, y, w, edge, colour);
            c.rect(x, y + h - edge, w, edge, colour);
            c.rect(x, y, edge, h, colour);
            c.rect(x + w - edge, y, edge, h, colour);
        }
    }
}

void GameScreen::sendArrows(GameSession& session) const {
    if (tilting_) {
        session.pad(0);
        const float x = (arrows_ & 2 ? 1.0f : 0.0f) - (arrows_ & 1 ? 1.0f : 0.0f);
        const float y = (arrows_ & 8 ? 1.0f : 0.0f) - (arrows_ & 4 ? 1.0f : 0.0f);
        session.tilt(x, y);
    } else {
        session.pad(arrows_);
        session.tilt(0, 0);
    }
}

void GameScreen::reset() {
    movieGesture_ = stylusGesture_ = blowing_ = pinching_ = false;
    fingers_.clear();
    held_.reset();
    release_.reset();
    view_ = {};
}

namespace {
constexpr double kMaxZoom = 4;
} // namespace

ScreenRect GameScreen::zoomed(const ScreenRect& r) const {
    return {r.x * view_.zoom + view_.x, r.y * view_.zoom + view_.y, r.width * view_.zoom,
            r.height * view_.zoom};
}

void GameScreen::clampView(float width, float height) {
    // Zoomed in, the screens cover the window where they can (no empty
    // band opens beside them); where they are smaller than the window,
    // they stay centred as without zoom.
    view_.zoom = std::clamp(view_.zoom, 1.0, kMaxZoom);
    const auto l = layout(width, height, geometry_);
    const double x0 = std::min(l.top.x, l.bottom.x), y0 = std::min(l.top.y, l.bottom.y);
    const double x1 = std::max(l.top.x + l.top.width, l.bottom.x + l.bottom.width),
                 y1 = std::max(l.top.y + l.top.height, l.bottom.y + l.bottom.height);
    auto axis = [&](double& offset, double lo, double hi, double window) {
        const double size = (hi - lo) * view_.zoom;
        if (size <= window) {
            offset = (window - size) / 2 - lo * view_.zoom;
        } else {
            offset = std::clamp(offset, window - hi * view_.zoom, -lo * view_.zoom);
        }
    };
    axis(view_.x, x0, x1, width);
    axis(view_.y, y0, y1, height);
}

void GameScreen::startPinch(GameSession& session) {
    // A held press is dropped: the game never sees the pinch. A stylus the
    // first finger already put down slides off the screen and lifts there.
    pinching_ = true;
    held_.reset();
    passRelease(session, width_, height_); // a tap just before ends as a tap
    if (stylusGesture_) {
        stylusGesture_ = false;
        session.touch(true, 100000, 100000, true);
        session.touch(false, 100000, 100000, false);
    }
    const auto& a = fingers_.begin()->second;
    const auto& b = std::next(fingers_.begin())->second;
    pinchX_ = (a.first + b.first) / 2.0;
    pinchY_ = (a.second + b.second) / 2.0;
    pinchDistance_ = std::max(1.0, double(std::hypot(a.first - b.first, a.second - b.second)));
}

void GameScreen::pinchTo(double x, double y, double distance) {
    // The point of the screens under the fingers' middle stays under it:
    // spreading zooms in around it, moving both fingers moves the screens.
    const double zoom =
        std::clamp(view_.zoom * distance / std::max(1.0, pinchDistance_), 1.0, kMaxZoom);
    const double px = (pinchX_ - view_.x) / view_.zoom, py = (pinchY_ - view_.y) / view_.zoom;
    view_ = {zoom, x - px * zoom, y - py * zoom};
    pinchX_ = x;
    pinchY_ = y;
    pinchDistance_ = std::max(1.0, distance);
}

namespace {
constexpr double kHoldBack = 0.15; // seconds a resting finger waits for a second one
constexpr float kSlop = 8;         // window points a held finger may move
constexpr double kShortestTap = 2.0 / 60;
} // namespace

void GameScreen::passHeld(GameSession& session, float width, float height) {
    if (!held_) {
        return;
    }
    Event press;
    press.type = Event::Type::MouseDown;
    press.button = 1;
    press.x = held_->x;
    press.y = held_->y;
    held_.reset();
    handle(press, session, width, height);
}

void GameScreen::passRelease(GameSession& session, float width, float height) {
    if (!release_) {
        return;
    }
    Event up;
    up.type = Event::Type::MouseUp;
    up.button = 1;
    up.x = release_->x;
    up.y = release_->y;
    release_.reset();
    handle(up, session, width, height);
}

void GameScreen::update(double now, GameSession& session, float width, float height) {
    now_ = now;
    width_ = width;
    height_ = height;
    geometry_ = session.geometry();
    if (held_ && now - held_->at >= kHoldBack) {
        passHeld(session, width, height);
    }
    if (release_ && now >= release_->at) {
        passRelease(session, width, height);
    }
    clampView(width, height);
}

bool GameScreen::handle(const Event& e, GameSession& session, float width, float height) {
    const auto state = session.state();
    geometry_ = session.geometry();
    const auto l = layout(width, height, geometry_);
    blow_ = blowButton(width, height, state);
    // A window point to lower-screen pixels (may be outside 0..255, 0..191),
    // through the zoom.
    auto toScreen = [&](float x, float y) {
        const double ux = (x - view_.x) / view_.zoom, uy = (y - view_.y) / view_.zoom;
        return std::pair{
            int(std::floor((ux - l.bottom.x) * geometry_.bottomWidth / l.bottom.width)),
            int(std::floor((uy - l.bottom.y) * geometry_.bottomHeight / l.bottom.height))};
    };
    auto inside = [&](const ScreenRect& r, float x, float y) {
        const ScreenRect z = zoomed(r);
        return x >= z.x && y >= z.y && x < z.x + z.width && y < z.y + z.height;
    };

    // Fingers: two make a pinch, until all have lifted.
    switch (e.type) {
    case Event::Type::TouchDown:
        fingers_[e.finger] = {e.x, e.y};
        if (fingers_.size() == 2) {
            startPinch(session);
        }
        return false;
    case Event::Type::TouchMove:
        if (auto it = fingers_.find(e.finger); it != fingers_.end()) {
            it->second = {e.x, e.y};
            if (pinching_ && fingers_.size() >= 2) {
                const auto& a = fingers_.begin()->second;
                const auto& b = std::next(fingers_.begin())->second;
                pinchTo((a.first + b.first) / 2.0, (a.second + b.second) / 2.0,
                        std::hypot(a.first - b.first, a.second - b.second));
                clampView(width, height);
            }
        }
        return false;
    case Event::Type::TouchUp:
        fingers_.erase(e.finger);
        if (fingers_.size() >= 2) {
            startPinch(session); // the pinch goes on with the fingers left
        } else if (fingers_.empty()) {
            pinching_ = false;
            if (view_.zoom < 1.05) {
                view_.zoom = 1; // nearly out: all the way
                clampView(width, height);
            }
        }
        return false;
    default:
        break;
    }
    // The stylus from a finger: nothing during a pinch; a press waits until
    // it is clearly not a pinch's first finger (see the header).
    if (e.touch && (e.type == Event::Type::MouseDown || e.type == Event::Type::MouseMove ||
                    e.type == Event::Type::MouseUp)) {
        if (pinching_) {
            return false;
        }
        if (e.type == Event::Type::MouseDown && e.button == 1 && !blow_.contains(e.x, e.y)) {
            passRelease(session, width, height); // a tap before still lifts first
            held_ = Held{e.x, e.y, now_};
            return false;
        }
        if (held_ && e.type == Event::Type::MouseMove) {
            if (std::hypot(e.x - held_->x, e.y - held_->y) < kSlop) {
                return false;
            }
            passHeld(session, width, height); // a stroke: the press, then this move
        }
        if (held_ && e.type == Event::Type::MouseUp) {
            // A tap: the press now, the release once the tap's own length
            // has passed again (update()).
            const double length = std::max(kShortestTap, now_ - held_->at);
            passHeld(session, width, height);
            release_ = Held{e.x, e.y, now_ + length};
            return false;
        }
    }

    switch (e.type) {
    case Event::Type::MouseMove:
        mouseX_ = e.x;
        mouseY_ = e.y;
        // Dragging outside the lower screen keeps the stylus down (and lets a
        // DS button notice that the stylus left it).
        if (e.button == 1 && stylusGesture_ && !movieGesture_) {
            const auto [x, y] = toScreen(e.x, e.y);
            session.touch(true, x, y, true);
        }
        break;
    case Event::Type::MouseDown: {
        if (e.button != 1) {
            break;
        }
        if (blow_.contains(e.x, e.y)) {
            blowing_ = true;
            session.blow(true);
            return false;
        }
        const bool doubleClick =
            e.clicks >= 2 && (inside(l.top, e.x, e.y) || inside(l.bottom, e.x, e.y));
        if (state.scene == SceneKind::Movie) {
            // A click during a movie is never a stylus contact in the next
            // scene, even if its release arrives after the movie has ended.
            movieGesture_ = true;
            session.liftStylus();
            return doubleClick;
        }
        // Presses on the upper screen do not become stylus contacts.
        const auto [x, y] = toScreen(e.x, e.y);
        stylusGesture_ =
            x >= 0 && x < geometry_.bottomWidth && y >= 0 && y < geometry_.bottomHeight;
        if (stylusGesture_) {
            session.touch(true, x, y, false);
        }
        return doubleClick;
    }
    case Event::Type::MouseUp:
        if (e.button != 1) {
            break;
        }
        if (blowing_) {
            blowing_ = false;
            session.blow(false);
        } else if (movieGesture_) {
            movieGesture_ = false;
        } else if (stylusGesture_) {
            stylusGesture_ = false;
            const auto [x, y] = toScreen(e.x, e.y);
            session.touch(false, x, y, false);
        }
        break;
    case Event::Type::KeyDown:
        if (e.command) {
            break;
        }
        // The arrow keys are the pad; with Shift they tilt the console
        // (the 3DS games' motion sensor).
        if (const unsigned bit = arrowBit(e.key)) {
            arrows_ |= bit;
            tilting_ = e.shift;
            sendArrows(session);
        }
        // Holding B blows into the microphone (only puzzles listen to it).
        if (e.key == Key::Character && e.symbol == 'b' && state.scene == SceneKind::Puzzle) {
            session.blow(true);
        }
        // Holding S shakes the console (the sixth adventure's shake puzzles).
        if (e.key == Key::Character && e.symbol == 's') {
            session.shake(true);
        }
        if (state.scene == SceneKind::Name) {
            if (e.key == Key::Return) {
                session.nameInput({NameInput::Action::Confirm, 0});
            } else if (e.key == Key::Backspace || e.key == Key::Delete) {
                session.nameInput({NameInput::Action::Erase, 0});
            }
        }
        break;
    case Event::Type::KeyUp:
        if (e.key == Key::Character && e.symbol == 'b') {
            session.blow(false);
        }
        if (e.key == Key::Character && e.symbol == 's') {
            session.shake(false);
        }
        if (const unsigned bit = arrowBit(e.key)) {
            arrows_ &= ~bit;
            sendArrows(session);
        }
        break;
    case Event::Type::FocusLost:
        blowing_ = false;
        session.blow(false);
        session.shake(false);
        arrows_ = 0;
        sendArrows(session);
        break;
    case Event::Type::Text:
        // Typed characters for the name, in the game's character set; the
        // name editor applies the game's own rules to them.
        if (state.scene == SceneKind::Name) {
            for (const char c : cp1252FromUtf8(e.text)) {
                if (std::uint8_t(c) >= 32) {
                    session.nameInput({NameInput::Action::Insert, std::uint8_t(c)});
                }
            }
        }
        break;
    default:
        break;
    }
    return false;
}

} // namespace picarat::shell
