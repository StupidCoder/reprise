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
// trainer of the maneuvers' demo flies them. The missions' roles (chasing
// a target point, attacks) come with the missions.
namespace interceptor::game {

namespace role {
constexpr std::uint8_t none = 0;
// The maneuvers' first roles (`$11756A`'s entries).
constexpr std::uint8_t aileron_roll = 0x0B, inside_loop = 0x0E, split_s = 0x11, half_loop = 0x14,
                       barrel_roll = 0x17, inverted = 0x1A, entry_0 = 0x23, break_turn = 0x27;
} // namespace role

struct PilotWorld {
    std::uint8_t mode{};      // $13AB96
    bool practice_ready{};    // $13AA8A negative: the practice's trigger
    bool practice{};          // $13AA83 (the practice's longer pause after a maneuver)
};

class Autopilot {
public:
    explicit Autopilot(const amiga::LoadedImage& program);

    // `$120B6E`: one frame of the record's virtual pilot (before its flight
    // step). `pilot` is the record's +$64 byte.
    void fly(Aircraft& a, std::uint8_t& pilot, const PilotWorld& world) const;
    // `$121D4C`, in the attitude step: roles 6 and 10 roll the wings level,
    // by the roll the frame started with; the next frame flies by it.
    static void level_out(Aircraft& a, std::int16_t roll_before);

private:
    // The rate tables (`$121406`, `$12141C`, `$121432`, `$121448`).
    std::int16_t pitch_rate(int index) const { return program_.s16(0x12141C + std::uint32_t(2 * index)); }
    std::int16_t roll_rate(int index) const { return program_.s16(0x121432 + std::uint32_t(2 * index)); }

    const amiga::LoadedImage& program_;
};

} // namespace interceptor::game
