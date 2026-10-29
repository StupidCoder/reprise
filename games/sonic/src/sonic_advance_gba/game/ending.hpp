#pragma once

// The endings: after act 12 (08065850: the fall from space, the Tornado,
// the birds, the congratulations), then the staff roll (0805E3CC) and the
// last picture (080696DC) before the SEGA logo.
namespace sonic_advance_gba {

class Game;

// 08065850: the ending (everything else has ended; the save is written).
void start_ending(Game& g);
// 080670C4: the true ending after act 13 (the save is written; the act's
// results are tallied under the night sky).
void start_true_ending(Game& g);
// 0805E3CC: the staff roll.
void start_staff_roll(Game& g);

} // namespace sonic_advance_gba
