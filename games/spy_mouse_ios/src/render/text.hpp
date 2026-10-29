// Text as the original draws it (ManagerFont / ManagerFontFT @0x134b44.., backend
// ManagerBackendFT @0x282da0..): TrueType fonts from the game files rasterised with FreeType
// at a pixel size, glyph pictures that are only coverage (tinted when drawn), and a global
// style: an optional shadow (the outlined shape, offset), an optional outline (stroked with
// FreeType's stroker) and the fill. A string is drawn shadow first, then outline, then fill
// (renderLayoutWithState @0x21ead8).
#pragma once

#include "platform/host.hpp"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace doc::render {

struct Colour {
    float r = 1, g = 1, b = 1, a = 1;
    static Colour bytes(int r, int g, int b, int a = 255) { return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}; }
};

// Outline shapes (FontOutline): 1 a stroke along the glyph edges, 2 the inside border,
// 3 the glyph grown by its outside border.
enum class Outline : int { None = 0, Stroke = 1, Inside = 2, Grown = 3 };

struct TextStyle {
    bool fill = true;
    bool fillColourSet = false;  // setFill was called; else the current colour is used
    Colour fillColour;
    Outline outline = Outline::None;
    float outlineWidth = 0.0f;   // pixels
    Colour outlineColour;
    bool shadow = false;
    float shadowX = 0.0f, shadowY = 0.0f;  // screen pixels, y down
    Colour shadowColour;
};

// A scale and shift of screen points (x' = ax x + bx, y' = ay y + by): how a GUI frame that
// is popping in (GuiBounceFrame::render @0x1d6f54: glScalef about the frame's centre) moves
// what it holds.
struct Affine {
    float ax = 1, bx = 0, ay = 1, by = 0;
    float x(float v) const { return ax * v + bx; }
    float y(float v) const { return ay * v + by; }
    // This transform applied after `inner`.
    Affine after(const Affine& inner) const { return {ax * inner.ax, ax * inner.bx + bx, ay * inner.ay, ay * inner.by + by}; }
};

class Font {
public:
    ~Font();
    // `ttf`: the font file's bytes (kept alive by the font).
    static std::unique_ptr<Font> load(std::vector<std::uint8_t> ttf, int pixelSize, std::string& error);

    int pixelSize() const { return pixelSize_; }
    float lineHeight() const { return lineHeight_; }
    float ascent() const { return ascent_; }
    // Width of a string (UTF-8) at a scale.
    float width(const std::string& text, float scale = 1.0f);
    // renderStr / renderStrWrap with any flags (ManagerFontFT::renderStr @0x21ed30,
    // finalizeLayout @0x21d19c): `flags` 1 left, 2 centre, 4 right (each line by its ink),
    // 0x10 top, 0x20 middle, 0x40 bottom on y (lines reach from the top of 'A' to the
    // bottom of 'y'), none: first baseline on y. (x, y)
    // are screen points, y down. `wrapWidth` > 0 breaks lines between words to fit.
    void drawFlags(platform::Host& host, const std::string& text, float x, float y, int flags, float scale,
                   float wrapWidth, const TextStyle& style, const Colour& current, const Affine& xf = {});
    // Height of the wrapped text (ManagerFont::getWrapHeight).
    float wrapHeight(platform::Host& host, const std::string& text, float wrapWidth, float scale);

private:
    struct Glyph {
        platform::Texture texture;
        float left = 0, top = 0;  // bitmap offset from the pen (pixels, y up)
        int width = 0, height = 0;
    };
    struct Shape {
        unsigned index = 0;
        float advance = 0;
        float inkLeft = 0;   // the rendered glyph's left edge from the pen (bitmap left)
        float inkWidth = 0;  // and its width (bitmap width)
    };
    // How wide a line is for word wrapping: from the first glyph's ink to the last glyph's
    // (layoutStrWrap @0x21dbe8), not by advances.
    float inkWidth(const std::string& text, float scale);
    Font() = default;
    const Shape& shape(char32_t c);
    Glyph* glyph(platform::Host& host, unsigned index, Outline outline, float width);
    std::vector<std::pair<unsigned, float>> layout(const std::string& text, float& total);
    std::vector<std::string> wrapLines(const std::string& text, float wrapWidth, float scale);
    void lineExtent(platform::Host& host, float& top, float& bottom);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::uint8_t> data_;
    int pixelSize_ = 0;
    float lineHeight_ = 0, ascent_ = 0;
    std::map<char32_t, Shape> shapes_;
    std::map<std::tuple<unsigned, int, int>, std::unique_ptr<Glyph>> glyphs_;
};

}  // namespace doc::render
