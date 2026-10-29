#pragma once
#include "sonic2_gg/game/types.hpp"

// What the game asks the sound driver for (SoundDriver::request): songs are
// 81-99, effects 9A-BF; a number without bit 7 stops everything. The zones'
// songs come from table 10CF (level.cpp).
namespace sonic2_gg::sound {

inline constexpr u8 stop = 0x00;
// Songs.
inline constexpr u8 intro = 0x87;
inline constexpr u8 title = 0x88;
inline constexpr u8 invincibility = 0x8B;
inline constexpr u8 act_clear = 0x8C;
inline constexpr u8 life_lost = 0x8D;
inline constexpr u8 continue_screen = 0x8E;
inline constexpr u8 emerald = 0x8F;
inline constexpr u8 game_over = 0x90;
inline constexpr u8 ending = 0x92;
inline constexpr u8 act_start = 0x93; // the title card
inline constexpr u8 boss = 0x94;
// Effects.
inline constexpr u8 ring = 0x9A;
inline constexpr u8 roll = 0x9B;
inline constexpr u8 spring = 0x9C;
inline constexpr u8 jump = 0x9D;
inline constexpr u8 rings_lost = 0x9F; // also the continue screen's countdown
inline constexpr u8 skid = 0xA1;
inline constexpr u8 tally = 0xA2;
inline constexpr u8 extra_life = 0xA6; // also taking a continue
inline constexpr u8 bomb_bounce = 0xA8;
inline constexpr u8 thud = 0xA9;     // the end sign landing, boarding the cart
inline constexpr u8 boss_hit = 0xAA; // hitting a boss or bouncing off it
inline constexpr u8 cart_crash = 0xAB;
inline constexpr u8 swing = 0xAE;
inline constexpr u8 pipe = 0xB0;
inline constexpr u8 floor_breaks = 0xB1;
inline constexpr u8 ten_rings = 0xB3;

} // namespace sonic2_gg::sound
