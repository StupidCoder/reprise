#pragma once
#include "marble_madness_amiga/game/terrain.hpp"
#include <array>
#include <cstdint>

// The ground under a point: which cell it is in, the corners around it, its
// height and slope, and the steps along the cell's sides and corners.
//
// The game keeps one such probe ($1006DC-$1006F2 and the corner block
// $101ED2) for everything that asks about the ground, and reloads the four
// vertices only when the point moves to another cell or a caller forces it
// (the cell numbers set to -1). Animated ground that changes while a marble
// stays in its cell is therefore seen only at the next reload; the probe
// keeps that behaviour.
namespace marble_madness::game {

class Probe {
public:
    // $10EC02: the point's integer position (16.16 world values), reloading
    // the corners when the cell changed.
    void locate(const Terrain&, std::int32_t x, std::int32_t y, std::int32_t z);
    // $10EC76: position only (no reload), as the region code uses it.
    void place(std::int32_t x, std::int32_t y);
    // $106A00 leaves the point it projects here (x and y only, the cell
    // stays); the next sound's side ($1224D8) reads it.
    void set_point(std::int16_t x, std::int16_t y) {
        x_ = x;
        y_ = y;
    }
    // $10EDD0 with 1: reload the corners where the point is, then the
    // height (creatures placed with place()).
    std::int32_t ground_height_here(const Terrain& t) {
        reload(t);
        return ground_height();
    }
    // Forces the next locate() to reload the corners ($1006E2 = $1006E4 = -1).
    void invalidate() { cell_x_ = cell_y_ = -1; }

    // $10EDD0: ground height at the point (16.16), from one of the cell's two
    // triangles; also sets the slope used by the slope force.
    std::int32_t ground_height();
    // $10F390: steps along the cell's four sides and up at its four corners,
    // relative to the point's height.
    void analyse_sides();

    std::int16_t x() const { return x_; }
    std::int16_t y() const { return y_; }
    std::int16_t z() const { return z_; }
    std::int16_t cell_x() const { return cell_x_; }
    std::int16_t cell_y() const { return cell_y_; }
    int fraction_x() const { return fx_; } // 0-7 within the cell
    int fraction_y() const { return fy_; }
    // The slope of the last ground_height() ($1006F0/$1006F2), in height
    // units per pixel; the slope force works on these.
    std::int16_t slope_x{}, slope_y{};

    // Side analysis ($1006BC-$1006BF kinds, $1006C0-$1006C6 heights): side
    // 0 is the cell's x = 0 edge, 1 its y = 8 edge, 2 its x = 8 edge, 3 its
    // y = 0 edge. Kind 0: no step; 1 or 2: a wall up (2: along the whole
    // side); 3 or 4: a drop. The height is the step's top relative to the
    // point (interpolated along the side for kinds 1 and 3).
    std::array<std::int8_t, 4> side_kind{};
    std::array<std::int16_t, 4> side_height{};
    // Corner peaks ($1006BB bits, $1006C8-$1006CE heights): bits 0-3 the
    // four corners' outer vertex higher than its neighbours, bits 4-7 the
    // inner one; heights relative to the point.
    std::uint8_t corner_bits{};
    std::array<std::int16_t, 4> corner_height{};

private:
    void reload(const Terrain&);

    std::int16_t x_{}, y_{}, z_{};
    std::int16_t cell_x_{-1}, cell_y_{-1};
    int fx_{}, fy_{};
    // The four vertices around the cell, in the game's order: (cx, cy + 1),
    // (cx + 1, cy + 1), (cx + 1, cy), (cx, cy).
    std::array<Vertex, 4> v_{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(slope_x, slope_y, side_kind, side_height, corner_bits, corner_height, x_, y_, z_, cell_x_, cell_y_,
           fx_, fy_, v_);
    }
};

} // namespace marble_madness::game
