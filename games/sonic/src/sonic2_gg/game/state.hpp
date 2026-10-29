#pragma once
#include "sonic2_gg/game/object.hpp"
#include "sonic2_gg/game/types.hpp"
#include <array>
#include <cstdint>

// The game's state, grouped by what it is about. The groups are members of
// Game (game.hpp).
//
// Each field names in its comment the work-RAM address where the original
// keeps it, to link the code to the original routines; nothing depends on
// that layout. Fields keep the original's widths where the game's arithmetic
// depends on them (8-bit counters wrap, positions are 16 bits with an 8-bit
// fraction).
namespace sonic2_gg {

// Low and high byte of a 16-bit value, and the value with one byte replaced.
constexpr u8 lo(std::uint16_t v) {
    return u8(v);
}

constexpr u8 hi(std::uint16_t v) {
    return u8(v >> 8);
}

constexpr std::uint16_t with_lo(std::uint16_t v, unsigned b) {
    return std::uint16_t((v & 0xFF00) | (b & 0xFF));
}

constexpr std::uint16_t with_hi(std::uint16_t v, unsigned b) {
    return std::uint16_t((v & 0x00FF) | ((b & 0xFF) << 8));
}

// Bits of FlowState::game_flags (D293): what the main loop runs (065C).
namespace game_flag {
inline constexpr u8 scene = 0x80;         // the title, the intro or the ending
inline constexpr u8 playing = 0x40;       // a game is on
inline constexpr u8 act_complete = 0x20;  // the act's tally
inline constexpr u8 zone_complete = 0x10; // the zone's tally
inline constexpr u8 life_lost = 0x08;     // Sonic died
inline constexpr u8 act_start = 0x04;     // the title card and set-up
inline constexpr u8 paused = 0x02;
} // namespace game_flag

// Bits of FlowState::attract_flags (D292): the attract sequence (2472).
namespace attract_flag {
inline constexpr u8 start = 0x80; // Start pressed: a game begins (or the demo ends)
inline constexpr u8 logo = 0x40;
inline constexpr u8 intro = 0x20;
inline constexpr u8 title = 0x10;
inline constexpr u8 demo = 0x08;
} // namespace attract_flag

// Bits of CameraState::map_flags (D15E).
namespace map_flag {
inline constexpr u8 enabled = 0x80;        // the camera follows (49EF runs)
inline constexpr u8 update_pending = 0x40; // the new camera position takes effect at the next frame
inline constexpr u8 right = 0x08, left = 0x04, down = 0x02, up = 0x01; // this frame's moves
} // namespace map_flag

// Bits of CameraState::mode (D15F).
namespace camera_mode {
inline constexpr u8 scripted = 0x01;      // the camera aims at scripted_x/y (4A21)
inline constexpr u8 hold_vertical = 0x02; // vertical following paused
} // namespace camera_mode

struct ProbeState {
    u8 block_attribute{};     // D100 second byte of the current terrain profile (733E)
    u8 block{};               // D353
    std::uint16_t cell{};     // D354 the probed block's index in the level map
    std::uint16_t x{};        // D356
    std::uint16_t y{};        // D358
    std::uint16_t debris_x{}; // D35A where broken blocks and collected map rings spawn objects
    std::uint16_t debris_y{}; // D35C
    u8 type{};                // D35E profile type of the probed block
    std::uint16_t extra{};    // D35F
    u8 wall{};                // D361 wall position at the probed Y
    u8 floor{};               // D362 floor height at the probed X
};

struct VideoState {
    u8 frame_counter{};       // D12F incremented by every frame interrupt
    u8 line_interrupt{};      // D131 nonzero: line interrupt in use (water)
    u8 line_interrupt_line{}; // D132 the water line's picture line (VDP register 10)
    u8 frame_done{};          // D135 set by the frame interrupt, cleared by 05C5
    u8 skip_video{};          // D136 frame interrupt does sound only
    u8 text_attribute{};      // D2C7 high byte written with text tiles
    u8 irq_colors_only{};     // D47E
};

struct InputState {
    u8 controller2_code{}; // D12C port DD at boot; B6 enables the zone select
    std::array<u8, 16>
        held{}; // D137 newest first; bits 0 up 1 down 2 left 3 right 4 button 1 5 button 2 7 start
    std::array<u8, 16>
        pressed{};       // D147 newly pressed buttons, newest first (start excluded from bit 6)
    u8 pressed_latest{}; // D157
};

// One entry of the game's sprite table: the Y, X and tile the hardware
// shows (8 bits each; the game hides sprites with Y = E0 and puts those past
// the screen's sides at X = 0), and for views larger than the LCD where the
// game meant it to be: the full position, camera relative, kept while the
// entry still holds the bytes it was placed with.
struct SpriteSlot {
    u8 y{}, x{}, tile{};
    std::int16_t full_x{}, full_y{};
    u8 placed_y{}, placed_x{};
    bool placed{};
    bool repeat{}; // Rain: repeated every 256 pixels across a larger view
    u8 owner{};    // Type of the object that placed it (0: none or unknown)
};

// The HUD's entries in the sprite table (224A, table 227B): the lives icon
// and digit (52-55), the ring counter (56-59) and the clock (60-63).
inline constexpr unsigned hud_first = 52, hud_lives = 52, hud_rings = 56, hud_minutes = 60,
                          hud_seconds = 62, hud_end = 64;

// One sprite's offset in a layout (Y, then X), and the fractions.
struct LayoutOffset {
    std::uint16_t dy{}, dx{};
    u8 dy_fraction{}, dx_fraction{};
};

struct SpritesState {
    u8 dirty{};            // D134 sprite table needs uploading
    u8 next_y{}, next_x{}; // D369/D36B next entries while building (1821)
    // DB00-DBBF: the table as the frame interrupt uploads it. Entries 52-63
    // are the HUD.
    std::array<SpriteSlot, 64> table{};
    // D3CE: a sprite layout built at run time (an Aqua Lake bubble cluster,
    // the final boss's fire): the offsets of up to eight sprites, each with
    // a fraction (D3EE) for the cluster's slow spreading.
    std::array<LayoutOffset, 8> run_layout{};
};

struct CameraState {
    u8 map_flags{};             // D15E map_flag bits
    u8 mode{};                  // D15F camera_mode bits
    u8 scroll_x{};              // D172 the background's scroll (VDP register 8)
    u8 scroll_y{};              // D173 (register 9)
    std::uint16_t x{};          // D174
    std::uint16_t y{};          // D176
    std::uint16_t min_y{};      // D27C
    std::uint16_t max_y{};      // D27E
    std::uint16_t min_x{};      // D280
    std::uint16_t max_x{};      // D282
    std::uint16_t target_x{};   // D284
    std::uint16_t target_y{};   // D286
    u8 offset_x{};              // D288 Sonic's screen position the camera aims for
    u8 offset_y{};              // D289
    std::array<u8, 6> window{}; // D28A current offsets and dead zone edges (5CC7)
    u8 shake_ticks{};           // D290 screen shake
    std::uint16_t scripted_x{}; // D2CE
    std::uint16_t scripted_y{}; // D2D0
};

struct LevelState {
    // C000-CFFF: the decompressed level map, block numbers; index = address - C000
    std::array<u8, 4096> map{};
    u8 map_bank{};                         // D162 bank of the block table and terrain profiles
    u8 layout_bank{};                      // D163
    std::uint16_t block_table{};           // D164 32 bytes (4x4 name table entries) per block
    std::uint16_t layout_source{};         // D166 compressed layout
    std::uint16_t row_table{};             // D168 offset of each layout row in level_map
    std::uint16_t attribute_table{};       // D16A per-block terrain profile numbers
    std::uint16_t start_camera_x{};        // D2CA
    std::uint16_t start_camera_y{};        // D2CC
    std::uint16_t terrain_profiles{};      // D2D4 table of profile pointers (bank 1E)
    std::uint16_t animated_tiles_source{}; // D395 bank 1D, 6 phases of 4 x 128 bytes? (7EAF)
    std::uint16_t animated_tiles_vram{};   // D397 0 = none
};

struct FlowState {
    u8 attract_flags{};               // D292 attract_flag bits
    u8 game_flags{};                  // D293 game_flag bits
    u8 zone_select_used{};            // D294
    u8 zone{};                        // D295
    u8 act{};                         // D296
    u8 zone_select_cursor{};          // D2C2
    u8 continue_ticks{};              // D2C3
    u8 continue_count{};              // D2C4
    std::uint16_t demo_input{};       // D2D2 next demo input pair
    u8 frame_parity{};                // D2D6
    u8 demo_number{};                 // D2D7
    u8 demo_bank{};                   // D2D8
    std::uint16_t demo_start{};       // D2D9
    std::uint16_t title_card_ticks{}; // D3BA
};

struct ScoreState {
    u8 lives_at_start{};            // D297
    u8 lives{};                     // D298
    u8 rings{};                     // D299 BCD
    std::array<u8, 3> points{};     // D29C BCD, low byte first
    std::array<u8, 3> high{};       // D29F BCD
    std::array<u8, 3> bonus{};      // D2A2 BCD tally
    std::array<u8, 3> time_bonus{}; // D2A5 BCD
    u8 changed{};                   // D2B5
    u8 extra_life_awarded{};        // D2B6
    u8 continues{};                 // D2BD
    u8 emeralds{};                  // D2C5 bit per zone
    u8 emerald_found{};             // D2C6 in this act
};

struct ClockState {
    u8 keep{};      // D2B7
    u8 running{};   // D2B8
    u8 seconds{};   // D2B9 BCD
    u8 minutes{};   // D2BA
    u8 ticks{};     // D2BC
    u8 time_over{}; // D47A the clock reached 10:00
};

struct PaletteState {
    u8 fade_speed{};             // D2C8 frames per fade step
    u8 fade_counter{};           // D2C9
    std::array<u8, 8> cycle1{};  // D481 cycle number, -, phase, ticks, ...
    std::array<u8, 8> cycle2{};  // D489
    std::array<u8, 8> cycle3{};  // D491
    std::array<u8, 8> cycle4{};  // D499
    std::array<u8, 64> colors{}; // D4A1 CRAM shadow
    std::array<u8, 4> fades{};   // D4E1 background: mode/step, palette; sprites: mode/step, palette
    u8 dirty{};                  // D4E5
};

struct TilesState {
    u8 player_mode{};              // D34E 7 upload pending, 6 mirrored, 5 always set (12A8)
    u8 player_frame{};             // D34F Sonic's frame whose tiles are uploaded (10F3)
    u8 player_mirror{};            // D350 last mirror state
    u8 animated_phase{};           // D351 0-5 (7EAF)
    u8 object_request{};           // D3AB 7743
    u8 object_bank{};              // D3AC
    u8 object_count{};             // D3AD
    std::uint16_t object_source{}; // D3AE
    std::uint16_t object_vram{};   // D3B0
    std::uint16_t object_list{};   // D3B2
};

struct Position {
    std::uint16_t x{}, y{};
};

struct PlayerState {
    u8 slope{};                  // D363 slope of the floor Sonic stands on (index into 402E)
    u8 slope_previous{};         // D364
    u8 floor_block{};            // D365
    u8 floor_type{};             // D366
    u8 floor_block2{};           // D367
    u8 cart_block{};             // D368 block under the mine cart last frame (8AB3)
    std::uint16_t max_speed{};   // D36D
    std::uint16_t input_force{}; // D36F horizontal force from the controller
    std::uint16_t slope_force{}; // D371
    std::array<Position, 8>
        speed_trail{}; // D373 Sonic's last 8 positions, newest first (speed shoes images, 03)
    std::array<u8, 3> tube_position{}; // D399 24-bit distance along a tube (state 0C)
    u8 tube_delay{};                   // D39C
    u8 item_flags{};                   // D39D monitor rewards waiting (4820)
    std::uint16_t riding{};            // D39E address of the cart Sonic rides, 0 if none
    std::array<u8, 8> cart_frame{};    // D3A0 tile numbers of Sonic-in-cart frames (4717, 47BB)
    u8 hit_request{};                  // D3A8
    u8 invulnerable_ticks{};           // D3A9
    u8 jump_ticks{};                   // D3AA
    std::uint16_t idle_ticks{};        // D3B4
    u8 platform_slot{}; // D3B9 slot number of the platform carrying Sonic (814F); also holds him
                        // vertically
    u8 pipe_block{};    // D3C4 floor block Sonic's pipe ride saw last (43DC)
    u8 pipe_aligned{};  // D3C5 FF once Sonic is centred in the current pipe block
    u8 swing_angle{};   // D3C6 Sonic on a swing pole (4290); sine table index
    std::uint16_t swing_speed{};    // D3C7 angle step in the high byte, 300-7FF
    std::uint16_t swing_center_x{}; // D3C9 bit 15: the short (38) radius instead of 50
    std::uint16_t swing_center_y{}; // D3CB
    u8 swing_pole_slot{}; // D3CD slot number of the moving swing pole (2A) Sonic hangs on; 0 when
                          // he lets go
    std::uint16_t power_up_ticks{}; // D47B
    u8 power_up_hold{};             // D47D
};

struct BossState {
    std::array<u8, 8>
        parts{};        // D3BC slot numbers of a boss's living children (Sky High: 9D6A); 0 = free
    u8 slot{};          // D445 slot number (1-based) of the act's boss (UGZ: A513)
    u8 target_slot{};   // D446 slot number of the boss's target (UGZ 4D: A7B7)
    u8 throw_seconds{}; // D447 clock seconds when the last bomb was thrown (A37C)
};

struct SpawnerState {
    std::array<u8, 64>
        placement_spawned{}; // D400 per placement record: object type while it exists
};

struct WaterState {
    u8 type{};              // D440 profile type at Sonic's head (723C)
    u8 type_previous{};     // D441
    u8 underwater{};        // D442
    u8 air_ticks{};         // D443
    u8 air_count{};         // D444
    std::uint16_t rise_y{}; // D47F Aqua Lake act 2  # frame interrupt only uploads colours (05BF)
};

struct EndingState {
    u8 phase{}; // D448 the ending's credits (bank 1D, B2FC): 0 waiting, 1 next line, 2-3 letters
                // moving, 4 done
    u8 line{};  // D449 credits line number
    std::array<u8, 48>
        letters{}; // D44A while waiting: a frame count (word); then 16 letters x (X word, Y)
};

struct RainState {
    u8 mode{};  // DBC0 0 none, 1 starting, 2 raining (also the wind for the glider), 3 stopping
    u8 ticks{}; // DBC1
    std::uint16_t stop_ticks{};       // DBC2
    u8 drops_shown{};                 // DBC4 8 while raining; 0 while stopping (drops leaving hide)
    std::array<std::uint16_t, 8> y{}; // DBC5
    std::array<std::uint16_t, 8> x{}; // DBD5
};

} // namespace sonic2_gg
