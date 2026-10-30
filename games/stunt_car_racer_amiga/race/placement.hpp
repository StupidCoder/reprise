#pragma once
#include "stunt_car_racer_amiga/race/car.hpp"
#include "stunt_car_racer_amiga/race/contact.hpp"
#include "stunt_car_racer_amiga/race/tables.hpp"

// Where the car is on the circuit (game.md, "Car physics: placement"), as the
// game's render pass works it out each frame before the physics reads it:
// the camera quadrant and eye height, the segment under the car, its offset
// from the piece's centre line, its progress along the piece and the piece's
// heading there; and whether it has gone over the road's edge.
namespace stuntcar::race {

void place(Car&, const Tables&, const TrackContact&);

} // namespace stuntcar::race
