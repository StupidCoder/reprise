#pragma once
#include "sonic_advance_gba/game/sprite.hpp"
#include "sonic_advance_gba/game/types.hpp"

// Helpers the object routines share.
namespace sonic_advance_gba {

class Game;
struct Player;
struct StageState;

// Whether span [lo, hi] meets [a, b], as the object code compares (the
// test starts from whichever span begins first; touching counts).
inline bool spans_meet(s32 lo, s32 hi, s32 a, s32 b) {
    return lo < a ? hi >= a : lo <= b;
}

// Whether hitbox `a` of something at (ax, ay) meets hitbox `b` at (bx, by).
inline bool hitboxes_meet(s32 ax, s32 ay, const Hitbox& a, s32 bx, s32 by, const Hitbox& b) {
    return spans_meet(bx + b.left, bx + b.right, ax + a.left, ax + a.right) &&
           spans_meet(by + b.top, by + b.bottom, ay + a.top, ay + a.bottom);
}

// An object placed at layout position (x, y), drawn at screen position
// (sx, sy), has left the spawner's range (and the view's surroundings):
// it gives its layout record back and ends.
bool out_of_range(const StageState& s, s32 x, s32 y, s16 sx, s16 sy);

// 0800C558: the player is hurt (unless blinking or invincible): knocked
// back, scattering the rings (or losing the shield); without rings the
// player dies. True when hurt.
bool hurt_player(Game& g, Player& p);
// 0800B4F8: a player touches an enemy (its first hitbox) at (x, y). An
// attack (the player's second hitbox, or the first while invincible)
// destroys it: true. Otherwise the player is hurt.
bool enemy_touched(Game& g, const Sprite& s, s16 x, s16 y);
// 0800B798: a projectile at (x, y) hurts a player it touches.
bool projectile_hits(Game& g, const Sprite& s, s16 x, s16 y);

// 0800BA5C: a boss (sprite `s` at (x, y): its first hitbox the target,
// its second the dangerous part) against player `p`.
enum class BossContact { none, attacked, hurt };
// attacked: the player's attack box met the target (the player bounces
// back); hurt: the player's body met either box (hurt_player was called).
BossContact boss_contact(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800BCBC: the same where only a player in the air attacks (the attack
// box against the first hitbox), and the body is hurt by either hitbox.
BossContact boss_contact_from_air(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800B8B4: the same for a boss with one hitbox: the player's attack box
// on it attacks (the player bounces back), the body on it is hurt.
BossContact boss_contact_single(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800BF10: something harmful (its first hitbox) meeting the player's body
// hurts the player: true.
bool hazard_hits(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800BFEC: the same with the second hitbox of `s` (in act 13 Super
// Sonic's knock-back, 0800C714).
bool hazard_hits_second(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800C0E0: the player's attack box meets the first hitbox of `s` at
// (x, y): the player bounces back (Amy keeps half her speed; not in act
// 13). True when it met.
bool attack_bounces(Game& g, const Sprite& s, s16 x, s16 y, Player& p);
// 0800CE34: the player's size (its radii around its position) meets the
// first hitbox of sprite `s` at (x, y) (not when the player is dead).
bool touches_player(const Sprite& s, s16 x, s16 y, const Player& p);

} // namespace sonic_advance_gba
