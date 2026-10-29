#pragma once
// The shell drawing kit: scalable bundled Lato text, smooth outline icons,
// and software shapes. Canvas scales layout points to device pixels; text
// is rasterized at that exact density, including accents and kerning.
// The game's own text and graphics are not affected.
#include "platform/image.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace picarat::shell::style {

using platform::Color;
using platform::Image;

struct FontStrike;

// Layout units per em for text size 1 (size 2, body text, is the font's
// 16-pixel design at scale 1), and the line spacing in ems.
inline constexpr float kEmUnits = 8;
inline constexpr float kLineEms = 1.375f;

// A strike of the font and the whole number its pixels are enlarged by.
struct FontSize {
    const FontStrike* strike;
    int factor;
};

// The font size closest to an em of `pixels`.
FontSize fontSize(int pixels);

// Private icon code points, usable inside any string drawn as text.
inline constexpr std::string_view kIconLeft = "\x01", kIconRight = "\x02", kIconUp = "\x03",
                                  kIconDown = "\x04", kIconCheck = "\x05", kIconSearch = "\x08",
                                  kIconPuzzle = "\x10", kIconMinigame = "\x11", kIconMovie = "\x12",
                                  kIconMusic = "\x13", kIconRestart = "\x14";

// --- Pixel level -----------------------------------------------------------

// Width in pixels of UTF-8 text.
int textWidth(std::string_view utf8, FontSize font);
// Draws text with the top of its capitals at y, starting at x; returns the x
// after it. Accents and descenders reach beyond the capitals.
int text(Image&, std::string_view utf8, int x, int y, FontSize font, Color color);

Color mix(Color a, Color b, int amount); // amount 0..255 towards b
void fill(Image&, Color);
void rect(Image&, int x, int y, int w, int h, Color);
// Source-over compositing with the colour's alpha.
void blendRect(Image&, int x, int y, int w, int h, Color);
// Rectangle with corners cut in pixel steps (no anti-aliasing), composited
// with the colour's alpha.
void rounded(Image&, int x, int y, int w, int h, int radius, Color);
// Copies `source` into the rectangle with bilinear filtering, cropped to
// fill it without distortion, faded by `opacity` 0..255.
void picture(Image&, const Image& source, int x, int y, int w, int h, int opacity);
// A copy of `source` resized to w x h by averaging (for shrinking artwork
// with transparency, such as the logo).
Image scaled(const Image& source, int w, int h);
// Composites `source` at (x, y) by its alpha, without scaling.
void blend(Image&, const Image& source, int x, int y);

// --- UTF-8 -----------------------------------------------------------------

// Decodes one code point at `index` and advances it. Bytes that are not
// valid UTF-8 are read as Latin-1, so stray CP1252 text cannot break it.
char32_t nextCodePoint(std::string_view utf8, std::size_t& index);
void appendUtf8(std::string& out, char32_t c);
// Lower-case copy for case-insensitive search (ASCII and Latin-1 letters).
std::string foldCase(std::string_view utf8);
// Removes the last code point (for Backspace in a text field).
void popCodePoint(std::string& utf8);

// --- Layout level ----------------------------------------------------------

// Draws into an image in layout units: (x, y) in layout units maps to
// (x * scale, y * scale) pixels. Text sizes are em sizes in layout
// units / kEmUnits (1.5 = small print, 2 = body text, 3 = headings, 5 =
// titles). The font is drawn at a whole-pixel size near the asked one, so
// always measure text with the Canvas rather than computing widths.
class Canvas {
public:
    Canvas(Image& image, float scale) : image_(image), scale_(scale) {}

    float scale() const { return scale_; }

    int px(float v) const;

    // The font strike and enlargement used for a text size.
    FontSize font(float size) const;
    float textWidth(std::string_view utf8, float size) const;
    // Height of capital letters, for centring text vertically.
    float capHeight(float size) const;

    // Draw an icon in an explicit square, independent of text metrics.
    void icon(std::string_view symbol, float x, float y, float side, Color color);
    float text(std::string_view utf8, float x, float y, float size, Color color);
    float textRight(std::string_view utf8, float right, float y, float size, Color color);
    float textCentred(std::string_view utf8, float centreX, float y, float size, Color color);
    // Greedy word wrap by measured width: the lines, and drawing them
    // (returns the number of lines drawn).
    std::vector<std::string_view> lines(std::string_view utf8, float width, float size) const;
    int wrapped(std::string_view utf8, float x, float y, float width, int maxLines, float size,
                Color color);
    float lineHeight(float size) const;
    // Shortens text with "..." until it fits `width`.
    std::string ellipsize(std::string_view utf8, float width, float size) const;

    void fill(Color color);
    void rect(float x, float y, float w, float h, Color color);
    void rounded(float x, float y, float w, float h, float radius, Color color);
    // A rounded outline `thickness` layout units wide on top of `inside`.
    void frame(float x, float y, float w, float h, float radius, float thickness, Color edge,
               Color inside);
    // The picture is px(w) x px(h) device pixels wherever it is placed;
    // cache pre-scaled copies at that size.
    void picture(const Image& source, float x, float y, float w, float h, int opacity = 255);
    // Composites an image already scaled to device pixels (style::scaled)
    // with its top-left corner at (x, y).
    void blend(const Image& source, float x, float y);

private:
    Image& image_;
    float scale_;
};

} // namespace picarat::shell::style
