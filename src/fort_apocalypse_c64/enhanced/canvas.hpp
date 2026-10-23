#pragma once
#include "c64/text_frame.hpp"
#include <cstdint>
#include <vector>

// Drawing the enhanced mode's layers into a C64 picture. Everything stays on
// the C64's own pixel grid and in its 16 colours: rotated graphics are
// resampled onto the double-wide pixels the game's multicolour characters
// and expanded sprites use, and particles are such pixels too.
namespace fort_apocalypse::enhanced {

using u8 = std::uint8_t;

// A small picture as C64 colour numbers. Its pixels are `pixel_width`
// hires pixels wide: 2 for multicolour characters and expanded sprites.
struct Bitmap {
    static constexpr u8 clear = 0xFF;
    int width{}, height{};
    int pixel_width{1};
    std::vector<u8> pixels;

    Bitmap() = default;
    Bitmap(int w, int h, int pixel_w = 1)
        : width(w), height(h), pixel_width(pixel_w), pixels(std::size_t(w * h), clear) {}
    u8 at(int x, int y) const {
        return x < 0 || y < 0 || x >= width || y >= height ? clear
                                                           : pixels[std::size_t(y * width + x)];
    }
    void put(int x, int y, u8 colour) {
        if (x >= 0 && y >= 0 && x < width && y < height) {
            pixels[std::size_t(y * width + x)] = colour;
        }
    }
};

// A character as it shows: multicolour pairs (00 clear, 01 and 10 the shared
// colours, 11 the cell colour's low three bits) when the cell colour has bit
// 3 set, otherwise single-colour bits. x and y are in the bitmap's pixels;
// a bitmap of double-wide pixels takes multicolour characters only.
void draw_character(Bitmap&, int x, int y, const c64::Glyph&, u8 cell_colour, u8 shared1, u8 shared2);

// Pixel art twice the size, edges and diagonals kept (Scale2x / EPX).
Bitmap scale2x(const Bitmap&);

// The playfield as the last picture shows it. World coordinates are map
// pixels (map column * 8, map row * 8); the picture's playfield lines show
// consecutive world lines, so the mapping is a shift.
class Viewport {
public:
    Viewport(const c64::Layers&, int window_column, int window_row, int window_first_row,
             const c64::TextFrame&);

    bool valid() const { return first_line_ >= 0; }
    double picture_x(double world_x) const { return world_x + origin_x_; }
    double picture_y(double world_y) const { return world_y + origin_y_; }
    double world_x(double picture_x) const { return picture_x - origin_x_; }
    double world_y(double picture_y) const { return picture_y - origin_y_; }
    // The double-wide pixel grid of the playfield's characters.
    int grid_parity() const { return parity_; }

    int first_line() const { return first_line_; }
    int last_line() const { return last_line_; } // exclusive
    int left() const { return left_; }
    int right() const { return right_; } // exclusive

private:
    double origin_x_{}, origin_y_{};
    int parity_{};
    int first_line_{-1}, last_line_{-1}, left_{0}, right_{c64::Picture::width};
};

// Where a layer may draw, and whether foreground graphics cover it.
struct Clip {
    int first_line{}, last_line{}, left{}, right{};
    const c64::Layers* behind{}; // non-null: foreground pixels cover the layer
};

// One double-wide pixel whose left half is at x (x on the grid).
void plot(c64::Picture&, int x, int y, u8 colour, const Clip&);

// A bitmap rotated by `radians` about its point (pivot_x, pivot_y), in hires
// pixels from its top left, placed with that point at picture position
// (x, y), resampled onto the double-wide grid starting at even or odd x
// (`parity`). As in RotSprite, the art is enlarged eight times with Scale2x
// first, so thin lines and corners survive the turn.
void draw_rotated(c64::Picture&, const Bitmap&, double pivot_x, double pivot_y, double x, double y,
                  double radians, int parity, const Clip&);

} // namespace fort_apocalypse::enhanced
