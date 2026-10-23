#pragma once
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <span>

// Drawing C64 character screens. A game builds a TextFrame from its own
// state every frame (which character, in which colour, in each of the 40x25
// cells) together with the colours that apply line by line, and rasterises
// it into a Picture: the 320x200 display window as C64 colour numbers plus
// the border colour. The TextFrame is a drawing surface only, never a
// game's model of its screen.
namespace c64 {

using Glyph = std::array<std::uint8_t, 8>;

struct Picture {
    static constexpr int width = 320, height = 200;
    std::array<std::uint8_t, width * height> pixels{};
    std::uint8_t border{};

    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y * width + x)]; }

    // The picture with `margin` pixels of border around it, in RGB colours.
    platform::Image to_image(int margin_x, int margin_y) const;
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
};

class TextFrame {
public:
    static constexpr int columns = 40, rows = 25;

    struct Cell {
        const Glyph* glyph{};      // nullptr: empty (all pixels background)
        std::uint8_t colour{};     // cell colour; bit 3 selects multicolour
    };

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

    // Draws the cells line by line. Which character row and which of its
    // eight lines appear on a raster line follows the VIC-II's row
    // sequencing (VC/VCBASE/RC, "bad lines" where (raster & 7) == yscroll
    // between rasters $30 and $F7), so a scroll change in the middle of the
    // screen moves, repeats or skips lines as on the real machine.
    Picture rasterise() const;

private:
    std::array<Cell, columns * rows> cells_{};
    std::array<LineSetup, Picture::height> lines_{};
    std::uint8_t idle_pattern_{};
};

} // namespace c64
