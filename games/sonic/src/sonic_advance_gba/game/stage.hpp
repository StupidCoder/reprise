#pragma once
#include "sonic_advance_gba/game/background.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/player.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include "sonic_advance_gba/game/water.hpp"
#include <array>

// The state of an act being played: what the act's start sets up and the
// level's processes share.
namespace sonic_advance_gba {

class Game;
class Process;

// Bits of StageState::flags (03005004).
namespace stage_flag {
inline constexpr u16 paused = 0x0001;      // the clock stops (also: the act is over)
inline constexpr u16 frozen = 0x0002;      // rings and objects stop
inline constexpr u16 countdown = 0x0004;   // the clock counts down (multiplayer)
inline constexpr u16 title_card = 0x0020;  // the act's title card is up
inline constexpr u16 demo = 0x0040;        // an attract demo is playing
inline constexpr u16 upside_down = 0x0080; // gravity is reversed
} // namespace stage_flag

// Bits of StageState::Camera::locks (030059F6) besides the holds (bits 0-1).
namespace camera_lock {
inline constexpr u16 far_still =
    0x2000; // act 10: the far background is left as it is (the act is over)
inline constexpr u16 rocket_space = 0x4000; // act 10: the rocket's last stage is in space
inline constexpr u16 rocket_up = 0x8000; // act 10: the rocket flies (the far background moves down)
} // namespace camera_lock

struct StageState {
    s8 zone{};             // 0300514C the act (zone * 2 + act)
    s8 previous_zone = -1; // 03005020 the act whose title card was shown last
    u8 character{};        // 03005084
    u8 lives{};            // 03005024
    u16 rings{};           // 03004FEC
    u16 total_rings{};     // 03005588 rings carried out of acts
    u32 score{};           // 03005030
    s32 clock{};           // 03005034 frames on the act's clock
    s32 frames{};          // 03004FE4 frames since the act started (counted with the clock)
    u32 ticks{};           // 03005120 frames of play
    u16 flags{};           // 03005004 stage_flag bits
    u16 previous_flags{};  // 0300502C the flags at the stage process's last update
    u8 players = 1;        // 03005088
    u8 tails_follows{};    // 03005050 Tails follows Sonic
    u16 jump_buttons = 1;  // 030060E0
    u16 other_buttons = 2; // 030060E2
    s16 water_level = -1;  // 03006082 (negative: no water)
    WaterState water;
    u8 demo_index{};                   // 03005054 the next attract demo
    u8 ring_sparkles{};                // 03004FFC sparkles of collected rings alive
    u8 bubbles{};                      // 03005C78 air bubbles alive (at most 12)
    u8 next_rotation_set = 4;          // 0300504C sprite rotation sets 0-3 are the players'
    u8 checkpoint{};                   // 03005148 the last checkpoint passed
    u8 unknown_501c{};                 // 0300501C
    u8 unknown_5124{};                 // 03005124 cleared by the character select before a new game
    u8 unknown_4fe0{};                 // 03004FE0 a countdown (multiplayer)
    u8 saved_5178{};                   // 03005080 the save's byte +18, kept while a demo plays
    std::array<u8, 4> player_blinks{}; // 03005048 per player (FF: none)
    u8 start_pressed{};                // 0300506C a demo was ended with Start

    // The music manager's requests (03005040-03005044).
    u8 music_off{};        // 03005040 FF: no music changes (the act is ending)
    u8 music_queued{};     // 03005041 high nibble: request, low: the jingle
    u8 music_invincible{}; // 03005042
    u8 music_one_up{};     // 03005043 a life was won: the jingle
    u8 music_drowning{};   // 03005044

    u32 random{}; // 03004C58 the effects' random numbers

    // The next random number: a linear congruential generator.
    u32 next_random() { return random = random * 0x196225 + 0x3C6EF35F; }

    StageMap map;                                      // 03005A0C
    std::array<Player, 2> player{};                    // 03005A20, 03005AB0
    std::array<TransformedSprite, 4> player_sprites{}; // 03005B40, 03005B90, 03005BE0, 03005C30
    // The last 60 frames' buttons of the player (newest first) and which
    // were newly pressed (030060F0, 03006170).
    std::array<u16, 60> held_history{}, pressed_history{};

    // Processes other parts look for.
    Process* dust_budget{}; // 03005A14
    u8 dust_left{};         // 03005A10 skid dust clouds that may still be made
    u8 super_sparkles{};    // 03005C74 act 13: Super Sonic's sparkles that may still be made
    s16 saved_start_x{},
        saved_start_y{}; // 03005058, 03005064 the restart point kept over a special stage

    // The special stage's counts (kept for its results).
    struct SpecialCounts {
        u16 rings{};         // 03005078 rings collected
        u16 target{};        // 03005028 the rings needed at the first gate
        u16 second_target{}; // 03005070 the rings needed at the end
        u16 shown_target{};  // 03005154 the number shown (and checked at the end)
        u16 items{};         // 0300507C kind-10 items picked up
        u8 quiet{};          // 03005730 the effects' sounds off (a jingle plays)
        // The counter's blink (its record's +48) is never set: it starts with
        // what that memory held. Its record takes the place the welcome
        // scene's streaks freed, at their last height (y[15]).
        u8 counter_leftover{};
    } special;

    Process* moon_boss{};  // 030061F0 act 13: the boss (once made; never cleared)
    Process* lost_rings{}; // 03005A18

    // The camera (030059D0): the view's top left in act pixels and how it
    // follows the player.
    struct Camera {
        s16 x{}, y{};                            // +00, +02
        s16 max_step_x = 0x10, max_step_y = 0xC; // +04, +06 pixels per frame at most
        s16 target_x{}, target_y{};              // +08, +0A where it wants to be
        s16 shift_x{}, shift_y{};                // +0C, +0E added to the target
        s16 offset_x{}, offset_y{};              // +10, +12 the last wrap
        s16 min_y{}, max_y{};                    // +14, +16 the act's limits (pixels)
        s16 min_x{}, max_x{};                    // +18, +1A
        s16 hold{};                              // +1C frames the camera stays put
        s16 dead_zone_x = 8;                     // +1E
        u16 lead_y{};                            // +20 grows while the player is in the air
        s16 look_y{};                            // +22 looking up or down
        u16 locks{};                             // +26 bit 0: x held, bit 1: y held
        s16 parallax_x{}, parallax_y{};          // +28, +2A the far background's offset
        u8 zone_layer{};                         // +2C the act's far-background routine (by act)
        s16 extra_x{}, extra_y{};                // +34, +36 added after the limits (shaking)
        s16 vertical_offset{};                   // +38 follows the player's height
    } camera;

    // The level's backgrounds: layers 1 and 2 (03005910, 03005950), the far
    // background on layer 3 (03005990).
    std::array<Background, 3> layers;
    // 030058D0: a front layer (layer 0) some acts add (Ice Paradise's mist).
    Background front_layer;
};

// 0800CF04: a background colour cycle (an animation of background colours,
// ended with its script) at `priority`.
void create_color_cycle(Game& g, u16 priority, u16 animation, u8 variant);
// 0801A348: Start pressed during an act (or a special stage): the pause.
void request_pause(Game& g);

// 0803DBA0: a new game's act (score 0, three lives), then start_act.
void start_game_act(Game& g);
// 0803CE20: the act `zone` with `character` from its start: the players,
// the clock, then the act's processes (title card, player, camera,
// rings, objects, music).
void start_act(Game& g);
// 0803CF68: the act's processes again (as after a lost life; the special
// stage's results return to the act this way).
void restart_act(Game& g);
// 080452D0's end (story and time attack): after the player died, a life
// is lost (0803D55C; the act starts again from its checkpoint, 0803CF68)
// or, past the clock's limit, the time is over (0803D638).
void player_died(Game& g);

} // namespace sonic_advance_gba
