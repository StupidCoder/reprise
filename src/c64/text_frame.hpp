#pragma once
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Drawing C64 character screens. A game builds a TextFrame from its own
// state every frame (which character, in which colour, in each of the 40x25
// cells) together with the colours that apply line by line, and rasterises
// it into a Picture: the 320x200 display window as C64 colour numbers plus
// the border colour. The TextFrame is a drawing surface only, never a
// game's model of its screen.
//
// A frame can have more than 40 columns, for a widescreen view: the extra
// columns lie evenly to the left and right of the C64's own 40, and the
// picture grows with them (8 pixels a column). Sprites keep their VIC
// coordinates relative to the original window.
namespace c64 {

using Glyph = std::array<std::uint8_t, 8>;

struct Picture {
    static constexpr int standard_width = 320, height = 200;
    int width{standard_width};
    std::vector<std::uint8_t> pixels;
    std::uint8_t border{};

    explicit Picture(int w = standard_width) : width(w), pixels(std::size_t(w * height)) {}
    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y * width + x)]; }

    // The picture with `margin` pixels of border around it, in RGB colours;
    // or `width` of its columns from `left`.
    platform::Image to_image(int margin_x, int margin_y) const;
    platform::Image to_image(int margin_x, int margin_y, int left, int width) const;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(width, pixels, border); }
};

// The colours, window and scroll that apply to one raster line.
struct LineSetup {
    std::uint8_t background{}, shared1{}, shared2{}, border{};
    bool multicolour{true};
    // The 38-column window (the VIC's CSEL bit cleared): the first 7 and last
    // 9 pixels of the line show the border colour.
    bool narrow{};
    // The line is covered by the vertical border (24-row window).
    bool covered{};
    // Fine scroll: the picture moves right by xscroll pixels; yscroll decides
    // on which lines character rows start (see rasterise()).
    std::uint8_t xscroll{0}, yscroll{3};
    // When $D011 is rewritten early in this line (before cycle 14), the
    // scroll value in effect before the write: a bad-line match in those
    // first cycles still switches the VIC to display state.
    int yscroll_before{-1};
    // Hardware sprites switched on (bit n = sprite n). A sprite starts only
    // if it is switched on in the line of its y position.
    std::uint8_t sprites{0xFF};
    // When the colour registers are rewritten during this line: the pixels
    // left of this x still show the background and border colours before.
    int background_from_x{0};
    std::uint8_t background_before{}, border_before{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(background, shared1, shared2, border, multicolour, narrow, covered, xscroll, yscroll,
           yscroll_before, sprites, background_from_x, background_before, border_before);
    }
};

// A hardware sprite as the VIC shows it: 24x21 single-colour pixels (three
// bytes per row), placed by the VIC's coordinates (x 24 and y 51 are the
// top left of the display window; the sprite starts on line y + 1).
struct Sprite {
    bool shown{};
    int x{}, y{};
    bool expand_x{};
    bool behind_foreground{}; // foreground graphics cover it
    // Drawn into the picture. A sprite that is not drawn still collides, so
    // a presentation layer can draw its own version of it.
    bool drawn{true};
    std::uint8_t colour{};
    std::array<std::uint8_t, 63> data{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(shown, x, y, expand_x, behind_foreground, drawn, colour, data);
    }
};

// The VIC's collision registers for one frame: sprites touching each other,
// and sprites touching foreground graphics (multicolour pairs 10 and 11,
// single-colour 1 bits), counted under the border too, as the chip does.
struct Collisions {
    std::uint8_t sprite_sprite{}, sprite_background{};
    // Not a VIC register: sprites over any graphics pixel, also multicolour
    // pair 01 (which the VIC counts as background), for enhancements.
    std::uint8_t sprite_graphics{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(sprite_sprite, sprite_background, sprite_graphics); }
};

// What a picture is made of, for layers drawn on top of it: which pixels
// are foreground graphics (they cover sprites behind them), and which line
// of which character row each picture line shows.
struct Layers {
    std::vector<bool> foreground; // picture width x height
    bool foreground_at(int x, int y, int width) const {
        return foreground[std::size_t(y * width + x)];
    }
    // Character row * 8 + line within it, or -1 where no row is shown (idle
    // lines, the vertical border).
    std::array<std::int16_t, Picture::height> source_line{};
    std::array<std::uint8_t, Picture::height> xscroll{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(foreground, source_line, xscroll); }
};

class TextFrame {
public:
    static constexpr int standard_columns = 40, rows = 25;

    explicit TextFrame(int columns = standard_columns);
    int columns() const { return columns_; }
    int width() const { return columns_ * 8; }
    // Pixels left of the original window (0 for a standard frame).
    int extension() const { return (columns_ - standard_columns) * 4; }

    struct Cell {
        Glyph glyph{};             // a copy, so frames can be kept and copied
        std::uint8_t colour{};     // cell colour; bit 3 selects multicolour

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(glyph, colour); }
    };

    // A null glyph is an empty cell (all pixels background).
    void set(int column, int row, const Glyph* glyph, std::uint8_t colour);
    void set_glyph(int column, int row, const Glyph* glyph);
    void set_colour(int column, int row, std::uint8_t colour);
    const Cell& cell(int column, int row) const;
    void clear();

    // The same setup on every line, or one line's setup.
    void set_lines(const LineSetup& setup);
    void set_lines(int first, int end, const LineSetup& setup);

    // The byte the VIC shows on lines where no character row is active
    // ("idle" lines): the last byte of the video bank.
    void set_idle_pattern(std::uint8_t pattern) { idle_pattern_ = pattern; }
    std::uint8_t idle_pattern() const { return idle_pattern_; }

    // Draws the cells line by line. Which character row and which of its
    // eight lines appear on a raster line follows the VIC-II's row
    // sequencing (VC/VCBASE/RC and the row buffer filled on "bad lines",
    // where (raster & 7) == yscroll between rasters $30 and $F7), so a scroll
    // change in the middle of the screen moves, repeats or skips rows as on
    // the real machine.
    Picture rasterise(Collisions* collisions = nullptr, Layers* layers = nullptr) const;

    const LineSetup& line(int y) const { return lines_[std::size_t(y)]; }
    const Sprite& sprite(int index) const { return sprites_[std::size_t(index)]; }
    void set_sprite(int index, const Sprite& sprite) {
        sprites_[std::size_t(index)] = sprite;
        sprites_[std::size_t(index)].colour &= 15; // the VIC's colour registers have 4 bits
    }

private:
    int columns_{standard_columns};
    std::vector<Cell> cells_;
    std::array<LineSetup, Picture::height> lines_{};
    std::uint8_t idle_pattern_{};
    std::array<Sprite, 8> sprites_{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(columns_, cells_, lines_, idle_pattern_, sprites_); }
};

} // namespace c64
