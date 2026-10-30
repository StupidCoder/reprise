#pragma once
#include "ui/draw.hpp"
#include <string_view>

// Collector's shelf presentation. Separate from the original pixel UI used by
// decoded-data viewers and game modules, so gameplay artwork is unaffected.
namespace ui::shelf {
inline constexpr Color paper{220, 222, 215}, card{243, 242, 233}, ink{21, 59, 71},
    muted{76, 101, 108}, line{193, 201, 195}, cobalt{37, 78, 207}, pale{209, 220, 239},
    green{44, 126, 79}, red{169, 57, 38}, white{255, 255, 255};
int width(std::string_view, int size = 16, bool bold = false);
int text(Image&, std::string_view, int x, int y, int size = 16, Color = ink, bool bold = false);
void right(Image&, std::string_view, int x, int y, int size = 16, Color = ink, bool bold = false);
void clipped(Image&, std::string_view, int x, int y, int w, int size = 16, Color = ink, bool bold = false);
// Paragraph-aware prose layout. Returns consumed pixel height, including gaps.
int prose(Image&, std::string_view, int x, int y, int w, int size, Color);
int wrap(Image&, std::string_view, int x, int y, int w, int lines, int size = 16, Color = muted);
void button(Image&, std::string_view, int x, int y, int w, int h, bool primary = false);
// A destructive action: red outline and text, or filled red for the
// confirmation itself.
void danger_button(Image&, std::string_view, int x, int y, int w, int h, bool filled = false);
void cover(Image&, std::string_view id, int x, int y, int w, int h);
// Original bitmap atmosphere; decoded and resized once, rendered through SDL.
void backdrop(Image&, std::string_view collection);
void reprise(Image&, int x, int y);
void port(Image&, int x, int y, int scale = 1);
}
