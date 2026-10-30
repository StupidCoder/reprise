#pragma once
#include "stunt_car_racer_amiga/race/car.hpp"
#include "stunt_car_racer_amiga/race/tables.hpp"

// One frame of a car's physics (game.md, "Car physics"), in the game's order:
// orientation, the road under the suspension, springs, tyre loads, the spawn
// servo, drive, grip, track following, drag, torques, then integration.
namespace stuntcar::race {

class TrackContact;

// The controls become drive, steering and boost for the coming frame.
void read_controls(Car&, const Controls&);

// The end of a race-loop frame: the wheel animation ages the road-edge bump
// (its byte shifts left once per front wheel), and the slow clock (238/256 of the frames) that
// paces recovery and boost advances.
void end_frame(Car&);

void step_physics(Car&, const Tables&, TrackContact&);

} // namespace stuntcar::race
