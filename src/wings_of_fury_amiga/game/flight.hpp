#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/dashboard.hpp"
#include "wings_of_fury_amiga/game/effects.hpp"
#include "wings_of_fury_amiga/game/enemies.hpp"
#include "wings_of_fury_amiga/game/ffp.hpp"
#include "wings_of_fury_amiga/game/islands.hpp"
#include "wings_of_fury_amiga/game/ships.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The player's Hellcat: how it taxis, takes off, flies, turns, lands and
// crashes. Rebuilt from `wings`' compiled-C flight code ($10C660 and the
// functions it calls, .analysis/wings_of_fury_amiga/game.md "Flight"), which
// runs once per logic tick (12.5 per second on a PAL Amiga: one per command
// word the vertical-blank interrupt queues every fourth frame).
//
// Integer widths and the order of updates follow the original; the airborne
// physics use the Amiga's Fast Floating Point (ffp.hpp) exactly as the game
// calls it.
namespace wings::game {

// The command word one tick runs on ($10CA32 builds it every fourth vertical
// blank from the stick and the fire button).
namespace command {
constexpr int up = 0x01;
constexpr int down = 0x02;
constexpr int right = 0x04;
constexpr int left = 0x08;
constexpr int fire_held = 0x10;  // a press held 10 ticks or longer
constexpr int fire_tapped = 0x20; // a shorter press
} // namespace command

// The sound effects (`sounds/*`, loaded by $103368).
enum class Sample : std::uint8_t { none, boom, splash, machinegun, engine, grind, clang, screech, scream };

// The game's sound board ($101F76 .. $102306). Each of Paula's four channels
// has two requests; the tick's driver ($102066) plays the first that is
// active, else the second, and starts, retunes or stops the channel when
// that changes. Events (a blast, a splash, the elevator, a landing) set a
// request and clear what the channel last started, so it starts again.
struct SoundRequest {
    bool active{};
    Sample sample{};
    int length{};  // bytes (played as whole words)
    int period{}, volume{};
    int repeat{};  // 1: once; $FFFF: on and on
};
struct SoundBoard {
    struct Channel {
        std::array<SoundRequest, 2> request{};
        Sample playing{};         // +$10: the sample last started
        int playing_period{}, playing_volume{};
    };
    std::array<Channel, 4> channels{};
    // The engine's drone: loudness and pitch ease toward targets the flight
    // code sets (-$5BD2 toward -$5BD6, -$5BD0 toward -$5BD4 + climb / 128 +
    // altitude / 16).
    int engine_level{}, engine_target{};
    int engine_period{}, engine_base{};
    std::array<int, 9> lengths{}; // by Sample: the files' lengths
    // What the driver did this tick, for the host's Paula.
    struct Command {
        enum class Kind : std::uint8_t { start, retune, stop } kind{};
        int channel{};
        SoundRequest request{};
    };
    std::vector<Command> commands;
};

// What the flight code needs from the map and the ships.
struct FlightTables {
    // `wings` data, read at load (addresses in flight.cpp).
    std::array<int, 11> wheel_height{};       // $115A00, by the attitude
    std::array<int, 11> gear_height{};        // $115A16
    std::array<int, 6> turn_height{};         // $115A54
    std::array<ffp::Ffp, 26> turn_speed{};    // $115A60: horizontal speed factor per turn step
    std::array<ffp::Ffp, 91> sine{};          // $115AC8: sin of 0..90 degrees
    std::array<int, 20> attitude{};           // $115A2C: the attitude (gun flash and gear frames) per pitch step
    std::array<std::string, 20> flying_names{}; // $115D34
    std::array<std::string, 26> turn_left_names{}, turn_right_names{}; // $115C64, $115CCC
    std::array<std::string, 12> deck_names{};    // $115C34
    std::array<std::string, 20> wheel_left_names{}, wheel_right_names{}; // $115DA6, $115DF6
    std::array<int, 7> deck_front{}, deck_back{}; // $115D84, $115D92: the plane's extent on deck per turn step
    std::array<int, 7> obstacle_height{};      // $115716: by scenery class ($105710)
    std::array<int, 8> sea_motion{};           // $114B98
    int pitch_rate{};                          // $115E6A (600)
    std::array<int, 3> weapon_loads{};         // $114B4D: rockets, bombs, torpedo
    std::array<std::string, 2> wreck_names{};  // `hcr5` facing right, `hcrf` facing left ($10C88C)
    // The game's trigonometry: sine over a quarter (-$6692, 257 words of a
    // 1024-step circle, x 32767), the cotangent-like table the guns' line of
    // fire uses (-$6892, 256 words) and the arctangent (-$5810, 257 bytes).
    std::array<int, 257> sine_quarter{};
    std::array<int, 256> slope{};
    std::array<int, 257> arctangent{};
    IslandTables islands;
    EnemyTables enemies;
    ShipTables ships;
    std::vector<int> gun_angles;   // -$63E2 - 32 ..: the guns' picture by row and column ($104DB8)
    int gun_angle(int index) const {
        const auto at = std::size_t(index + 32);
        return at < gun_angles.size() ? gun_angles[at] : 0;
    }
    // The messages (`wings` text).
    struct Messages {
        std::string island_taken;  // -$7604 "All enemy forces on island have been destroyed . . . Bonus %ld points . . ."
        std::string next_mission;  // -$7660 ". . . Good work ! You are ordered to return to carrier ..."
        std::string promoted;      // -$75A4 ". . . Congratulations! You have completed your mission!! ..."
    } messages;
};
FlightTables read_flight_tables(const amiga::LoadedImage& wings);

// A ship the plane can land on or hit (the five object slots $105710 and
// $104A4E know: four enemy slots and the US carrier, last).
// An enemy ship's anti-aircraft gun (+6 list, $E bytes each, $10350E).
struct ShipGun {
    int x{};              // +4: world x
    int lift{};           // +6: drawn this much higher
    bool destroyed{};     // +8
    int smoke_left{};     // +$A
    int smoke_timer{};    // +$C
};
// A ship's parked planes (lists -$5F68 + $40 n, $10252C): they take off
// as lone fighters while the player is between `low` and `high`.
struct ShipPlanes {
    int count{};          // +0: parked
    int limit{};          // +2: no take-off while this many fighters are up
    int low{}, high{};    // +4, +6
    struct Plane {
        int flag{};       // +0 ($8000 parked; the carrier's: 1 rolling)
        int x{}, altitude{}; // +2, +4
        int facing{};     // +6
    };
    std::vector<Plane> planes;
    int shown_count{};    // the frame start's copy ($10102C)
};

struct ShipState {
    int first{}, last{};  // byte offsets of the map columns it covers
    bool present{};       // -$5C84.. (enemy ships): on this map
    bool afloat{};        // +4
    int ready{};          // +$C: torpedo hits it can still take (the carrier: 4); 0: sinking
    int deck{};           // +$E
    int value{};          // +$12: the score for sinking it (0: the US carrier)
    int sunk{};           // +$14: how far it has sunk
    int sink_timer{};     // +$16: ticks to the next step down
    int sink_delay{};     // +$18: the timer's next start ($14 when hit for the last time; shrinks)
    int bob{};            // +$1A: the frame's copy of +$14
    int overlay{};        // +$1C: its library's first scenery index
    std::vector<ShipGun> guns; // +6, +$A
    ShipPlanes planes;
};

struct PlayerFlight {
    // The player record ($114FCC).
    int altitude{};      // +0
    int x{};             // +2
    std::string shape;   // +8: the hellcat.shp entry shown
    int mode{1};         // +$C: 0 flying, 1 on deck, 4 falling, 6 sinking, 7 caught by a wire, 8, $B
    int fuel{};          // +$E
    int spare{};         // +$10 (hypothesis: bombs/rockets/torpedoes left)
    int oil{};           // +$12
    int facing{-1};      // +$14: -1 left, 1 right
    int speed_x{};       // +$16
    int speed_y{};       // +$18
    int extra{};         // +$1A (cleared when the wreck comes to rest)
    int enemy_timer{};   // +$1C: ticks until a torpedo bomber is sent ($10BC02)

    // The flight code's globals.
    int climb{};          // $115356: commanded pitch (x 100 degrees: -4500..3000)
    int speed{};          // $115368: 0..$578
    int throttle{};       // $117D3E: acceleration step
    bool engine_loud{};   // $117D3C (sound)
    int turn{};           // $115362: 0, or the step of a turn round (1..$19)
    int smoothed_climb{}; // $1159F6
    int climb_kick{};     // $11535C
    bool pulling_up{};    // $1159FE: up held while facing left, level
    bool lift{};          // $1159F0
    int gear{};           // $11535E: 0 or 5 (wheels down)
    int signal{};         // $1152B3: the deck officer's signal
    bool fresh_on_deck{}; // $1152BB: a new plane that has not taken off yet
    int wire_x{};         // $115360
    int attitude{};       // $1154E6 (-$5A6C): set with the shape; also picks the wheel heights
    bool eighth_scale{};  // $115304: the 1/8 view, above altitude $BA
    std::string wheels;   // $115376
    int wheel_attitude{}; // $11537A
    int deck_low{}, deck_high{}; // $115350, $115352: x range of the deck
    int oil_counter{}, fuel_counter{}, fuel_period{}; // -$3CAE, -$3CB0, -$3E96
    int sea_step{}, sea_counter{}, sea_period{};      // -$6894, -$5C52, -$5C16
    int sea{};            // -$5C50: the sea's motion (vertical scroll source)
    int turn_delay{2};    // $1159F4: a turn steps every second call
    int sink_step{};      // $1159F8: sinking drops a pixel every third tick
    int wreck_time{};     // $1159FA: ticks since the plane came to rest wrecked
    int next_blast{};     // $1159FC: the wreck's next blast is due at this wreck_time
};

struct FlightWorld {
    const data::MissionMap* map{};   // the mission's map as play changes it (= &mission_map)
    data::MissionMap mission_map;    // (wrecked huts and pillboxes rewrite columns)
    void set_map(const data::MissionMap& m) {
        mission_map = m;
        map = &mission_map;
    }
    Islands islands;
    Enemies enemies;
    bool night{};                    // -$5C6E: a night mission ($1011FC)
    int rank{};                      // -$5C40
    int mission{1};                  // -$5C3E
    bool mission_accomplished{};     // -$5C42: back below deck, the debriefing
    bool celebration{};              // -$5CA1: balloons over the carrier
    long vblank_clock{};             // -$5C34: vertical blanks
    bool smoke_tick{};               // -$3CB8: every second tick
    // The message ticker (-$5848): a new message only when none runs, unless
    // forced.
    std::string message;
    bool message_new{};
    void say(const std::string& text, bool force = false) {
        if (!message.empty() && !force) return;
        message = text;
        message_new = true;
    }
    std::array<ShipState, 5> ships{}; // $105710 order; [4] = the US carrier
    int carrier_x{};                  // $1152E6 (-$5C6C)
    // The elevator (-$5C6A): 0 up, 1 down with the weapon menu shown,
    // 2 rising, 3 lowering; `lift` (-$5C68) is how far it is down (0..$20).
    int elevator{};
    int lift{};
    // The weapon menu below deck ($1012B0).
    bool menu{};                      // -$5C9A
    int menu_repeat{};                // -$5C90: ticks before up/down act again
    int input_delay{};                // -$42C0: ticks before menu and elevator act
    int weapon{1};                    // -$5C5A: 0 rockets, 1 bombs, 2 torpedo
    int weapon_load{};                // -$5C91 ($FF: none)
    int spare_planes{3};              // -$5CA2
    long score{};                     // -$5CB2
    int kills{};                      // -$5C7F: enemy planes shot down
    int caught_wire_x{};              // -$42C4: the wire the hook caught (drawn as a cable)
    // The elevator moves once per drawn frame: it steps on a tick only when
    // the draw counter (-$5C36) has changed since the last step (-$3CAC).
    int draw_counter{};
    int elevator_seen{};
    // The record's +$10 for a plane set up during the tick (the original
    // takes the beam position: 6..9); set by the caller.
    int random_load{6};
    // What the crash leaves behind (started by the tick, stepped as frames
    // are drawn) and the C library's random numbers the wreck uses.
    Effects effects;
    CRandom random;
    BeamRandom beam;
    SoundBoard sound;
    DeckCrew crew;    // the deck crew's flags, stepped as frames are drawn
    DashboardGauges gauges; // the dashboard's needles, lights and rolling digits
    // The weapons ($10B5B0, $1007F2, $100A72, $1019BC).
    bool guns{};                      // -$5C94: the guns firing
    int gun_supply{};                 // -$50EA: $600 for a new plane; never counts down
    std::int32_t gravity{0x6000};     // -$5CAE: what falling weapons gain per tick ($103562)
    // The frame's copies the tick reads ($100F88): the camera (-$41A2), the
    // altitude (-$419E) and the 1/8 view (-$60CA); and whether a frame was
    // drawn since the last tick (-$41C2). Set by the caller before a tick.
    int frame_camera{}, frame_altitude{};
    bool frame_eighth{}, frame_drawn{};
    // $101274 at the end of each tick: the plane as the weapons see it.
    struct Snapshot {
        int speed_y{}, speed_x{}, x{}, altitude{};
        int pitch{}; // -$5BFA: the smoothed climb in the game's angle steps (1024 to the circle)
    } last;
    // Set by the tick, for the game around it.
    int blackout{};                   // -$3BAC: a plane was lost; the view goes black this many waits
    bool game_over{};                 // -$5C3C: no plane left (or no carrier)
    bool the_end{};                   // -$5C9C: the last plane is lost: THE END shows
    // -$5A3C: drawn frames left before the game is over (the carrier went
    // down with the plane on deck: $64); 0 when not counting.
    int game_over_countdown{};
};

// A new plane below deck ($103684 with the record set-up $10B7EC): the
// elevator down with the weapon menu shown. `random_load` stands for the
// record's +$10 (the original takes the beam position: random 6..9).
void new_plane(PlayerFlight&, FlightWorld&, const FlightTables&, int random_load);

// The sound file each Sample is (`sounds/boom`, ...), or nullptr.
const data::Sample* sample_file(const data::Assets&, Sample);
// Their lengths in bytes, by Sample.
std::array<int, 9> sample_lengths(const data::Assets&);

// $101F76: the sound board's requests as a mission starts; `lengths` gives
// each sample's length in bytes (by Sample).
void set_up_sounds(FlightWorld&, const std::array<int, 9>& lengths);


// The islands (islands.cpp).
void soldiers_come_out(PlayerFlight&, FlightWorld&, const FlightTables&);   // $101DE4
void damaged_plane_smoke(PlayerFlight&, FlightWorld&, const FlightTables&); // $101BFC (every second tick)
// $1046DC: what a weapon (0 rocket or a crash, 1 bomb, 2 torpedo; `running`:
// a torpedo running in the water) hits at x: island targets, or a ship.
void island_hit(PlayerFlight&, FlightWorld&, const FlightTables&, int x, int weapon, bool running = false);
void kill_soldiers_near(PlayerFlight&, FlightWorld&, const FlightTables&, int x, int reach); // $101A8C
// Each drawn frame: the drawn wrecked huts' smoke ($104E18), the dugouts'
// guns ($103D78), the soldiers ($103EEE).
void islands_frame(PlayerFlight&, FlightWorld&, const FlightTables&, const std::vector<int>& wrecked_huts = {});

// $101A8C with $101AE2: a blast at x kills the soldiers within reach and
// sets off the torpedoes near it.
void set_off_near(PlayerFlight&, FlightWorld&, const FlightTables&, int x, int reach);
// $102DE8 (ship markers), $103252, $10252C: the enemy ships of a map, for
// the rank and mission (`row`: the bonus table's row).
void find_ships(FlightWorld&, const data::MissionMap&, const FlightTables&, int row);
// $10391E (enemies.cpp): the planes parked on the enemy ships.
std::vector<EnemySprite> ship_plane_sprites(const FlightWorld&, const EnemyTables&, const data::Assets&, const EnemyView&,
                                           int vertical_scroll);
// $101D0E (islands.cpp): an enemy ship's sinking reaches step 10.
void ship_sunk(PlayerFlight&, FlightWorld&, const FlightTables&, const ShipState&);
// $101CAE: the ships hit for the last time sink a step at a time.
void ships_sinking(PlayerFlight&, FlightWorld&, const FlightTables&);

// The enemy planes (enemies.cpp).
void enemy_timer(PlayerFlight&, FlightWorld&, const FlightTables&, int command); // $10BC02
void enemies_tick(PlayerFlight&, FlightWorld&, const FlightTables&);             // $10E7D6
void airfields_tick(PlayerFlight&, FlightWorld&, const FlightTables&);           // $10143E, $101622
void guns_at_planes(PlayerFlight&, FlightWorld&, const FlightTables&);           // $10B682
void launch_enemy(PlayerFlight&, FlightWorld&, const FlightTables&, bool formation, int x, int altitude,
                  int facing);                                                    // $10E4D0

// One logic tick ($101386: the timers, then the flight code $10C660).
void flight_tick(PlayerFlight&, FlightWorld&, const FlightTables&, int command);

} // namespace wings::game
