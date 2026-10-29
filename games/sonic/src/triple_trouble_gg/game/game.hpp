#pragma once
#include "triple_trouble_gg/audio/sound_driver.hpp"
#include "triple_trouble_gg/data/assets.hpp"
#include "triple_trouble_gg/game/display.hpp"
#include "gg_engine/enhancements/widescreen.hpp"
#include "gg_engine/flow.hpp"
#include "triple_trouble_gg/game/scripts.hpp"
#include "triple_trouble_gg/game/state.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

// Sonic the Hedgehog - Triple Trouble (Game Gear): the game.
//
// Each video frame has two parts, as in the cartridge: the frame's services
// (frame_interrupt: the picture's updates, the controller, palette fades,
// the clock, the sound driver), then the game's program until it waits for
// the next frame. The program is a set of modes (the SEGA logo, the intro,
// the title, an act, ...), each a long-running routine that waits for frames
// with co_await (gg_engine/flow.hpp) and finally names the next mode.
//
// Where things are:
//   state.hpp          the game's state in groups: flow, camera, score,
//                      palettes, sprites, the player's art, ...
//   object.hpp,        the 24 object slots (slot 0 is the player), their
//   scripts.hpp,       scripts (decoded per type and state) and the
//   routines.hpp       routines the scripts run (dispatch.cpp)
//   display.hpp        what is shown (gg_engine/video): tile art, the background map, sprites,
//                      colours; picture() renders it
//   audio/             the sound driver
//
// Hexadecimal numbers in comments are Z80 addresses of the original routines
// (bank 0 at 0000-3FFF, bank 1 at 4000-7FFF, banked code at 8000-BFFF with its
// bank named) and work-RAM addresses (C000-DFFF) of the state they describe:
// they link the re-implementation to the code it reproduces.
namespace triple_trouble_gg {

using gg_engine::Flow;
using gg_engine::FlowRunner;
namespace save {
struct Writer;
struct Reader;
} // namespace save

// Controller buttons (as the cartridge keeps them in D137).
namespace button {
inline constexpr std::uint8_t up = 0x01, down = 0x02, left = 0x04, right = 0x08;
inline constexpr std::uint8_t one = 0x10, two = 0x20, start = 0x80;
inline constexpr std::uint8_t jump = one | two;
} // namespace button

// A part of the original that the reconstruction has not recovered yet was
// reached. The game stops there instead of guessing.
struct Unrecovered : std::runtime_error {
    explicit Unrecovered(const std::string& what)
        : std::runtime_error("Not reconstructed yet: " + what) {}
};

class Game {
public:
    explicit Game(const Assets& assets);

    // The app's level select: power-on, then a new game as `character`
    // (1 Sonic, 2 Tails) from the act's title card. Shell option (not in
    // the original): `direct` goes straight to play, leaving the title
    // card out, as Sonic 1's and Sonic 2's level starts do.
    //
    // Or, by `kind`: special stage `number` (0-4) with that many emeralds
    // won, as an item box enters it from Great Turquoise act 1 (where it
    // returns to); or the ending (mode 14, as after Atomic Destroyer act 3's
    // tally) with all six emeralds (`number` 6: the credits) or one.
    struct LevelStart {
        u8 zone, act, character;
        bool direct = false;
        enum class Kind : u8 { act, special_stage, ending } kind = Kind::act;
        u8 number = 0;
    };

    Game(const Assets& assets, LevelStart start);

    // One video frame. `buttons` uses the button:: bits.
    void frame(std::uint8_t buttons);

    // Calls f on every part of the state that a quick save keeps.
    template <class F>
    void for_each_state(F&& f) {
        f(video);
        f(input);
        f(flow);
        f(score);
        f(clock);
        f(camera);
        f(palette);
        f(sprites);
        f(player_art);
        f(animation);
        f(player);
        f(act);
        f(level);
        f(level_map);
        f(camera_targets);
        f(probe);
        f(object_art);
        f(placements);
        f(map_animations);
        f(terrain_state);
        f(player_box);
        f(objects);
        f(sound_.state);
    }

    // The time model and the program's position, for quick saves.
    template <class F>
    void for_each_timing(F&& f) {
        f(clock_);
        f(di_);
        f(irq_pending_);
        f(stall_frames_);
        f(random_state_);
        f(buttons_);
        f(act_loop_water_);
    }

    // A quick save can be taken: an act is played and the program waits at
    // the top of its loop (1F2E), with no work pending.
    bool can_save() const {
        return at_act_loop_wait_ && flow.mode == 0x05 && !(flow.demo & 0x80) &&
               stall_frames_ == 0 && !irq_pending_;
    }

    // After a quick save's state is restored: the program goes on in the
    // act's loop as from that wait.
    void resume_play();
    // A quick save of the game (save.cpp); load ends with resume_play().
    void save(save::Writer& w);
    void load(save::Reader& r);

    // True while the program waits for the next frame.
    bool at_frame_wait() const { return in_frame_wait_; }

    // The time model's state (research tools).
    unsigned debug_clock() const { return clock_; }

    int debug_stall() const { return stall_frames_; }

    bool debug_di() const { return di_; }

    // An act or a special stage is being played (not a demo).
    bool in_play() const {
        return (flow.mode == 0x05 || flow.mode == 0x08 || flow.mode == 0x09) && !(flow.demo & 0x80);
    }

    // Waits for frames completed (development aid for comparisons).
    std::uint64_t frame_waits{};
    // Called as the program starts waiting for a frame (084C); used by the
    // test harness to take snapshots at the same point as the original.
    std::function<void()> wait_hook;
    // The buttons for a frame that starts inside a routine that cannot
    // suspend (wait_frame_now); without it the current buttons hold.
    std::function<std::uint8_t()> buttons_hook;
    // Replaces LD A,R (refresh_register) when set: the test harness replays
    // the values the original read.
    std::function<std::uint8_t()> random_source;

    // CPU speed relative to the Game Gear's for the time model (the
    // research harness compares with the original running 4 times faster).
    unsigned cpu_speed{1};

    // Requests from this frame for the audio side.
    bool voice_requested{}; // the logo's sampled voice (bank 1A, 8000) started

    // The game's state (state.hpp).
    VideoState video;
    InputState input;
    FlowState flow;
    ScoreState score;
    ClockState clock;
    CameraState camera;
    PaletteState palette;
    SpritesState sprites;
    PlayerArtState player_art;
    AnimationState animation;
    PlayerState player;
    ActState act;
    LevelState level;
    LevelMap level_map;
    CameraTargets camera_targets;
    ProbeState probe;
    ObjectArtState object_art;
    PlacementState placements;
    MapAnimationState map_animations;
    TerrainState terrain_state;
    PlayerBox player_box;
    std::array<Object, 24> objects{}; // D500: slot 0 is the player
    Display display;                  // What is shown (gg_engine/video/display.hpp)
    PictureOrigin picture_origin;     // Where the act's map shows (display.hpp)
    const Assets& rom;

    // The picture of this frame (160x144).
    gg::Screen picture() const {
        return display.render(display.level_shown ? level_background()
                                                  : Display::LevelBackground{});
    }

    // The background entry of the act's map at a world position.
    std::uint16_t level_entry(int x, int y) const;
    // The world position of the 256x192 picture's top left corner, from
    // the scroll the frame is shown with (shake included).
    int picture_x() const;
    int picture_y() const;
    // The act's background for the display (camera.cpp).
    Display::LevelBackground level_background() const;

    // =========================================================================
    // Widescreen enhancement (enhancements/widescreen.cpp)
    // =========================================================================
    //
    // The host reports the presented view size before each frame. The default
    // 160x144 is the Game Gear LCD and leaves every widescreen rule inert, so
    // the original format and the attract demos keep the original behaviour.
    int view_width{160}, view_height{144};
    // Pixels the presented view extends beyond the LCD crop (picture columns
    // 48-207, lines 24-167) on each side, set once the frame interrupt has
    // set the scroll. Camera-relative tests add them so that objects keep
    // running, and stay drawn, until they leave the presented view.
    int wide_left{}, wide_right{}, wide_top{}, wide_bottom{};

    bool widescreen() const { return wide_left || wide_right || wide_top || wide_bottom; }

    // True while an act is played (also while the player dies or the act
    // ends) with its background drawn from the act's map: the states in
    // which a widescreen view applies. Attract demos replay original
    // controller bytes, so they keep the LCD rules.
    bool wide_play() const;

    // B2A7/B42C keep the player between screen X 38 and C7, inside the LCD.
    // Where the camera can still scroll toward a side, the limit on that
    // side is moved to the edge of the presented view, so that he is not
    // stopped in plain sight when he outruns the camera. Where it cannot (at
    // a limit, locked behind him, stopped or held by a script in boss arenas
    // and at the act's end) the LCD's edge remains a wall, shown by the
    // arena force field.
    struct ScreenLimits {
        int left, right;
    };

    ScreenLimits wide_screen_limits() const;

    // The world rectangle the camera (and so the LCD) can ever reach in this
    // act: the act header's limits, widened by any later limits.
    using Area = gg_engine::CameraArea;

    Area camera_area() const;

    // The sound driver's state (research harness).
    SoundState& sound_state() { return sound_.state; }

    // The PSG writes of this frame's sound driver run, in order.
    std::vector<SoundWrite> take_sound_writes() { return sound_.take_writes(); }

private:
    // Widescreen (enhancements/widescreen.cpp): the per-frame margins.
    void update_wide_view();
    // Whether an object's position lies where the presented view shows it
    // (camera-relative: X -wide_left..255+wide_right, Y 16-wide_top..
    // 239+wide_bottom, the drawn part of 5456's window grown by the
    // margins), or within `slack` pixels of it. Always false on the LCD.
    bool in_wide_view(unsigned x, unsigned y, int slack = 0) const;

    SoundDriver sound_{rom};
    Scripts scripts_{rom};
    FlowRunner program_;
    std::uint8_t buttons_{};
    bool in_frame_wait_{};
    bool at_act_loop_wait_{}; // waiting at the top of the act's loop (quick saves)
    bool act_loop_water_{}; // that pass builds the sprites itself (the water line or band variant)
    bool powered_on_{};

    // --- Time spent by the original's code (game.cpp) ---
    // The reconstruction does its work instantly; where the original works
    // for longer than a frame, the routine reports the cost (busy) and the
    // program continues only in the frame where the work ends.
    static constexpr unsigned frame_clocks = 228 * 262;
    unsigned clock_{};   // CPU clocks since the last frame interrupt (modelled work)
    bool di_{};          // interrupts disabled (modelled)
    bool irq_pending_{}; // a frame interrupt waits for the next EI
    int stall_frames_{}; // frames that pass before the program goes on
    // About how long the frame interrupt takes when it runs in the middle of
    // the program's work (a pending one at EI); measured on the logo.
    static constexpr unsigned interrupt_clocks = 9700;

    void di() { di_ = true; }

    void ei();
    bool take_pending_interrupt();

    // `co_await next_frame()`: the frame wait 084C: EI, D133 = 0, then wait
    // until the frame interrupt has counted a frame in D132 (and clear it).
    // An interrupt held pending by DI runs at the EI, still seeing D133 as
    // it was; the wait then ends at once. (wait_hook runs at 0851, after the
    // EI and D133 = 0: the point where the harness compares.)
    auto next_frame() {
        struct Awaiter {
            Game& game;

            bool await_ready() {
                game.take_pending_interrupt();
                game.video.busy = 0;
                if (game.wait_hook) {
                    game.wait_hook();
                }
                return game.video.frame_done != 0;
            }

            void await_suspend(std::coroutine_handle<> h) {
                game.in_frame_wait_ = true;
                game.program_.suspend_at(h);
            }

            void await_resume() noexcept {
                game.in_frame_wait_ = false;
                game.video.frame_done = 0;
                ++game.frame_waits;
            }
        };

        return Awaiter{*this};
    }

    // `co_await busy(clocks)`: the program works for `clocks` CPU clocks
    // without waiting for a frame. The frames that pass meanwhile have their
    // interrupt (or, with interrupts off, one held pending until the next
    // EI); the program goes on in the frame where the work ends.
    auto busy(unsigned clocks) {
        struct Awaiter {
            Game& game;

            bool await_ready() { return game.stall_frames_ == 0; }

            void await_suspend(std::coroutine_handle<> h) { game.program_.suspend_at(h); }

            void await_resume() const noexcept {}
        };

        clock_ += clocks / cpu_speed;
        while (clock_ >= frame_clocks) {
            clock_ -= frame_clocks;
            ++stall_frames_;
        }
        return Awaiter{*this};
    }

    // 084C from code that cannot suspend (deep in the player's physics):
    // the wait, the frame's end and the next frame's interrupt happen here,
    // so the game's time runs as in the original; the host sees one frame
    // fewer (the current frame() call covers two).
    void wait_frame_now();

    // --- The program (frontend/program.cpp) ---
    Flow boot();     // 0000, 053A
    void power_on(); // 053A's set-up
    Flow boot_to_act(LevelStart start);
    Flow run_modes();                           // 0DFE
    Flow wait_frames(unsigned count);           // 21BA, 252C
    Flow wait_frames_or_button(unsigned count); // 21C5
    Flow sega_logo();                           // 0E53 (mode 0)
    Flow intro();                               // 0F3E (mode 1)
    Flow play_act_music();                      // 21DC
    bool request_act_music();                   // 21DC without its frame's wait
    Flow title();                               // 0FED (mode 2)
    Flow title_screen();                        // 1043 (the second part of mode 2)
    Flow demo_mode();                           // 1167 (mode 3)
    Flow character_select();                    // 1AD0 (mode 18)
    Flow load_select_screen();                  // 77D0
    Flow after_select();                        // 1A27 (mode 16)
    Flow title_sound_test();                    // 1BFE (mode 19)
    bool run_sound_test_routine(Object& o, Routine r);
    bool sound_test_choose(unsigned choice, u8 first, u8 last); // 0C:8C03, 8C4C
    void sound_test_digits(unsigned choice, unsigned digits);   // 1C:B786, B7AF
    Flow sound_test();                          // 10:BCCC
    void sound_test_input();                    // 10:BD22
    Flow title_card();                          // 11DD (mode 4)
    Flow load_title_card();                     // 7827
    Flow play_mode();                           // 1256 (mode 5)
    Flow finish_play();                         // 1256 after the act's loop
    Flow resumed_play();                   // a quick save's: the act's loop, its end, the modes
    Flow life_lost();                      // 13C8 (mode 7)
    Flow game_over();                      // 156B
    Flow continue_screen(bool& continued); // 141F
    Flow special_stage();                  // 15A1 (modes 8, 9)
    Flow special_results();                // 1633 (mode 0A)
    // --- The flight stage, mode 0D (frontend/flight.cpp) ---
    Flow flight_stage();                                       // 1672
    void flight_setup();                                       // 1A:BA58
    void flight_palettes();                                    // 76A7
    Flow flight_loop();                                        // 1DED
    Flow flight_frame();                                       // 1EA9
    Flow update_alternate_objects();                           // 4F9C
    void flight_steer();                                       // 03:BDE2
    void draw_plane();                                         // 40D6
    void run_course();                                         // 1C:97AC
    Flow emerald_scene();                                      // 1EB2
    void flight_frame_interrupt();                             // 078D
    void horizon_interrupts();                                 // 095A
    bool run_flight_routine(Object& o, Routine r);             // game/flight_objects.cpp
    Flow flight_title_end(Object& o);                          // 17:81C6
    void flight_letter_gather(Object& o);                      // 17:8212
    void flight_project(Object& o);                            // 552C
    void flight_touch(Object& o);                              // 55DB
    void guardian_hop_start(Object& o, std::uint16_t landing); // 17:B90A
    void guardian_hop(Object& o);                              // 17:B966
    void rider_run_left_start(Object& o);                      // 1C:B551
    // --- The special stages' guardians (game/special_guardians.cpp) ---
    bool run_special_guardian_routine(Object& o, Routine r);
    Flow glider_meet(Object& o);      // 17:B2EF
    Flow knuckles_appear(Object& o);  // 17:B5DD
    Flow guardian_fly_off(Object& o); // 17:B482
    Flow knuckles_gone(Object& o);    // 17:B773
    void glider_level_off(Object& o); // 17:B3E1
    void glider_fly(Object& o);       // 17:B40D
    void knuckles_touch(Object& o);   // 17:B709
    void knuckles_chase(Object& o);   // 17:B625
    Flow reload_sprite_art();         // 79E7 (048F)
    Flow sign_fall_away(Object& o);   // 1E:9BA1
    // --- The full ending's objects (game/credits.cpp) ---
    bool run_credits_routine(Object& o, Routine r);
    void sky_wait(Object& o, std::uint16_t from, std::uint16_t script); // 1C:BC41-BC6F
    void step_colours(const std::uint16_t* targets);                    // 1C:B9BB, BACC
    void step_horizon(const std::uint16_t* targets);                    // 1C:BA68, BB79, BBDD
    void text_place(Object& o, u8 y);                                   // 1C:BD29
    void text_line(Object& o);                                          // 1C:BD3A
    void text_score();                                                  // 1C:BDB7
    Flow load_special_results();                                        // 7914
    void draw_reward_emblems();                                         // 78C2
    void reset_player_for_continue();                                   // 1A:BA18
    Flow act_cleared();                                                 // 1352 (mode 6)
    Flow ending();                                                      // 17F4 (modes 12-14)
    Flow load_ending_scene();                                           // 7B36
    Flow full_ending();                                                 // 1824
    Flow load_full_ending_art();                                        // 7BB5
    void start_credits();                                               // 0F:AFC7
    void credits_frame();                                               // 18C3
    void credits_sway();                                                // 190F
    std::uint16_t credits_frames() const;                               // DC24
    void stop_level_animations();                                       // 25B6
    Flow load_tally_screen();                                           // 7871
    Flow tally();                                                       // 240B
    Flow tally_bonuses();                                               // 19:BD4A
    void tally_tick();                                                  // 19:BEC0
    unsigned show_points();                                             // 19:BECE
    unsigned show_ring_bonus();                                         // 19:BEDD
    unsigned show_time_bonus();                                         // 19:BEF2
    unsigned draw_number(unsigned name_address, const u8* low, unsigned count); // 19:BF13
    Flow play_demo();                                                           // 24A1
    Flow start_act();                                                           // 1327
    Flow act_loop(bool resumed = false);                                        // 1F2E
    Flow check_pause();                                                         // 20E2
    Flow update_music_timer();                                                  // 20C7
    Flow act_events();                                                          // 1FC6
    Flow update_partner();                                                      // 34B9
    void spawn_placements();                                                    // bank 1C, 8000
    Flow load_title_screen();                                                   // 7727
    void title_select(unsigned& left);                                          // 1110
    Flow play_voice();                                                          // bank 1A, 8000
    void reset_game();                                                          // 32A8
    void clear_rings_and_time();                                                // 32C3
    void clear_continue_state();                                                // 32F4
    void stop_music();                                                          // 2288

    // --- The frame interrupt (game/interrupt.cpp) ---
    void frame_interrupt();              // 0038 -> 0576
    void read_controller();              // 2D43
    void update_clock();                 // 2F5F
    void arm_water_line();               // 05A7
    void water_line_interrupt();         // 085B
    void band_interrupts();              // 0930
    u8 band_scroll(unsigned band) const; // DBE7
    void show_clock();                   // 3035

    // --- The screen (game/video.cpp) ---
    void display_on();             // 26D6
    void display_off();            // 26E4
    void enable_frame_interrupt(); // 26F2
    void init_vdp_registers();     // 2641
    void clear_vram();             // 7BCF
    void clear_cram();             // 7C08
    Flow clear_screen();           // 273B
    void clear_sprites();          // 2757
    void clear_game_state();       // 327E
    void clear_map_state();        // 428B
    void reset_camera();           // 3310
    // 2DC7: a compressed block of tile art from bank:address, decoded into
    // the tiles from the byte position `vram` on. Returns the CPU clocks the
    // original takes (with the CALL).
    unsigned load_tiles(unsigned vram, unsigned bank, unsigned address, bool mirrored = false);
    // 2C3B: a rectangle of map entries from bank:address, `rows` rows of
    // `columns` entries, from the name table address `name_address` (moved
    // by the camera's position, 2CB9). Returns the CPU clocks (with the CALL,
    // up to its closing EI, which the caller does).
    unsigned draw_rect(unsigned name_address, unsigned bank, unsigned address, int rows,
                       int columns);
    unsigned draw_rect_at(unsigned name_address, unsigned bank, unsigned address, int rows,
                          int columns); // 2C3E
    void sunset_park_3_service();       // 1C72
    unsigned draw_byte_rect(unsigned name_address, unsigned bank, unsigned address, int rows,
                            int columns, u8 high); // 2C75

    // --- Acts (game/level.cpp) ---
    Flow load_act(); // 3167
    Table act_entry(Table table) const;
    void load_act_header();                 // 4299
    Flow load_map();                        // 40A5
    unsigned decompress_map();              // 459F; returns the CPU clocks
    void load_start_position();             // 41C0
    Flow draw_first_screen();               // 4163
    Flow load_tile_list_slowly(Table list); // 7A58
    Flow destroyer_drop();                  // 1FDB
    void load_act_properties();             // bank 1D, 872A
    unsigned load_act_tiles();              // 7524; returns the CPU clocks
    unsigned load_tile_entry(Table entry);  // 7548
    Flow finish_act_load();                 // 3258
    void set_special_hud();                 // 1B:BE43
    void fade_in_act();                     // 7650
    unsigned load_play_art();               // 31BA-3220
    void set_hud_layout();                  // 1B:BD12
    void show_rings();                      // 33BA
    void show_lives();                      // 3370
    void place_partner();                   // 1B:BEF5
    void load_water_level();                // 1B:BE6C

    // --- The camera (game/camera.cpp) ---
    void update_map();                 // 3F81
    void enable_map();                 // 4CC3
    void disable_map();                // 4CCE
    void follow_camera();              // 4B1D
    void follow_script();              // 4C54
    void ramp_camera_step();           // 4CA3
    void update_camera_window();       // 4BD9
    void reset_camera_window();        // 4C2D
    void scroll_horizontally();        // 3FA6
    void scroll_vertically();          // 3FED
    void set_scroll_registers();       // 4034
    unsigned camera_cell(unsigned dy); // 4053
    void fetch_map_column();           // 45E5
    void fetch_map_row();              // 46AA
    void apply_camera();               // 068C (frame interrupt)
    void freeze_level_background();
    void ramp_camera();                                   // 4D13
    void hold_camera_left();                              // 4D00 (031B)
    void script_camera(std::uint16_t x, std::uint16_t y); // 4CDA (0321)
    void release_camera();                                // 4CF2 (0324)
    u8 level_block(unsigned cell) const;                  // the map as 45E5/46AA read it
    unsigned camera_left_x() const;
    void stream_column(); // 4644
    void stream_row();    // 46F0

    // --- Terrain (game/terrain.cpp) ---
    void probe_terrain(const Object& o, unsigned dx, unsigned dy); // 6FA6
    u8 probe_flags(const Object& o, unsigned dx, unsigned dy);     // 709A
    void probe_outside();                                          // 7122
    void object_terrain(Object& o);                                // 713D
    void land_on_floor(Object& o);                                 // 670D
    void landed(Object& o);                                        // 65F3
    void check_block_above(Object& o);                             // 6671

    // --- Time costs of loading routines (game/timing.cpp) ---
    unsigned decode_clocks(unsigned bank, unsigned src, bool mirrored) const;

    struct DecodeClocks {
        unsigned head{}, tail{};
        std::vector<unsigned> tiles;
        // After each tile: where the decoder's code and data reads stand.
        std::vector<std::uint16_t> codes_after, data_after;
    };

    DecodeClocks decode_clock_parts(unsigned bank, unsigned src, bool mirrored) const;
    Flow load_tiles_slowly(unsigned vram, unsigned bank, unsigned address,
                           bool mirrored = false); // 2DC7 with D12C set
    void clear_decoder_buffer();                   // 2DD7
    void show_decoder_work(const DecodeClocks& parts, const Bytes& plain, unsigned n,
                           bool mirrored); // DBE6-DC31
    static unsigned fill_clocks(unsigned words);
    static unsigned sprite_clear_clocks();

    // --- Colours (game/palettes.cpp) ---
    void upload_colors();                                           // 2582
    void run_fades();                                               // bank 0E, B4D3
    void fade_step(PaletteState::Fade& fade, std::span<u8> colors); // bank 0E, B539
    void fill_white();                                              // 340C
    void set_fades(u8 period, u8 bg_mode, u8 bg_palette, u8 sprite_mode, u8 sprite_palette);

    // --- Sprites (game/sprites.cpp) ---
    void build_sprites();                                                   // 2A14
    void build_player_sprites();                                            // 2A88
    void draw_object(Object& o, unsigned& y_entry, unsigned& xt_entry);     // 2BF5
    void toggle_flicker(Object& o);                                         // 2BBA
    void upload_sprites();                                                  // 27A0
    void upload_player_art();                                               // 0A0C
    void track_player_art();                                                // 0B01
    void track_partner_art();                                               // 0B2E
    void animate_tiles();                                                   // bank 1D, 8770
    void spin_hud_ring();                                                   // bank 1D, 87D5
    void run_animators();                                                   // bank 1D, 8000
    void animate_title_text(AnimationState::Animator& a);                   // bank 1D, 8425
    void pulse_color(AnimationState::Animator& a);                          // bank 1D, 8155
    void boss_flash(AnimationState::Animator& a);                           // bank 1D, 8470
    void open_ground(AnimationState::Animator& a);                          // bank 1D, 82B5
    void freeze_over(AnimationState::Animator& a);                          // bank 1D, 8625
    void breathe_color(AnimationState::Animator& a);                        // bank 1D, 84D3
    void lightning(AnimationState::Animator& a);                            // bank 1D, 8515
    void flash_once(AnimationState::Animator& a);                           // bank 1D, 85F3
    static std::uint16_t step_color(std::uint16_t c, std::uint16_t target); // 25DC
    void start_animator(u8 type);                                           // 259E
    void stream_object_art();                                               // 7C15
    void next_art_block();                                                  // 7CC0
    void finish_object_art();                                               // 7CE5

    // --- Objects (game/objects.cpp) ---
    Flow update_player();                   // 3431
    Flow animate_object(Object& o);         // 5A60
    Flow start_script(Object& o);           // 5A86
    Flow run_script(Object& o);             // 5AAF
    void load_frame(Object& o);             // 5AED
    Flow run_routine(Object& o, Routine r); // dispatch.cpp; the carry in routine_carry_
    bool run_routine_now(Object& o, Routine r);
    bool routine_carry_{};
    u8 object_byte(const Object& o, u8 offset) const; // a byte a script names by offset
    void set_object_byte(Object& o, u8 offset, u8 v);
    void update_contact(Object& o);                     // 5A3B
    void limit_player_speed();                          // 349A
    void move_object(Object& o);                        // 5359
    void jump_script(Object& o, unsigned address);      // a routine sets the script position
    Flow update_objects();                              // 4F3F
    Flow update_object(Object& o, bool marked = false); // 4FC0
    void start_removed_script(Object& o);
    u8 cpu_byte(unsigned bank, unsigned address) const; // through the CPU's map, `bank` in slot 2
    std::uint16_t cpu_word(unsigned bank, unsigned address) const;
    void remove_object(Object& o);    // 509C
    void delete_object(Object& o);    // 515D
    void check_visibility(Object& o); // 5456
    Object* find_free_slot();         // 5142
    Object* script_spawned_{};        // the object a script's spawn command made (IY)
    std::uint16_t object_address(const Object& o) const;
    Object& object_at(std::uint16_t address);
    unsigned slot_number(const Object& o) const;            // 52CA (minus 1)
    void spawn_from_script(Object& o, const ScriptStep& s); // 5CA6, 5EA3

    // --- Player and object routines (dispatch.cpp and the files they name) ---
    void logo_sonic_start(Object& o); // 0C:893E
    void sonic_start(Object& o);      // 0C:97BC
    Flow player_frame_checks();       // 3484-3497: BA5D, 37B5, AFDE, AF82
    // --- Power-ups (game/power_ups.cpp) ---
    Flow player_power_ups();      // 0C:B811
    Flow start_power_up(u8 item); // 0C:B8AB
    Flow end_power_up();          // 0C:B866
    Flow extra_life();            // 3342
    Flow restart_point_item();    // 0C:B92B
    void extra_life_now();        // 3342 where the caller cannot suspend

    // --- The player (game/player.cpp, player_states.cpp, physics.cpp) ---
    bool held(u8 bits) const;
    bool run_player_routine(Object& o, Routine r);
    void player_start_in_act(Object& o);               // 0C:97CF
    void set_player_box();                             // 0C:988E
    void aim_camera_at_player();                       // 0C:98CE
    void player_standing(Object& o);                   // 0C:98E8
    void player_looking_up(Object& o);                 // 0C:9945
    void player_crouching(Object& o);                  // 0C:998C
    void player_walking(Object& o);                    // 0C:99B6
    void stop_when_slow(Object& o);                    // 0C:9A04
    void skid_or_stop(Object& o);                      // 0C:9A2C
    void skid_check(Object& o);                        // 0C:9A5B
    void player_running(Object& o);                    // 0C:9A7C
    void player_skidding(Object& o, bool facing_left); // 0C:9AAE / 9AD9
    void player_rolling(Object& o);                    // 0C:9B04
    void player_jumping(Object& o);                    // 0C:9B40
    void player_air_spin(Object& o);                   // 0C:A509
    void player_balancing(Object& o);                  // 0C:A165
    void start_flight(Object& o);                      // 3E9D
    void tails_take_off(Object& o);                    // 3C91 (0408)
    void tails_hover(Object& o);                       // 0C:A238
    void release_peel_out(Object& o);                  // 3C12 (0429)
    void player_peeling(Object& o);                    // 0C:9BC6
    void player_peel_run(Object& o);                   // 0C:9C06
    void card_run_start(Object& o);                    // 0C:898D
    void hurt_start(Object& o);                        // 0C:83CC
    void float_start(Object& o);                       // 0C:866E
    void victory_start(Object& o);                     // 0C:A8D2
    void pipe_chamber(Object& o);                      // 0C:BD69
    void pipe_choose(Object& o);                       // 0C:BD4D
    void pipe_out(Object& o);                          // 0C:BDC6
    void pipe_fly(Object& o);                          // 0C:BDF9
    void victory_run(Object& o);                       // 0C:A91A
    void move_horizontally_only(Object& o);            // 539A (0462)
    void velocity_from_angle(Object& o);               // 52E8 (032D)
    void set_boss_flash_color();                       // 5888 (050D)
    void move_vertically_only(Object& o);              // 5379 (045F)
    void run_off(Object& o);                           // 0C:84FF
    void player_sprung_diagonally(Object& o);          // 0C:9BAB
    void player_bounced(Object& o);                    // 0C:9B10
    void player_float(Object& o);                      // 0C:A51C
    void float_hold(Object& o);                        // 0C:A562
    void player_float_short(Object& o);                // 0C:A570
    void player_hurt(Object& o);                       // 0C:9CD6
    void death_start(Object& o, std::uint16_t vy);     // 0C:843C, 8434
    void player_dead(Object& o);                       // 0C:A18D
    void player_drowned(Object& o);                    // 0C:A1D3
    void dead_below_screen(Object& o);                 // 0C:A1B9
    void tails_fly(Object& o);                         // 0C:A0D7
    void keep_in_view(Object& o);                      // 0C:B410
    void entry_drop(Object& o);                        // 0C:A590
    void entry_slide(Object& o);                       // 0C:A5C4
    void entry_jump(Object& o);                        // 0C:A60B
    void entry_land(Object& o);                        // 0C:A64F
    // Paths (game/paths.cpp).
    std::uint16_t path_step() const; // D3D1
    void follow_curve(Object& o, const Table& y, const Table& x,
                      std::uint16_t distance);     // 34F1, 365B, 35B7
    void path_right(Object& o);                    // 3542 (03E4)
    void path_left(Object& o);                     // 36B3 (03EA)
    void path_rise(Object& o);                     // 35B7 (03E7)
    void fall_off_path(Object& o);                 // 3591, 3717
    void animate_path(Object& o);                  // 0C:ADB7
    void animate_path_fall(Object& o);             // 0C:AD85
    void path_fall(Object& o);                     // 0C:9CBE
    void player_sprung(Object& o);                 // 0C:9B8E
    void player_charging(Object& o);               // 0C:9C52
    void player_dashing(Object& o);                // 0C:9C8A
    void player_falling(Object& o);                // 0C:9C9C
    void jump_in_air(Object& o);                   // 0C:A4D9
    void tails_flight_start(Object& o);            // 0C:A4E6
    void face_input(Object& o);                    // 0C:B7B6
    void player_move(Object& o);                   // 0C:B05B
    bool accelerate(Object& o);                    // 0C:B2A7
    void accelerate_in_water(Object& o);           // 0C:B346
    void hold_at_edge(Object& o, unsigned offset); // 0C:B3D3 / B3E0
    void keep_on_screen(Object& o);                // 0C:B42C
    void move_horizontally(Object& o);             // 0C:B0F3
    void three_quarters_force();                   // 0C:B191
    void move_vertically(Object& o);               // 0C:B1A3
    void special_blocks(Object& o);                // 0C:B093
    struct Gait;
    static const Gait sonic_gait, tails_gait;
    void animate_walking(Object& o, const Gait& g);     // 0C:AC6E, 92AE
    void animate_running(Object& o, const Gait& g);     // 0C:ACDA, 931A
    void animate_spinning(Object& o);                   // 0C:ACF0
    void to_standing(Object& o);                        // 3A3A
    void check_ledge(Object& o);                        // 0C:B7D0
    void to_walking(Object& o);                         // 3A8D
    void to_jumping(Object& o);                         // 3AB8
    void player_held(Object& p);                        // 0C:A078
    void player_swim(Object& o);                        // 0C:A862
    void to_falling(Object& o);                         // 3B47
    void to_falling_from_flight(Object& o);             // 3B62
    void to_falling_state(Object& o);                   // 3B6F
    void jump(Object& o);                               // 3AD6
    void overhang_swing(Object& o, std::uint16_t push); // 0C:A6D7, A713
    void tunnel_run(Object& o);                         // 0C:A782
    void continue_wait(Object& o);                      // 0C:8DE1
    void to_bounced_roll(Object& o);                    // 3D99
    void to_rolling_fall(Object& o);                    // 3BB8
    void to_looking_up(Object& o);                      // 3BCF
    void to_crouching(Object& o);                       // 3BDA
    void to_peel_out(Object& o);                        // 3BF2
    void to_spin_dash(Object& o);                       // 3C4A
    void release_spin_dash(Object& o);                  // 3C6E
    void to_running(Object& o);                         // 3D3B
    void to_skid_7(Object& o);                          // 3D48
    void to_skid_8(Object& o);                          // 3D5B
    void to_rolling(Object& o);                         // 3D6E
    void roll(Object& o);                               // 3D7B
    void special_move(Object& o);                       // 3E7A
    void to_float(Object& o);                           // 3EE3
    // Terrain against the player (physics.cpp).
    void player_terrain(Object& o);                      // 5F14
    void player_floor(Object& o);                        // 5F53
    void floor_landing(Object& o);                       // 65AD
    void land_on_profile(Object& o, bool check_rising);  // 65BE / 65C3
    void sinking_block(Object& o);                       // 6258
    void snap_to_cell(Object& o);                        // 1E:8706
    void dropping_block(Object& o);                      // 639E
    void player_terrain_plain(Object& o);                // 5F27
    void overhead_wall(Object& o);                       // 6C13
    void wall_on_right(Object& o);                       // 6C8E
    void wall_on_left(Object& o);                        // 6CC7
    void block_kind_effect(Object& o, u8 kind);          // table 5FF2
    void in_the_air(Object& o);                          // 6487
    void conveyor_slope(Object& o);                      // 6075
    void ramp_launch(Object& o);                         // 6113
    void block_kind_10(Object& o);                       // 6B3C
    void player_walls(Object& o);                        // 6780
    bool left_wall(Object& o);                           // 6855
    void spike_wall(Object& o, bool right);              // 6960, 6980
    void push_from_wall(Object& o, bool right);          // 67D9, 6896
    bool wall_path_switch(Object& o, u8 first, u8 path); // 69FB / 6A14

    struct SideProbe {
        std::uint16_t dx, dy;
    };

    SideProbe side_probe(const Object& o, bool right);          // 6A2E / 6A6D
    void player_ceiling(Object& o);                             // 6A92
    void ceiling_solid_from_below(Object& o);                   // 6D91
    void player_sides(Object& o);                               // 6CF2
    void player_head(Object& o);                                // 6E16
    void collect_map_ring(Object& o);                           // 6E7D
    void ring_block(Object& o);                                 // 6219
    bool break_wall(Object& o, bool right);                     // 6910, 6937
    void shatter_block(Object& o);                              // 7228
    void path_entry_block(Object& o);                           // 64BA
    void spring_block(Object& o);                               // 642A
    void spring_floor(Object& o);                               // 61B4
    void crumble_block(Object& o);                              // 62CE
    void step_on_switch(Object& o);                             // 63CE
    void switch_effect(Object& o);                              // 18:B858
    void bumper(Object& o, std::uint16_t vx, std::uint16_t vy); // 72B2
    void launch_ramp(Object& o);                                // 6174
    void breakable_floor(Object& o);                            // 6330
    void convey(Object& o, std::uint16_t amount);               // 6384
    void diagonal_spring(Object& o);                            // 6F44
    void pipe_junction(Object& o);                              // 72F1
    void wall_spring(Object& o, std::uint16_t vx);              // 3DFF, 3E0C
    void enter_path(Object& o, bool leftward);                  // 373D, 378C
    void break_ring_block(Object& o);                           // 71D8
    void set_map_block(std::uint16_t cell, u8 block);           // 2CEB with the map write
    void set_map_block_at(std::uint16_t cell, u8 block, std::uint16_t x, std::uint16_t y); // 2D17
    Object* spawn_object(Object& spawner, u8 type, u8 param);                              // 50E1
    Object* spawn_object_at(Object& spawner, u8 type, u8 param, std::uint16_t x,
                            std::uint16_t y); // 510A
    Flow player_fall_check(Object& p);        // 0C:AFDE
    void water_surface(Object& p);            // 0C:AF82
    void player_air(Object& p);               // 0C:BA5D
    void skim_water(Object& p);               // 0C:BB1E

    static u8 high_speed(std::uint16_t v) { return (hi(v) & 0x80) ? u8(-hi(v)) : hi(v); }

    void add_ring(); // 33AB

    // --- The intro's objects (game/intro.cpp) ---
    void sonic_intro_place(Object& o);      // 0C:985D
    void sonic_intro_wait(Object& o);       // 0C:88B0
    void sonic_intro_start(Object& o);      // 0C:88C2
    void sonic_intro_run(Object& o);        // 0C:88D6
    void tails_intro_start(Object& o);      // 0C:950F
    void tails_intro_fly(Object& o);        // 0C:9530
    void emerald_start(Object& o);          // 1E:ADE6
    void emerald_drop(Object& o);           // 1E:AE86
    void emerald_taken(Object& o);          // 1E:AED8
    void emerald_fall(Object& o);           // 1E:AEAA
    void emerald_toss(Object& o);           // 1E:ADB7
    void knuckles_start(Object& o);         // 1E:B462
    void knuckles_intro_place(Object& o);   // 1E:B311
    void knuckles_intro_wait(Object& o);    // 1E:B32B
    void knuckles_intro_hop(Object& o);     // 1E:B356
    void knuckles_intro_fly(Object& o);     // 1E:B3E5
    void knuckles_bob(Object& o);           // 1E:B42A
    void sparkle_start(Object& o);          // 1E:B7EF
    void bumper_light(Object& o, u8 block); // 1E:B8C7, B8ED, B8A4
    void switch_wait(Object& o);            // 1E:BF55
    void switch_release(Object& o);         // 1E:BF68
    void ship_intro_start(Object& o);       // 17:8B6D

    // --- The title (game/title.cpp) ---
    void start_map_animation(u8 number);  // 23A3 (044D)
    Flow run_map_animations();            // 228E
    void knuckles_title_start(Object& o); // 1E:B5B7
    void knuckles_title_run(Object& o);   // 1E:B5D4
    void knuckles_title_jump(Object& o);  // 1E:B601
    void knuckles_title_fall(Object& o);  // 1E:B616
    void emblem_start(Object& o);         // 1E:8B11

    // --- Objects and the player (game/collision.cpp) ---
    void touch_test(Object& o);                    // 58AC (0369)
    void solid_object(Object& o);                  // 51FF (036C)
    bool nothing_below(const Object& o);           // 53CD (03A5)
    void destroy_object(Object& o);                // 51B1 (0366)
    void destroy_if_attacked(Object& o);           // 51B8 (0363)
    void become_explosion(Object& o);              // 51D0
    void add_points(const std::array<u8, 3>& add); // 2430
    void add_ten_rings();                          // 3396 (0432)
    void add_hundred_rings();                      // 332A
    bool player_touch(Object& p);                  // 381B

    // --- Harm (game/damage.cpp) ---
    Flow player_contact(Object& p);  // 37B5
    Flow hurt_player(Object& p);     // 384A
    void hurt_player_now(Object& p); // 384A from the terrain
    void scatter_rings(Object& p);   // 389A
    void throw_back(Object& p);      // 38F2
    void lose_rings();               // 38C4
    Flow kill_player(Object& p);     // 3942
    bool start_death(Object& p);     // 3942 without its frame's wait
    u8 script_bank_ = 0;             // a script started now uses this bank (0: its type's; 34C7)
    void blink_after_hit(Object& p); // 39CF
    void blink_tick(Object& p);      // 39D4
    Flow restore_act_music();        // 2216
    bool request_restored_music();   // 2216 without its frame's wait

    // --- Enemies (game/enemies.cpp) ---
    bool run_enemy_routine(Object& o, Routine r);
    void bouncer_start(Object& o);                                          // 1E:A047
    void bouncer_touch(Object& o);                                          // 1E:A0FD
    bool touches_x(const Object& a, const Object& b, unsigned range) const; // 5424 (0378)
    void piston_hold(Object& o);                                            // 1E:95D2 (type 1A)
    void hopper_touch(Object& o);                                           // 1E:B156
    void popper_fall(Object& o);                                            // 17:845C
    void sub_wait(Object& o);                                               // 1E:BFC2
    void roller_roll(Object& o);                                            // 1E:A9D7
    void roller_break(Object& o);                                           // 1E:AA49
    void shooter_start(Object& o);                                          // 1E:BB1A
    void shooter_move(Object& o);                                           // 1E:BB8F
    void bomber_start(Object& o);                                           // 1E:A513
    void bomber_patrol(Object& o);                                          // 1E:A56E
    void bomber_bob(Object& o);                                             // 1E:A584
    void bomb_fall(Object& o);                                              // 1E:A5D2
    void harmful_touch(Object& o);                                          // 550E (0375)
    bool below_water(const Object& o, unsigned depth) const;                // 5607 (03AE)
    void spring_player(Object& spring, std::uint16_t vy);                   // 5186 (03B1)
    void launch_player(Object& source, std::uint16_t vy);                   // 3DB3
    void leave_special_state(Object& p);                                    // 5854

    // --- The title card (game/card.cpp) ---
    bool run_card_routine(Object& o, Routine r);
    void card_slide(Object& o); // 1E:9203

    // --- Sunset Park act 3's train (game/train.cpp) ---
    bool run_train_routine(Object& o, Routine r);
    u8 train_gap_x() const;                                    // DBEB
    u8& band(unsigned k);                                      // DBE7 + k
    void draw_block_at(unsigned name_address, unsigned block); // 1CDE + 1DE4
    void change_speed(Object& o, bool up);                     // 1E:8F35, 8FE1
    void ride_bands(Object& o);                                // 1E:8E5F
    void station_columns(Object& o, unsigned step);            // 1E:914E-9196
    void director_end(Object& o);                              // 1E:8EB3
    Flow train_ride(Object& o);                                // 0C:BB98
    Flow train_jump(Object& o);                                // 0C:BBAB
    Flow train_fall(Object& o);                                // 0C:BBDC
    Flow train_thrown(Object& o);                              // 0C:BCC5
    Flow train_hit(Object& o);                                 // 0C:BC7F
    void return_to_roof(Object& o);                            // 0C:BBCB
    void train_steps(Object& o);                               // 0C:BC21
    void train_drift(Object& o);                               // 0C:BC45
    bool on_train_roof(const Object& o) const;                 // 0C:BC5C
    void train_boss_start(Object& o);                          // 17:9111
    void train_boss_follow(Object& o);                         // 17:916B
    void train_boss_touch(Object& o);                          // 17:9174

    // --- The rival's scenes (game/rival.cpp) ---
    bool run_rival_routine(Object& o, Routine r);
    void rival_next(Object& o);         // 16:BCDB
    void rival_run(Object& o);          // 16:BDAE
    void rival_brake(Object& o);        // 16:BDEF
    void rival_leave(Object& o);        // 16:BE29
    void rival_meet(Object& o);         // 16:BE5F
    Flow rival_scene_camera(Object& o); // 16:BEAF
    void knuckles_in_act(Object& o);    // 1E:B478
    void rival_restore_art();           // 762C (04A1)
    void rival_scatter(Object& o);      // 16:BFBE

    // --- The lift (type 4D, game/platforms.cpp) ---
    void set_lift_travel(Object& o, std::uint16_t v);
    void reset_lift(Object& o); // 17:88D4
    void lift_wait(Object& o);  // 17:8892
    void lift_ride(Object& o);  // 17:88ED

    // --- Meta Junglira's boss (game/boss_jungle.cpp) ---
    struct ArmSweep {
        std::uint16_t dy{}, dx{}, y{}, x{}; // D100, D108, D104, D10C
    };

    bool run_jungle_boss_routine(Object& o, Routine r);
    std::uint16_t work_word(unsigned offset) const;
    void set_work_word(unsigned offset, std::uint16_t v);
    Object& jungle_boss();
    void jungle_start(Object& o);                     // 17:93CB
    void hold_by_arm(Object& o);                      // 17:9517
    void arm_start(Object& o);                        // 17:9817
    void arm_lower(Object& o);                        // 17:9790
    ArmSweep arm_steps_at(const Object& o, u8 angle); // 17:966E
    void arm_group(unsigned group, ArmSweep& s);      // 17:95F0
    void arm_swing(Object& o);                        // 17:957F
    void arm_reach_start(Object& o);                  // 17:9740
    void arm_reach(Object& o);                        // 17:9697
    void arm_retract(Object& o);                      // 17:9843

    // --- Tidal Plant's boss (game/boss_tidal.cpp) ---
    bool run_tidal_boss_routine(Object& o, Routine r);
    void run_off_to_clear();         // 3E5C (03C0)
    void tidal_frame(Object& o);     // 17:A821
    void count_down_clock();         // 2FE8
    void enter_special_stage();      // 1E:8A26
    void tidal_hit_check(Object& o); // 17:A79E
    void tidal_scroll(Object& o);    // 17:A7DE
    void tidal_shard(Object& o);     // 17:A6E2
    std::uint16_t step_address(const Object& o) const;

    // --- Atomic Destroyer's boss (game/boss_destroyer.cpp) ---
    bool run_destroyer_boss_routine(Object& o, Routine r);
    void destroyer_chase(Object& o); // 17:AB75
    void destroyer_fly(Object& o);   // 17:AC49

    // --- Atomic Destroyer act 3's last machine (game/boss_final.cpp) ---
    bool run_final_machine_routine(Object& o, Routine r);
    Object* final_machine();                      // D3E5 (IY)
    void machine_start(Object& o);                // 0F:B7CF
    Flow machine_art(Object& o);                  // 0F:B9F2
    Flow machine_core_art(Object& o);             // 0510 (7A3A)
    void first_form_hit(Object& o);               // 0F:B4BF
    void machine_hit(Object& o);                  // 0F:BED3
    void machine_follow(Object& o);               // 0F:BA1E
    void machine_turned(Object& o);               // 0F:BA87
    void machine_sway(Object& o);                 // 0F:BB04
    void last_form_touch(Object& o, bool charge); // 0F:BC3E, BC7A
    void core_hit(Object& o);                     // 0F:BEF1
    void hatch_hit(Object& o);                    // 0F:BF2A
    void hatch_follow(Object& o);                 // 0F:BDFA

    // The Z80 refresh register (LD A,R) as a source of chance. Its value
    // depends on exact instruction timing, which a native program does not
    // have; a pseudo-random sequence stands in for it.
    u8 refresh_register();
    std::uint32_t random_state_{0x12345678};

    // --- Atomic Destroyer act 3's end scene (game/destroyer_end.cpp) ---
    bool run_destroyer_end_routine(Object& o, Routine r);
    void craft_hold_player(Object& o); // 17:AF15, AEFE
    Flow cage_art(Object& o);          // 1C:B85E
    bool run_ending_player_routine(Object& p, Routine r);
    void ending_held(Object& p); // 0C:A9C4
    void ending_beat(Object& p, std::uint16_t sonic, std::uint16_t tails);

    // --- Robotnik Winter's boss (game/boss_winter.cpp) ---
    bool run_winter_boss_routine(Object& o, Routine r);
    bool winter_boss_hit(Object& o);
    void scroll_arena(Object& o);                                // 17:A00E
    void winter_part(Object& o, std::uint16_t dx, bool harmful); // 17:A093

    // --- Sonic's flight (game/jet.cpp) ---
    bool run_jet_routine(Object& o, Routine r);
    Flow jet_fly(Object& o);            // 0C:9D06
    Flow start_power_flight(Object& p); // 3CFC (0405)
    Flow jet_end(Object& o);            // 0C:82F8
    void jet_count(Object& o);          // 0C:9D43
    void jet_move(Object& o);           // 0C:9D71
    void jet_steer(Object& o);          // 0C:9DCD
    void jet_limits(Object& o);         // 0C:9E19

    // --- The special stages' emerald (game/special.cpp) ---
    bool run_special_routine(Object& o, Routine r);
    Flow stage_emerald_start(Object& o); // 17:89DD

    // --- The sparkle (game/sparkle.cpp) ---
    bool run_sparkle_routine(Object& o, Routine r);
    void follow_player(Object& o, std::uint16_t dy); // 519B (03A2)
    Flow sparkle_power(Object& o);                   // 1E:80AA
    void sparkle_peel(Object& o);                    // 1E:80DF

    // --- Sonic's board (game/board.cpp) ---
    bool run_board_routine(Object& o, Routine r);
    void board_start(Object& o);                // 0C:A3CB
    void board_frame(Object& o, unsigned pose); // 0C:AF28, AF55
    void board_ride(Object& o);                 // 0C:A3E9
    void lose_board(Object& o);                 // 5860

    // --- The crawler (game/crawler.cpp) ---
    static constexpr unsigned trail_length = 0x30;
    std::uint16_t trail_x(unsigned i) const; // DBE6 + 2i
    void set_trail_x(unsigned i, std::uint16_t x);
    bool run_crawler_routine(Object& o, Routine r);
    void crawler_start(Object& o);               // 1E:BCA5
    void crawler_head(Object& o);                // 1E:BD4A
    void crawler_segment(Object& o, bool sting); // 1E:BE29, BE11
    void crawler_range(Object& o);               // 1E:BF0B

    // --- Platforms (game/platforms.cpp) ---
    bool run_platform_routine(Object& o, Routine r);
    void platform(Object& o);                         // 5669 (0396)
    void move_platform(Object& o);                    // 567F
    void ride_platform(Object& o);                    // 572E
    void rise_platform(Object& o, bool reset = true); // 5835, 5839
    void stomp_check(Object& o);                      // 59DB (036F)
    void platform_start(Object& o);                   // 1E:A3D6
    void bobber_start(Object& o);                     // 1E:A6DE
    void bobber_platform(Object& o);                  // 1E:A7BE

    // --- Shells (game/shells.cpp) ---
    bool run_shell_routine(Object& o, Routine r);
    void walker_start(Object& o);   // 1E:A85A
    void walker_walk(Object& o);    // 1E:A892
    void shell_slide(Object& o);    // 1E:9E25
    void shell_friction(Object& o); // 1E:9EE4
    void shell_rest(Object& o);     // 1E:9E8A
    void shell_ride_start(Object& o);    // 0C:8325
    void shell_ride_frame(Object& o);    // 0C:AEB2
    void shell_ride(Object& o);          // 0C:BE2F
    void shell_ride_input(Object& o);    // 0C:BE71
    void shell_ride_speed_up(Object& o); // 0C:BE94

    // --- Meta Junglira's barrels (game/barrel.cpp) ---
    bool run_barrel_routine(Object& o, Routine r);
    void barrel_center(Object& o); // 0C:A672

    // --- The Great Turquoise boss (game/boss_turquoise.cpp) ---
    bool run_boss_routine(Object& o, Routine r);
    void boss_start(Object& o);              // 17:8D92
    void boss_wait(Object& o);               // 17:8DB8
    void boss_patrol(Object& o, bool right); // 17:8DFF, 8E40
    void boss_touch(Object& o);              // 17:8F5E
    void boss_hit_end(Object& o);            // 17:8EBC
    void boss_rise_start(Object& o);         // 17:8EE0
    void boss_after();                       // 3F53 (04D4)
    void bounce_off(Object& o);              // 561B (0438)

    // --- The act's end sign (game/signpost.cpp) ---
    bool run_sign_routine(Object& o, Routine r);
    void sign_start(Object& o);  // 1E:9968
    void sign_frame(Object& o);  // 1E:99E9
    void sign_touch(Object& o);  // 1E:9A4B
    Flow sign_result(Object& o); // 1E:9AA3
    Flow sign_award(Object& o);  // 1E:9AF8
    void sign_done(Object& o);   // 1E:9B37

    // --- Springs (game/springs.cpp) ---
    bool run_spring_routine(Object& o, Routine r);
    void spring_start(Object& o); // 17:A11F
    void spring_wait(Object& o);  // 17:A1F7

    // --- Explosions (game/explosion.cpp) ---
    bool run_explosion_routine(Object& o, Routine r);
    void explosion_start(Object& o); // 1E:853C
    void explosion_burn(Object& o);  // 1E:85D5
    void explosion_end(Object& o);   // 1E:8618
    void piece_start(Object& o);     // 1E:8800

    // --- Item boxes (game/item_box.cpp) ---
    bool run_box_routine(Object& o, Routine r);
    void box_start(Object& o);               // 1E:828D
    void box_wait(Object& o);                // 1E:82FD
    void box_shown(Object& o, unsigned art); // 1E:8325, 8336
    void break_box(Object& o);               // 1E:840F
    void box_broken(Object& o);              // 1E:843C
    void icon_end(Object& o);                // 1E:844C

    // --- Rings and sparkles (game/rings.cpp) ---
    bool run_ring_routine(Object& o, Routine r);
    bool touches_player(const Object& o) const; // 53FD (0372)
    void take_ring(Object& o);                  // 1E:987E
    void ring_to_sparkle(Object& o);            // 1E:988B
    void sparkle_place(Object& o);              // 1E:8020
    void scatter_start(Object& o);              // 1E:975C
    void scatter_bounce(Object& o);             // 1E:97F9

    // --- The water's objects (game/water_objects.cpp) ---
    bool run_water_routine(Object& o, Routine r);
    void water_start(Object& o);  // 1E:A27E
    void bubble_drift(Object& o); // 1E:A2E3
    void glint(Object& o);        // 1E:A357
    void breathe(Object& o);      // 1E:A32B
    void breath_start(Object& o); // 1E:A38F
    void air_start(Object& o);    // 1E:BA04
    void air_bubble(Object& o);   // 1E:BA77
    void rise_air(Object& o);     // 1E:BACF
};

} // namespace triple_trouble_gg
