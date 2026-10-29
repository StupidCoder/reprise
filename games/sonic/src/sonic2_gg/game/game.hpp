#pragma once
#include "sonic2_gg/audio/sound_driver.hpp"
#include "sonic2_gg/audio/sounds.hpp"
#include "sonic2_gg/data/assets.hpp"
#include "sonic2_gg/game/display.hpp"
#include "gg_engine/enhancements/widescreen.hpp"
#include "gg_engine/flow.hpp"
#include "sonic2_gg/game/object_types.hpp"
#include "sonic2_gg/game/rom_tables.hpp"
#include "sonic2_gg/game/scripts.hpp"
#include "sonic2_gg/game/state.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// Sonic the Hedgehog 2 (Game Gear): the game.
//
// Each video frame has two parts, as in the cartridge: the frame's services
// (frame_interrupt: the picture's updates, the controller, palette fades, the
// clock, the sound driver), then the game's program until it waits for the
// next frame. The program's long-running flows (boot, the attract sequence,
// the act start, tallies, the ending) are coroutines (gg_engine/flow.hpp) that wait for
// frames with co_await.
//
// Where things are:
//   state.hpp          the game's state in groups: level, camera, flow, score,
//                      clock, Sonic's movement, palettes, sprites, water, ...
//   object.hpp,        the 20 object slots (slot 0 is Sonic), their types,
//   object_types.hpp,  their scripts (decoded per type and state) and the
//   scripts.hpp,       routines the scripts run (dispatch.cpp)
//   routines.hpp
//   display.hpp        what is shown (gg_engine/video/display.hpp): tile
//                      art, the background map (or the act's map, drawn
//                      from the level, map.cpp), sprites, colours; picture()
//                      renders it
//   rom_tables.hpp     the cartridge tables the code reads, by name
//   audio/             the sound driver and the sound numbers
//   timing.cpp         how long the original's loading takes
//
// Hexadecimal numbers in comments are Z80 addresses of the original routines
// (bank 0 at 0000-3FFF, bank 1 at 4000-7FFF, banked code at 8000-BFFF with its
// bank named) and work-RAM addresses (C000-DFFF) of the state they describe:
// they link the re-implementation to the code it reproduces.
namespace sonic2_gg {

using gg_engine::Flow;
using gg_engine::FlowRunner;

// Controller buttons (as the cartridge reads them into D137).
namespace button {
inline constexpr std::uint8_t up = 0x01, down = 0x02, left = 0x04, right = 0x08;
inline constexpr std::uint8_t one = 0x10, two = 0x20, start = 0x80;
inline constexpr std::uint8_t jump = one | two;
} // namespace button

class Game {
public:
    // With `zone` >= 0 the game skips the attract sequence and starts that
    // act, the way the original's zone select does (D294 set).
    explicit Game(const Assets& assets, int zone = -1, int act = 0);

    // Continues from a state captured while the main loop waits for a frame
    // (all state groups, the sound driver and the display, restored through
    // for_each_state): used to resume play from a saved state.
    void resume_main_loop();

    // Calls f on every part of the state that a quick save keeps.
    template <class F>
    void for_each_state(F&& f) {
        f(level);
        f(camera);
        f(input);
        f(video);
        f(flow);
        f(score);
        f(clock);
        f(player);
        f(probe);
        f(tiles);
        f(sprites);
        f(objects);
        f(spawner);
        f(boss);
        f(water);
        f(palette);
        f(rain);
        f(ending);
        f(sound_.state);
    }

    // True while the main loop waits for the next frame (quick saves).
    bool at_main_loop() const { return in_main_wait_; }

    std::uint32_t random_state() const { return random_state_; }

    void set_random_state(std::uint32_t s) { random_state_ = s; }

    // Main loop iterations completed (development aid for comparisons).
    std::uint64_t main_loop_iterations{};
    // Called as each main loop iteration starts waiting (05CA); used by the
    // test harness to take snapshots at the same point as the original.
    std::function<void()> main_loop_hook;
    // Called at every frame wait (05C5); test harness only.
    std::function<void()> wait_hook;
    // Replaces LD A,R (refresh_register) when set: the test harness replays
    // the values the original read.
    std::function<u8()> random_source;

    // One video frame. `buttons` uses the button:: bits.
    void frame(std::uint8_t buttons);

    // Requests from this frame for the audio side.
    bool voice_requested{}; // the logo's sampled voice (bank 3, 8000) started

    // The game's state (state.hpp).
    LevelState level;
    CameraState camera;
    InputState input;
    VideoState video;
    FlowState flow;
    ScoreState score;
    ClockState clock;
    PlayerState player;
    ProbeState probe;
    TilesState tiles;
    SpritesState sprites;
    std::array<Object, 20> objects{}; // D500: slot 0 is Sonic
    SpawnerState spawner;
    BossState boss;
    WaterState water;
    PaletteState palette;
    RainState rain;
    EndingState ending;
    Display display; // What is shown (gg_engine/video/display.hpp)
    const Assets& rom;

    // Shell option (not in the original): an act started from the level
    // selector goes straight to play, as Sonic 1's level starts do. Until
    // that first act has started, loading takes no time (see spend) and the
    // title card is left out; everything after keeps the original timing.
    bool direct_start{};
    // Shell option: the new game goes straight to the ending (zone 7, the
    // start act: 0, or 1 with all six emeralds), as after the last zone.
    bool ending_start{};

    // The picture of this frame (160x144).
    gg::Screen picture() const;
    // The background entry of the act's map at a world position, as the
    // map rows and columns are drawn (56E0, 57A5).
    std::uint16_t level_entry(int x, int y) const;
    // The world position of the 256x192 picture's top left corner, from
    // the scroll the frame was shown with (shake included).
    int picture_x() const;
    int picture_y() const;
    // The act's background for the display: each background cell holds the
    // map's entry for the world position within the 256x224 area from the
    // picture's corner that falls on it (what the original draws row by
    // row and column by column ahead of the camera).
    Display::LevelBackground level_background() const;
    // Keeps the act's background as the display's tile map when the map it
    // is drawn from is about to go.
    void freeze_level_background();

    // The PSG writes of this frame's sound driver run, in order.
    std::vector<SoundWrite> take_sound_writes() { return sound_.take_writes(); }

    // =========================================================================
    // Widescreen enhancement (render/wide_view.cpp)
    // =========================================================================
    //
    // The host reports the presented view size before each frame. The default
    // 160x144 is the Game Gear LCD and leaves every widescreen rule inert, so
    // the original format and the attract demos keep the original behaviour.
    int view_width{160}, view_height{144};
    // Pixels the presented view extends beyond the LCD crop (VDP pixels
    // 48-207, lines 24-167) on each side, set once the frame interrupt has
    // moved the camera. Camera-relative tests add them so that objects keep
    // running, and stay drawn, until they leave the presented view.
    int wide_left{}, wide_right{}, wide_top{}, wide_bottom{};

    bool widescreen() const { return wide_left || wide_right || wide_top || wide_bottom; }

    // 3BF5 keeps Sonic between screen X 38 and C7, inside the LCD. Where
    // the camera can still scroll toward a side, the limit on that side is
    // moved to the edge of the presented view, so that he is not stopped in
    // plain sight when he outruns the camera. Where it cannot (at a limit,
    // locked behind him, or held by a script in boss arenas and at the act's
    // end) the LCD's edge remains a wall, shown by the arena force field.
    struct ScreenLimits {
        int left, right;
    };

    ScreenLimits wide_screen_limits() const;

    // The world rectangle the camera (and so the LCD) can ever reach in this
    // act: the map header's limits, widened by any later limits.
    using Area = gg_engine::CameraArea;

    Area camera_area() const;

    // True on a frame whose whole picture has the underwater colours only
    // because a water line armed at line 192 fired right after the frame
    // interrupt (05E8, see interrupt.cpp). Enhanced views draw the surface
    // split there instead (render/wide_view.cpp).
    bool water_line_fired_late() const { return water_line_fired_late_; }

private:
    // Widescreen (enhancements/widescreen.cpp): the per-frame margins.
    void update_wide_view();
    // Whether a world position lies inside the presented view (camera-
    // relative: VDP pixels -wide_left..255+wide_right, lines -wide_top..
    // 255+wide_bottom, the original's 603C window grown by the margins), or
    // within `slack` pixels of it. Always false on the LCD.
    bool in_wide_view(unsigned x, unsigned y, int slack = 0) const;

    SoundDriver sound_{rom};
    Scripts scripts_{rom}; // decoded object scripts

    // Sprite frames in the cartridge may point into work RAM: the cart
    // frames' tiles (D3A0) and the run-time layout (D3CE).
    u8 ram_data(unsigned address) const;

    int start_zone_{-1}, start_act_{};
    bool in_main_wait_{};
    int fast_tally_steps_{};
    u8 spawned_slot_{};            // slot number IY points at after 6025
    int water_line_{-1};           // line interrupt armed by this frame's interrupt
    bool water_line_late_{};       // armed at line 192: fires after the next frame interrupt
    bool water_line_fired_late_{}; // it fired at the start of this frame
    FlowRunner program_;
    std::uint8_t buttons_{};
    bool interrupts_{};  // the frame interrupt runs this frame
    int voice_frames_{}; // frames the main program is stalled by the voice

    // --- Time spent by the original's loading code (game/timing.cpp) ---
    // The reconstruction does its work instantly; the original's decoding
    // and video memory fills take CPU time, much of it with interrupts off.
    // Loading routines report their cost (spend); frame boundaries crossed
    // meanwhile become stalled frames (nothing runs), and with interrupts
    // off one frame interrupt is pending until the next EI.
    static constexpr unsigned frame_clocks = 228 * 262;
    unsigned clock_{};   // CPU clocks spent in this frame by modelled work
    bool di_{};          // interrupts disabled (modelled)
    bool irq_pending_{}; // a frame interrupt waits for the next EI
    int stall_frames_{}; // frames that pass before the program goes on
    void spend(unsigned clocks);
    static unsigned fill_clocks(unsigned words); // 13A3

    void di() { di_ = true; }

    void ei();
    bool take_pending_interrupt();

    // `co_await next_frame()`: EI; HALT. With an interrupt pending it runs
    // at once and the HALT ends immediately.
    auto next_frame() {
        struct Awaiter {
            Game& game;

            bool await_ready() { return game.take_pending_interrupt(); }

            void await_suspend(std::coroutine_handle<> h) { game.program_.suspend_at(h); }

            void await_resume() const noexcept {}
        };

        di_ = false;
        return Awaiter{*this};
    }

    // --- Boot and attract sequence (frontend/attract.cpp) ---
    Flow boot();                              // 0000, 0457
    Flow attract();                           // 2472
    Flow title_sequence();                    // 0484
    Flow sega_logo();                         // 28F4
    Flow wait_frames(int count);              // EI; HALT loops
    Flow wait_frame();                        // 05C5
    Flow intro();                             // 0ED8
    Flow title_screen();                      // 0FD4
    void blink_press_start();                 // 1094
    Flow demo();                              // 24EE
    bool attract_aborted() const;             // 10B0
    bool count_to(Object& o, unsigned limit); // 9883 / A738
    void intro_runner_start(Object& o);
    void intro_runner_follow(Object& o);
    void intro_runner_away(Object& o);
    void intro_tails_start(Object& o);
    void intro_tails_follow(Object& o);
    void intro_tails_move(Object& o, unsigned frames, u8 next);
    void intro_tails_wait(Object& o);
    void intro_tails_leave(Object& o);
    void sonic_intro_stand(); // A706
    void sonic_intro_run();   // A729
    void title_sonic_start(Object& o);
    void title_tails_start(Object& o);
    void title_tails_secret(Object& o);

    // --- The frame interrupt (game/interrupt.cpp) ---
    void frame_interrupt();    // 04B7
    void read_controller();    // 1A9D
    void upload_sprites();     // 144B
    void upload_colors();      // 130A
    void run_fades();          // bank 9, 8000
    void fade_step(int index); // bank 9, 8036
    void update_timer();       // 20FD
    void check_demo_abort();   // 05D4

    // --- The screen (game/video.cpp) ---
    void init_display();  // 132E
    void display_on();    // 13C2
    void display_off();   // 13D0
    void clear_screen();  // 17EE
    void clear_sprites(); // 1809
    void clear_vram();    // 76C6
    void clear_cram();    // 7700
    // 1B00: a compressed block of tile art, decoded into the tiles from
    // `first_tile` on.
    void load_tiles(unsigned first_tile, Table block, bool mirrored = false);
    // 19C1: a rectangle of map entries from bank:address, `rows` rows of
    // `columns` entries, from the cell `at` (with the scroll of 1A3F).
    void draw_rect(Display::Cell at, Table entries, int rows, int columns);
    Display::Cell scrolled(Display::Cell c) const; // 1A3F
    void start_fade_out();                         // 12FC
    void clear_level_state();                      // 223C
    void clear_map_state();                        // 53B6

    // --- Sprites (game/sprites.cpp) ---
    void build_sprites();                   // 1821
    void update_screen_position(Object& o); // 3A4C
    void add_sprite_y(Object& o);           // 1884
    void add_sprite_x(Object& o);           // 18D8
    void check_player_tiles();              // 12A8
    void upload_player_tiles();             // 10F3

    // --- Palettes (game/palettes.cpp) ---
    void run_palette_cycles();          // 7BDD
    void palette_cycle(u8* controller); // 7BF5
    void flash_ring_colors();           // 7E0F
    void animate_tiles();               // 7EAF

    // --- Game state helpers (game/game.cpp) ---
    Flow play_voice();          // bank 3, 8000
    void fade_music();          // 10E7
    void new_game();            // 22B7
    void update_timer_digits(); // 212E

    // --- Levels (game/level.cpp) ---
    void load_level_tiles(); // 750A
    Flow init_act();         // 2206
    void init_hud_sprites(); // 224A
    Table act_entry(Table table) const;
    void load_act_header();     // 22ED
    void load_map_header();     // 53C4
    Flow load_map();            // 518A
    void load_start_position(); // 5245
    void show_lives();          // 2643
    void show_rings();          // 2672
    void add_ring();            // 265A
    void play_act_music();      // 10B8
    void fade_in_act();         // 7578

    // --- The map (game/map.cpp) ---
    void update_map();                          // 49EF
    void enable_map();                          // 4A0F
    void disable_map();                         // 4A15
    void script_camera(unsigned x, unsigned y); // 4A21
    void follow_sonic();                        // 4A34
    void lock_camera_left();                    // 4A3F
    void lock_camera_right();                   // 4A4F
    void follow_camera();                       // 5C18
    void update_camera_window();                // 5CC7
    void reset_camera_window();                 // 5D26
    void follow_script();                       // 5D47
    void scroll_horizontally();                 // 4A5F
    void scroll_vertically();                   // 4A9C
    void set_scroll_registers();                // 4AE1

    // --- Main loop (game/main_loop.cpp) ---
    void game_frame();          // 09BB
    void check_pause();         // 0A3E
    Flow pause();               // 099D
    void stop_palette_cycles(); // 7C26
    Flow start_act();           // 0953
    Flow title_card();          // 269D
    Flow reveal_text(Display::Cell at, unsigned text, unsigned stride, int steps, bool tall);
    Flow load_card_tiles();   // 75B6
    Flow hold_title_card();   // 2970
    void move_card_streaks(); // 29D7

    // --- Score (game/score.cpp) ---
    void add_score(Table points);                                    // 1D2C
    void update_high_score();                                        // 1D61
    void show_ring_tally();                                          // 1DAB
    void show_bonus_tally();                                         // 1DBC
    void show_score_tally();                                         // 1DCB
    void draw_big_number(std::span<const u8> bcd, Display::Cell at); // 1DDB

    // --- Objects (game/objects.cpp, dispatch.cpp, spawner.cpp) ---
    void update_objects();                         // 5DA4
    void update_object(Object& o);                 // 5DBD
    void animate_object(Object& o, unsigned bank); // 5F0A
    void start_script(Object& o, unsigned bank);   // 5F27
    void run_script(Object& o, unsigned bank);     // 5F47
    void load_frame(Object& o);                    // 5F6C
    void run_object_handler(Object& o);            // 5FF9
    void check_off_screen(Object& o);              // 603C
    void check_respawn_window(Object& o);          // 60D2
    void delete_object(Object& o);                 // 6108
    void spawn_object(u8 type, u8 param);          // 6004
    Object* find_free_slot();                      // 6025
    int slot_number(const Object& o) const;        // 6376
    void run_routine(Object& o, Routine routine);  // dispatch.cpp
    void spawn_objects();                          // bank 1E, 8000
    void stream_object_tiles();                    // 7743
    void start_object_tiles();                     // 77D2
    void next_object_tiles();                      // 77F3
    void finish_object_tiles();                    // 7818

    // --- Sonic (game/player.cpp, player_support.cpp) ---
    Object& sonic();
    bool jump_pressed() const;
    void update_sonic();                   // 2F4F
    void animate_sonic();                  // 5EFF
    void sonic_start();                    // 2F72
    void to_standing();                    // 2FBD
    void to_walking();                     // 2FE3
    void to_look_up();                     // 300A
    void to_crouch();                      // 3015
    void to_running();                     // 3028
    void to_skid(bool left);               // 303A / 3052
    void to_rolling();                     // 306A
    void to_launched();                    // 3091
    void spring_up(unsigned vy);           // 30A7
    void bounce_up(unsigned vy);           // 30CC
    void launch_rolling(unsigned vx);      // 30ED / 310C
    void to_falling();                     // 312B
    void to_floating();                    // 314F
    void to_slide();                       // 316D
    void face_input();                     // 318D
    void halve_speed_under_water();        // 491C
    void sonic_physics();                  // 3A73
    void start_jump();                     // 3A9D
    void roll_off();                       // 3AE2
    void apply_input_force();              // 3BF5
    void pin_to_screen(unsigned screen_x); // 3CF1
    void move_horizontally();              // 3AF9
    void move_vertically();                // 3B66
    void add_to_position(u8& sub, std::uint16_t& whole, unsigned v);
    void touch_objects();           // 3773
    void hurt_sonic();              // 37B5
    void die();                     // 3828
    void bounce_off_object();       // 3858
    void blink_after_hit();         // 388C
    void check_balance();           // 38BE
    void sonic_standing();          // 31A2
    void sonic_idle();              // 3205
    void sonic_balancing();         // 31DB
    void sonic_look_up();           // 3228
    void sonic_crouch();            // 324A
    void sonic_walking();           // 3266
    void check_stop();              // 32AB
    void check_skid();              // 3304
    void sonic_running();           // 3320
    void sonic_skid(u8 skid_state); // 3345 / 3368
    void sonic_rolling();           // 338B
    void sonic_jumping();           // 33CD
    void sonic_launched();          // 339A
    void sonic_sprung();            // 3407
    void sonic_bounced();           // 3426
    void sonic_falling();           // 343E
    void sonic_hurt();              // 3470
    void sonic_floating();          // 344F
    void sonic_slide();             // 349A
    void sonic_dying();             // 34CB
    void sonic_pipe_exit();         // 3675
    void sonic_pushed();            // 3764
    void sonic_tube();              // 3598
    void sonic_act_end_run();       // 34F8
    void sonic_tube_frames();       // 39F9
    void sonic_rise_frames();       // 39AC
    void sonic_breathe();           // 3495
    void sonic_walk_animation();    // 38FF
    void sonic_roll_animation();    // 3934
    bool zone_triggers();           // 367C
    void enter_tube();              // 372B
    void update_air();              // 48DF
    void check_water();             // 4948
    void update_power_ups();        // 4820

    // --- Terrain (game/terrain.cpp) ---
    void probe_terrain(unsigned dx, unsigned dy);             // 733E
    u8 probe_type(const Object& o, unsigned dx, unsigned dy); // 73F2
    void probe_cell(const Object& o, unsigned dx, unsigned dy, bool heights);
    void collide_terrain();                 // 6AB6
    void combine_contacts();                // 67FC
    void collide_floor();                   // 6B00
    void no_floor();                        // 6C6E
    void leave_ground();                    // 6C99
    void roll_off_slope();                  // 6CB1
    void land_on_floor(u8 block);           // 6E46
    void floor_from_block_above(Object& o); // 6ED4
    void collide_walls();                   // 6FC6
    void collide_left_wall();               // 7036
    void push_from_right_wall();            // 6FFD
    void push_from_left_wall();             // 7059
    void break_wall(bool right);            // 708D / 70B3
    void break_block();                     // 70D9
    void replace_block(u8 block);           // 1A71
    void collide_ceiling();                 // 717C
    void push_from_ceiling(bool spikes);    // 71AA / 71ED
    void spike_wall();                      // 716E
    void conveyor();                        // 6BD1
    void bounce_back();                     // 6CF4
    void booster();                         // 6D7B
    void diagonal_launcher();               // 6B71
    void floor_cell_object();               // 6C16
    void collect_map_items();               // 723C
    void skim_water();                      // 730B
    void object_floor(Object& o);           // 7490
    void object_land(Object& o, u8 rule);   // 6F51

    // --- Object helpers (game/object_api.cpp) ---
    u8 data_byte(unsigned bank, unsigned address) const;
    unsigned data_word(unsigned bank, unsigned address) const;
    void move_to_sonic(Object& o);                     // 6127
    void destroy_if_attacked(Object& o);               // 613C
    void explode(Object& o);                           // 614C
    void fall(Object& o, unsigned dy, unsigned limit); // 61BE
    int land_object(Object& o, unsigned speed);        // 61DA
    void solid_object(Object& o);                      // 620F
    void bumper_object(Object& o);                     // 6215
    void bounce_sonic_off(Object& o);                  // 621B
    void hit_object_bumper(Object& o);                 // 6269
    void hit_object_solid(Object& o);                  // 6280
    void blink_or_reset(Object& o, bool count);
    void push_sonic_out(Object& o);                                                  // 62B1
    void clear_blink(Object& o);                                                     // 6371
    void velocity_from_angle(Object& o);                                             // 6394
    void object_wall(Object& o);                                                     // 6404
    void face_motion(Object& o);                                                     // 643B
    void face_x(Object& o, unsigned x);                                              // 6452 / 6456
    void move_toward_x(Object& o, unsigned target, unsigned offset, unsigned speed); // 6479
    void move_toward_y(Object& o, unsigned target, unsigned offset, unsigned speed); // 64AF
    void move_object(Object& o);                                                     // 66C3
    void touch_sonic(Object& o);                                                     // 6704

    // --- Common objects, bank 1F (game/objects_common.cpp) ---
    void sparkle_start(Object& o);
    void debris_start(Object& o);
    void debris_fall(Object& o);
    void lost_ring_start(Object& o);
    void lost_ring_move(Object& o);
    void lava_ball_start(Object& o);
    void lava_ball_jump(Object& o);
    void lava_ball_rise(Object& o);
    void lava_ball_drop(Object& o);
    void lava_ball_wait(Object& o);
    void lava_ball_fly(Object& o);
    void lava_ball_move(Object& o);
    void explosion_start(Object& o);
    void explosion_repeat(Object& o);
    void explosion_end(Object& o);
    void monitor_start(Object& o);
    void monitor_appear(Object& o);
    void monitor_update(Object& o);
    void request_tiles_on_screen(Object& o, u8 request);
    void emerald_start(Object& o);                                // B7D1
    Object& slot_object(u8 n);                                    // 0233 (6385)
    void air_count_start(Object& o);                              // AFEB
    void draw_text(Display::Cell at, Table text, unsigned count); // 13FA
    void zone_select_input();                                     // 0E29
    void zone_select_scroll();                                    // 0EB0
    Flow ending_scene();                                          // 06BD
    void credits_clear();                                         // B2F0
    void credits_tick();                                          // B2FC
    void credits_next_line();                                     // B323
    void credits_move();                                          // B39F
    void sonic_ending_move();                                     // A965
    void sonic_ending_run();                                      // A925
    void sonic_ending_arrive();                                   // A938
    void sonic_ending_stop();                                     // A951
    void sonic_face(bool left);                                   // A91B / A920
    void companion_start(Object& o);                              // 958F
    void companion_follow(Object& o);                             // 95E9
    void companion_close(Object& o);                              // 95D9
    void companion_signal();                                      // 950E
    void companion_drop(Object& o);                               // 9519
    void companion_fall(Object& o);                               // 9555
    void boss_palette(u8 kind);                                   // 915E / 9164
    void final_boss_electric(bool on);                            // 916A / 9170
    void break_block_at(Object& o, unsigned dx, unsigned dy);     // 0311 (64D8)
    void final_boss_start(Object& o);                             // 92AD
    void final_boss_wait(Object& o);                              // 92C3
    void final_boss_arena(Object& o);                             // 92DB
    void final_boss_guard(Object& o);                             // 937E
    void final_boss_choose(Object& o);                            // 930B
    void final_boss_shock(Object& o);                             // 935A
    void final_boss_blink(Object& o);                             // 936E
    void final_boss_lift(Object& o);                              // 91F7
    void final_boss_call_sonic();                                 // 9443
    void final_boss_hop(Object& o);                               // 93A4
    void final_boss_run(Object& o);                               // 93F6
    void final_boss_rest(Object& o);                              // 9435
    void final_boss_pause(Object& o);                             // 9288
    void countdown_next(Object& o);                               // 9462
    void final_boss_transform(Object& o);                         // 946E
    void fireballs_start(Object& o);                              // 8E8C
    void fireballs_move(Object& o);                               // 8E7A
    void fireballs_circle(Object& o);                             // 8EDE
    void fire_start(Object& o);                                   // 9043
    void fire_move(Object& o);                                    // 9049
    void sonic_follow_remains();                                  // A9F8
    void sonic_watch_camera();                                    // A9E8
    void sonic_watch_done();                                      // A9F2
    void spawn_debris();                                          // 710C
    void moving_pole_start(Object& o);                            // 8B7A
    void moving_pole_update(Object& o);                           // 8B83
    void pendulum_start(Object& o);                               // 8CC9
    u8 pendulum_speed(const Object& o, bool back) const;          // 8D95
    void pendulum_swing(Object& o);                               // 8CDB
    void drop_block2_watch(Object& o);                            // 8DF8
    void drop_block2_shake(Object& o);                            // 8E5D
    void drop_block2_fall(Object& o);                             // 8E66
    void drop_block2_break(Object& o);                            // 8E89
    void crawler_start(Object& o);                                // B6C5
    void crawler_move(Object& o);                                 // B6E5
    void sez_boss_start(Object& o);                               // A838
    void sez_boss_wait(Object& o);                                // A9E7
    void sez_boss_defeated(Object& o);                            // AC99
    void sez_boss_guard(Object& o);                               // AC91
    void sez_boss_choose(Object& o);                              // AA1F
    bool sez_boss_fall(Object& o, unsigned probe_dy);
    void sez_boss_jump(Object& o);                     // AA9D / AB21
    void sez_boss_hop(Object& o);                      // AAC9
    bool sez_boss_at_edge(const Object& o) const;      // ABDE
    void sez_boss_dash(Object& o);                     // AAF5
    void sez_boss_charge(Object& o);                   // AB4D
    void sez_boss_leap(Object& o);                     // AB64
    void sez_boss_charge2(Object& o);                  // ABA7
    void sez_boss_bounce(Object& o);                   // ABB3
    void sez_boss_gone(Object& o, bool zone_complete); // A9DD / A9E2
    void gmz_boss_start(Object& o);                    // 97E4
    void gmz_boss_wait(Object& o);                     // 97EE
    void gmz_boss_approach(Object& o);                 // 9816
    void gmz_boss_aim(Object& o);                      // 9917
    void gmz_boss_hurts(Object& o);                    // 999A
    void gmz_boss_turn(Object& o);                     // 996D
    void gmz_boss_rebound(Object& o);                  // 98C3
    void gmz_boss_roll(Object& o);                     // 984E
    void gmz_boss_jump(Object& o);                     // 9940
    void gmz_boss_stand(Object& o);                    // 98F8
    void gmz_boss_blink(Object& o);                    // 9983
    void gmz_boss_gone(Object& o);                     // 995F
    void boulder_start(Object& o);                     // 99BD
    void boulder_fall(Object& o);                      // 99F9
    void path_platform_start(Object& o);               // 835C
    void path_platform_wait(Object& o);                // 838D
    void path_platform_ride(Object& o);                // 8389
    void path_platform_ride_free(Object& o);           // 8374
    void shuttle_start(Object& o);                     // 83D9
    void shuttle_move(Object& o);                      // 83ED
    void shoe_image_start(Object& o);                  // AC7A
    void shoe_image_follow(Object& o);                 // ACA5
    void star_start(Object& o);                        // AE06
    void star_follow(Object& o);                       // AE19
    void debris2_start(Object& o);                     // ABF2
    bool patrol_turn(Object& o, unsigned range);       // BCB5
    void jumper_start(Object& o);                      // B768
    void jumper_watch(Object& o);                      // B773
    void jumper_jump(Object& o);                       // B7B3
    void jumper_land(Object& o);                       // B812
    void jumper_walk(Object& o);                       // B846
    void shaker_start(Object& o);                      // B941
    void shaker_patrol(Object& o);                     // B958
    void shaker_shake(Object& o);                      // B996
    void shaker_burst(Object& o);                      // B9BD
    void fireball_start(Object& o);                    // BB3E
    void fireball_fly(Object& o);                      // BB96
    void hopper2_start(Object& o);                     // B9F6
    void hopper2_hop(Object& o);                       // BA19
    void hopper2_air(Object& o, unsigned hop);
    void hopper2_turn(Object& o);                      // BA72
    void hopper2_move(Object& o);                      // BAA5
    void pipe_centre_x();                              // 46E4
    void pipe_centre_y();                              // 46F4
    void pipe_go(u8 direction);                        // 46A4-46D4
    bool pipe_x_before_middle() const;                 // 4686
    bool pipe_x_past_middle() const;                   // 468E
    bool pipe_y_in_window() const;                     // 4697
    void sonic_pipe();                                 // 43DC
    void pipe_piece(u8 piece);                         // 442A
    unsigned swing_offset(u8 angle) const;             // 4309
    void swing_place();                                // 4309
    void sonic_swing();                                // 4290
    void swing_pole_start(Object& o);                  // 8823
    void swing_pole_grab(Object& o);                   // 8828
    void boss_bumper(Object& o);                       // AC29
    void boss_blink_or_clear(Object& o);               // AC58
    void boss_solid_hit(Object& o);                    // AC6C
    void ghz_boss_start(Object& o);                    // 952F
    void ghz_boss_wait(Object& o);                     // 9539
    void ghz_boss_choose(Object& o);                   // 9560
    void ghz_boss_face_right(Object& o);               // 9407
    void ghz_boss_guard(Object& o);                    // 96B1
    void ghz_boss_stand(Object& o);                    // 9695
    bool ghz_boss_at_edge(const Object& o) const;      // 964A
    void ghz_boss_bounce_back(Object& o);              // 9603
    void ghz_boss_roll_on(Object& o);                  // 95CF
    void ghz_boss_roll(Object& o);                     // 95C9
    void ghz_boss_hop(Object& o);                      // 958A
    void ghz_boss_shake();                             // 973F
    void ghz_boss_jump_back(Object& o);                // 962F
    void ghz_boss_gone(Object& o);                     // 9521
    void alz_boss_start(Object& o);                    // A1EF
    void alz_boss_wait(Object& o);                     // A202
    void alz_boss_count(Object& o);                    // A1AD
    void alz_boss_choose_jump(Object& o);              // A1B1
    void alz_boss_guard(Object& o);                    // A229
    void alz_boss_jump(Object& o);                     // A23C
    void alz_boss_to_walk(Object& o);                  // A1BF
    void alz_boss_hit(Object& o);                      // A1D0
    void alz_boss_walk(Object& o);                     // A26A
    void alz_boss_rise(Object& o);                     // A2F0
    void alz_boss_carry(Object& o);                    // A313
    void alz_boss_gone(Object& o);                     // A1E1
    void alz_boss_leap(Object& o);                     // A177
    void alz_boss_turn(Object& o);                     // A18B
    void alz_boss_land(Object& o);                     // A169
    void alz_bomb_start(Object& o);                    // 9F11
    void alz_bomb_fly(Object& o);                      // 9F36
    void alz_bomb_bounce(Object& o);                   // 9FC6
    void trap_start(Object& o);                        // 86B3
    void trap_fire(Object& o);                         // 8748
    void trap_wait_above(Object& o);                   // 86BC
    void trap_wait_below(Object& o);                   // 86ED
    void trap_wait_left(Object& o);                    // 8702
    void trap_wait_right(Object& o);                   // 8733
    void trap_fly(Object& o);                          // 8750
    void fish_rest(Object& o);                         // AEDE
    void fish_watch(Object& o);                        // AEEC
    void fish_chase(Object& o);                        // AF41
    void fish_return(Object& o);                       // AF88
    void air_count_show(Object& o);                    // B012
    void air_count_drown(Object& o);                   // B040
    void bubble_start(Object& o);                      // B164
    void bubble_wobble(Object& o);                     // B316
    void bubble_check_surface(Object& o);              // B3DD
    void bubble_rise(Object& o);                       // B30F
    void bubble_cluster(Object& o);                    // B17C
    void bubble_spread();                              // B32F
    void bubble_wait(Object& o);                       // B2B2
    void bubble_reset(Object& o);                      // B2CA
    void bubble_from_sonic(Object& o);                 // B2E3
    void bubble_face(Object& o);                       // B29B
    void bubble_clear_cluster();                       // B2FA
    void bubble_breathe(Object& o);                    // B187
    void bubble_catch(Object& o);                      // B1A9
    void bubble_steer(Object& o);                      // B24E
    void bubble_carry(Object& o);                      // B1ED
    void splash_start(Object& o);                      // B43A
    void water_start(Object& o);                       // B485
    void water_rise(Object& o);                        // B4AC
    void water_surface_x(Object& o);                   // B55B
    void water_line();                                 // B4E0
    void water_off();                                  // B527
    void set_fade_palettes(u8 background, u8 sprites); // B516 / B530
    void sonic_in_bubble_update();                     // 347B
    void sonic_bubble_frame(u8 frame);                 // A84B / A852
    void sonic_drown();                                // 34D7
    void boss_add_part();                              // 9D6A
    void boss_remove_part(Object& o);                  // 9D81 / B69A
    bool boss_part_hit(Object& o);
    void shz_boss_start(Object& o);             // 9D98
    void shz_boss_wait(Object& o);              // 9DB9
    void shz_boss_wait_parts(Object& o);        // 9DD8
    void shz_boss_open_floor(Object& o);        // 9DF0
    void shz_boss_place(Object& o);             // 9E03
    void shz_boss_hover(Object& o);             // 9E32
    void shz_boss_fly(Object& o);               // 9E16
    void shz_boss_blink(Object& o);             // 9E59
    void shz_boss_gone(Object& o);              // 9D62
    void ball_frame(Object& o, u8 right_frame); // 9A5E-9A6C
    void ball_rise(Object& o);                  // 9A7F
    void ball_bounce(Object& o);                // 9A95
    void launcher_start(Object& o);             // 9B1B
    void launcher_wait(Object& o);              // 9B2F
    void launcher_drop(Object& o);              // 9B47
    void shz_flier(Object& o);                  // 9EA6
    void minion_check_hit(Object& o);           // B684
    void minion_start(Object& o);               // B4EB
    void minion_rise(Object& o);                // B4F0
    void minion_land(Object& o);                // B510
    void minion_wait(Object& o);                // B558 / B5EF
    void minion_hop(Object& o);                 // B568
    void minion_hop_air(Object& o);             // B59F
    void minion_walk(Object& o);                // B5FF
    void minion_jump(Object& o);                // B61B
    void sonic_drop_straight();                 // AA56
    void sonic_drop_through(unsigned bottom);   // AA26 / AA3E
    void drop_block_wait(Object& o);            // 8622
    void platform_carry(Object& o);             // 814F
    void platform_start(Object& o);             // 8067
    void platform_frame(Object& o, u8 ticks);   // 81EE / 81E7
    void platform_move(Object& o);              // 8080
    void platform_wait(Object& o);              // 80F6
    void platform_shake(Object& o);             // 8104
    void platform_fall(Object& o);              // 8108
    void platform_swing(Object& o);             // 8123
    void falling_block_start(Object& o);        // 8EC4
    void drop_block_fall(Object& o);            // 8633
    void hopper_start(Object& o);               // AD2F
    void hopper_touch(Object& o);
    void hopper_watch(Object& o);                             // AD45
    void hopper_move(Object& o);                              // AD72 / ADC2
    void hopper_charge(Object& o);                            // AD86
    void hopper_run(Object& o);                               // ADD6
    void hopper_approach(Object& o);                          // AE09
    void hopper_drop(Object& o);                              // AE3A
    void hopper_walk(Object& o);                              // AE84
    void glider_stand(Object& o);                             // 403C
    void rain_fall();                                         // 68B9
    void rain_move_drop(int i);                               // 6942
    void rain_trigger();                                      // 6A25
    void glider_run(Object& o);                               // 404F
    void glider_take_off(Object& o);                          // 405C
    void sonic_glider_stand();                                // 40A6
    void sonic_glider_run();                                  // 40CF
    void glider_check_end();                                  // 4107
    bool glider_off_screen() const;                           // 4134
    void sonic_glider_fly();                                  // 40F8
    void glider_fly_control();                                // 4168
    void sonic_glider_climb();                                // 41D5
    void sonic_glider_dive();                                 // 421E
    void glider_slow_down();                                  // 426C
    void glider_start(Object& o);                             // 8798
    void glider_wait(Object& o);                              // 879D
    void glider_carried(Object& o);                           // 87BD
    void glider_drift(Object& o);                             // 87F9
    void capsule_touch(Object& o);                            // 9174
    void capsule_wait(Object& o);                             // 903B
    void capsule_descend(Object& o);                          // 8FFB
    void capsule_check_hit(Object& o);                        // 911C
    void capsule_float(Object& o, bool down, bool right);     // 912C-9161
    void capsule_hover(Object& o, bool down, bool right);     // 90DC-910C
    void capsule_open(Object& o);                             // 90A4
    void capsule_free_animal(Object& o);                      // 918E
    void animal_start(Object& o);                             // 9273
    void animal_frame(Object& o, bool second);                // 9278 / 9286
    void animal_fall(Object& o);                              // 9295
    void animal_hop(Object& o);                               // 92C0
    void hud_slide_start(Object& o);                          // AE45
    void hud_slide_out(Object& o);                            // AE4E
    void hud_slide_back(Object& o);                           // AE7C
    void ugz_boss_wait(Object& o);                            // A47B
    void ugz_boss_enter(Object& o);                           // A513
    void ugz_boss_approach(Object& o);                        // A52E
    void ugz_boss_swoop(Object& o);                           // A55F
    void ugz_boss_grab(Object& o);                            // A585
    void ugz_boss_carry(Object& o, unsigned dx, unsigned dy); // A72B
    void ugz_boss_lift(Object& o);                            // A5B5
    void ugz_boss_carry_right(Object& o);                     // A5FD
    void ugz_boss_set_down(Object& o);                        // A639
    void ugz_boss_to_post(Object& o);                         // A67A
    void ugz_boss_fall(Object& o);                            // A6AA
    void ugz_boss_fly_off(Object& o);                         // A6F1
    void ugz_bomb_throw(Object& o);                           // A37C
    void ugz_bomb_fly(Object& o);                             // A3A9
    void ugz_target_start(Object& o);                         // A7B7
    void ugz_target_frame(Object& o, u8 frame);               // A7C5 / A7CC
    void ugz_target_hit(Object& o);                           // A7D9
    void ugz_target_gone(Object& o);                          // A7EB
    void emerald_wait(Object& o);                             // B80C
    void emerald_taken(Object& o);                            // B854
    void sign_start(Object& o);
    void sign_wait(Object& o);
    void sign_lock_camera();
    void sign_ready(Object& o);
    void sign_spin(Object& o);
    void sign_face_short(Object& o);
    void sign_face_long(Object& o);
    void sign_finish(Object& o);
    void rock_throw(Object& o);
    void rock_start(Object& o);
    void rock_wait(Object& o);
    void rock_shake(Object& o);
    void rock_fall(Object& o);
    void rock_break(Object& o);
    void rock_fly(Object& o);
    void intro_cloud_start(Object& o);
    void intro_cloud_move(Object& o);
    void intro_star_start(Object& o);
    void intro_star_move(Object& o);
    void intro_hill_start(Object& o);
    void intro_hill_move(Object& o);
    void wrap_intro_scenery(Object& o);

    // --- Under Ground Zone (game/underground.cpp) ---
    u8 enemy_must_turn(Object& o);                      // bank 1C, BD38
    bool sonic_near_x(const Object& o, unsigned range); // BC2D
    bool sonic_near_y(const Object& o, unsigned range); // BC71
    void bomb_walker_start(Object& o);
    void bomb_walker_right(Object& o);
    void bomb_walker_left(Object& o);
    void bomb_walker_walk_right(Object& o);
    void bomb_walker_walk_left(Object& o);
    void bomb_walker_throw(Object& o);
    void bomb_start(Object& o);
    void bomb_fly(Object& o);
    void cart_start(Object& o);
    bool cart_board(Object& o);
    void cart_parked(Object& o);
    void cart_go(Object& o);
    void cart_roll(Object& o, bool left);
    void cart_fall(Object& o);
    void cart_crash(Object& o);
    void cart_broken(Object& o);
    void cart_gone(Object& o);
    void cart_move(Object& o);
    void throw_off_sonic();         // 37EF
    void sonic_board_cart();        // 4717
    void sonic_ride();              // 4712
    void sonic_riding();            // 4741
    void follow_cart(Object& cart); // 477E
    void sonic_cart_frames();       // 47BB
    void pacer_start(Object& o);
    void pacer_walk(Object& o, bool left);
    void pacer_turn(Object& o, bool to_left);
    void pacer_creep(Object& o, bool left);
    void pacer_charge(Object& o, bool left);
    void turret_start(Object& o);
    void turret_watch(Object& o);
    void turret_fire(Object& o);
    void turret_remove(Object& o);
    void shot_start(Object& o);
    void shot_fly(Object& o);

    // The Z80 refresh register (LD A,R) as a source of chance. Its value
    // depends on exact instruction timing, which a native program does not
    // have; a pseudo-random sequence stands in for it.
    u8 refresh_register();
    std::uint32_t random_state_{0x12345678};

    // --- Not yet reconstructed (game/pending.cpp) ---
    void sound_init();           // bank 2, 441E
    void sound_tick();           // bank 2, 4000
    void update_rain();          // 681A
    void water_line_interrupt(); // 05E8
    void water_line_after_frame_interrupt();
    Flow ending_state(); // 0686
    Flow game_over();    // 0703
    Flow show_tally();   // 0817
    Flow count_tally();  // 1C6C
    Flow fast_tally_step();
    void record_emerald();           // 084C
    void add_bonus(const u8* value); // 20D5
    void add_time_bonus();           // 1F4A
    Flow continue_screen();          // 0766
    Flow continue_countdown();       // 07AF
    void show_game_over();           // 284B
    void show_continue_screen();     // 2885
    void fade_in_screen();           // 2870
    void show_continue_count();      // 28C2
    void sonic_continue_wait();      // AA94
    void sonic_continue_frames();    // AA7E
    void sonic_continue_leave();     // AAB8
    Flow act_complete();             // 07F4
    Flow zone_complete();            // 086F
    Flow lose_life();                // 08FE
    Flow main_loop();                // 065C
    Flow zone_select();              // 0A61
};

} // namespace sonic2_gg
