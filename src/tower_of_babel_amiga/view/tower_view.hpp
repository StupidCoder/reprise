#pragma once
#include "tower_of_babel_amiga/data/shape.hpp"
#include "tower_of_babel_amiga/game/tower_map.hpp"
#include "tower_of_babel_amiga/view/canvas.hpp"
#include <array>
#include <functional>
#include <optional>

// The 3D view of the tower ($D432 and the routines it calls).
//
// The camera only ever faces one of the four compass directions. Everything
// is done in view space: the tower's cells are turned with the camera
// (heading h turns (x, z) to (-z, x) h times, $AAD4), cells are 512 units
// across, floor slabs are 32 units thick, and points project as
//   screen x = centre x + (x - camera x) * 256 / depth   (truncated)
//   screen y = centre y - (y - camera y) * 256 / depth
// with the depth of each row of cell edges, nearest first clamped to 256
// ($CDE6). Cells are drawn back to front (painter's order): rows from the
// far edge towards the camera; in each row the levels above eye height from
// the top down, then the levels below from the ground up; in each level the
// columns right of the camera from the outside in, then those on the left.
namespace babel::view {

// The camera in the tower's units (the tower spans -2048..2048 in x and z,
// floor n's top at y = 512 n), already turned to its heading.
struct Camera {
    int x{}, y{}, z{};
    int heading{}; // 0-3
};

// The camera on a spider at its position and heading ($AAD4): turned to
// view space, then raised by the view offsets the options set ($9F84):
// sideways, height and distance behind.
Camera spider_camera(int x, int y, int z, int heading, int offset, int height, int distance);

// The pens the view fills with, in the program's order ($A22(a6), 16 bytes
// each): under, side, front (side when the heading is odd), front (odd), top,
// top lit; the same six for the second square colour; then the ground
// shadow, a lift top and a lit lift top.
struct Pens {
    std::array<Pen, 16> pens{};
    // Objects' pens ($892(a6) on: solid colours 0-15, dithers 16-22, then
    // the program's following bytes, which the per-object pens reach) and
    // see-through pens ($B22(a6), 32 bytes each).
    std::array<Pen, 80> object{};
    std::array<Pen, 32> see_through{};
};
Pens read_pens(const data::Memory& program); // from $1228, $1098, $1328

// An object as the view draws it ($15DA + 32 n in the original).
struct SceneObject {
    int slot{};      // its number, which picks its own pens (style bits 7 + 6)
    int type{};      // record +0: 0-$19 shapes, $1A particles (not drawn yet), more: not drawn
    const data::Shape* shape{};
    int x{}, y{}, z{}; // tower units
    bool moving{};   // between cells: clipped at the cell's boundary ($13934)
};
// The object a cell holds (the original's object map beside the cells).
using ObjectAt = std::function<const SceneObject*(int floor, int x, int z)>;

// The height (tower units) of the lift object standing in a lift cell, if
// any; lifts without one show their platform at the floor's level.
using LiftHeight = std::function<std::optional<int>(int floor, int x, int z)>;

class TowerView {
public:
    explicit TowerView(const Pens& pens) : pens_(pens) {}

    // Draws the view of `map` into `window` of `canvas`, centred at
    // (centre_x, centre_y) (160, 72 in the original's 320-pixel screen).
    void draw(Canvas&, const Window&, int centre_x, int centre_y, const game::TowerMap&, const Camera&,
              const LiftHeight& lift_height = {}, const ObjectAt& objects = {}) const;

private:
    Pens pens_;
};

} // namespace babel::view
