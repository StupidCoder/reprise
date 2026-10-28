#pragma once
#include "c64/sid.hpp"
#include "c64/text_frame.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/tables.hpp"
#include "fort_apocalypse_c64/game/controls.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <vector>

// Fort Apocalypse, rebuilt: the whole game from the title screen on.
//
// One frame runs in the order the original's work happens around the
// picture: the playfield part of its interrupt (raster $76, during play),
// the picture with the VIC's collision latches, the interrupt at raster
// $F9 (the HUD part, or the title's), then the main loop's passes. During
// play the original's main loop runs freely in whatever time the
// interrupts leave (1.2 to 3 frames per pass, depending on the work); here
// it runs at a set average rate.
//
// Model and view: the game's state (craft, objects, level map, counters) is
// modelled with its own types. Positions keep the original's numbers,
// because its rules compare them directly: objects use map column + 5, the
// craft and the enemy map column + 1. The screen is a 40x25 character
// display the game draws into when the original does (texts, HUD, the
// playfield window copied from the map); it is the view only.
//
// Routine addresses in comments are the original's; notes in
// .analysis/fort_apocalypse_c64/game_code.md.
namespace fort_apocalypse::game {

using u8 = std::uint8_t;

// Game modes ($9D).
enum class Mode : u8 {
    title = 1, // also the debrief's screen, until it times out
    playing = 2,
    new_game = 3,
    get_ready = 4, // "ENTERING ...", while the level is built
    life_start = 5, // "GET READY"
    debrief = 6,
    locked = 7, // level change, fort explosion, teleport
    paused = 8,
    options = 9,
    teleport = 0x0A,
};

// The player's craft ($64).
enum class Craft : u8 {
    inactive = 1,
    flying = 3,
    crashing = 4,
    landed = 6,
    appearing = 7,
    boarding = 0x0B, // landed next to a prisoner
};

struct Settings {
    int gravity{0};     // $F4: WEAK, NORMAL, STRONG
    int pilot_skill{0}; // $F6: NOVICE, PRO, EXPERT
    int robo_pilots{2}; // $F8: THREE, FIVE, SEVEN

    int& value(int setting) {
        return setting == 0 ? gravity : setting == 1 ? pilot_skill : robo_pilots;
    }
    int value(int setting) const {
        return setting == 0 ? gravity : setting == 1 ? pilot_skill : robo_pilots;
    }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(gravity, pilot_skill, robo_pilots); }
};

// Changes the player can switch on (all off is the original).
struct Options {
    // The original's enemy helicopter probes the wrong column before it
    // climbs (the first byte of its routine at $A000 is corrupt), so it flies
    // up into rock and explodes. On: it probes its own column.
    bool fixed_enemy_climb{};
    // The widescreen view shows the shots that have left the window. The
    // original works out a shot's column with an unsigned subtraction
    // ($A341), so past the window's left edge a shot tests cells 53-63
    // columns to its right and hits nothing it flies over. On: its own
    // column, so it hits what the wider view shows it reach.
    bool widescreen{};
    // Cheats (not in the attract game): a crash costs no life; the fuel tank
    // stays full; the lasers
    // stay dark (the barriers and the laser grids; level 1's barriers stay,
    // they are its teleport); the tanks launch no missiles and the enemy
    // helicopter does not shoot; the sweeping walls move half as fast.
    bool infinite_lives{}, infinite_fuel{}, no_lasers{}, enemies_hold_fire{}, slow_walls{};
    // Carnage mode (not the original game; not in the attract game): fire
    // is a rapid-fire gun whose tracers carve the brown rock pixel by pixel
    // (carnage.cpp).
    bool carnage{};
    // The widescreen view's extra columns on each side (0 without it): the
    // tracers end at the edges of what is seen, as the original's shots end
    // at its window's.
    int wide_view{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(fixed_enemy_climb, widescreen, infinite_lives, infinite_fuel, no_lasers, enemies_hold_fire,
           slow_walls, carnage, wide_view);
    }
};

class Game {
public:
    Game(const data::Assets&, const data::GameTables&);

    // One PAL frame; returns its picture.
    const c64::Picture& tick(const Controls&, const Options&);
    const c64::Picture& picture() const { return picture_; }

    // The original reads its random numbers from the SID's noise
    // generator; here they come from a generator of our own.
    void seed(std::uint32_t value) { rng_ = value ? value : 1; }
    // For comparisons with the original: take the random numbers from a
    // recording of its noise reads instead.
    void set_noise_source(std::function<u8(std::uint16_t site)> source) {
        noise_source_ = std::move(source);
    }
    // Main-loop passes per frame during play.
    void set_pass_rate(double passes_per_frame) { pass_rate_ = passes_per_frame; }
    // For comparisons with the original: the main loop's pass steps as its
    // loop ran them in the next frame, instead of the rate. `runs` steps
    // start (and, unless they wait, finish); `returns` end steps that waited
    // or ran long. Per frame part: before the playfield's interrupt (raster
    // $76), before the HUD's ($F9), after it.
    struct Steps {
        int runs{}, returns{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(runs, returns); }
    };
    void set_next_steps(Steps before_playfield, Steps before_hud, Steps after_hud) {
        scheduled_ = true;
        steps_early_ = before_playfield;
        steps_before_ = before_hud;
        steps_after_ = after_hud;
    }

    Mode mode() const { return mode_; }
    // For research tools: the character screen, and a line of key state
    // under the original's zero-page names.
    const std::array<u8, 1000>& screen() const { return screen_; }
    std::string debug_line() const;
    std::string debug_objects() const;
    // For comparisons with the original: set a value under its zero-page
    // address (the craft's checkpoint $5D-$63, level $5C, fuel $9B/$9C, men
    // left $EB, lives $F9, fort $ED, the enemy's state $6E, column $76 and
    // row $77, the tanks' states $BE-$C3), as the reference harness pokes RAM.
    // Returns false for addresses it does not know.
    bool debug_poke(std::uint16_t address, u8 value);
    // For tests: a carnage-mode crater at map pixel (x, y).
    void debug_blast(int x, int y, double radius) { blast(x, y, radius); }
    c64::Sid& sid() { return sid_; }

    // --- for presentation layers (the enhanced mode) ---------------------------
    // Read-only views of what the last picture was drawn from, and what
    // happened in the frame. Nothing here changes the game.
    struct MapCell {
        int column{}, row{}; // the map's own column (0-255) and row

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(column, row); }
    };
    struct ShotImpact {
        MapCell cell;     // the cell the shot hit
        u8 direction{};   // 1-5, as the shot flew (bullet_dx/dy)
        u8 code{};        // the character it hit
        bool by_player{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(cell, direction, code, by_player); }
    };
    struct TankDestroyed {
        MapCell body;     // the left cell of the tank's three
        u8 turret{};      // the turret character above its middle

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(body, turret); }
    };
    struct MineDestroyed {
        MapCell cell;     // the left of its two cells
        std::array<u8, 2> look{}; // the two characters it showed
        int direction{};  // 1 or -1, as it was flying

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(cell, look, direction); }
    };
    struct ShellBurst {
        MapCell cell;     // where the tank's shell burst
        int direction{};  // -1 flying left, 1 right
        u8 code{};        // the character it hit or lay over

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(cell, direction, code); }
    };
    // Carnage mode: a pixel the gun carved out of the rock, in map pixels
    // (8 per column and row; a multicolour pixel is two wide), with its
    // pixel pair (1-3: the colour it had) and the way the tracer flew.
    struct CarvedPixel {
        int x{}, y{};
        u8 pair{};
        double dx{}, dy{};
        bool bite{}; // the first of a tracer's bite

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, pair, dx, dy, bite); }
    };
    struct Tracer {
        double x{}, y{}, dx{}, dy{}; // map pixels, per frame
        int age{};
        u8 direction{}; // as the original's shot would fly (1-5)

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, dx, dy, age, direction); }
    };
    struct Happenings {
        std::vector<ShotImpact> impacts; // the craft's shots and the enemy's
        std::vector<ShellBurst> shells;
        std::vector<TankDestroyed> tanks;
        std::vector<MineDestroyed> mines;
        std::vector<CarvedPixel> carved;
        std::vector<MapCell> depot_blasts; // a depot blew up (its middle, in pixels)

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(impacts, shells, tanks, mines, carved, depot_blasts); }
    };
    const Happenings& happenings() const { return happenings_; }
    // The last picture as drawn: its character frame (cells, lines,
    // sprites), the screen codes in it, and the map cell its playfield
    // window starts with (screen column 0, row 9).
    const c64::TextFrame& shown_frame() const { return shown_frame_; }
    const std::array<u8, 1000>& shown_screen() const { return shown_screen_; }
    MapCell shown_window() const { return shown_window_; }
    static constexpr int window_first_row = 9;
    // Display lines above the playfield's split (raster $77): the HUD.
    static constexpr int hud_lines = 0x77 - 51;
    // The map as the game addresses it, and the playfield characters now.
    u8 map_code(int column, int row) const { return map_at(row * 256 + column); }
    const c64::Glyph& playfield_glyph(u8 code) const { return glyphs_[code & 0x7F]; }
    // The pixels of a map cell as shown: its character's, or in carnage mode
    // what the gun has left of it.
    const c64::Glyph& cell_glyph(int column, int row) const {
        return cell_glyph_at(row * 256 + column, map_at(row * 256 + column));
    }
    // The same for a cell of the widescreen view's margins (see
    // shown_wide_code).
    const c64::Glyph& shown_wide_glyph(int screen_column, int screen_row) const;
    // Carnage mode's tracers in flight.
    const std::vector<Tracer>& tracers() const { return tracers_; }
    // Carnage mode: something that came loose and falls (it bursts where it
    // lands): a chunk of rock, a tank whose ground was shot away, a laser
    // emitter shot off its frame. Map pixels, its box's top left.
    struct FallingBody {
        enum class Kind { rock, tank, emitter, tower } kind{Kind::rock};
        double x{}, y{}, vx{}, vy{};
        int width{}, height{}; // pixels (a rock chunk's are pairs: two wide)
        // A rock chunk's pixel pairs, width / 2 by height, row by row (0
        // empty, else its pair 1-3); a tank's characters (body three, turret
        // above the middle); an emitter's character.
        std::vector<u8> pixels;
        std::array<u8, 4> codes{};
        double spin{}; // how fast it turns as it falls (for the picture)
        int age{};
        // A tower (pixels as a rock chunk's) turns on its base corner, then
        // drops: turned by `angle` (radians, clockwise) about the pivot, then
        // moved by (ox, oy). It comes to rest as terrain.
        double pivot_x{}, pivot_y{}, angle{}, omega{}, ox{}, oy{}, ovx{}, ovy{};
        bool pivoting{};
        int resting{}; // frames it is still shown after coming to rest
        // An emitter's beam, still firing as it tumbles: its direction
        // unturned, and where it ends this frame.
        double beam_angle{}, beam_ex{}, beam_ey{};
        bool beam_on{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(kind, x, y, vx, vy, width, height, pixels, codes, spin, age, pivot_x, pivot_y, angle, omega,
               ox, oy, ovx, ovy, pivoting, resting, beam_angle, beam_ex, beam_ey, beam_on);
        }
    };
    const std::vector<FallingBody>& falling() const { return falling_; }
    // Carnage mode: towers rocking on their base from the gun's hits (cells,
    // in the copy of the map that holds them whole; lean in radians,
    // clockwise). The map still has them upright.
    struct TowerView {
        int column{}, row{}, width{}, height{};
        double lean{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(column, row, width, height, lean); }
    };
    std::vector<TowerView> rocking_towers() const;
    // Carnage mode: fuel depots under fire (0 untouched, 1 about to blow; a
    // blown one is gone), each copy of the map's seam listed.
    struct DepotView {
        MapCell cell; // its top left
        double damage{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(cell, damage); }
    };
    std::vector<DepotView> depot_damage() const;
    // Carnage mode's arcade layer: kills within a short time of each other
    // make a combo (with a bonus); combos fill the overdrive meter; a full
    // one gives a while of a faster, triple gun. `impact` is how hard this
    // frame hit (0-3: hit-stop and shake for the picture).
    struct CarnageState {
        int combo{}, combo_timer{};   // kills in a row, frames left to add one
        double meter{};               // 0-1
        int overdrive{};              // frames left
        int impact{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(combo, combo_timer, meter, overdrive, impact); }
    };
    const CarnageState& carnage_state() const { return carnage_state_; }
    struct TankView {
        bool on_map{};    // driving or exploding
        bool exploding{};
        MapCell body;
        std::array<u8, 3> saved{}; // what its body covers

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(on_map, exploding, body, saved); }
    };
    std::array<TankView, 6> tank_views() const;
    struct MineView {
        bool flying{};
        MapCell cell;     // the left of its two cells
        int phase{};      // 0-3: two pixels further right each ($957B)

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(flying, cell, phase); }
    };
    std::array<MineView, 39> mine_views() const;
    struct ShellView {
        bool flying{};
        MapCell cell;
        u8 look{};        // its character, by the way it flies

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(flying, cell, look); }
    };
    std::array<ShellView, 6> shell_views() const;
    struct PrisonerView {
        bool running{};   // or boarding: he shows until he is taken in
        MapCell feet;     // his lower cell (the torso is the one above)
        bool left{};      // running left

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(running, feet, left); }
    };
    std::array<PrisonerView, 8> prisoner_views() const;
    // The fuel depots: their left top cell, and the stage of their sinking
    // last drawn (4 standing up, 0 sunk; $ACD5).
    std::vector<MapCell> depot_cells() const;
    int depot_stage_shown() const { return depot_shown_stage_; }
    // The landing pad sinking at the end of level 0 ($8C57): its 6x5 block
    // as it was, and how many rows down it has moved.
    struct SinkView {
        bool active{};
        MapCell top_left;
        std::array<u8, 30> look{};
        int rows{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(active, top_left, look, rows); }
    };
    const SinkView& sink_view() const { return sink_view_; }
    // A widescreen view: the map cells around the playfield window as they
    // were copied with it (columns -wide_columns to 39 + wide_columns of
    // screen rows 9-24), and where the enemy would show off the window.
    static constexpr int wide_columns = 24, wide_window_columns = 40 + 2 * wide_columns;
    u8 shown_wide_code(int screen_column, int screen_row) const {
        return shown_wide_window_[std::size_t((screen_row - window_first_row) * wide_window_columns +
                                              screen_column + wide_columns)];
    }
    struct WideEnemy {
        bool placed{};
        int sprite_x{}, sprite_y{}; // VIC coordinates, past the window too

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(placed, sprite_x, sprite_y); }
    };
    WideEnemy shown_wide_enemy() const { return shown_wide_enemy_; }
    // The last picture had the playfield part (rows 9-24 show the map).
    bool shown_playfield() const { return split_active_; }
    bool enemy_hidden() const { return enemy_.sprite == 1; }
    bool enemy_exploding() const { return enemy_.state == 4; }
    // The turret character above a tank's middle, by where it points.
    static constexpr u8 turret_left = 0x70, turret_right = 0x6F;
    // The craft's bank step (0-17; bit 0 the rotor's frame).
    u8 craft_bank() const { return bank_; }
    bool craft_shown() const { return craft_ != Craft::inactive; }
    bool craft_crashing() const { return craft_ == Craft::crashing; }
    // Frames of the crash still to go ($3F; the HUD interrupt counts it).
    static constexpr int crash_frames = 0x1E; // a crash's length
    // For a copy the effects show during a crash (while the real game runs
    // it and then waits on its last frame): the crash does not end, the rest
    // plays on. The crashed craft is put where its wreck is (the sprite
    // position the game would give it, VIC coordinates), so the camera
    // follows the wreck by its own rules; without one (it burst) the craft
    // stays inside the camera's still zone ($A64D), so the camera rests.
    void prolong_crash(const std::optional<std::array<int, 2>>& wreck_sprite) {
        if (!craft_crashing()) {
            return;
        }
        crash_timer_ = 2;
        if (wreck_sprite) {
            x_ = u8(std::clamp((*wreck_sprite)[0] / 2 + 0x24, 0, 255));
            y_ = u8(std::clamp((*wreck_sprite)[1] - 0x16, 0, 255));
        } else {
            // (One line above its lower edge: the crash still sinks it by
            // one every other frame.)
            x_ = std::clamp<u8>(x_, 0x6E, 0x82);
            y_ = std::clamp<u8>(y_, 0x92, 0xA5);
        }
    }
    int crash_frames_left() const { return crash_timer_; }

private:
    // --- frame -------------------------------------------------------------------
    void playfield_interrupt(); // $AE19
    void hud_interrupt();       // $9BD4
    void title_interrupt();     // $8AA2
    void main_loop();           // $8BB1
    void main_loop_pass();  // the rest of the pass at the cursor
    void run_pass_step();   // the step at the cursor
    void run_scheduled_steps(Steps);
    void pass_rest();       // $8BDB-$8C05: teleport, the mode's work, keys
    void render();
    void read_joystick(); // $B032
    void read_keyboard(); // $AFED

    // Long routines that wait in the main loop ($9B99).
    struct Task {
        enum Kind { none, level_build, life_start, bonus, sink, fort, teleport } kind{none};
        int stage{};
        int frames{};
        Mode mode{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(kind, stage, frames, mode); }
    };
    void wait(Task::Kind, int stage, int frames);
    void run_task();
    void continue_task(Task::Kind, int stage);

    // --- front end and keys --------------------------------------------------------
    void draw_title();          // $89C7
    void start_play_screens();  // $8B97
    void handle_keys();         // $92E2
    void options_key();         // $9351
    void draw_options();
    void pause_loop_check();    // $9333

    // --- game flow ---------------------------------------------------------------------
    void new_game();         // $8D6B
    void choose_gate_wall(); // $8DD3
    // A cheat that is on, when a player plays (never in the attract game).
    bool cheat(bool on) const { return on && play_kind_ != 0; }
    void enter_level();      // $8EDD
    void build_level();      // $8F8F
    void start_life();       // $8E11
    void finish_life_start(int stage);
    void debrief();          // $916E
    void collect_bonus();    // $9B81
    void level_exits();      // $8C08
    void after_bonus(int stage);
    void sink_step(int stage);
    void teleport();         // $9892
    void teleport_land();
    void fort_explosion();   // $97B0
    void fort_step(int stage);

    // --- screen ---------------------------------------------------------------------------
    void set_cell(int index, u8 code);
    void clear_play_area();   // $9AB3
    void clear_message_row(); // $9BBB
    void copy_hud();          // $9BC6
    void fill_colours(u8);    // $AFA0
    void draw_text(const data::Text&, int column, int text_row); // $9B56
    void draw_number(u8 bcd, int& at, int& digits_left, bool& started); // $A9D3
    void draw_counters();     // $A92F

    // --- craft, camera, bullets ---------------------------------------------------------
    void camera();        // $A64D
    void copy_window();   // $A72C
    void set_sprite_position(int sprite, u8 x, u8 y); // $B0CB
    void player_sprite(); // $A24B
    void update_player_cell(); // $A2B9
    void fire();          // $A5B4
    static u8 bank_direction(u8 bank);
    void bullets();       // $A332
    void terrain_contact(); // $A066
    int prisoner_in_reach(); // $ABDA
    void save_checkpoint(); // $A176
    void movement();      // $A4CE
    void level_bank();    // $A62F

    // --- enemy helicopter -----------------------------------------------------------------
    void radar_blip(u8 column, u8 row); // $A498
    void enemy_blip();                  // $A488
    void enemy_interrupt();             // $9C52
    void enemy_try_spawn();             // $9C6B
    void enemy_tick();                  // $9CDA
    void enemy_left();                  // $9DB9
    void enemy_right();                 // $9DE9
    void enemy_down();                  // $9E19
    void enemy_up(u8 difference, u8 x_register); // $A000
    u8 zero_page(u8 address) const;
    void enemy_visibility();            // $A1B0
    void enemy_sprite();                // $A2DE
    void enemy_watchdog();              // $B268

    // --- objects ---------------------------------------------------------------------------
    void object_blip(u8 column, u8 row); // $AD8B
    void add_score(u8 low, u8 middle);   // $AA37
    void mines();                        // $94D2
    void mine_step(int slot);
    void tank_blip(int i);               // $9919
    void tanks();                        // $992A
    void tank_step(int i);
    void restore_missile_cell(int i);    // $9706
    void missile_detonate(int i);        // $96EB
    void missiles();                     // $9644
    void missile_move(int i);            // $9724
    void prisoners();                    // $AABA
    void prisoner_removed(int i);        // $AB31
    void show_men_to_rescue();           // $AB3B
    void draw_depot(int column, int stage); // $ACD5
    void refuel();                       // $AC2B
    void radar_window();                 // $AD2E

    // --- characters redefined in place, sound ----------------------------------------------
    void barriers();       // $A7ED, $A830
    void laser_grids();    // $A86B
    void sweeping_walls(); // $A8B8
    void static_noise();   // $A8F3
    void shimmer();        // $AF54
    void sound_interrupt();  // $AE90
    void sound_off();        // $B11A
    void sound_engine_on();  // $B1D8
    void sound_explosion();  // $B189
    void sound_shot(u8 control); // $B1AB
    void sound_rescue(u8 frequency); // $B1FA
    void sound_missile();    // $B210

    // --- the map: 40 rows of 256 cells as the original addresses them -------------------
    u8 map_at(int index) const;
    void map_set(int index, u8 code);
    bool solid_glyph(u8 code) const;                         // $98F2 / $A034
    bool solid_at_player_column(int column, int row) const;  // $A031
    // --- carnage mode (carnage.cpp) ---
    // A carved cell: the code it had when first hit (it applies while the
    // map still holds that code there) and its pixels now.
    struct CarvedCell {
        u8 code{};
        c64::Glyph glyph{};
        bool painted{}; // a fallen tower painted in (joined by whole cells)

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(code, glyph, painted); }
    };
    bool carnage() const { return cheat(options_.carnage); }
    // The rock the gun carves: the brown rock and its edges.
    static bool carvable(u8 code) { return code == 0x4B || (code >= 0x61 && code <= 0x6B); }
    const c64::Glyph& cell_glyph_at(int index, u8 code) const;
    bool solid_cell(int index) const; // cell_glyph_at has a set pixel
    void carnage_fire();
    void move_tracers();
    void carnage_frame(); // tracers, loose rock, falling things
    // Hits by a tracer at map pixel (x, y); true when it stops there.
    bool tracer_hit(Tracer&, int x, int y);
    void carve(int x, int y, double radius, const Tracer&);
    void carve_cell(int index, int x, int y, double radius, const Tracer&, bool report);
    // A map pixel pair (pair column 0-1023, line 0-319) as falling things and
    // loose rock see it: open, carvable rock, or fixed (any other solid that
    // is part of the place: frames, depots, walls; not objects or beams).
    enum class Ground { open, rock, fixed };
    Ground ground_at(int pair_x, int y) const;
    void clear_pair(int pair_x, int y); // a rock pair goes (and its wrap copy)
    void set_cell_both(int index, u8 code); // a cell and its wrap copy
    void settle_rock();   // loose rock around this frame's craters falls
    void drop_tanks();    // driving tanks with no ground under them fall
    // A tank's home (its object column and row) has ground: always, unless
    // carnage mode shot it away.
    bool home_has_ground(int home_column, int home_row) const;
    bool structural_cell(int index) const;   // part of the place, not rock
    void move_falling();
    void add_falling(FallingBody);
    void land(FallingBody&);
    void blast(int x, int y, double radius); // a crater, its pixels as debris
    void hit_object(int index); // as a shot hits an object: the explosion char
    void break_emitter(int index);
    void sweep_beam(FallingBody&);
    bool object_pixel(int x, int y) const;
    void carnage_score(); // the frame's kills: combo, bonus, overdrive
    void carnage_impact(int strength) { frame_impact_ = std::max(frame_impact_, strength); }
    // Towers: ladders and mesh ($54-$58) standing three rows or more, not a
    // fuel depot.
    struct Tower {
        int column{}, row{}, width{}, height{}; // cells, contiguous copy
        double lean{}, spin{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(column, row, width, height, lean, spin); }
    };
    Tower* tower_at(int column, int row); // (and starts rocking it)
    void push_tower(Tower&, double x, double y, double dx);
    void rock_towers();
    bool tower_side_open(const Tower&, int side) const;
    void topple(const Tower&, int side);
    bool move_tower(FallingBody&); // true when it has come to rest
    void rest_tower(const FallingBody&);
    std::array<double, 2> tower_pixel(const FallingBody&, int c, int r, double sub) const;
    void paint_pair(int pair_x, int y, u8 pair);
    // Fuel depots: the one whose cells these are (its map column, the
    // first copy), or -1; damage; a blown one stays gone.
    int depot_at(int column, int row) const;
    int depot_near(int column) const; // the craft's map column over one
    void damage_depot(int depot, int hits);
    bool depot_gone(int depot) const; // (false for -1)
    u8 carnage_random(); // its own numbers: the game's stay as they were
    // Map pixels from the game's screen coordinates (as the shots use them).
    std::array<double, 2> world_from_screen(int x, int y) const;
    static int object_index(int column, int row) { return row * 256 + column - 5; } // $A7D8
    static int actor_index(int column, int row) { return row * 256 + column - 1; }  // $A7C3

    // A random number for the read at `site` (the original's address).
    u8 noise(std::uint16_t site); // $D41B

    // ===== state =========================================================================
    const data::Assets& assets_;
    const data::GameTables& tables_;
    Options options_{};
    std::uint32_t rng_{0x2545F491};
    std::function<u8(std::uint16_t)> noise_source_;
    // Measured: the original's loop takes 1.70-1.81 frames per pass in five
    // played runs (1.84 in the attract game, which has more shots).
    double pass_rate_{1.0 / 1.75}, pass_credit_{};
    // The main loop's pass as steps, in the original's order ($8BC3-$8BDB):
    // the next to run, and whether the one before still waits inside its
    // routine ($9B99), so the pass goes on after that.
    enum PassStep { mines_step, tanks_step, missiles_step, prisoners_step, radar_step,
                    refuel_step, fort_step_, exits_step, rest_step, pass_steps };
    int pass_step_{};
    bool step_waiting_{};
    bool step_scheduled_{}; // the step ran by the schedule, which also ends its wait
    // The next pass runs the objects: the loop restarts at once after a
    // pass and checks the mode then ($8BBD), not when its first step runs.
    bool play_pass_{};
    bool scheduled_{};
    Steps steps_early_{}, steps_before_{}, steps_after_{};
    Steps steps_pending_{};
    Task task_{};

    Mode mode_{Mode::title};
    u8 frame_{};      // $15
    u8 play_kind_{1}; // $50: 0 attract game, $FF attract about to start
    Settings settings_;
    int selected_{};   // $FA
    u8 idle_{};        // $42
    u8 title_timer_{}; // $29
    bool title_irq_{true};       // the title's interrupt runs instead of the game's
    bool main_loop_running_{};
    bool main_loop_starting_{};  // it starts with the next frame
    bool miss_interrupt_{};      // a redraw kept interrupts off past raster $F9

    Controls controls_{};
    u8 stick_{0x0F};   // $16: bits clear = pressed (up, down, left, right)
    bool fire_{};      // $17 == 0
    u8 fire_latch_{}; // $5B (cleared at power-on)
    FunctionKey key_{FunctionKey::none}; // $18
    std::optional<FunctionKey> last_key_; // $5A
    bool space_down_{}, space_event_{};   // $FF, $19
    Mode paused_mode_{Mode::playing};

    // From the options at a new game ($8D9F).
    u8 gravity_mask_{}, enemy_mask_{}, barrier_step_{}, last_mine_{}, tank_period_{},
        missile_period_{}, sweep_period_{};

    // Counters, in BCD as the original keeps them.
    std::array<u8, 3> score_{}, high_score_{};
    std::array<u8, 2> bonus_{}, fuel_{};
    u8 lives_{}, rank_{}, rescued_{}, men_left_{}, level_{};
    u8 fort_{2};         // $ED: 2 standing, 5 hit, 1 destroyed
    u8 barriers_off_{2}; // $EE: 1 once the fort is destroyed
    u8 engine_{9};       // $99: 8 running, 9 out of fuel, $0A refuelling
    u8 depot_stage_{}, depot_timer_{}; // $9A, $40
    u8 refuel_sound_{};  // $4C
    u8 teleport_grace_{}; // $4D
    u8 message_timer_{}; // $45
    u8 crash_timer_{};   // $3F
    u8 spawn_grace_{};   // $9C4E
    u8 level_colour_{};  // $9E
    u8 playfield_colour_{}; // $9F
    u8 shot_sound_{};    // $4A
    u8 explosion_sound_{}; // $4B
    u8 engine_pitch_{0x14}; // $49
    u8 engine_sound_timer_{}; // $FD
    u8 tone_timer_{};    // $AF53
    int sink_index_{};   // the landing pad's floor block
    // The work of the last level build, for its duration: random rock
    // draws and map cells the prisoner scan looked at.
    int rock_draws_{}, scanned_cells_{};
    int build_frames() const;

    // The craft ($64-$6A) and its checkpoint ($5D-$63).
    Craft craft_{Craft::inactive};
    u8 x_{}, y_{}, bank_{}, column_{}, row_{};
    struct Checkpoint {
        u8 camera_column, camera_row, fine_x, fine_y, x, y, bank;

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(camera_column, camera_row, fine_x, fine_y, x, y, bank); }
    } checkpoint_{};
    u8 attract_step_{}; // $51
    // The camera ($56-$59) and the playfield window ($11, $46, $13, $14).
    u8 camera_column_{}, camera_row_{}, fine_x_{}, fine_y_{};
    u8 copy_now_{}, copy_timer_{}, xscroll_{}, yscroll_{};
    bool interrupt_cut_short_{};
    // The VIC's collision latches, as read by the interrupt ($68, $73).
    u8 sprite_hits_{}, background_hits_{};
    u8 latched_sprite_hits_{}, latched_background_hits_{};
    // Sprites over any rock pixel, also those the VIC counts as background
    // (for the fixed enemy pilot).
    u8 graphics_hits_{}, latched_graphics_hits_{};

    struct Bullet {
        u8 state{}; // 0 free, 1-5 flying (direction), 7 splash
        u8 x{}, y{}, saved{}, column{}, row{}, timer{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(state, x, y, saved, column, row, timer); }
    };
    std::array<Bullet, 3> bullets_{}; // 0-1 the player's, 2 the enemy's

    struct Enemy {
        u8 state{1};  // $6E: 1 waiting, 3 hunting, 4 exploding
        u8 sprite{1}; // $6D: 1 hidden, 2 on screen
        u8 x{}, y{}, bank{}, column{}, row{}, sub_x{}, sub_y{};
        u8 timer{}, reload{}, watchdog{}, colour{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(state, sprite, x, y, bank, column, row, sub_x, sub_y, timer, reload, watchdog, colour);
        }
    } enemy_;
    u8 enemy_blip_on_{}; // $B297
    u8 rotor_bit_{};     // $2C

    struct Tank {
        u8 state{1}; // 1 gone, 2 driving, 4 exploding, 7 to appear
        u8 column{}, row{}, direction{1};
        std::array<u8, 3> saved{};
        u8 home_column{}, home_row{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(state, column, row, direction, saved, home_column, home_row);
        }
    };
    std::array<Tank, 6> tanks_{};
    u8 tank_countdown_{1}, tank_explosion_{}; // $F0, $41

    struct Missile {
        u8 state{1}; // 1 idle, 7 launching, 4 left, 8 right
        u8 column{}, row{}, fuel{}, saved{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(state, column, row, fuel, saved); }
    };
    std::array<Missile, 6> missiles_{};
    u8 missile_countdown_{1}, missiles_active_{}; // $F2, $4E

    struct Mine {
        u8 state{1}; // low nibble: 1 dormant, 2 flying, 7 to appear; bits 4-5 phase
        u8 column{}, row{}, saved_left{}, saved_right{}, direction{1};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(state, column, row, saved_left, saved_right, direction); }
    };
    std::array<Mine, 39> mines_{};
    u8 mine_cursor_{}; // $E8

    struct Prisoner {
        u8 state{1}; // 1 gone, 2 running, $0B boarding
        u8 column{}, row{}, motion{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(state, column, row, motion); }
    };
    std::array<Prisoner, 8> prisoners_{};
    u8 prisoner_cursor_{}; // $EA

    u8 barrier_a_{}, barrier_b_{0x80}; // $3D, $3E
    u8 sweep_timer_{1}, sweep_phase_{}, sweep_direction_{1}; // $8F, $8D, $8E
    bool life_kept_{}; // the infinite-lives cheat was on at the last crash
    std::unordered_map<int, CarvedCell> carved_; // by map index
    std::vector<Tracer> tracers_;
    int gun_cooldown_{};
    std::uint32_t carnage_seed_{0x9E3779B9};
    bool enemy_shot_down_{}; // a tracer hit the enemy helicopter
    std::vector<FallingBody> falling_;
    std::vector<FallingBody> born_; // came loose while move_falling ran
    bool moving_falling_{};
    std::vector<std::array<int, 2>> craters_; // this frame's, for settle_rock
    std::vector<std::array<int, 2>> size_held_; // rock held only by its size
    std::unordered_map<int, int> emitter_hits_; // by map index
    bool crushed_{}; // something fell on the craft
    std::vector<Tower> towers_; // rocking ones
    std::unordered_map<int, int> depot_hits_; // by depot_at's column
    CarnageState carnage_state_;
    int frame_kills_{}, frame_impact_{}; // carnage's own this frame
    bool enemy_was_down_{};
    // Carnage mode blew up the last level's depot, where the mission ends:
    // frames of the message left, then the craft goes down and the game
    // ends ("MISSION ABORTED").
    int doomed_timer_{};
    bool doomed_{};
    double glide_speed_{}, glide_step_{}; // carnage mode's momentum (game units)

    // The level map, its radar picture, and the character sets' run-time parts.
    std::array<u8, 40 * 256> map_{};
    data::Spill spill_terrain_{};
    std::array<std::array<u8, 40>, 40> radar_{};
    std::array<c64::Glyph, 128> glyphs_{};      // the playfield set
    std::array<c64::Glyph, 36> radar_glyphs_{}; // HUD characters $5C-$7F

    // The screen: characters and colours, and the VIC's settings.
    std::array<u8, 1000> screen_{};
    std::array<u8, 1000> colour_ram_{};
    struct Display {
        u8 shared1{2}, shared2{1}, background{}, border{};
        u8 xscroll{}, yscroll{3};
        bool rows25{true}, columns40{true};
        bool playfield_charset{};
        u8 sprites{1};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(shared1, shared2, background, border, xscroll, yscroll, rows25, columns40, playfield_charset,
               sprites);
        }
    };
    Display vic_{};                  // the registers now
    Display top_{}, split_{}, lower_{}; // as they apply to this frame's parts
    bool split_layout_{};            // $12 = 2: the playfield part is wanted
    bool playfield_part_installed_{}; // and set up for this frame
    bool split_active_{};
    struct SpriteRegister {
        int x{}, y{};
        u8 pointer{}, colour{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, pointer, colour); }
    };
    std::array<SpriteRegister, 5> sprite_regs_{};

    c64::Sid sid_;
    c64::Picture picture_{};

    // The last picture's description, for presentation layers.
    c64::TextFrame shown_frame_;
    std::array<u8, 1000> shown_screen_{};
    MapCell window_{}, shown_window_{};
    // The playfield window with the widescreen view's columns around it,
    // copied with the window ($A72C).
    std::array<u8, 16 * wide_window_columns> wide_window_{}, shown_wide_window_{};
    WideEnemy wide_enemy_{}, shown_wide_enemy_{};
    Happenings happenings_;
    int depot_shown_stage_{4};
    SinkView sink_view_{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(options_, rng_, pass_rate_, pass_credit_, pass_step_, step_waiting_, step_scheduled_, play_pass_,
           scheduled_, steps_early_, steps_before_, steps_after_, steps_pending_, task_, mode_, frame_,
           play_kind_, settings_, selected_, idle_, title_timer_, title_irq_, main_loop_running_,
           main_loop_starting_, miss_interrupt_, controls_, stick_, fire_, fire_latch_, key_, last_key_,
           space_down_, space_event_, paused_mode_, gravity_mask_, enemy_mask_, barrier_step_, last_mine_,
           tank_period_, missile_period_, sweep_period_, score_, high_score_, bonus_, fuel_, lives_, rank_,
           rescued_, men_left_, level_, fort_, barriers_off_, engine_, depot_stage_, depot_timer_,
           refuel_sound_, teleport_grace_, message_timer_, crash_timer_, spawn_grace_, level_colour_,
           playfield_colour_, shot_sound_, explosion_sound_, engine_pitch_, engine_sound_timer_, tone_timer_,
           sink_index_, rock_draws_, scanned_cells_, craft_, x_, y_, bank_, column_, row_, checkpoint_,
           attract_step_, camera_column_, camera_row_, fine_x_, fine_y_, copy_now_, copy_timer_, xscroll_,
           yscroll_, interrupt_cut_short_, sprite_hits_, background_hits_, latched_sprite_hits_,
           latched_background_hits_, graphics_hits_, latched_graphics_hits_, bullets_, enemy_, enemy_blip_on_,
           rotor_bit_, tanks_, tank_countdown_, tank_explosion_, missiles_, missile_countdown_,
           missiles_active_, mines_, mine_cursor_, prisoners_, prisoner_cursor_, barrier_a_, barrier_b_,
           sweep_timer_, sweep_phase_, sweep_direction_, life_kept_, carved_, tracers_, gun_cooldown_,
           carnage_seed_, enemy_shot_down_, falling_, born_, moving_falling_, craters_, size_held_,
           emitter_hits_, crushed_, towers_, depot_hits_, carnage_state_, frame_kills_, frame_impact_,
           enemy_was_down_, doomed_timer_, doomed_, glide_speed_, glide_step_, map_, spill_terrain_, radar_,
           glyphs_, radar_glyphs_, screen_, colour_ram_, vic_, top_, split_, lower_, split_layout_,
           playfield_part_installed_, split_active_, sprite_regs_, sid_, picture_, shown_frame_,
           shown_screen_, window_, shown_window_, wide_window_, shown_wide_window_, wide_enemy_,
           shown_wide_enemy_, happenings_, depot_shown_stage_, sink_view_);
    }
};

} // namespace fort_apocalypse::game
