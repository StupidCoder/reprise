#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/ffp.hpp"
#include <array>
#include <string>

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

// What the flight code needs from the map and the ships.
struct FlightTables {
    // `wings` data, read at load (addresses in flight.cpp).
    std::array<int, 11> wheel_height{};       // $115A00, by the attitude
    std::array<int, 11> gear_height{};        // $115A16
    std::array<int, 6> turn_height{};         // $115A54
    std::array<ffp::Ffp, 26> turn_speed{};    // $115A60: horizontal speed factor per turn step
    std::array<ffp::Ffp, 91> sine{};          // $115AC8: sin of 0..90 degrees
    std::array<int, 20> attitude{};           // $115A2C: the propeller index per pitch step
    std::array<std::string, 20> flying_names{}; // $115D34
    std::array<std::string, 26> turn_left_names{}, turn_right_names{}; // $115C64, $115CCC
    std::array<std::string, 12> deck_names{};    // $115C34
    std::array<std::string, 20> wheel_left_names{}, wheel_right_names{}; // $115DA6, $115DF6
    std::array<int, 7> deck_front{}, deck_back{}; // $115D84, $115D92: the plane's extent on deck per turn step
    std::array<int, 7> obstacle_height{};      // $115716: by scenery class ($105710)
    std::array<int, 8> sea_motion{};           // $114B98
    int pitch_rate{};                          // $115E6A (600)
};
FlightTables read_flight_tables(const amiga::LoadedImage& wings);

// A ship the plane can land on or hit (the five object slots $105710 and
// $104A4E know: four enemy slots and the US carrier, last).
struct ShipState {
    int first{}, last{};  // byte offsets of the map columns it covers
    bool afloat{};        // +4
    int ready{};          // +$C (hypothesis: the deck is clear)
    int deck{};           // +$E
    int sunk{};           // +$14: how far it has sunk
    int bob{};            // +$1A
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
};

struct FlightWorld {
    const data::MissionMap* map{};
    std::array<ShipState, 5> ships{}; // $105710 order; [4] = the US carrier
    int carrier_x{};                  // $1152E6 (-$5C6C)
    int elevator{};                   // -$5C6A
    int lift{};                       // -$5C68
};

// One logic tick ($101386: the timers, then the flight code $10C660).
void flight_tick(PlayerFlight&, FlightWorld&, const FlightTables&, int command);

} // namespace wings::game
