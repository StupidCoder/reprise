// Launcher drawing kit: a proportional pixel font (the shell font shared with the other
// reconstruction projects) and a few primitive shapes, drawn into an RGBA canvas that the host
// uploads as a texture. No game graphics are used.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace doc::shell {

struct Color {
    std::uint8_t r{}, g{}, b{}, a{255};
};

// RGBA pixels, rows top to bottom (the byte order of SDL_PIXELFORMAT_RGBA32).
struct Image {
    int width = 0, height = 0;
    std::vector<Color> pixels;
    Image(int w, int h) : width(w), height(h), pixels(std::size_t(w) * std::size_t(h)) {}
};

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
int text_right(Image&, std::string_view value, int right, int y, int scale, Color color, Ink ink = Ink::plain,
               Color edge = {0, 0, 0});
// Greedy word wrap by measured width; returns the number of lines drawn.
int wrapped(Image&, std::string_view value, int x, int y, int width, int max_lines, int scale, Color color);

Color mix(Color a, Color b, int amount);  // amount 0..255 towards b
void fill(Image&, Color);
void rect(Image&, int x, int y, int w, int h, Color);
void blend_rect(Image&, int x, int y, int w, int h, Color, int alpha);
// Corners are cut in pixel steps rather than antialiased.
void rounded(Image&, int x, int y, int w, int h, int radius, Color);

}  // namespace doc::shell
