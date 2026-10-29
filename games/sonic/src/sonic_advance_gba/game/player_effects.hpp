#pragma once
#include "sonic_advance_gba/game/types.hpp"

// Small processes that belong to the player: skid dust, the effect of
// Sonic's air attack and the rings the player loses (08040244-08041FFF).
namespace sonic_advance_gba {

class Game;
struct Player;

// 080405B0: the dust budget (20 clouds at a time; a process at 4000 whose
// existence allows dust).
void start_player_effects(Game& g);
// 0804046C: a dust cloud at act position (x, y), if the budget allows,
// moving `drift_x` pixels a frame (the last boss's sets it to -4).
void create_skid_dust(Game& g, s16 x, s16 y, s16 drift_x = 0);
// 08040614: the dust kicked up while the player charges a spin dash.
void start_spin_dash_dust(Game& g);
// 080478F8: the effect of Sonic's air attack, following the player.
void create_air_attack_effect(Game& g, const Player& p);
// 0804088C: the handle the player holds while riding (drawn with the
// riding animation, until the ride ends).
void create_ride_handle(Game& g);
// 0804C878: a splash at the water's surface (x, its level), shown on the
// water line.
void create_water_splash(Game& g, s16 x, s16 y);
// 0804BF3C: number `n` (0-5) of the air countdown, rising from the player.
void create_air_countdown(Game& g, const Player& p, u8 n);
// 0804BFF8: an air bubble at (x, y) (8.8) drifting across at `speed_x`;
// false when 12 are already about.
bool create_bubble(Game& g, s32 x, s32 y, s16 speed_x, bool big);
// 0804C398: bubbles from a drowned player while it is dead.
void start_drowning_bubbles(Game& g, Player& p);
// 08047DE4: Tails' tails while he spins down a chute (ends at once for
// anyone else).
void create_chute_tails(Game& g, const Player& p);
// 080409C4: the process of the rings the player scatters when hit.
void start_lost_rings(Game& g);
// From now on the lost rings bounce off ceilings (their process's +336 bit
// 0, set by the Ice Paradise boss).
void lost_rings_bounce_off_ceilings(Game& g);
// 0804B598, 0804B6E8, 0804B644: the shield, magnetic shield and
// invincibility shown round player `player`.
void create_shield(Game& g, u8 player);
void create_magnetic_shield(Game& g, u8 player);
void create_invincibility(Game& g, u8 player);
// 08040ADC: `count` rings fly out from act position (x, y).
void scatter_rings(Game& g, s32 x, s32 y, u16 count);
// 08040BE8: `count` rings thrown out in random directions from act
// position (x, y) (pixels), without a sound.
void spread_rings(Game& g, s32 x, s32 y, u16 count);

} // namespace sonic_advance_gba
