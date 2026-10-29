#pragma once
// The shell's controls, drawn with the style kit: the palette, buttons and
// the modal message box. Screens lay controls out in layout units, keep
// their rectangles for hit testing, and draw them every time they redraw
// (immediate mode); there is no retained widget tree.
#include "platform/host.hpp"
#include "shell/style.hpp"
#include <string>
#include <vector>

namespace picarat::shell::ui {

using platform::Color;
using platform::Rect;

// Ivory paper, teal ink and brass, the library's colours.
inline constexpr Color kPaper{242, 237, 224};
inline constexpr Color kCard{255, 251, 240};
inline constexpr Color kInk{37, 55, 52};
inline constexpr Color kMuted{92, 105, 96};
inline constexpr Color kGold{47, 91, 82};
inline constexpr Color kLine{213, 207, 188};
inline constexpr Color kRowAlternate{233, 232, 218};
inline constexpr Color kDisabled{214, 207, 192};
inline constexpr Color kDisabledInk{113, 120, 109};
inline constexpr Color kWhite{255, 255, 255};
inline constexpr Color kShadow{40, 28, 16, 40};
inline constexpr Color kToolbar{29, 53, 49};

inline constexpr Color kBrass{164, 125, 65};

void paper(style::Canvas&, float width, float height);
void ornament(style::Canvas&, float x, float y, float width);

enum class ButtonKind {
    Plain,   // light button with a fine outline
    Primary, // teal, the default action
    Toolbar, // on the dark game toolbar
};

// Draws a button; `hover` brightens it, a disabled button is greyed out.
void button(style::Canvas&, const Rect&, std::string_view label, ButtonKind kind, bool enabled,
            bool hover, float textSize = 2);

// A modal message box over the whole window: a title, a message and one to
// three buttons. Return presses the first button, Escape the last.
class Dialog {
public:
    // `id` tells the owner which question was answered.
    void open(int id, std::string title, std::string message, std::vector<std::string> buttons);

    bool isOpen() const { return open_; }

    struct Answer {
        int id = 0;
        int button = -1; // index into the buttons, -1 while unanswered
    };

    // Handles an event in window points; returns the answer once a button
    // was chosen (the dialog then closes).
    Answer handle(const platform::Event& event);
    // Draws over a canvas whose layout units are window points.
    void draw(style::Canvas& canvas, float width, float height);

private:
    bool open_ = false;
    int id_ = 0;
    std::string title_, message_;
    std::vector<std::string> buttons_;
    std::vector<Rect> buttonRects_;
    float mouseX_ = -1, mouseY_ = -1;
};

} // namespace picarat::shell::ui
