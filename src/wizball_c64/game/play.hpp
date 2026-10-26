#pragma once
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/game/display.hpp"
#include "wizball_c64/game/keyboard.hpp"
#include "wizball_c64/game/objects.hpp"
#include "wizball_c64/game/playfield.hpp"
#include "wizball_c64/game/random_source.hpp"
#include "wizball_c64/game/sound.hpp"
#include "wizball_c64/game/catellite.hpp"
#include "wizball_c64/game/wiz.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

// A game in progress: players, scores, the place being played and
// everything on the play screen. One pass of the original's main loop
// ($640E) is one call of pass(); the work its raster interrupts do once a
// frame is frame(). The parts not rebuilt yet (enemies, shots, the
// catellite, tokens, sound) are left out; see PLAN.md.
namespace wizball::game {

// The two control ports as the game reads them (bits low while pressed:
// 0 up, 1 down, 2 left, 3 right, 4 fire).
struct Ports {
    std::uint8_t port2{0xFF}; // $DC00 ($B256)
    std::uint8_t port1{0xFF}; // $DC01 ($B257)
    KeyMatrix keys{};         // the keyboard ($7429 reads three of its rows)
};

// A per-player score: six shown digits, the last always 0 (points count in
// tens), and the digit above them that catches an overflow ($BFDA + 7p).
struct Score {
    std::array<std::uint8_t, 7> digits{}; // [0] overflow, [1..6] shown
};

// An entry of a place's spawn list ($9F9B): the view column at which it
// comes, the kind of thing, and whether it has come already (bit 7).
struct SpawnEntry {
    std::uint8_t column{}, kind{};
};

class Play {
public:
    // The sound lives on between games (the front end's tunes), so it is the caller's.
    Play(const data::Assets& assets, const data::GameImage& image, RandomSource& random, Sound& sound);

    void new_game(int mode);   // $70D3, $6830 (mode: $B272, 0 = one player)
    // The same with what the front end holds: the start place ($AF7E), the
    // top score ($BFEB) and the cheat ($BB07).
    void new_game(int mode, int start_place, const std::array<std::uint8_t, 6>& top_score, bool cheat);
    // $6965: the port whose fire started the life plays (one-player games).
    void set_port(bool port1) { port_select_ = port1 ? 0x10 : 0; }
    // RUN/STOP paused the game ($6EB7); Q in the pause ends it ($6ED7).
    bool paused() const { return paused_; }
    std::uint8_t shot_volume() const { return shot_volume_; } // $BA81 outlives the game
    void set_shot_volume(std::uint8_t v) { shot_volume_ = v; }
    bool quit() const { return quit_; }
    // $69F2: the last life of the player has gone: "GAME OVER" over the
    // play screen until the tune ends, fire, or 1000 frames.
    void start_game_over();
    bool game_over_shown() const { return stage_ == Stage::game_over; }
    bool game_over_done() const { return game_over_done_; }
    std::uint8_t stick_edge() const { return edge_last_; }
    void set_stick_edge(std::uint8_t v) { edge_last_ = v; }
    // Scores and lives for the front end.
    int player() const { return player_; }
    std::uint8_t control(int player) const { return controls_[std::size_t(player - 1)]; }
    bool one_player() const { return controls_[2] == 1; }
    int furthest() const { return furthest_; }
    std::array<std::uint8_t, 6> score_digits(int player) const {
        std::array<std::uint8_t, 6> d{};
        for (int i = 0; i < 6; ++i) d[std::size_t(i)] = scores_[std::size_t(player)].digits[std::size_t(i + 1)]; // $BFDB on
        return d;
    }
    // The title's colour run, which the lab and the game over move on.
    struct RightRun {
        std::uint8_t clock, index;
        std::array<std::uint8_t, 13> colours;
    };
    RightRun right_run() const { return {colour_run_clock_, colour_run_index_, colour_run_}; }
    void set_right_run(const RightRun& r) {
        colour_run_clock_ = r.clock;
        colour_run_index_ = r.index;
        colour_run_ = r.colours;
    }
    void start_life();         // $6E42, $63CA
    void begin_play();         // $70EC, $63E0
    void frame();              // the raster interrupts' work ($7269, $67A1, $8B9A)
    // One pass: part A (scroll to cauldrons) and part B (catellite, spawns,
    // objects, timer); the original's interrupts of a frame fall between.
    void pass_a(const Ports& ports);
    void pass_b();
    // Some sequences run frame by frame inside part A in the original (a
    // tunnel's place change waits for the raster at $6E92 88 times): part A
    // then stops with waiting() true, and resume() does one frame's work;
    // when the sequence ends, the rest of part A runs and part B may follow.
    bool waiting() const { return arrival_.has_value() || pending_ != Pending::none; }
    void resume(const Ports& ports);
    // One pass of whichever loop the original is in (the play loop $640E,
    // the place's end $64CF, the bonus wave $6576, the Wizlab $AB7B), or
    // one frame of a sequence while waiting().
    void step(const Ports& ports);
    // One frame's main-loop work for the game module: step(), plus the
    // first frame of the place's end, which the original runs in the same
    // frame as the pass that finds the colour completed.
    void pass(const Ports& ports);
    // Checks against the original: called where the original passes an
    // address its run is logged at ($6E82, $6E91, $7DF4, $8640, $682F, $6E92, $654E, $6573, $AB58, $AB5B, $7DBE), so that
    // the recorded interrupts can be replayed in between.
    void set_marker_hook(std::function<void(std::uint16_t)> hook) { marker_ = std::move(hook); }
    enum class Stage { play, vanish, bonus, lab, flight, round_end, game_over };
    Stage stage() const { return stage_; }
    bool life_over() const { return stage_ == Stage::play && dying_ != 0 && death_clock_ == 1; }
    // $6E38, $6B27: the player's progress kept, a life gone, the next player.
    // False when the game is over.
    bool end_life();

    // `extension`: widescreen side columns (an enhancement; 0 = as the C64).
    // `blackout_fade`: while the landscape is blacked out, its shared
    // colours this many C64 brightness steps darker instead of off (an
    // enhancement fading it after the wiz's bump; 0 = as the C64).
    Display picture(int extension = 0, int blackout_fade = 0) const;
    bool field_dark() const { return field_dark_; } // $8A02: the blackout hides the landscape now

    // The frame counters behind the colour cycles run from power-on in the
    // original; checks against it start them where it had them.
    struct Clocks {
        std::uint8_t hud_clock, hud_flash, panel_clock, panel_flash;
        // The title's colour run ($98BE), which the lab's words reuse.
        std::uint8_t run_clock{}, run_index{};
        std::array<std::uint8_t, 13> run{};
    };
    void set_clocks(const Clocks& c) {
        hud_clock_ = c.hud_clock;
        hud_flash_ = c.hud_flash;
        panel_clock_ = c.panel_clock;
        panel_flash_ = c.panel_flash;
        colour_run_clock_ = c.run_clock;
        colour_run_index_ = c.run_index;
        colour_run_ = c.run;
    }

    // State, also for checks against the original.
    const Playfield& playfield() const { return playfield_; }
    const Wiz& wiz() const { return wiz_; }
    const Catellite& catellite() const { return catellite_; }
    // Pearls waiting to move the token light on ($B259); for tests and cheats.
    std::uint8_t pearls() const { return pearls_; }
    void give_pearls(std::uint8_t n) { pearls_ = n; }
    void give_paint(int colour, std::uint8_t n) { colour_[std::size_t(colour)] = n; }
    void give_painted(int place, std::uint8_t n) { painted_[std::size_t(place)] = n; }
    std::uint8_t painted(int place) const { return painted_[std::size_t(place)]; }
    void set_invincible(bool on) { invincible_ = on; } // the cheat's flag ($BB07)
    void set_infinite_lives(bool on) { infinite_lives_ = on; } // an option: a lost life is not counted
    // An option with the invincibility cheat (the game's own cheat leaves
    // the catellite open to hits): hits on the catellite are not counted.
    void set_catellite_protected(bool on) { catellite_protected_ = on; }
    // The automatic catellite (an option, not the original game): a second
    // player steers it, as in the team modes, so fire stays the wiz's.
    void set_catellite_ai(bool on) { catellite_ai_ = on; }
    // Carnage mode (not the original game, carnage.cpp): fire is a rapid
    // paint gun whose shots carve the landscape and splash it with paint.
    void set_carnage(bool on) { carnage_ = on; }
    bool carnage() const { return carnage_; }
    // How hard carnage hit since the last call (0-3: hit-stop and shake for
    // the picture), and the arcade layer's state.
    int take_carnage_impact() {
        const int i = carnage_impact_;
        carnage_impact_ = 0;
        return i;
    }
    struct CarnageScore {
        int combo{}, combo_timer{}; // kills in a row, passes left to add one
        float meter{};              // the overdrive meter, 0-1
        int overdrive{};            // passes left
    };
    const CarnageScore& carnage_score() const { return carnage_score_; }
    std::size_t carnage_bodies() const { return bodies_.size(); } // falling now (for tests)
    void give_points(int tens) { add_points(tens); } // for tests
    // For effect previews: the landscape blacked out (as a black droplet caught, $88B5).
    void blackout_for_test() {
        flash_colours_ = true;
        flash_show_ = 1;
        fill_field_colours();
    }
    // For effect previews: into a tunnel where the wiz is (as $81C9 on a mouth).
    void tunnel_for_test() { enter_tunnel(-1); }
    // For effect previews: a hit on the catellite (as an alien's shot gives).
    void hit_catellite() { ++catellite_hit_; }
    // For effect previews: an alien that drops paint when hit (state 1).
    void make_dropper(int slot) {
        auto& o = objects_[std::size_t(slot)];
        if (!o.active() || o.state != 0) return;
        o.state = 1;
        o.kind = std::uint8_t(slot % 3 == 0 ? 2 : slot % 3 == 1 ? 5 : 6); // paint colours
    }
    bool catellite_on() const { return powers_.catellite; }
    int place() const { return place_; }
    std::uint8_t levels_done() const { return levels_done_; }
    std::uint8_t rounds() const { return rounds_; }
    const Score& score(int player) const { return scores_[std::size_t(player)]; }
    std::uint8_t aliens_left() const { return aliens_left_; }
    const std::array<std::uint8_t, 4>& cauldron_rows() const { return cauldron_clock_; }
    std::uint8_t level_cleared() const { return level_cleared_; } // $B874
    const std::vector<SpawnEntry>& spawn_list(int slot) const { return spawn_lists_[std::size_t(slot)]; }
    // An enemy kind's template ($3ECF table): 23 bytes copied into an
    // object, then random fields (AND mask, add, field). $A070 re-rolls a
    // few of its bytes for each spawn list it is put in, for good.
    struct Template {
        std::array<std::uint8_t, 0x17> bytes{};
        std::vector<std::array<std::uint8_t, 3>> random;
    };
    const std::array<Template, 18>& templates() const { return templates_; }
    std::uint8_t sprite_mask() const { return sprite_mask_; } // $E7D9
    const std::array<std::uint8_t, 2>& sprite_shared() const { return sprite_shared_; } // $E7E9/$E7EA
    std::uint8_t wiz_colour() const { return wiz_colour_; }             // $E7EB
    std::uint8_t shield() const { return shield_; }                     // $7F06
    // The place change's tunnel exit: 0 none, 1 rising or sinking, 2 the
    // wiz sliding out (at `arrival_x()`, sprite x).
    int arrival_phase() const {
        if (!arrival_) return 0;
        return arrival_->stage == Arrival::Stage::slide ? 2 : 1;
    }
    int arrival_x() const { return arrival_ ? arrival_->slide_x : 0; }
    // The Wizlab: the weapon taken (its token, 1-5; 0 not yet).
    int chosen_weapon() const { return in_lab_ && weapon_chosen_ ? chosen_weapon_ : 0; }
    // The token bar's sprite n ($89BB, table $B4C6).
    std::array<int, 2> token_position(int n) const {
        return {image_.byte(std::uint16_t(0xB4C6 + 2 * n)) | (n == 6 ? 0x100 : 0), image_.byte(std::uint16_t(0xB4C7 + 2 * n))};
    }
    // The level-complete flight's text sprites 2-7 ($AE5F, table $B4E5).
    std::array<int, 2> flight_text_position(int n) const {
        return {image_.byte(std::uint16_t(0xB4E5 + 2 * n)), image_.byte(std::uint16_t(0xB4E6 + 2 * n))};
    }
    // A cauldron's panel sprite position (the panel's table at $B505).
    std::array<int, 2> cauldron_position(int c) const {
        static constexpr std::uint16_t at[] = {0xB507, 0xB509, 0xB50D, 0xB50F};
        return {image_.byte(at[c]) | (c == 3 ? 0x100 : 0), image_.byte(std::uint16_t(at[c] + 1))};
    }
    // The wizspray's rings ($8592): sprite numbers (5: none) and tint.
    std::array<std::uint8_t, 3> spray() const { return {spray_top_, spray_bottom_, spray_tint_}; }
    std::uint8_t flash_background() const { return flash_background_; } // $8A44's counter
    struct ObjectSprite {
        int x{}, y{};
        std::uint8_t colour{}, pointer{};
        bool multicolour{};
    };
    const ObjectSprite& object_sprite_of(int slot) const { return object_sprites_[std::size_t(slot)]; }
    bool in_lab() const { return in_lab_; }
    bool hung() const { return hung_; }
    const std::array<std::uint8_t, 9>& kept_weapons() const { return retained_; }
    bool flying_text() const { return flying_text_; }
    std::uint8_t flight_colour() const { return flight_colour_; }
    std::array<std::uint8_t, 2> colour_run_clocks() const { return {colour_run_clock_, colour_run_index_}; }
    struct LabSprite {
        int x{}, y{};
        std::uint8_t pointer{}, colour{};
    };
    const LabSprite& lab_sprite(int n) const { return lab_sprites_[std::size_t(n)]; } // sprites 2-4
    const std::array<Object, 6>& objects() const { return objects_; }
    const std::array<PlayerShot, 6>& shots() const { return shots_; }
    const std::array<EnemyShot, 16>& enemy_shots() const { return enemy_shots_; }
    std::uint8_t cursor() const { return std::uint8_t(cursor_ * 2); } // $B1AE
    std::uint8_t dying() const { return dying_; }
    std::uint8_t death_clock() const { return death_clock_; }
    std::uint8_t lives(int player) const { return lives_[std::size_t(player)]; }
    std::uint8_t token_selected() const { return token_selected_; }
    std::uint8_t cell_colour(int column, int row) const { return cell_colours_[std::size_t(row * 40 + column)]; }
    const std::array<std::uint8_t, 4>& colour() const { return colour_; } // $B12B-$B12E
    SpriteShape sprite(int pointer) const { return sprite_shape(pointer); }
    Stick wiz_stick() const { return wiz_stick_; }
    Sound& sound() { return *sound_; }
    const Sound& sound() const { return *sound_; }
    // A copied game (a quick save) plays on its own front end's sound.
    void rebind_sound(Sound& sound) { sound_ = &sound; }
    std::uint8_t fire_held() const { return fire_held_; }
    std::array<std::uint8_t, 3> waggle() const { return {waggle_mask_, waggle_idle_, waggles_}; }

private:
    struct CauldronImage {
        std::array<std::array<std::uint8_t, 3>, 21> rows{};
    };
    void new_spawn_list(int place);       // $9F9B
    void first_spawn_list(int place);     // $A0EC
    void prepare_kind(int kind, int place); // $A070: re-roll a template
    void count_aliens();                  // $9ECF (and $9EFD)
    void open_next_wave();                // $9EFD
    void add_points(int tens);            // $710B
    void show_scores();
    void show_lives();                    // $7D5E
    void update_cauldrons();              // $9E0D
    void set_up_place();                  // $776D / $77C6 + $7D1A
    void draw_place_digit();              // $770D
    Charset play_charset() const;
    void put_side_column(Display& d, int column) const;
    SpriteSettings catellite_sprite_settings() const;

    void wiz_routine();
    void marker(std::uint16_t pc) {
        if (marker_) marker_(pc);
    }
    void latch_sprites() { shown_sprites_ = {sprite_mask_, catellite_.sprite(), object_sprites_}; }
    // The end of a place (clear.cpp).
    void load_bonus_templates();
    void level_end_drift();        // $7F0B
    void grant_flight();           // $7379 twice
    void start_vanish();           // $646F
    void vanish_frame();           // $64CF
    void bonus_setup_step();       // $6512-$6573
    void bonus_frame(const Ports& ports); // $6576
    void bonus_speed();            // $6755
    void wave_script();            // $9CEA
    void messages();               // $6615
    void count_kills();            // $6693
    void show_message();           // $66FC
    void print_text(std::uint16_t address, std::uint8_t colour); // $8CFA
    void start_lab();              // $AB20
    void lab_frame(const Ports& ports); // $AB7B
    // The lab (lab.cpp).
    void rearm_weapons(bool lab);  // $6E4F, $B002
    void choose_weapon(const Ports& ports); // $B076
    void perch_catellite();        // $AB9D
    void lab_wiz();                // $AC4F
    void leave_lab();              // $AD98
    void after_lab();              // $ADEF
    void flight_frame(const Ports& ports); // $AE9C
    void set_furthest();           // $A9C6
    void reset_spawn_lists();      // $9F5F
    void clear_for_lab();          // $AAC7
    void draw_room(int room);      // $75EA for the lab (9) and the round's end (10)
    void lab_settings();           // $AB02
    void start_round_end();        // $AEFD, $AF20
    void read_keys(const Ports& ports); // $7429
    bool pause_check();            // $6EB7: true while paused
    void volume_keys();            // $BA82
    void game_over_frame(const Ports& ports); // $6A94
    void end_game_over();          // $6ABE
    void step_colour_run();        // $98BE
    void round_end_step();         // $AF59
    // The level complete (flight.cpp).
    std::uint8_t& sprite_memory(int pointer, int offset);
    void draw_sprite_text(std::uint16_t address, int first_row, int first_sprite, bool big); // $8DE4, $8E32
    void flight_colours();         // $B12F
    void start_flight();           // $AE11
    void flight_wait_step();       // $AE8E, $AEC1
    void finish_flight();          // $AECA
    void catellite_body();         // $8652
    void pass_a_rest(const Ports& ports); // part A after the wiz routine ($6433 on)
    void pass_a_body(const Ports& ports); // part A after the pause check ($6422 on)
    void bonus_frame_rest(const Ports& ports); // $658A on
    void enter_place();            // $75E0: the place's first screen, characters and colours
    // Tunnels and the place change.
    void enter_tunnel(int direction); // $81C9
    void sink_step();              // $8216
    void start_arrival();          // $8239-$82DE
    void arrival_frame();          // one frame of $830A, $8334, $82E1
    void remove_objects_for_place(); // $A1C9
    void update_catellite();       // $8640
    void catch_with_catellite();   // $88B5
    void catellite_fire();         // $881F
    void read_sticks(const Ports& ports);

    // objects.cpp
    std::uint8_t table_byte(std::uint16_t address) const;
    void spawn(int slot, int kind, int list_slot, int entry);
    void spawn_template(int slot, const Template& t); // $9DA1
    int free_slot() const;
    void spawn_from_list();
    void spawn_filler();
    void level_timer();
    void update_objects();
    void update_object(int s);
    void move_object(Object& o);
    void scroll_object(Object& o);
    void animate_object(Object& o);
    void bounds_object(int s);
    void object_fire(int s);
    bool left_screen(int s);
    void remove_object(int s);
    void object_sprite(int s);
    void object_box(Object& o);
    int object_at(int x, int y);
    bool resolve_hit(int s);
    void reduced_pass(int s);
    void killed(int s);
    void explode(int s);
    void dying(int s);
    void start_dropping(int s);
    void dropping(int s);
    void splashing(int s);
    void drop_pearl(int s);
    void pearl(int s);
    void enemy_fire(int s, int direction);
    void erase_enemy_shots();
    void move_enemy_shots();
    // combat.cpp
    std::uint8_t field_colour() const;
    void set_cell_colour(int column, int row, std::uint8_t colour);
    void restore_cell(int screen, int column, int row, std::uint8_t code);
    void fill_field_colours();
    void erase_player_shots();
    void fire_shots();
    void launch_shots();
    void launch_blazers(int column, int row);
    void draw_player_shots();
    void wiz_touches();
    void shield_and_hits();
    void spray();
    void start_death();
    void death_step();
    void tokens();
    void activate_token();
    SpriteShape sprite_shape(int pointer) const;

    const data::Assets& assets_;
    const data::GameImage& image_;
    RandomSource& random_;
    Playfield playfield_;
    Wiz wiz_;
    Powers powers_;
    Sound* sound_; // the caller's (the front end's)
    std::uint8_t hum_clock_{}; // $7DF3

    // Players ($BFD6-$BFD9, $B277).
    std::array<std::uint8_t, 3> controls_{};
    int player_{1};
    std::array<std::uint8_t, 3> lives_{};
    std::array<Score, 3> scores_{};
    std::array<std::uint8_t, 6> high_score_{};
    bool new_high_{};

    // What a player takes from life to life ($B116-$B12D, saved at $E3C0
    // and $E3D8 per player).
    struct Progress {
        std::array<std::uint8_t, 9> retained{};
        std::array<std::uint8_t, 10> painted{};
        std::uint8_t place{}, levels_done{}, rounds{};
        std::array<std::uint8_t, 3> colour{};
    };
    std::array<Progress, 3> saved_{};
    void save_progress();
    void restore_progress();

    // Places ($B11E-$B12E, $B128, $B1CE).
    std::array<std::uint8_t, 10> painted_{}; // $B11F + place: 0-3 colours applied
    std::array<std::uint8_t, 9> retained_{}; // $B115-$B11D (permanent weapons)
    int place_{};
    int furthest_{};    // $B1CE
    std::uint8_t levels_done_{}; // $B129: levels flown through (0: the first level's spawn rules)
    std::uint8_t rounds_{};      // $B12A: rounds completed (enemies fire faster after the first)
    std::array<std::uint8_t, 4> colour_{};    // $B12B-$B12E: collected paint, [3] = total
    std::array<std::uint8_t, 10> slot_of_place_{}; // $B19D
    std::array<std::vector<SpawnEntry>, 4> spawn_lists_{}; // slots 1-3
    std::uint8_t aliens_left_{}; // $B1AF
    std::uint8_t level_cleared_{}; // $B874
    std::uint8_t cleared_at_loop_top_{}; // $8309: $B874 as the play loop last saw it
    std::array<Template, 18> templates_{};

    // Colours ($B27D-$B27F, $B51A).
    std::uint8_t shared1_{}, shared2_{}, character_{}, panel_extra_{};

    // Screen bits.
    std::uint8_t sprite_mask_{};   // $E7D9
    std::uint8_t token_selected_{}; // $B258
    std::array<std::uint8_t, 8> token_pointer_{}; // $EFF8-$EFFF
    std::array<std::uint8_t, 7> token_colour_{};  // $B4D6-$B4DC
    std::uint8_t token_flash_clock_{}, token_flash_{}; // $B25A, $B25B
    std::uint8_t hud_clock_{}, hud_flash_{};           // $B88F, $B890
    std::uint8_t panel_clock_{}, panel_flash_{}, panel_flash_base_{}; // $770A, $770B, $770C
    std::uint8_t panel_colour0_{};                     // $B515
    // What the raster interrupts showed this frame: they set the sprites
    // before stepping the colour cycles ($8B78 before $8B9A; $89D2 before $7269).
    std::uint8_t panel_colour_shown_{};
    std::array<std::uint8_t, 7> token_colour_shown_{};
    std::uint8_t waggle_mask_{4}, waggle_idle_{}, waggles_{}; // $723A-$723C
    std::uint8_t flash_background_{}; // $B25C
    std::array<std::uint8_t, 4> cauldron_clock_{}; // $9EC6-$9EC9
    std::array<CauldronImage, 4> cauldrons_{};    // sprites $B9-$BC
    std::array<std::uint8_t, 63> place_digit_{};  // sprite $A0
    bool letters_inverted_{true}; // $F100: the front-end letters' state
    std::uint8_t fire_held_{}, port_select_{}, catellite_fire_{}; // $B242, $B240, $B241
    Stick wiz_stick_{}, catellite_stick_{};

    // Objects and shots.
    std::array<Object, 6> objects_{};
    std::array<ObjectSprite, 6> object_sprites_{};
    // What the line-48 handler ($8A52) copies into the VIC: part B (the
    // catellite and the objects) runs after line 48, so the playfield
    // sprites on screen are those of the pass before; the wiz's, from
    // part A, are current. Latched at the start of part B.
    struct ShownSprites {
        std::uint8_t mask{};
        Catellite::Sprite catellite{};
        std::array<ObjectSprite, 6> objects{};
    };
    ShownSprites shown_sprites_{};
    std::array<PlayerShot, 6> shots_{};
    std::array<EnemyShot, 16> enemy_shots_{};
    std::array<std::uint8_t, 40 * Playfield::rows> cell_colours_{}; // colour RAM of the playfield rows
    int cursor_{};                 // $B1AE (in entries)
    int landscape_slot_{};         // $B1B3
    std::uint8_t beeping_{}, beeping_last_{}, beep_clock_{}, beep_effect_{0x0B}; // $A9A7, $A9A8, $A9A5, $A9A6
    std::uint8_t kills_{}, pearl_kills_{}, pearl_kill_limit_{9}, pearl_count_{}; // $A858, $A9A9, $A9AA, $A763
    std::array<std::uint8_t, 6> special_at_{};  // $A76C-$A771
    std::uint8_t timer_ticks_{}, timer_seconds_{}; // $A1B7, $A1B8
    std::uint8_t filler_burst_{}, filler_burst_clock_{}; // $A133, $A134
    bool shots_full_{};            // $B281
    std::uint8_t shot_half_{};     // $B280
    std::uint8_t wiz_hit_{}, catellite_hit_{}; // $7F0A, $89BA (hits waiting)
    std::uint8_t dying_{}, death_clock_{}; // $B1CF, $B1D0
    std::uint8_t pearls_{};        // $B259
    std::uint8_t clear_sequence_{}; // $6733
    bool flash_colours_{};         // $8959
    bool fire_pressed_{};          // $B23D
    std::uint8_t fire_latch_{};    // $B23C
    std::uint8_t beam_side_{}, blazer_set_{}; // $B23A, $B23B
    std::uint8_t spray_clock_{}, spray_step_{}, spray_colour_{}, spray_top_{5}, spray_bottom_{5}, spray_tint_{}; // $B8DB-$B8E0
    std::uint8_t shield_{}, shield_clock_{}, shield_warned_{}; // $7F06, $7F07, $7F09
    std::uint8_t shield_blink_{};  // $7F08
    std::uint8_t wiz_colour_{5};   // $E7EB
    Catellite catellite_;
    // The place change's sprites: the tunnel opening (sprites 1 and 2,
    // $B5 and $B7, stretched) and the wiz sliding out of it (sprite 3).
    struct Arrival {
        enum class Stage { rise_open, settle_open, slide, rise_close, settle_close } stage{};
        int count{};
        std::uint8_t offset{}; // $BB02 (the sound's last-effect byte, borrowed as a counter)
        int slide_x{};         // $E7CA
        bool back_to_play{};   // from the lab: the play loop starts over ($AE0C)
    };
    std::optional<Arrival> arrival_;
    bool finished_arrival_back_to_play_{};
    // Frame waits in set-ups other than the tunnel's.
    enum class Pending { none, bonus_dark, bonus_colours, flight_intro, flight_outro, round_end };
    Pending pending_{Pending::none};
    int pending_count_{};
    int pending_inner_{};
    Stage stage_{Stage::play};
    std::function<void(std::uint16_t)> marker_;
    // The end of a place.
    std::array<Template, 20> bonus_templates_{};   // $39DC
    std::array<std::uint8_t, 256> wave_script_{}; // $ED02: the copy the script runs from (its counts are used up)
    std::uint8_t wave_at_{}, wave_wait_{};          // $BB06, $BB05
    std::uint8_t bonus_speed_{}, bonus_speed_fraction_{}, accelerating_{}; // $B1F6, $B1F7, $B1F8
    std::uint8_t message_state_{}, message_clock_{}, message_id_{}; // $6733, $6734, $6735
    std::uint8_t message_flash_{}, message_colour_{};               // $6737, $6738
    std::uint8_t message_chars_{0xA0};                              // $6732: added to the text's codes
    std::uint8_t kills_counted_{}, kill_toggle_{};                  // $A859, $A85A
    std::array<std::uint8_t, 3> kill_digits_{0x30, 0x30, 0x30};    // $B841-$B843 (inside the message text)
    std::uint8_t bonus_tune_toggle_{};                              // $BB0C
    bool font_in_field_{};                                          // $6743: the font is over characters $A0-$DF
    Charset field_font_{};
    bool in_lab_{};                                                 // $AC4E
    // The lab.
    std::uint8_t lab_return_place_{};                               // $7A
    Powers saved_powers_{};                                         // $B228-$B230
    std::array<std::uint8_t, 6> saved_icons_{};                     // $B21E-$B223
    std::uint8_t saved_token_{};                                    // $B227
    bool weapon_chosen_{};                                          // $B10C
    std::uint8_t chosen_weapon_{};                                  // $B075
    std::uint8_t weapon_select_{};                                  // $B074: the bar shows the weapons (quiet, other colours)
    std::uint8_t stick_last_{};                                     // $944B
    bool hung_{};                                                   // $B0D2: the original's endless loop
    KeyLatch keys_{};                                               // $B254/$B255
    bool paused_{}, quit_{};                                        // $6EB7, $6ED7
    bool game_over_done_{};
    std::uint8_t edge_last_{};                                      // $B270 (the front end's stick edge)
    std::uint8_t game_over_timer_{}, game_over_clock_{};            // $B25F, $B260
    bool columns40_{};                                              // $AB9B = 8
    std::array<std::uint8_t, 2> sprite_shared_{0x0B, 0x0D};        // $E7E9/$E7EA
    std::uint8_t perch_state_{}, perch_clock_{}, perch_step_{};     // $AC4D, $AC41, $AC42
    std::uint8_t lab_wiz_state_{}, lab_clock_{}, lab_step_{}, lab_tick_{}, lab_wait_{}; // $B185, $B17A, $B17B, $B186, $B187
    std::array<std::uint8_t, 3> paint_left_{};                      // $B188-$B18A
    std::array<LabSprite, 3> lab_sprites_{};                        // sprites 2-4: the perch, the pot, the pour
    std::array<std::array<std::uint8_t, 16>, 2> hud_text_{};        // over the score lines' columns 12-27
    bool hud_text_shown_{};
    std::uint8_t colour_run_clock_{}, colour_run_index_{}; // $EBB3, $EBB8
    std::array<std::uint8_t, 13> colour_run_{};            // $EBD7-$EBE3
    std::optional<std::uint8_t> hud_band_colour_;           // $6810: columns 12-27 of the score lines in the lab
    int charset_place_{};
    std::uint8_t panel_slot1_{};                                    // $B51E: cauldron 0's panel sprite
    std::uint8_t level_digit_{0x31};                                // $B85E
    std::uint8_t flight_colour_{};                                  // $E7EB-$E7F2 during the flight
    bool flying_text_{};
    std::uint8_t unshown_byte_{};                                           // whose characters $77C6 loaded
    std::uint8_t shot_volume_{};                                    // $BA81: 0-15, keys 5 and 6
    std::array<std::uint8_t, 0x143> work_sprites_{}; // $ED00-$EE42: sprites $B4-$B8 drawn at run time
    std::array<std::uint8_t, 8> wander_colours_{}; // $B5DF
    std::uint8_t flash_show_{};    // $895A: frames the blacked-out landscape shows
    bool field_dark_{};            // this frame's playfield shared colours are off ($8A1C)
    bool invincible_{};            // $BB07: the cheat ($AFA8: seven keys from $AFCE)
    bool infinite_lives_{};        // not in the original (a launcher cheat)
    bool catellite_protected_{};   // not in the original (a launcher cheat)
    bool catellite_ai_{};          // not in the original (an option)
    Stick catellite_ai_stick() const; // what the automatic player does this pass
    // Carnage mode's state (carnage.cpp).
    bool carnage_{};
    std::map<std::uint32_t, Display::CellArt> damage_; // damaged landscape cells by map key
    struct Tracer {
        float x{}, y{}, dx{}, dy{}; // VIC x, raster line
        std::uint8_t colour{};
        int life{};
    };
    std::vector<Tracer> tracers_;
    struct PaintBlob {
        float x{}, y{}, dx{}, dy{};
        std::uint8_t colour{};
        int life{};
    };
    std::vector<PaintBlob> blobs_;
    // Landscape cut loose: its pixels (pair values as in the cells' art,
    // two screen pixels each), falling and turning until it lands.
    struct BodyPixel {
        std::int16_t dx{}, dy{};
        std::uint8_t value{};
    };
    struct Body {
        std::vector<BodyPixel> pixels; // round the pivot
        float x{}, y{}, vx{}, vy{}, angle{}, spin{};
        int age{};
    };
    std::vector<Body> bodies_;
    std::vector<Body> new_bodies_;       // cut loose during the walk over the falling ones
    float last_hit_x_{};                 // where the last bite was (which way a cut-off part tips)
    struct Wave {
        float x{}, y{}, radius{};
    };
    std::vector<Wave> waves_;            // the smart bomb's shockwave
    CarnageScore carnage_score_{};
    std::uint8_t kills_seen_{};
    int carnage_impact_{};
    int banner_timer_{};
    float flail_x_{}, flail_y_{}, flail_vx_{}, flail_vy_{};
    bool flail_on_{};
    int gun_clock_{}, gun_colour_{};
    std::uint32_t carnage_seed_{0x2545F491};
    float flood_radius_{-1}, flood_x_{}, flood_y_{};
    void carnage_pass();                 // the gun, tracers, paint blobs
    void carnage_flood();                // a completed place floods with colour (vanish)
    std::uint8_t carnage_random();
    // A landscape pixel pair at a VIC position: -1 none (no landscape cell
    // there), else its value (0 empty, 1-3 the character's, $10+ paint).
    int carnage_pair(int x, int y) const;
    Display::CellArt& carnage_art(int column, int row); // the cell's own pixels, made on first damage
    Glyph carnage_glyph(std::uint8_t code) const;
    void carve(float x, float y, float radius);
    void splash(float x, float y, float radius, std::uint8_t colour, int chance);
    void crumble(int column, int row);    // too little left: the cell opens
    void put_carnage(Display& d, int extension) const;
    void detach_from(int column, int row);  // cut-off landscape next to an opened cell falls
    void make_body(const std::vector<int>& cells);
    void move_bodies();
    void land(const Body& b);
    void carnage_waves();
    void wreck();                         // the wiz rams, the catellite flails
    void carnage_kills();                 // combos, the meter, overdrive, banners
    void banner(const char* top, const char* bottom);
    std::uint8_t pair_colour(std::uint8_t value) const; // a pair value's colour now
    void impact(int strength) { carnage_impact_ = std::max(carnage_impact_, strength); }
    void carnage_flail();                 // the catellite on a tether (update_catellite)
    void carnage_smart_bomb();            // its shockwave (activate_token)
};

} // namespace wizball::game
