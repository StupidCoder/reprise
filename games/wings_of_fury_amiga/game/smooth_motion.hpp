#pragma once
#include "wings_of_fury_amiga/game/flight.hpp"

// Smooth motion (an option): the game moves everything once a tick, every
// fourth vertical blank (12.5 times a second on PAL), and draws about once
// a tick, so the original's planes, weapons and landscape jump a few pixels
// at a time. With this option each picture shown places the moving things
// between the last two ticks, at the moment the picture is shown, so they
// glide at the display's own rate. The game itself plays exactly the same:
// only copies made for drawing are moved.
//
// Moved: the camera and the plane's height (so the landscape scrolls and
// the world's vertical scroll follow), the sea's motion, the elevator, the
// ships' bob, the enemy planes, the planes on the decks and on the
// airfields, the soldiers, the weapons, the smoke and the fireworks. Things
// that change their picture (the plane's attitude, blasts, waves) keep the
// game's own frames. Anything that appears, goes or jumps further than a
// tick's motion is drawn where the game has it now.
namespace wings::game {

// Moves the copies `world` and `plane` (made from the game's present state)
// back towards `before` and `was` (the state one tick earlier) to the time
// `t` (0: the earlier tick, 1: the present one).
void place_between(FlightWorld& world, PlayerFlight& plane, const FlightWorld& before, const PlayerFlight& was,
                   double t);

} // namespace wings::game
