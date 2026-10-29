#pragma once
// Shell drawing kit: bundled portable typography, primitive
// shapes and the per-generation visual themes. No cartridge graphics are used.
#include "platform/image.hpp"
#include <string_view>

namespace shell::style {
using platform::Color;
using platform::Image;

// Glyph rows: 0..6 cap height, 2..6 x-height, 7..8 descenders.
inline constexpr int glyph_rows = 9, line_rows = 11;
// Private icon code points, usable inside any string drawn with text().
inline constexpr char icon_left = '\x01', icon_right = '\x02', icon_up = '\x03', icon_down = '\x04',
                      icon_note = '\x05', icon_play = '\x06', icon_lock = '\x07';
enum class Ink { plain, shadow, outline };
int text_width(std::string_view value, int scale);
// Returns the x position after the last glyph.
int text(Image&, std::string_view value, int x, int y, int scale, Color color, Ink ink = Ink::plain,
         Color edge = {0, 0, 0});
int text_right(Image&, std::string_view value, int right, int y, int scale, Color color,
               Ink ink = Ink::plain, Color edge = {0, 0, 0});
// Greedy word wrap by measured width; returns the number of lines drawn.
int wrapped(Image&, std::string_view value, int x, int y, int width, int max_lines, int scale,
            Color color);

Color mix(Color a, Color b, int amount); // amount 0..255 towards b
void fill(Image&, Color);
void rect(Image&, int x, int y, int w, int h, Color);
void blend_rect(Image&, int x, int y, int w, int h, Color, int alpha);
// Corners are cut in pixel steps rather than antialiased.
void rounded(Image&, int x, int y, int w, int h, int radius, Color);
// Top edge shifted right by slant pixels; negative slant leans left.
void slanted(Image&, int x, int y, int w, int h, int slant, Color);
void checker(Image&, int x, int y, int w, int h, int cell, Color a, Color b, int offset = 0);
// Diagonal bands rising to the right; offset scrolls the pattern.
void stripes(Image&, int x, int y, int w, int h, int period, int thickness, Color, int offset = 0);
void dots(Image&, int x, int y, int w, int h, int period, Color);
void ring(Image&, int cx, int cy, int radius, int thickness, Color, int cx0, int cy0, int cx1,
          int cy1);

struct Theme {
    Color bg, bg_alt, accent, accent2, ink, card, card_ink, muted;
    int pattern; // 0 checker, 1 diagonal stripes, 2 dot grid
    int border, radius;
};

// Fills a rectangle with a generation's background pattern.
void backdrop(Image&, const Theme&, int x, int y, int w, int h, int scroll = 0);
// Shadowed panel in the theme's corner style.
void panel(Image&, const Theme&, int x, int y, int w, int h, Color fill, Color border);
void badge(Image&, const Theme&, std::string_view label, int x, int y, int scale = 2);
// Small key/button chip in a hint bar; returns x after it.
int key_hint(Image&, std::string_view key, std::string_view action, int x, int y);

// Shared frame colours.
inline constexpr Color frame_blue{49, 103, 246}, frame_red{16, 34, 56}, frame_gold{113, 229, 226},
    frame_night{10, 14, 36}, white{255, 255, 255}, black{0, 0, 0};

inline bool reduce_motion = false;
double ease_out(double t);
double ease_back(double t);
} // namespace shell::style
