#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <cstdint>

// The object autopilot (`$120B6E`, game.md "The maneuvers"): a virtual
// pilot for the aircraft the game flies. Its state is the record's role
// byte (+5) and timer (+$4C); each role is a handler (table `$1202D4`)
// that sets the record's control byte (+$65, stick and rudder bits) and
// moves on to the next role. The record then flies by the same input,
// attitude and velocity steps as the player's aircraft.
//
// Built: the maneuvers' roles (2, 7, 10, 11-40) and their ending, as the
// trainer of the maneuvers' demo flies them, and the missions' roles:
// heading for a target point (0, 1, 3, 4, 8: `$120C66`), the fixed banked
// turns (5, 9) and the wait (6).
namespace interceptor::game {

// A place the paths table holds and an object heads for (+$2C .. +$34):
// coarse cells x and z (<< 22), offsets (<< 8) and the height (<< 8).
struct Waypoint {
    std::int16_t cell_x{-1}, cell_z{}, fine_x{}, fine_z{};
    std::int32_t height{};
    bool none() const { return cell_x < 0; } // (+$2C -1)
    bool operator==(const Waypoint&) const = default;
};

// A record's fields for where it goes, which the autopilot and the object
// step read.
struct Course {
    Waypoint target;                  // +$2C
    std::uint8_t target_record{0xFF}; // +$38: $FF a point; $80 | n: record n's place
    std::uint8_t behaviour{};         // +$7A
    std::int16_t distance{};          // +$4A: to the target point (0x7FFF: far)
    std::uint8_t path{};              // +$3A: the path entry its role 8 follows (x 2)
};

namespace role {
constexpr std::uint8_t none = 0;
// The maneuvers' first roles (`$11756A`'s entries).
constexpr std::uint8_t aileron_roll = 0x0B, inside_loop = 0x0E, split_s = 0x11, half_loop = 0x14,
                       barrel_roll = 0x17, inverted = 0x1A, entry_0 = 0x23, break_turn = 0x27;
} // namespace role

struct PilotWorld {
    std::uint8_t mode{};      // $13AB96
    std::int8_t level{};      // $13AB97
    bool practice_ready{};    // $13AA8A negative: the practice's trigger
    bool practice{};          // $13AA83 (the practice's longer pause after a maneuver)
};

class Autopilot {
public:
    Autopilot(const amiga::LoadedImage& program, const FlightTables& tables);

    // `$120B6E`: one frame of the record's virtual pilot (before its flight
    // step). `pilot` is the record's +$64 byte; `course` its target (none:
    // +$2C -1).
    void fly(Aircraft& a, std::uint8_t& pilot, const PilotWorld& world, Course* course = nullptr) const;
    // `$121C6E`, in the attitude step for others: the stick let go without
    // the pilot's bit 4; roles 6 and 10 (and on course 0, 1, 8 by the
    // pilot's bits) roll the wings level, by the roll the frame started
    // with; the next frame flies by it.
    static void level_out(Aircraft& a, std::uint8_t pilot, std::int16_t roll_before);

private:
    // The rate tables (`$121406`, `$12141C`, `$121432`, `$121448`).
    std::int16_t pitch_rate(int index) const { return program_.s16(0x12141C + std::uint32_t(2 * index)); }
    std::int16_t roll_rate(int index) const { return program_.s16(0x121432 + std::uint32_t(2 * index)); }
    // `$120CD8`: steering towards the target point; false when there is
    // none (the pilot's bits go).
    bool steer_to(Aircraft& a, std::uint8_t& pilot, Course& course, const FlightTables& tables) const;
    // `$121406` (others than aircraft), `$121448` / `$12145E` (type $14 and
    // role 8): the other rate tables.
    std::int16_t table(std::uint32_t at, int index) const { return program_.s16(at + std::uint32_t(2 * index)); }

    const amiga::LoadedImage& program_;
    const FlightTables& tables_;
};

} // namespace interceptor::game
