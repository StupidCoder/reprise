#pragma once
#include "sonic_advance_gba/game/sprite.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <array>

// The player character (the original's 0x90-byte player record, 03005A20;
// a second one at 03005AB0 for a partner) and its sprites.
//
// Positions are 24.8 fixed point in pixels of the act, speeds 8.8 pixels per
// frame. The ground angle is 0-255 for a full turn: 0 flat ground, 0x40 a
// wall on the right, 0x80 a ceiling, 0xC0 a wall on the left.
namespace sonic_advance_gba {

class Game;
class Process;

// Bits of Player::flags (+10).
namespace player_flag {
inline constexpr u32 facing_left = 0x1;
inline constexpr u32 in_air = 0x2;
inline constexpr u32 rolling = 0x4;
inline constexpr u32 on_object = 0x8;    // standing on a platform or object
inline constexpr u32 jump_cancel = 0x10; // jumped while rolling: no air control
inline constexpr u32 pushing = 0x20;
inline constexpr u32 underwater = 0x40;
inline constexpr u32 dead = 0x80;
inline constexpr u32 jumped = 0x100;      // in the air by jumping
inline constexpr u32 no_jump_cap = 0x200; // launched: the rise is not capped
inline constexpr u32 spin_dash = 0x400;
inline constexpr u32 control_locked = 0x800;
inline constexpr u32 left_water = 0x1000; // set for the frame the player leaves or enters water
inline constexpr u32 speed_shoes = 0x2000;
inline constexpr u32 hidden = 0x100000;      // not drawn (unless dead)
inline constexpr u32 frozen = 0x200000;      // the buttons are ignored
inline constexpr u32 no_movement = 0x400000; // the movement routines do not run
inline constexpr u32 keep_state = 0x800000;  // landing and stopping keep the state
inline constexpr u32 riding = 0x1000000;     // carried along a path (the movement of 08047284)
inline constexpr u32 grabbed = 0x8000000;    // act 13: held by the boss's claw
} // namespace player_flag

// Bits of Player::power (+26).
namespace player_power {
inline constexpr u8 shield = 0x01;
inline constexpr u8 invincible = 0x02;
inline constexpr u8 magnetic_shield = 0x08;
inline constexpr u8 fast = 0x04; // speeds doubled
inline constexpr u8 slow = 0x10; // speeds quartered
} // namespace player_power

struct Player {
    s32 x{}, y{};                         // +00, +04
    s16 speed_x{}, speed_y{};             // +08, +0A
    s16 ground_speed{};                   // +0C along the ground
    s8 radius_x = 6, radius_y = 0xE;      // +0E, +0F half width and height
    u32 flags{};                          // +10 player_flag bits
    u8 angle{};                           // +14 the ground's angle
    u8 look_timer = 0x78;                 // +15
    s16 spin_charge{};                    // +16 the spin dash's charge
    u8 sensor_angle[2]{};                 // +18, +19 the ground probes' angles
    s16 slip_timer{};                     // +1A no control after slipping off a steep slope
    s16 hurt_timer{};                     // +1C blinking after a hit
    s16 invincible_timer{};               // +1E
    s16 power_timer{};                    // +20 speed change (power bits 4 and 10)
    s16 confused_timer{};                 // +22 multiplayer: the directions are mixed up
    s16 power_timer_20{};                 // +24 power bit 20
    u8 power{};                           // +26 player_power bits
    u8 layer = 1;                         // +27 the collision layer (bit 0)
    const Sprite* standing_on{};          // +28 the sprite of the object stood on
    s32 top_speed = 0x480;                // +2C
    s32 acceleration = 8;                 // +30
    s32 deceleration = 0x60;              // +34
    u16 held{}, pressed{};                // +38, +3A the buttons as the player sees them
    u8 index{};                           // +3C 0 the player, 1 the partner
    u8 special{};                         // +3D the air move's state
    u8 move{};                            // +3E the stage of a ground move (0 none)
    u8 move_input{};                      // +3F a button pressed during a ground move
    u8 state{};                           // +40 what the player does (picks the animation)
    u8 previous_state{};                  // +41
    u16 animation = 0xFFFF;               // +42
    u16 variant = 0xFFFF;                 // +44
    s16 idle_timer = 0x168;               // +46 frames until the waiting animation
    s16 start_x{}, start_y{};             // +48, +4A (pixels)
    s32 checkpoint_time{};                // +4C the clock at the last checkpoint
    s16 unknown_50{}, unknown_52{};       // +50, +52
    s16 scale_x = 0x100, scale_y = 0x100; // +54, +56
    u8 chain{};                           // +58 enemies destroyed in a row (points)
    u8 character{};                       // +59 0 Sonic, 1 Tails, 2 Knuckles, 3 Amy
    u8 air = 0x1E;                        // +5A underwater: seconds of air left
    u8 air_ticks = 0x3C;                  // +5B frames left of the current second
    s8 splash_timer = 10;                 // +5C frames until the next splash may show
    u8 unknown_6c{};                      // +6C
    s8 tube_stage = 0x7F;                 // +6D the last tube waypoint's stage (7F: none)
    u8 back_from_special{}; // +6E the act restarted after a special stage (the special spring
                            // bounces)
    std::array<u8, 14> unknown_6f{}; // +6F
    // +80.. per character (Sonic: +80, +82, +84).
    u8 extra_80{}, extra_81{};
    u16 extra_82{};
    u32 extra_84{};

    // Act 13 (08049864's process record): the last 16 positions, for the
    // afterimages.
    std::array<s32, 16> trail_x{}, trail_y{}; // +0C, +4C
    u32 trail_newest{};                       // +08

    Process* process{};         // +60 the player's process (while it exists)
    TransformedSprite* body{};  // +64
    TransformedSprite* extra{}; // +68 Tails' tails
};

// 080421E8: the player record's character, index and start point (act
// `zone`).
void place_player(Game& g, Player& p, u8 character, u8 zone, u8 index);
// 08041FF8: the player's process and sprites for act `zone`.
void start_player(Game& g, Player& p, u8 zone);

// 08042814: the player lands (the moves end, the normal size again).
void land_player(Game& g, Player& p);
// 08046CB8: the player's moves end.
void end_player_moves(Game& g, Player& p);
// 08045138: a new size; the feet stay where they are.
void set_player_radius(Game& g, Player& p, s8 radius_x, s8 radius_y);
// 08043AA4: the room in `direction` (an angle) before a surface.
s32 player_room(Game& g, Player& p, u8 direction);

// 0804CFF8: which buttons jump (A) and do the other moves (B); a demo
// always uses the default.
void set_button_layout(Game& g, u8 layout);

} // namespace sonic_advance_gba
