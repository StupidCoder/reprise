#pragma once
#include "amiga/paula_channels.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The game's sound effects player (the file "effects", loaded at $3400;
// its routine $344A runs from the CIA-B interrupt at 49.92 Hz while a
// level plays). Its tables of effects, envelopes and samples are read
// from the file; the game asks for an effect per channel with a volume
// (the request block at $36D2). Evidence: game.md, "Sound".
namespace spindizzy::sound {

class EffectsPlayer {
public:
    explicit EffectsPlayer(std::vector<std::uint8_t> file);

    // The request block: an effect for a channel (0-3) and its volume
    // ($40 full); the last request before a call wins.
    void request(int channel, int effect, int volume);
    // A channel's volume word alone ($36D6 + 2n).
    void set_volume(int channel, int volume) { channels_[std::size_t(channel & 3)].volume = volume; }
    // $3400: every request and channel cleared, the channels silent.
    void reset(amiga::PaulaChannels&);
    // One call of $344A.
    void tick(amiga::PaulaChannels&);

    std::span<const std::int8_t> memory() const { return memory_; }
    // The effect a channel plays ($36EA + n; −1 none).
    int playing(int channel) const { return channels_[std::size_t(channel & 3)].playing; }

private:
    struct Channel {
        int requested{-1};   // $36D2 + n ($FF none)
        int volume{0x40};    // $36D6 + 2n
        int busy{};          // $36DE + n
        int duration{};      // $36E2 + n ($FF: no end)
        int sample{};        // $36E6 + n
        int playing{-1};     // $36EA + n
        std::uint32_t start{}, repeat{}; // $366E + 16n, + 6
        int length{}, repeat_length{};    // + 4, + $A
        int envelope_volume{};            // + $C
        int envelope{};                   // $36AE + 2n: envelope offset
        int position{}, countdown{}, repeats{}; // $36B6, $36BA, $36BE + n
        int period{};                     // $36CA + 2n
    };
    std::uint8_t byte(std::uint32_t address) const { return file_[address - 0x3400]; }
    std::uint16_t word(std::uint32_t address) const { return std::uint16_t(byte(address) << 8 | byte(address + 1)); }
    std::uint32_t longword(std::uint32_t address) const { return std::uint32_t(word(address)) << 16 | word(address + 2); }

    std::vector<std::uint8_t> file_;
    std::vector<std::int8_t> memory_;
    std::array<Channel, 4> channels_{};
};

} // namespace spindizzy::sound
