#pragma once
// The in-game menu: Escape on a computer, the Back key or gesture on a phone.
// While it is open the game is frozen (GameSession::sleep: no game frames,
// the sound paused) and its last frame stays on screen, dimmed, behind a
// panel with the menu's buttons: Continue playing, Skip, while the game plays something it
// can skip (a movie, logos, a narrated picture; system/game_types.hpp), the
// touch-area help's switch in the story (rooms outline what there is to
// touch, coloured by what a touch starts; a legend shows the colours), and
// Quit, back to the page the game was started from. Escape or Back again, or
// a click outside the panel, closes it and the game goes on.
//
// Like the siblings' menus (BlueStreak's pause menu, Diorama's GameMenu):
// arrows move, Return chooses, the mouse highlights by hovering.
#include "platform/host.hpp"
#include "shell/style.hpp"
#include <optional>
#include <string>
#include <vector>

namespace picarat::shell {

class GameMenu {
public:
    enum class Action { None, Resume, Skip, TouchAreas, Quit };

    // `title` names what is being played ("Pandora's Box · Hamster");
    // `canSkip` offers Skip; `touchAreas` the touch-area switch in that
    // state (none: not offered).
    void open(std::string title, bool canSkip, std::optional<bool> touchAreas = std::nullopt);

    // The touch-area switch: TouchAreas flips it and leaves the menu open.
    bool touchAreas() const { return touchAreas_.value_or(false); }

    bool isOpen() const { return open_; }

    void close() { open_ = false; }

    // An event in window points while the menu is open.
    Action handle(const platform::Event& event);
    // Draws over a transparent canvas whose layout units are window points.
    void draw(style::Canvas& canvas, float width, float height);

private:
    enum class Item { Resume, Skip, TouchAreas, Quit };

    bool open_ = false;
    std::optional<bool> touchAreas_;
    std::string title_;
    std::vector<Item> items_; // the buttons shown, top to bottom
    int selected_ = 0;        // index into items_
    platform::Rect panel_{};
    std::vector<platform::Rect> rects_;
    float mouseX_ = -1, mouseY_ = -1;
};

} // namespace picarat::shell
