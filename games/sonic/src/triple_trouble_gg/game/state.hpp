#pragma once
#include "triple_trouble_gg/game/object.hpp"
#include "triple_trouble_gg/game/types.hpp"
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
namespace triple_trouble_gg {

constexpr u8 lo(std::uint16_t v) {
    return u8(v);
}

constexpr u8 hi(std::uint16_t v) {
    return u8(v >> 8);
}

// Packed BCD addition (ADD then DAA), for values that stay below 100.
// ADC then DAA: the sum of two BCD bytes and a carry, with the carry out.
struct BcdSum {
    u8 value;
    bool carry;
};

constexpr BcdSum bcd_add_carry(u8 a, u8 b, bool carry_in) {
    const unsigned c = carry_in ? 1 : 0;
    const unsigned v = unsigned(a) + b + c;
    const bool half = ((a & 0x0F) + (b & 0x0F) + c) > 0x0F;
    bool carry = v > 0xFF;
    unsigned r = v & 0xFF;
    unsigned fix = 0;
    if (half || (r & 0x0F) > 9) {
        fix |= 0x06;
    }
    if (carry || r > 0x99) {
        fix |= 0x60;
        carry = true;
    }
    r = (r + fix) & 0xFF;
    return {u8(r), carry};
}

// SUB then DAA: a - b in BCD (the borrow is a < b).
constexpr u8 bcd_sub(u8 a, u8 b) {
    unsigned r = (unsigned(a) - b) & 0xFF;
    if ((a & 0x0F) < (b & 0x0F) || (r & 0x0F) > 9) {
        r = (r - 0x06) & 0xFF;
    }
    if (a < b || unsigned(a - b) > 0x99) {
        r = (r - 0x60) & 0xFF;
    }
    return u8(r);
}

constexpr u8 bcd_add(u8 a, u8 b) {
    unsigned v = unsigned(a) + b;
    if ((v & 0x0F) > 9 || ((a & 0x0F) + (b & 0x0F)) > 0x0F) {
        v += 0x06;
    }
    if (v > 0x9F) {
        v += 0x60;
    }
    return u8(v);
}

// Program modes (FlowState::mode, D143): the routine the main program runs
// (table 0E1D).
namespace mode {
inline constexpr u8 sega_logo = 0x00;
inline constexpr u8 intro = 0x01;
inline constexpr u8 title = 0x02;
} // namespace mode

// Bits of FlowState::frame_work (D144): what the frame interrupt does
// besides its fixed services.
namespace frame_work {
inline constexpr u8 no_level_services =
    0x01; // no animated tiles, fades, sprites or animators (06C3, 06DE; the pause)
inline constexpr u8 build_sprites = 0x02; // objects become sprites (06ED)
} // namespace frame_work

struct VideoState {
    std::array<u8, 11> registers{}; // D11E VDP registers 0-10 as last written
    u8 loading{};                   // D12C nonzero: the frame interrupt only plays sound
    u8 frame_counter{};             // D12E incremented by every frame interrupt
    u8 water_line_mode{};           // D12F FE/FF: the line interrupt is armed
    u8 line_split{};                // D130 nonzero: the line interrupt does nothing
    u8 band{};                      // D131 the band effect's next band (0930)
    u8 band_counter{};  // the line counter register 10 holds for the band effect (hand-bound: none)
    u8 water_line{};    // the line counter (VDP register 10) the frame interrupt armed
    u8 frame_done{};    // D132 set by the frame interrupt, cleared by the wait (084C)
    u8 busy{};          // D133 nonzero: the frame interrupt only uploads colours and plays sound
    u8 sprites_dirty{}; // D134 the sprite table is uploaded at the next frame
    u8 sprite_parity{}; // D135 even and odd frames upload the sprites in different orders
};

struct InputState {
    u8 held{}; // D137 buttons held: bits 0 up 1 down 2 left 3 right 4 button 1 5 button 2 7 start
    u8 held_previous{};    // D138
    u8 pressed{};          // D139 newly pressed buttons (start excluded from bit 6)
    u8 pressed_previous{}; // D13A
    u8 pressed_copy{};     // D13B
    u8 disabled{};         // D136 nonzero: the controller is not read
    u8 repeat_ticks{};     // D116 frames Up or Down has been held on the sound test (10:BD22)
};

struct FlowState {
    u8 character{};                            // D13F 1 Sonic, 2 Tails (the SEGA logo sets 1)
    u8 mode{};                                 // D143 the program mode (mode::)
    u8 frame_work{};                           // D144 frame_work bits
    u8 zone{};                                 // D145
    u8 act{};                                  // D147
    u8 demo{};                                 // D160 bit 7: a demo is playing; the low bits which
    std::uint16_t demo_frames{};               // D161 frames left
    std::uint16_t demo_input{};                // D163 next input pair (bank demo_bank)
    u8 demo_bank{};                            // D165
    u8 demo_aborted{};                         // D166 FF: Start ended the demo
    u8 placement_ticks{};                      // D141 the placements are scanned every 8 frames
    u8 next_mode_hint{};                       // D168
    u8 no_clock{};                             // D149 nonzero: the act's clock does not run (3258)
    std::array<std::uint16_t, 2> checkpoint{}; // D169 where the act restarts (X, Y - 32)
    std::array<std::uint16_t, 2> checkpoint_camera{}; // D16D the camera there
    std::array<u8, 3> checkpoint_time{}; // D17B the clock there (seconds, minutes, D15F); also the
                                         // clock kept for a special stage's return
    u8 restart_point{}; // D171 nonzero: the act starts at a checkpoint (41C0); the intro counts the
                        // emeralds taken in it
    u8 special_entry{}; // D17A
    std::array<std::uint16_t, 2> special_return{}; // D172 where the player comes back from a
                                                   // special stage (X, Y; cleared with D17A, 3302)
    std::array<std::uint16_t, 2> special_camera{}; // D176 the camera there
    u8 flight_won{}; // D180 the flight stage (mode 0D) won: its emerald scene follows (1EB2)
    u8 emeralds{};   // D17E (hypothesis: the emeralds won) cleared at the character select (1AF0);
                   // picks the next special stage (12F3); 5 turns item boxes of kind 1 into kind 8
                   // (1E:8298)
    u8 saved_zone{};             // D146 the act to return to from a special stage
    u8 saved_act{};              // D148
    u8 object_updates{};         // D142 counts the object updates (4F48)
    u8 intro_secret{};           // DC98 the intro's button code was entered (0C:B356)
    u8 rings_kept{};             // DC99 rings carried into the next act's start (1268)
    std::uint16_t title_ticks{}; // C001 the title's frames left (the level map's first bytes)
};

struct ScoreState {
    u8 lives{};                        // D140 BCD
    std::array<u8, 3> points{};        // D14C BCD, lowest byte first
    std::array<u8, 3> points_before{}; // D14F the points before the last addition (2430)
    u8 points_changed{};               // D153 1 after an addition, 2 when it overflowed
    std::array<u8, 2>
        ring_bonus{}; // D155 the act's ring bonus in hundreds of points (BCD, D156; D155 stays 0)
    std::array<u8, 2> time_bonus{}; // D157 the time bonus likewise (D158)
    u8 point_rewards{};             // D14B counts each 50000 points (2484; 1 at the start)
    u8 rings_low{};                 // D159 BCD
    u8 rings_high{};                // D15A
    std::array<u8, 2> ring_goal{};  // D15B the flight stage's rings to take (BCD: 0080)
    u8 seconds{};                   // D15D BCD
    u8 minutes{};                   // D15E
    u8 clock_extra{};               // D15F
};

struct ClockState {
    u8 running{}; // D181
    u8 stopped{}; // D182
    u8 ticks{};   // D183 frames in the current second
};

// Bits of CameraState::map_flags (D184).
namespace map_flag {
inline constexpr u8 enabled = 0x80;        // the camera follows (3F81 runs)
inline constexpr u8 update_pending = 0x40; // the new position takes effect at the next frame
inline constexpr u8 column_pending = 0x20; // a new column waits for the frame interrupt (4644)
inline constexpr u8 row_pending = 0x10;    // a new row waits for the frame interrupt (46F0)
inline constexpr u8 right = 0x08, left = 0x04, down = 0x02, up = 0x01; // this frame's moves
} // namespace map_flag

// Bits of CameraState::mode (D185).
namespace camera_mode {
inline constexpr u8 scripted = 0x01; // the camera moves toward scripted_x/y (4C54)
inline constexpr u8 centred =
    0x02; // the player is aimed at the screen's middle (4BD9; Atomic Destroyer's chase)
inline constexpr u8 ramp = 0x04; // the step limit grows back to 8 (4CA3)
} // namespace camera_mode

// The camera (D184-D3C0, cleared with the level's map state by 428B).
struct CameraState {
    u8 map_flags{};               // D184 map_flag bits
    u8 mode{};                    // D185 camera_mode bits
    u8 block_row{};               // D187 the camera's row of 32-pixel blocks (4053)
    std::uint16_t block_column{}; // D196 its column (4053)
    u8 scroll_x{};                // D198 the background's scroll (VDP register 8)
    u8 scroll_y{};                // D199 (register 9)
    std::uint16_t x{};            // D19A
    std::uint16_t y{};            // D19C
    std::uint16_t map_cell{}; // D39E the level map cell at the camera (4053), as an index (address
                              // - C000; 0: none yet)
    std::array<std::uint16_t, 32>
        column_entries{}; // D19E the map entries of the column entering the screen (45E5)
    std::array<std::uint16_t, 36> row_entries{}; // D29E those of the row entering it (46AA)
    std::uint16_t min_y{};                       // D3A2
    std::uint16_t max_y{};                       // D3A4 (the header's value minus 18)
    std::uint16_t min_x{};                       // D3A6
    std::uint16_t max_x{};                       // D3A8
    std::uint16_t act_min_y{};                   // D3AA the act's limits as loaded
    std::uint16_t act_max_y{};                   // D3AC
    std::uint16_t act_min_x{};                   // D3AE
    std::uint16_t act_max_x{};                   // D3B0
    std::uint16_t next_x{};                      // D3B2 the position for the next frame
    std::uint16_t next_y{};                      // D3B4
    // D3B6-D3BD: where the player should be on screen and the dead zone
    // around it: the aim (X, Y), the current position (moving one pixel per
    // frame toward the aim), its right and left edges (X +-8) and bottom and
    // top edges (Y +-4).
    u8 aim_x{};           // D3B6
    u8 aim_y{};           // D3B7
    u8 window_x{};        // D3B8
    u8 window_right{};    // D3B9
    u8 window_left{};     // D3BA
    u8 window_y{};        // D3BB
    u8 window_bottom{};   // D3BC
    u8 window_top{};      // D3BD
    u8 shake_ticks{};     // D3BE screen shake
    std::uint16_t step{}; // D3BF the horizontal step limit (high byte, 8.8)
};

// The act's map: its header (table 434F, D188-D194: cleared with the
// camera by 428B), the decompressed map of block numbers (C001 on), and the
// block properties table (D3FA).
struct LevelState {
    u8 block_bank{};             // D188 bank of the block table
    u8 map_bank{};               // D189 bank of the compressed map
    std::uint16_t block_table{}; // D18A per block the address of its 4x4 map entries
    std::uint16_t map_source{};  // D18C the compressed map
    std::uint16_t row_table{};   // D18E per row of blocks its offset in the map (bank 1)
    std::uint16_t row_up{};      // D190 the offset to the row above (minus a row)
    std::uint16_t row_stride{};  // D192 blocks per row
    std::uint16_t
        bottom_offset{}; // D194 the offset from the camera's row to the screen's bottom row
};

// The level map (C000-CFFF; the map's first byte is at C001). Kept apart
// from LevelState because no reset clears it.
struct LevelMap {
    // C000 block numbers; index = address - C000. The area runs on to D0FF:
    // the largest map (zone 1 act 2) ends at D001, the rest is free RAM the
    // streaming may read near an act's edge (zeros, or what a longer map
    // left there).
    std::array<u8, 0x1100> cells{};
    // The title counts its time in C001-C002 (FlowState::title_ticks) over
    // whatever map is there, until the next map is decompressed.
    bool holds_title_count{};
};

// The camera's start and its scripted target (D3F2-D3F8).
struct CameraTargets {
    std::uint16_t start_x{};    // D3F2
    std::uint16_t start_y{};    // D3F4
    std::uint16_t scripted_x{}; // D3F6
    std::uint16_t scripted_y{}; // D3F8
};

// The last terrain probe (6FA6, 709A): the block under a point, its
// properties and the floor and wall profile values there.
struct ProbeState {
    u8 attribute{}; // D100 the block record's second byte
    u8 block{};     // D400
    std::uint16_t
        cell{}; // D401 the map cell probed, as an index (address - C000; FFF: outside; 0: none yet)
    std::uint16_t x{};           // D405
    std::uint16_t y{};           // D407
    u8 flags{};                  // D411 the block's terrain flags
    u8 wall{};                   // D414 the wall profile at the probed Y
    u8 floor{};                  // D415 the floor profile at the probed X
    u8 landed_attribute{};       // D416 D100 when an object last landed (6668)
    std::uint16_t ring_x{};      // D409 where a taken ring was
    std::uint16_t ring_y{};      // D40B
    std::uint16_t switch_cell{}; // D403 the switch last stepped on (63CE), as an index (address -
                                 // C000; 0: none)
    std::uint16_t drop_cell{}; // DBC1 the map cell whose block last dropped (kind 0C, 639E), as an
                               // index (address - C000; 0: none)
    std::uint16_t block_x{};   // D40D where a crumbled block comes back (85F2)
    std::uint16_t block_y{};   // D40F
};

// Object art streamed into video memory over several frames (7C15): a
// request (D434/D435 waiting, D436 running) selects a list (7D1B) of
// blocks: bank (bit 7: mirrored), tile count, VRAM address, source.
struct ObjectArtState {
    u8 queued{};            // D434
    u8 queued_next{};       // D435
    u8 request{};           // D436
    u8 bank{};              // D437
    u8 tiles_left{};        // D438
    std::uint16_t source{}; // D439
    std::uint16_t vram{};   // D43B
    std::uint16_t list{};   // D43D
    // D430, D432: the item boxes showing their item with art slot 1 or 2
    // (slot number + 1; 0 none; the original keeps the object's address;
    // hand-bound).
    std::array<u8, 2> box_owner{};
};

struct PaletteState {
    std::array<u8, 64> colors{}; // D4B3 the colours as uploaded (CRAM shadow)
    u8 dirty{};                  // D4F3 the colours are uploaded at the next frame

    // D4F6-D4F9: fades of the background (0) and sprite (1) colours: a mode
    // (bits 7 fade in, 6 fade out, 5 set at once, 4 white instead of black;
    // low bits the step) and the palette number (table 0E:B6FC).
    struct Fade {
        u8 mode{}, palette{};
    };

    std::array<Fade, 2> fades{};
    std::array<Fade, 2> water_fades{}; // D4FA the underwater colours (DC58)
    u8 water_upload{}; // D4F4 FF: the line interrupt left the underwater colours; upload the others
                       // at the next frame
    u8 water_dirty{};  // D4F5 the underwater colours changed (fades)
    std::array<u8, 64> water_colors{}; // DC58 the underwater colours (both halves)
    u8 water_colors_shown{};           // DBCA nonzero: the colours are not uploaded (2582)
    u8 fade_period{};                  // D4FE frames per fade step, minus one
    u8 fade_counter{};                 // D4FF
};

// One entry of the game's sprite table (DB00 Y, DB40 X and tile). For the
// widescreen view (not in the original) an object's entries also keep the
// full position relative to the camera that their bytes were cut from
// (Y E0 above or below the picture, X 0 off its sides), as long as the
// bytes are still the ones placed (`placed_y`, `placed_x`).
struct SpriteSlot {
    u8 y{}, x{}, tile{};
    std::int16_t full_x{}, full_y{};
    u8 placed_y{}, placed_x{};
    bool placed{};
    u8 owner{}; // the type of the object that placed it
};

struct SpritesState {
    std::array<SpriteSlot, 64> table{}; // DB00 (Y) / DB40 (X, tile)
    u8 player_last{};                   // DBC0 nonzero: the player's entries go behind the others
    u8 hud_small{};                     // DBC3 nonzero: entries up to 62 are object sprites
};

// The player's sprite art, streamed into the first tiles as frames change
// (0A0C, 0B01): per player the upload state (bit 7 pending, 6 mirrored,
// 5 always set), the frame whose art is in place, and the state last seen.
struct PlayerArtState {
    u8 mode{};              // D42A
    u8 frame{};             // D42B
    u8 mode_seen{};         // D42C
    u8 partner_mode{};      // D42D
    u8 partner_frame{};     // D42E
    u8 partner_mode_seen{}; // D42F
};

// Level-wide tile animation and the HUD's spinning ring (bank 1D, 8770 and
// 87D5).
struct AnimationState {
    // D483: the sparkle's (type 5) sprite offsets, Y and X four each (its
    // frames' layouts point here; written by 1E:813B).
    std::array<u8, 8> sparkle_offsets{}; // D483
    u8 ring_phase{};                     // D3DC 0-3
    u8 ring_tile{};                      // D3DD
    std::uint16_t ring_art{};            // D3DF bank 1D source of the 4 x 128 bytes
    std::uint16_t ring_vram{};           // D3E1 0 = none
    u8 ring_ticks{};                     // D3E3

    // D490: four tile animations (8 bytes each): type (table 803E), and
    // the type's own bytes (a phase and a frame counter).
    struct Animator {
        u8 type{};
        std::array<u8, 7> bytes{};
    };

    std::array<Animator, 4> animators{};
    u8 opening{};                // D4B0 FF while the ground opens (type 9)
    std::uint16_t opening_row{}; // D4B1 the opening's next row: the address of its name table place
                                 // in 1D:832C on
};

// The player against the terrain (5F53 on): the last frames' blocks and
// flags under the player and at his head.
struct TerrainState {
    u8 timer{};           // D429
    u8 landed_previous{}; // D417 D416 of the frame before
    u8 block{};           // D418 the block under the player
    u8 flags{};           // D419 its flags
    u8 flags_previous{};  // D41A
    u8 head{};            // D41D the flags at his head (6E16)
    u8 head_previous{};   // D41E
    u8 block_previous{};  // D41F
    u8 pipe_block{}; // D41B the pipe junction block at the player's head (72F1); cleared by 3D99
    u8 pipe_block_previous{}; // D41C the one before
    u8 spring_ticks{};        // DBE0
    u8 ring_block{};          // D3DE the ring block's number minus 64 (6E8D)
};

// The player's side probes (988E): the left and right points, relative to
// his position.
struct PlayerBox {
    std::uint16_t left_x{};  // D420
    std::uint16_t left_y{};  // D422
    std::uint16_t right_x{}; // D424
    std::uint16_t right_y{}; // D426
};

// The object placements (bank 1C, 8000): per placement record whether it
// has an object (D43F) and whether the first scan is done (D47F).
struct PlacementState {
    std::array<u8, 64> spawned{}; // D43F
    u8 scanned{};           // D47F (the flight stage: its course 0 not started, 1 running, FF done)
    std::uint16_t course{}; // D480 the flight stage: its course's next entry (bank 1C)
};

// A name table animation (228E): a rectangle drawn every few frames, growing
// (four records at D43F, 16 bytes each; the same memory holds the
// placement flags during acts).
struct MapAnimation {
    u8 active{};                  // +0 1 running, FF ending
    u8 growth{};                  // +1 bit 0 left (source), 1 left (address), 3 up
    u8 period{};                  // +2
    u8 counter{};                 // +3
    u8 bank{};                    // +4 of the source
    u8 steps{};                   // +5 draws left
    u8 source_columns{};          // +6 the source's row length (and the widest)
    u8 columns{};                 // +7
    u8 max_rows{};                // +8
    u8 rows{};                    // +9
    u8 left_steps{};              // +A bit 0: steps that move the source
    u8 colors{};                  // +B colour set (0E:B497), 0 none
    std::uint16_t source{};       // +C
    std::uint16_t name_address{}; // +E
};

struct MapAnimationState {
    std::array<MapAnimation, 4> records{}; // D43F
};

// The uses of ActState::scene (DC46-DC52).
namespace scene_byte {
// Atomic Destroyer act 3's end scene.
inline constexpr unsigned landed = 1; // DC47 the player has leapt back (0C:AB98)
inline constexpr unsigned go = 2;     // DC48 he may run off (0C:AC0C)
inline constexpr unsigned done = 3;   // DC49 the capsule is open (17:AFDD)
// The flight stage (mode 0D).
inline constexpr unsigned speed = 0;       // DC46 added to the distance each frame
inline constexpr unsigned distance = 1;    // DC47-DC48 (16 bits)
inline constexpr unsigned phase = 3;       // DC49 0-3, a step every 7 frames
inline constexpr unsigned phase_ticks = 4; // DC4A
inline constexpr unsigned counter = 6;     // DC4C
inline constexpr unsigned plane_x = 7;     // DC4D the plane's place, moved with the view (03:BE38)
inline constexpr unsigned plane_y = 8;     // DC4E
inline constexpr unsigned horizon = 11;    // DC51 the line of the picture's split
inline constexpr unsigned split = 12;      // DC52 FF: the picture is split at the horizon (act 1)
} // namespace scene_byte

// The act (D3C1-DC97 outside the groups above; cleared by 327E).
struct ActState {
    u8 event{};       // D3FE an act event (1FC6); also stops the tile animations
    u8 boss_signal{}; // D3FF set by zone 9's guardian when beaten (17:BA1A); read by the rival
                      // (16:BF65)
    u8 variant{};     // D3E4 nonzero in some acts (no ring art, other music: 21F0, 8798)
    u8 object_mode{}; // D3EB nonzero: objects update plainly; no act music (21DC)
    u8 time_over{};   // D3CF C0 when the clock reached 10:00
    u8 palette_cycle_off{};           // D48F
    std::uint16_t music_ticks{};      // D3EE frames until the act's song returns
    u8 extra_loader{};                // DBD8
    u8 loop_exit{};                   // DBCD nonzero: the act loop ends
    u8 no_pause{};                    // DBCB
    std::uint16_t block_properties{}; // D3FA bank 0E: per block its terrain record
    std::uint16_t water_level{};      // D3E7 0 = no water in this act
    u8 water_kind{};                  // D3E9
    u8 line_effect{};                 // D3EA a line interrupt effect (outside mode 6)
    u8 music_request{};  // D3EC a song for the sound driver at the next frame interrupt
    u8 music_playing{};  // D3ED the act's song
    u8 objects_frozen{}; // D3FC nonzero: the objects are not updated (3431, 4F3F)
    u8 boss{}; // D3E5 the act's boss (slot number + 1, 0 none; the original keeps its address;
               // hand-bound)
    std::uint16_t flash_color{}; // DBDC the colour a hit boss flashes (5888, by zone)
    u8 rings_gone{};             // D3FD nonzero: scattered rings vanish (97F9)
    u8 rings_gone_too{};         // DBD9 the same (97F9)
    // DBE6-DC45: shared work bytes. The tile decoder uses the first 77 and
    // clears them (2DD7); the crawler keeps its trail here (type 3E: the
    // head's X, newest first, a word each: Game::trail_x); zone 1 act 3
    // keeps its scroll bands at 1-6 (Game::band_scroll).
    std::array<u8, 96> work{}; // DBE6
    // DC46-DC52: bytes the scenes use their own way (scene_byte names them).
    std::array<u8, 13> scene{}; // DC46
    u8 flight_hit{};            // DBD2 the flight stage: the plane was hit (03:BDE9)
    u8 flight_tumble{};         // DBD3 ... it tumbles
    u8 flight_tumble_ticks{};   // DBD4 ... frames left (120)
    u8 flight_rings_off{};      // DBD5 ... no more rings (1E3C)
    u8 credits_running{}; // DBE1 the ending with all emeralds: its credits run (its objects clear
                          // it)
    u8 machine_down{};    // DBD1 nonzero: the last machine is beaten (0F:BCB3; its lightning stops,
                          // B2F7)
    u8 menu_choice{};     // DBDB the sound test's choice (0-11h: zone and act, table 10:BF4A)
    u8 entry_high{};      // D3F1 the high byte 2C75 gives its entries
    u8 effect_flags{};    // DC56 bit 7: 1C72's effects are off (set when an act ends, 12CF)
    u8 ride_limit{};      // DC57 zone 1 act 3: the player's rightmost X on the train
    u8 breaker{}; // DBC4 the object that broke the last block (7228; slot number + 1, 0 none; the
                  // original keeps its address; hand-bound)
};

struct PlayerState {
    std::uint16_t flight_ticks{};  // D3C7 Tails' flight time left (state 18; 300 from 3C91)
    u8 air_ticks{};                // D3CD frames to the next bubble under water (78)
    u8 air_count{};                // D3CE bubbles so far
    u8 jump_ticks{};               // D3C3 frames the jump button has been held
    std::uint16_t top_speed{};     // D3D6 (high byte D3D7 compared in B0F3)
    std::uint16_t input_force{};   // D3D8 the controller's acceleration this frame (B2A7)
    std::uint16_t slope_force{};   // D3DA the floor's slope (table 0C:B79C)
    std::array<u8, 3> path{};      // D3D0 the position along a loop's path (24 bits)
    u8 path_end{};                 // D3D3 10 when a path ends (358D, 3711; no reader found)
    std::uint16_t sparkle_ticks{}; // D3D4 the peel out's sparkle: frames left (30)
    u8 spring_flag{};              // D3F0
    u8 hit{};                      // D3C1 (37B5)
    u8 hurt_pending{}; // D3C2 frames untouchable after a hurt (37EE: 120) or the peel out (8243:
                       // 30)
    u8 carried{}; // D3C4 the id (byte 32) of the platform the player stands on (572E; 37B5, 5A3B)
    u8 holder{};  // D3C9 the object (type 1B) holding the player (slot number + 1, 0 none; the
                  // original keeps its address; hand-bound)
    u8 shell{}; // D3CB the shell the player has jumped onto (state 16; slot number + 1, 0 none; the
                // original keeps its address; hand-bound)
    u8 checks_off{};       // D428 nonzero: no checks after the player's routine (3484)
    u8 partner_hidden{};   // DBDA nonzero: the partner's art is not streamed
    u8 in_scene{};         // DBD7 nonzero: a scene holds the player (3F76: player state 4A)
    u8 hold_x{};           // DBD6 nonzero: the player is held at X 4A (3472)
    u8 power_up_pending{}; // DBE3 (0C:B811)
    u8 power_flags{};      // DBE2 bit 2: the sneakers run (B9F3)
    std::uint16_t power_up_ticks{}; // DBE4 the power-up's time (cleared on a hit, 386E)
    u8 tails_landing{};             // DBCE cleared when Tails lands (65FD)
    u8 rings_rewarded{};            // DBDF the hundreds of rings that gave a life
    u8 touch_kind{}; // DBD0 30 after an object's touch test (58AC), 50 when crushed (37CE); read
                     // when hurt (38CA)
};

} // namespace triple_trouble_gg
