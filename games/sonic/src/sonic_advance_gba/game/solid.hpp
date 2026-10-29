#pragma once
#include "sonic_advance_gba/game/types.hpp"

// Solid objects: what a player does on meeting an object's first hitbox as
// a solid block (080096B0 with 0800C934 for the top and bottom and
// 0800CBBC for the sides).
namespace sonic_advance_gba {

class Game;
struct Player;
struct Sprite;

// Bits of collide_solid's result.
namespace solid {
inline constexpr u32 facing_left = 0x1;
inline constexpr u32 on_top = 0x8; // standing on it
inline constexpr u32 pushing = 0x20;
inline constexpr u32 from_below = 0x10000; // hit its underside
inline constexpr u32 from_left = 0x20000;  // pushed against its left side
inline constexpr u32 from_right = 0x40000;
} // namespace solid

namespace solid {
inline constexpr u32 attacked = 0x20; // breakable: hit with the player's attack box
} // namespace solid

// The object's sprite `s` at act position (x, y) against player `p`.
u32 collide_solid(Game& g, const Sprite& s, s16 x, s16 y, Player& p);

namespace solid {
inline constexpr u32 struck =
    0x8; // collide_wall: met with the attack box (or near it while attacking)
} // namespace solid

// 0800B1D4: an object solid at its top and bottom only (the sides let the
// player through).
u32 collide_top_bottom(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800AFDC: an object solid at its sides only (the player's size widened by
// 5 pixels). With `attack_counts`, a player whose attack box is out that
// meets it (or comes that near) strikes it instead (solid::struck; falling,
// the player bounces).
u32 collide_wall(Game& g, const Sprite& s, s16 x, s16 y, Player& p, bool attack_counts);
// 0800ABEC: an object the player breaks: attacked (the attack box, or
// jumping onto its top: solid::on_top with a bounce), bumped from below
// (solid::from_below) or blocked at the sides in the air.
u32 collide_breakable(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800C394: a block broken by a player rolling down onto it in a jump, or
// by the attack box of a player on the ground (the player bounces).
bool breaks_block(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800A850: a solid object whose sides report the push (for springs on
// the ground): solid::from_left / from_right, with solid::pushing when the
// player walked into it.
u32 collide_spring_box(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800B2BC: a platform solid only from above (tested in 24.8 precision):
// solid::on_top when the player lands or stands on it.
u32 collide_platform(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800A768: Amy's hammer (her attack states) meets the object.
bool hammer_hits(const Sprite& s, s16 x, s16 y, const Player& p);

} // namespace sonic_advance_gba
