#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/data/world.hpp"
#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include <array>
#include <cstdint>
#include <optional>

// The player's aircraft and its flight model (game.md "The flight step"):
// one call of `fly` is one tick of the original (15 a second). Units are
// the original's: positions in world units x 256 (y up; $708 = sitting on
// the gear), speeds in its speed units (a tick moves about 4 x speed),
// angles 80 per degree.
namespace interceptor::game {

// The controls the player holds this tick (the original's control byte).
struct FlightControls {
    bool nose_down{}, nose_up{}; // stick forward / back
    bool left{}, right{};        // stick left / right (roll)
    bool rudder_left{}, rudder_right{};
    bool throttle_up{}, throttle_down{}; // keys - and = (held)
    // A recorded control byte instead of the above (the demo, `$10DFEE`):
    // taken as it is, the throttle settings left alone.
    std::optional<std::uint8_t> recorded;
};

// Flag words of the original's object record (named where known).
namespace flag {
// `state`
constexpr std::uint16_t crashed = 0x0200;
constexpr std::uint16_t speed_brake = 0x0800;
constexpr std::uint16_t moves = 0x1000;
// `motion`
constexpr std::uint16_t afterburner = 0x0008;
constexpr std::uint16_t auto_level = 0x0010;
constexpr std::uint16_t nose_wheel_down = 0x0040;
constexpr std::uint16_t on_ground = 0x0080;
constexpr std::uint16_t reverse_armed = 0x0400;
constexpr std::uint16_t touched_down_fast = 0x1000;
constexpr std::uint16_t full_power = 0x2000;
constexpr std::uint16_t wheel_brakes = 0xC000;
} // namespace flag

// The fuel warnings' bits ($13AE43).
namespace fuel {
constexpr std::uint8_t low = 0x01, critical = 0x02, exhausted = 0x40;
} // namespace fuel

struct Aircraft {
    std::int32_t x{}, y{}, z{};
    std::int32_t vx{}, vy{}, vz{}; // added to the position each tick
    Angles attitude{};
    std::int16_t pitch_rate{}, yaw_rate{}, roll_rate{};
    std::int8_t pitch_input{}, rudder_input{}, roll_input{}; // ramped stick and rudder
    std::int8_t throttle{};          // 0 .. 120 (military power); negative: reverse
    std::int8_t throttle_target{};   // set by F1 - F10 (0: none)
    std::uint8_t throttle_steps{};   // low nibble: the throttle's step counter
    std::uint8_t controls{};         // the control byte for this tick
    std::int16_t speed{}, airspeed{};
    std::int32_t fuel{};             // pounds x 256
    std::int16_t sink{}, climb_speed{};
    std::int16_t ground_drag{};      // also the nose drop of a stall
    std::int16_t bank_sine{}, bank_cosine{};
    std::int16_t bank_side{}, bank_side_was{}, bank_turn{}; // the bank turn's memory
    std::int16_t bank_turn_limit{};
    std::int16_t bank_pitch{};       // $13AC36: the bank turn's share of this tick's pitch (the G reading)
    std::uint8_t gear{};             // high nibble: 0 down .. $60 up, bit 7 the handle up
    std::int16_t deck_height{};      // over a deck: its height / 256
    Matrix body{}, view{};
    std::uint16_t state{}, motion{};
    std::uint8_t surface{};          // byte +4: bit 1 over water, bit 3 no rudder or roll, 6-7 deck
    std::uint8_t handling{};         // byte +$20: bit 0 ground handling, 1 spin, 2 stall
    std::uint8_t type{0x11};
    std::uint8_t role{};             // byte +5: the autopilot's role (autopilot.hpp)
    bool player = true;              // the player's record ($13ACA4 0): the player's responses
    std::int16_t timer{};            // word +$4C
    bool stall_warning{};            // the stall warning is on ($13AE40 bit 7)
    std::uint8_t fuel_warning{};     // `fuel` bits ($13AE40 bits 0, 1, 6)
    bool supersonic{};               // past the speed of sound ($13ABC2 bit 14)
    bool gear_speed_warned{};        // the gear is down too fast: warned ($13AB36)
};

// The game-wide switches the flight model reads.
struct FlightRules {
    bool stall_model{true};   // $13AA74
    bool bank_turns{true};    // $13AA7F
    bool flying{true};        // $13ABBC bit 6: the flight runs
    std::int32_t ceiling{0x800000}; // $13A9EA
    // The ground under the wheels: water and runways ($11F83A). Without a
    // world the surface flags stay as they are.
    const data::World* world{};
};

// What a tick did that the cockpit and sounds need to know.
struct FlightEvents {
    bool took_off{}, touched_down{}, crashed{}, overspeed{};
    std::int16_t touchdown_rate{}; // vertical speed at touch-down (soft below $240)
    bool in_water{};               // on the ground over water
    // The engine's sound event ($13ABA0: -2 the engine starts, -3 / -4 the
    // afterburner lights / goes out, -6 the speed of sound passed) and the
    // frames to the engine's restart that comes with it ($13AA87, 0: none).
    std::int8_t engine_sound{}, engine_restart_in{};
    int tyres_volume{};            // a soft touch-down's tyre sound (0: none)
    bool stall_warning{};          // the stall warning came on: its beep
    bool gear_speed_warning{};     // the gear came down too fast (or got too fast): its beep
};

FlightEvents fly(Aircraft& aircraft, const FlightControls& controls, const FlightRules& rules,
                 const FlightTables& tables);

// One tick of an object that flies without controls (a missile): the
// velocity and height step ($106B0E) and the position, as `fly` does them.
void coast(Aircraft& aircraft, const FlightRules& rules, const FlightTables& tables);
// The attitude step of a record other than an aircraft ($121E74): its rates
// eased toward `rates` (pitch, yaw, roll), then the rotation as `fly`'s.
void turn_object(Aircraft& aircraft, std::array<std::int16_t, 3> rates, const FlightRules& rules,
                 const FlightTables& tables);

// A place of free flight's start table ($137CF6, 16 bytes each: coarse
// cell x and z, slot, offsets x and z (x 1024), fine offsets x and z (x 16),
// heading in degrees): its x and z, as $10D51E and the map's markers
// ($11FB9E) find them. Places 0-2 are the runways.
std::array<std::int32_t, 2> place_position(const amiga::LoadedImage& program, int place);
// $10D51E: the aircraft at a runway place, ready for free flight.
Aircraft start_at_place(const amiga::LoadedImage& program, const FlightTables& tables, int place);

} // namespace interceptor::game
