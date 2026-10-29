#pragma once
#include "sonic_advance_gba/game/types.hpp"

// Act 13, The Moon Zone (0804A184-0804B6E8, 0804FE20-08052858): Super
// Sonic runs on the moon's ground, which rolls by under a still view, and
// fights the last boss, a walking machine drawn as jointed sprites around a
// body in a turned background layer. Rings run out by themselves (one a
// second); getting hit only knocks the player back.
namespace sonic_advance_gba {

class Game;
struct Player;
struct ObjectSpawn;

// 0804A184: the trail of positions for the afterimages, 50 rings, the body
// drawn behind the level's front, the sparkle budget.
void start_super_sonic(Game& g, Player& p);
// 08049864: Super Sonic's frame. True when the player has just died (the
// process goes on with the fall, 080453E4).
bool super_sonic_frame(Game& g, Player& p);
// 0800C714: something harmful touched the player: knocked back, spinning,
// for 18h frames (unless blinking or invincible). True when it took.
bool super_sonic_hit(Game& g, Player& p);

// 0804A5A4: the warning sign in the middle of the view (four parts); at its
// half the boss starts.
void start_warning(Game& g, u16 priority);

// Creates the enemy whose creator routine is `creator` (the boss, enemy
// 31: 0805230C); false if it is not one of these.
bool create_moon_enemy(Game& g, u32 creator, const ObjectSpawn& o);

} // namespace sonic_advance_gba
