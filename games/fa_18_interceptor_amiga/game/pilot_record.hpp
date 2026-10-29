#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The pilot's record, the game's `config` file (78 bytes, big-endian;
// game.md "The front end"): call sign, launches, flight time, the flight
// log's numbers and which missions are open. The original reads it after
// the credits and writes it when a call sign is entered and from the
// flight log's menu; the port does the same to the `config` file of the
// imported game.
namespace interceptor::game {

struct PilotRecord {
    bool qualified = false;           // +0
    std::uint16_t word_2 = 0;         // +2 (not traced)
    std::uint16_t launches = 0;       // +4: 0 for a new pilot
    std::uint8_t last_mission = 0;    // +6: the last advanced mission
    std::uint8_t byte_7 = 0;          // +7 (not traced)
    std::uint32_t flight_seconds = 0; // +8
    std::array<std::uint8_t, 0x12> bytes_c{}; // +$C-$1D (+$14-$19: missions 3-8 open)
    std::string call_sign;            // +$1E, 24 bytes, zero-terminated
    // +$36-$49: the flight log's numbers.
    std::uint16_t missions_started = 0, missions_completed = 0;
    std::uint16_t cannon_fired = 0, cannon_hits = 0;
    std::uint16_t amraams_fired = 0, amraam_hits = 0;
    std::uint16_t sidewinders_fired = 0, sidewinder_hits = 0;
    std::uint16_t crashes = 0, missile_hits = 0;
    std::array<std::uint8_t, 4> tail{}; // +$4A-$4D (not traced)

    static constexpr std::size_t size = 78;
    static PilotRecord decode(const std::vector<std::uint8_t>& bytes);
    std::vector<std::uint8_t> encode() const;

    // $108556 (the flight log's Shift 2): the whole record cleared, call
    // sign and launches too: a new pilot.
    void reset() { *this = PilotRecord{}; }
};

// `config` in the game's folder; a missing or short file is a new pilot.
PilotRecord read_pilot(const std::filesystem::path& content);
// Writes it back ($10858A); false when the folder cannot be written.
bool write_pilot(const std::filesystem::path& content, const PilotRecord& pilot);

} // namespace interceptor::game
