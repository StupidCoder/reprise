#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// The bosses (enemy kinds 21 and up) and what follows a boss: Eggman's
// escape, the capsule with its animals, the walk off the screen and the
// results (08015C10-08017874, 080259C0-08027FF0).
namespace sonic_advance_gba {

// Creates the boss part whose creator routine is `creator`; false if it is
// not a boss or not reconstructed.
bool create_boss(Game& g, u32 creator, const ObjectSpawn& o);

// 0804CBE0: the view shakes (the camera's extra offset): `amplitude`
// (16.16 pixels) going down by `decay` each frame, a wave advancing by
// `step` (or random with mode bit 4), for `frames` frames (-1: until the
// amplitude is gone). Mode bits 0-1: 1 only positive, 2 only negative;
// bit 5: every other frame; bit 6: across, bit 7: up and down.
void shake_view(Game& g, s32 amplitude, s32 decay, s32 step, s32 frames, u32 mode);

// Act 10's rocket (08028CB4, 08029164, 08029478): six of the explosions'
// pieces fall from above the view, thrown out at random.
void rain_debris(Game& g);

} // namespace sonic_advance_gba
