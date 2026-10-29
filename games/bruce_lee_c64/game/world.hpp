#pragma once
#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/game/actor.hpp"
#include "bruce_lee_c64/game/playfield.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// One game in progress: the room on screen, the actors, the lanterns, the
// score and the timers, and the pass that runs every third frame (the
// original's main loop, $1BDD) plus the work done every frame ($15C0).
namespace bruce_lee::game {

// The game's random numbers ($117A): the byte at a counter (advanced by
// every draw and once per frame) plus an offset the caller happens to hold
// in Y, read through the pointer $16/$17. The init points it at a table of
// SID noise at $0200, but every new game ($0F69) clears $17 with the rest of
// the zero page, so in play the numbers are the game's own variables in
// pages 0 and 1 (World::memory_byte).
struct Random {
    std::uint8_t counter{};
    // Overrides for checking against the original: the values it drew, in order.
    std::function<std::optional<std::uint8_t>()> replay;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(counter); }
};

struct RoomProgress {
    std::vector<bool> lanterns; // per lantern of the room's list: still hanging
    std::uint8_t remaining{};   // $2A + room: lanterns left

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(lanterns, remaining); }
};

// The things the room scripts ($3A5C) move.

// A mover ($71-$79, three of them): a figure drawn as text strings, frame
// by frame; at frame 3 it steps one column on until its limit, after frame
// 6 it waits (a negative frame counting up) and starts again.
struct Mover {
    std::uint8_t frame{}, column{}, row{}; // $77 + i, $71 + i, $74 + i

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(frame, column, row); }
};

// A slider ($6E-$70): one piece moving a column per step along a row,
// drawn over its last place.
struct Slider {
    std::uint8_t at{}, was{}, row{}; // $6E, $70, $6F

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(at, was, row); }
};

// The lift ($2C5D, $60-$66, $430E): the glyphs $61-$63 scroll their
// pattern a row per step (the rides of move::ride follow it); the speed
// runs down from 7 to 0 (a pause), then the direction turns.
struct Lift {
    std::uint8_t timer{1};   // $60: passes to the next step (bit 7: pausing)
    std::uint8_t phase{};    // $64: the pattern row on top (0-7)
    std::uint8_t down{};     // $65
    std::uint8_t rounds{1};  // $66: steps left at this speed
    std::uint8_t speed{7};   // $430E: index into the speed table

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(timer, phase, down, rounds, speed); }
};

// Room 13's moving pieces ($1042, $010F-$014A): nine along their rows, each
// wrapping between two columns; mode 0 off, bit 7 rightwards.
struct Slide {
    std::uint8_t mode{}, at{}, row{}, low{}, high{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(mode, at, row, low, high); }
};

// Room 18's fireballs ($4120, $431B-$4322, sprites 6 and 7): they come
// down from the top towards Bruce's x and start again after leaving the
// bottom. The shape is four random rows each pass.
struct Fireball {
    bool active{};
    std::uint8_t wait{};          // $431D + i: passes before it comes
    std::uint8_t x{}, y{};        // $431F + i, $4321 + i (x in pixel pairs)
    std::uint8_t colour{};        // $D02D + i
    std::array<std::uint8_t, 4> shape{}; // first byte of rows 0-3 ($8340 / $8380)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(active, wait, x, y, colour, shape); }
};

// A floor cell of code $60 Bruce has stepped on ($3068, $4FEB-$4FF3): from
// then on it counts passes and the floor around it breaks up and mends
// ($3215); row and column of the three cells drawn.
struct Crumble {
    std::uint8_t timer{}; // 0: free
    int row{}, column{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(timer, row, column); }
};

// What a pass asks the game flow to do.
struct PassEvents {
    std::optional<int> room_change; // new room (Bruce took an exit)
    bool bruce_died{};              // the dying move ended or Bruce's energy is gone
    bool back_to_start{};           // bumped with nothing to undo: the room's start

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(room_change, bruce_died, back_to_start); }
};

class World {
public:
    explicit World(const data::Assets&);

    const data::Assets& assets;
    Playfield field;
    std::array<data::Charset, 4> charsets; // the room charsets as the flames change them
    Actors actors;
    Random random;
    int room{};
    std::array<RoomProgress, 20> progress{};
    bool playing{};          // $0104 (0 in the attract mode)
    bool two_players{};      // $42B7
    bool flag40{};           // $40: set when the last room was reached (counted up; only tested for zero)
    std::uint8_t clock_low{}, clock{}; // $87, $88
    std::uint8_t ninja_timer{}, yamo_timer{}; // $5D, $5E: passes until an enemy comes
    std::uint32_t score{};   // $41-$43, 24 bits
    std::uint8_t lives{};    // $28 ("FALLS")
    std::array<std::uint8_t, 3> flame_timers{}; // $0101-$0103
    // Not the original's: deaths by deadly cells and fireballs per actor,
    // counted for the presentation layer (the gore option).
    std::array<std::uint32_t, 3> hazard_deaths{};

    // Carnage mode (not the original game; not in the attract game,
    // carnage.cpp): Bruce's blows send the Ninja and the Yamo flying (and
    // the Yamo's send Bruce). A flying actor keeps its thrown move while the
    // flight moves it, bounces off the room's walls and floors (and hurts on
    // each hard hit), smashes the decoration it flies through (what stood
    // on it topples and falls), knocks over an enemy, and dies where the
    // game's own rules kill (deadly cells) or when its energy is gone as it
    // lands. Blows and hits in quick succession build a combo that scores;
    // each enemy beaten brings the next sooner (waves); in room 18 a blow
    // sends a fireball back at the wizard's face. All its random numbers
    // are its own.
    struct Carnage {
        bool on{};
        struct Flight {
            bool active{};
            double x{}, y{}, vx{}, vy{}; // actor units (x in pixel pairs, y in lines), per frame
            int age{};

          public:
            // Save schema v1: explicit values; imported resources stay bound to this instance.
            template <class A> void persist(A &ar) { ar(active, x, y, vx, vy, age); }
        };
        std::array<Flight, 3> flights{};
        int combo{}, combo_frames{}; // hits in the run; frames since the last
        int wave{};                  // enemies beaten in this room: the next come sooner
        // Room 18: fireballs a blow sent back up at the wizard's face
        // (sprite units, as the game's fireballs: x in pixel pairs).
        struct Returned {
            bool active{};
            double x{}, y{}, vx{}, vy{};
            std::array<std::uint8_t, 4> shape{};
            std::uint8_t colour{};

          public:
            // Save schema v1: explicit values; imported resources stay bound to this instance.
            template <class A> void persist(A &ar) { ar(active, x, y, vx, vy, shape, colour); }
        };
        std::array<Returned, 2> returned{};
        // Decoration that lost what it stood on, falling as one piece.
        struct Chunk {
            struct Cell {
                int column{}, row{}; // within the piece
                data::Glyph glyph{};

              public:
                // Save schema v1: explicit values; imported resources stay bound to this instance.
                template <class A> void persist(A &ar) { ar(column, row, glyph); }
            };
            std::uint32_t id{};
            double x{}, y{}, vy{};   // picture pixels: its top left
            int columns{}, rows{};
            std::vector<Cell> cells;

          public:
            // Save schema v1: explicit values; imported resources stay bound to this instance.
            template <class A> void persist(A &ar) { ar(id, x, y, vy, columns, rows, cells); }
        };
        std::vector<Chunk> chunks;
        std::uint32_t next_chunk{1};
        // What happened, for the presentation: kept for a while, numbered.
        struct Event {
            // (chunk, landed: `strength` is the piece's id; wave: the count;
            // returned: a fireball sent back, face: it hit the face.)
            enum class Kind { launch, impact, smash, knock, combo, chunk, landed, wave, returned, face } kind{};
            std::uint32_t id{};
            double x{}, y{};          // picture pixels
            int strength{};           // 1-3 (impact, launch, knock); combo count; smash: the cell's column
            int row{};                // smash: the cell's screen row
            data::Glyph glyph{};      // smash: the pixels that broke off
            double vx{};              // which way the body flew

          public:
            // Save schema v1: explicit values; imported resources stay bound to this instance.
            template <class A> void persist(A &ar) { ar(kind, id, x, y, strength, row, glyph, vx); }
        };
        std::vector<Event> events;
        std::uint32_t next_id{1};
        std::uint32_t seed{0x2545F491};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(on, flights, combo, combo_frames, wave, returned, chunks, next_chunk, events, next_id, seed);
        }
    } carnage;
    bool carnage_on() const { return carnage.on && playing; }
    bool flying(int slot) const { return carnage_on() && carnage.flights[std::size_t(slot)].active; }
    std::array<std::uint8_t, 3> fall_hits{};    // $4F52: landing-on hits left
    std::uint8_t fall_hit_refresh{};            // $4EF9
    std::array<std::int8_t, 5> sprite_owner{};  // $4F4D: actor per sprite (-1 none)
    Lift lift;
    std::array<Crumble, 3> crumbles{};          // $4FEB-$4FF3
    // The room scripts' state ($33A0 clears the counters at every entry).
    std::array<std::uint8_t, 5> room_counters{}; // $4EA4-$4EA8
    std::array<Mover, 3> movers{};
    Slider slider;
    bool off_beat{};                             // $7B: toggles every pass; most scripts act when clear
    std::array<Slide, 9> slides{};
    bool slides_set{};                           // room 13 has set $010F-$014A (else the loader's leftovers)
    std::array<std::array<std::uint8_t, 3>, 2> drop_bonus{}; // $014B + 4 * player: room 13's 500 points per height
    std::array<std::uint8_t, 2> extra_falls{};  // $4EA2 + player: room 16's extra falls left
    std::array<Fireball, 2> fireballs{};
    std::uint16_t countdown{};                   // $49/$4A: frames (room 19)
    std::vector<std::uint8_t> pending_sounds;   // effects requested this pass
    std::uint16_t demo_position{};              // $1D/$1E: the attract mode's recorded input

    // Saved per room and player: entering a room for the first time scores
    // ($0300 + 2 * room + player).
    std::array<std::array<bool, 2>, 20> first_visit{};
    int player{};                // $03
    std::uint32_t top_score{};   // $4375-$4377
    std::uint32_t next_life_at{};// $437F: score for the next extra fall
    std::uint32_t first_extra_life{}; // $4378 (kept across games; see update_top_score)
    std::uint8_t band_counter{};      // $44 as $3181 finds it (observed 0)
    std::uint8_t blink{};        // $43AA: the "1UP" blink timer
    std::uint8_t collision_latch{}; // the VIC's sprite-sprite register (read clears)
    // Cheats (pause menu; not in the original): a lost fall is not counted;
    // the Ninja's and the Yamo's hits on Bruce are ignored (they still strike).
    struct Cheats {
        bool infinite_lives{};
        bool harmless_enemies{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(infinite_lives, harmless_enemies); }
    } cheats;
    // Frame counters ($7F, and $80/$81 every 256 frames) and the idle count
    // ($82): only the random numbers read them.
    std::uint8_t frames_low{};
    std::uint16_t frames_high{};
    std::uint8_t idle{};
    std::array<std::uint16_t, 3> effect_streams{0xEC31, 0xEC31, 0xEC31}; // $F6-$FB, kept by the game's effect player
    bool blink_printed{};  // $1AB1 printed in this pass (Y = $2F at the flames)
    bool yamo_arrived{};   // the Yamo came in this pass (Y = 3 at the Ninja's check)

    // $117A: a random number (Y = `offset`).
    std::uint8_t draw(std::uint8_t offset);
    // What the original holds at $0000-$01FF, as far as the game's state
    // gives it (random_source.cpp).
    std::uint8_t memory_byte(int address) const;
    // The sprite pointers ($8FF8-$8FFF) and which half of the block table
    // sprites 0-4 take next ($276A); only the stack slip in terrain_events
    // reads them.
    std::array<std::uint8_t, 8> sprite_pointers{};
    bool second_blocks{};

    // A new game ($0F69, $0755): lanterns back, lives, the play screen.
    void new_game();
    // Enters a room ($09AE): Bruce at x, y (the exit's place or the start).
    void enter_room(int room, std::uint8_t x, std::uint8_t y, bool carry_enemies);
    // Prints string `text` like $17FA: row index (0, 1: screen rows 0, 1;
    // n >= 2: screen rows 2n-1 and 2n), column; the upper halves of the
    // characters on that row and, unless `single` or rows 0-1, the lower
    // halves on the next.
    void print(int text, int row_index, int column, bool single = false);
    void draw_status();
    // $1830: Bruce lost a fall. Returns false when none were left (game
    // over); otherwise everyone's energy is full again and Bruce restarts
    // at the room's start.
    bool lose_life();
    void restart_room();        // $189E
    void start_blink();         // $07F7
    void blink_status();        // $1AB1
    void toggle_version_line(); // $1B83
    // The state each of two alternating players keeps ($28-$43, the lantern
    // states at $0340, the next extra fall's mark).
    struct PlayerState {
        std::uint8_t lives{};
        int room{};
        std::array<RoomProgress, 20> progress{};
        bool flag40{};
        std::uint32_t score{}, next_life_at{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(lives, room, progress, flag40, score, next_life_at); }
    };
    PlayerState player_state() const;
    void set_player_state(const PlayerState&);
    // $081C: Bruce enters at the room's start ($0B80 with slot 0).
    void place_bruce_at_start() { place_actor(0, 0); }

    // The main loop's work for one pass. Combat reads (and so clears) the
    // collision latch, in rooms with fights.
    PassEvents pass();
    // Shared room-effect update, also used by the isolated teaching experiment.
    PassEvents step_room_effects();
    // The interrupt's per-frame work that the game state sees.
    void frame();

    int charset_index() const { return shown_charset; }
    // The charset the room is shown with ($F2, the $D018 copy: the room's,
    // until room 18's end changes it) and the one the terrain probe reads
    // ($7C, taken from $F2 by every frame's interrupt).
    int shown_charset{};
    int probe_charset{};
    // For checks against the original: called before each actor's probe
    // (the original's interrupt may refresh the probe's page in between).
    std::function<void(int slot)> before_probe;

private:
    ActorWorld actor_world();
    PassEvents run_pass();                // $1BDD's work up to the sprite blocks
    void flames();                        // $40A9
    void enemy_timers();                  // $0D98
    void yamo_arrives();                  // $0D03
    void ninja_arrives();                 // $0DA5
    void place_actor(int slot, std::uint8_t kind_flags); // $0B80
    void probe_actors();                  // $521B
    void terrain_events(PassEvents&);     // $2E26
    enum class RowScan { go_on, enemy_left, room_change };
    RowScan scan_row(int slot, int map_row, std::uint8_t& touching, PassEvents&); // $2E5B
    std::uint8_t code_at(int index) const;
    void combat(std::uint8_t collisions); // $29B0
    void remove_actor(int slot);          // $2FFE
    // Carnage mode (carnage.cpp).
    void carnage_launch(int slot, int attacker, std::uint8_t hit);
    void carnage_frame();
    void carnage_fly(int slot);
    void carnage_event(Carnage::Event);
    void carnage_hit(int strength);       // the combo and its score
    bool carnage_solid(int px, int py, bool walls) const;
    void carnage_smash(int slot);
    int carnage_empty_code() const;
    bool carnage_decor(const Cell&) const;
    void carnage_topple(int column, int row);
    void carnage_chunks();
    void carnage_fireballs(); // room 18, before its fireballs are tested
    void carnage_returned();
    void carnage_beaten(int slot); // an enemy out of energy taken away
    std::uint8_t carnage_random();
    void enemy_timer_for(int slot);       // $0D36
    void after_actors(PassEvents&);       // $2D42
    void add_score(std::uint32_t points); // $3172
    void update_top_score();              // $3181
    void draw_score();                    // $3157
    bool exit_open(const Actor&) const;   // $30D1
    void take_lantern(int column, int map_row); // $2EF1
    void extra_life();                    // $2FD3
    void mark_crumble(int column, int map_row); // $3068

    // rooms.cpp: the per-room setups and scripts.
    void setup_room();                    // $33A0, table $4EA9
    void room_script(PassEvents&);        // $3A5C, table $4ED1
    void script_room_0();
    void script_room_1();
    void script_room_2();
    void script_room_3();
    void script_room_4();
    void script_room_5();
    void script_room_6();
    void script_room_7();
    void script_room_8();
    void script_room_9();
    void script_room_10();
    void script_room_11();
    void script_room_12();
    void script_room_13();
    void script_room_14();
    void script_room_15();
    void script_room_16();
    void script_room_17();
    void script_room_18();
    void script_room_19(PassEvents&);
    bool hanging(int room, int lantern) const;
    bool cleared(int room) const { return progress[std::size_t(room)].remaining == 0; }
    bool once(int counter);               // the scripts' one-shot test: counter not negative, then counted down
    void set_colour(int column, int row, std::uint8_t colour);
    void request_sound(std::uint8_t effect) { pending_sounds.push_back(effect); }
    void print_column(int row, int column, std::array<std::uint8_t, 3> texts); // $3A77 family
    void print_at_mover(int mover, int text);
    void step_mover(int mover, int first_text, std::uint8_t limit, std::uint8_t restart_column,
                    std::uint8_t wait);   // the pattern at $37FE
    void slide_right(std::uint8_t last, std::uint8_t restart);
    void slide_left(std::uint8_t first, std::uint8_t restart);
    void draw_slider(int erase_text, int piece_text); // $403B / $4050
    void step_lift();                     // $2C5D
    void step_crumbles();                 // $3215
    void step_slides();                   // $1042
    void setup_slides();                  // $34B0
    void step_fireballs();                // $4120
    void start_fireball(int i);           // $41EE / $420D
    void erase_extra_fall();              // $2FEC

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(field, charsets, actors, random, room, progress, playing, two_players, flag40, clock_low, clock,
           ninja_timer, yamo_timer, score, lives, flame_timers, hazard_deaths, carnage, fall_hits,
           fall_hit_refresh, sprite_owner, lift, crumbles, room_counters, movers, slider, off_beat, slides,
           slides_set, drop_bonus, extra_falls, fireballs, countdown, pending_sounds, demo_position,
           first_visit, player, top_score, next_life_at, first_extra_life, band_counter, blink,
           collision_latch, cheats, frames_low, frames_high, idle, effect_streams, blink_printed,
           yamo_arrived, sprite_pointers, second_blocks, shown_charset, probe_charset);
    }
};

} // namespace bruce_lee::game
