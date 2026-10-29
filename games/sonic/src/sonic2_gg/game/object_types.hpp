#pragma once
#include "sonic2_gg/game/types.hpp"

// Object types: Object::type, the numbers of the placement tables and of
// the spawn commands. Types 00-1F have their scripts and routines in bank 1F,
// 20-4F in bank 1C, 50 and above in bank 1E.
//
// Some types are copies that share another type's scripts and routines and
// are never placed or spawned: 0D (lava ball), 11-13 (emerald), 15-17 (end
// sign), 19 (falling rock), 30-31 (hopper), 43 (Gimmick Mt.'s boss). Types
// 1F, 2E, 2F, 3D-3F, 52 and 56-5C have no scripts in use.
namespace sonic2_gg::object_type {

inline constexpr u8 none = 0x00;
inline constexpr u8 sonic = 0x01;
inline constexpr u8 ring_sparkle = 0x02; // a collected map ring
inline constexpr u8 shoe_image = 0x03;   // speed shoes after-image (never seen)
inline constexpr u8 block_piece = 0x04;  // a piece of a broken block
inline constexpr u8 invincibility_star = 0x05;
inline constexpr u8 hud_slide = 0x06; // the HUD sliding away during Under Ground's boss
inline constexpr u8 debris = 0x07;    // Under Ground's and Gimmick Mt.'s broken blocks
inline constexpr u8 lost_ring = 0x08;
inline constexpr u8 air_countdown = 0x09;
inline constexpr u8 bubble = 0x0A;
inline constexpr u8 splash = 0x0B;
inline constexpr u8 rising_water = 0x0C; // Aqua Lake act 2
inline constexpr u8 lava_ball = 0x0E;
inline constexpr u8 explosion = 0x0F;
inline constexpr u8 monitor = 0x10;
inline constexpr u8 emerald = 0x14; // a Chaos Emerald
inline constexpr u8 end_sign = 0x18;
inline constexpr u8 thrown_rock = 0x1A;
inline constexpr u8 falling_rock = 0x1B;
inline constexpr u8 intro_cloud = 0x1C;
inline constexpr u8 intro_star = 0x1D;
inline constexpr u8 intro_hills = 0x1E;
inline constexpr u8 platform = 0x20;
inline constexpr u8 path_platform = 0x21;
inline constexpr u8 shuttle_platform = 0x22;
inline constexpr u8 bomb_walker = 0x23;
inline constexpr u8 walker_bomb = 0x24;
inline constexpr u8 push_block = 0x25; // drops when pushed
inline constexpr u8 trap = 0x26;
inline constexpr u8 glider = 0x27;
inline constexpr u8 swing_pole = 0x28;
inline constexpr u8 mine_cart = 0x29;
inline constexpr u8 moving_swing_pole = 0x2A;
inline constexpr u8 swinging_ball = 0x2B;
inline constexpr u8 dropping_block = 0x2C; // drops on Sonic
inline constexpr u8 falling_block = 0x2D;  // Crystal Egg's breaking floor
inline constexpr u8 hopper = 0x32;
inline constexpr u8 fish = 0x33;
inline constexpr u8 pacer = 0x34;
inline constexpr u8 turret = 0x35;
inline constexpr u8 minion = 0x36;
inline constexpr u8 crawler = 0x37;
inline constexpr u8 jumper = 0x38;
inline constexpr u8 shaker = 0x39;
inline constexpr u8 hopping_enemy = 0x3A;
inline constexpr u8 shaker_fireball = 0x3B;
inline constexpr u8 turret_shot = 0x3C;
inline constexpr u8 capsule = 0x40;
inline constexpr u8 animal = 0x41;
inline constexpr u8 green_hills_boss = 0x42;
inline constexpr u8 gimmick_boss = 0x44;
inline constexpr u8 boulder = 0x45;
inline constexpr u8 bouncing_ball = 0x46;
inline constexpr u8 ball_launcher = 0x47;
inline constexpr u8 sky_high_boss = 0x48;
inline constexpr u8 boss_flier = 0x49;
inline constexpr u8 aqua_lake_boss = 0x4A;
inline constexpr u8 underground_boss = 0x4B;
inline constexpr u8 underground_bomb = 0x4C;
inline constexpr u8 underground_target = 0x4D;
inline constexpr u8 scrambled_egg_boss = 0x4E;
inline constexpr u8 aqua_lake_bomb = 0x4F;
inline constexpr u8 title_sonic = 0x50;
inline constexpr u8 title_tails = 0x51;
inline constexpr u8 fireball_cluster = 0x53;
inline constexpr u8 falling_fire = 0x54;
inline constexpr u8 final_boss = 0x55;
inline constexpr u8 companion = 0x5D; // the ending's friend
inline constexpr u8 intro_runner = 0x5E;
inline constexpr u8 intro_tails = 0x5F;
inline constexpr u8 waiting = 0xFE; // removed, waits until its place is out of range
inline constexpr u8 removed = 0xFF; // being removed

} // namespace sonic2_gg::object_type
