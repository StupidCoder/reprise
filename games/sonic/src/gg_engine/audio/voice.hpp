#pragma once
#include "gamegear/psg.hpp"
#include "gg_engine/data/assets.hpp"
#include "gg_engine/types.hpp"
#include <vector>

// The sampled "SEGA" voice on the logo screen.
//
// Both cartridges play it with a routine that runs with interrupts off: it
// sets the three tone channels to period 1 (a constant output) and then
// writes 4-bit volumes, two per sample byte (high nibble first, inverted),
// to all three channels. The playback rate comes from the routine's
// instruction timing, so the writes are stamped with their CPU clock time
// and replayed through the PSG at exactly those times. The two revisions of
// the routine differ only in the timing and in where the samples lie
// (VoiceProfile).
namespace gg_engine {

// A cartridge's voice routine: its samples and its instruction timing, in
// CPU clocks (each write at the end of its OUT).
struct VoiceProfile {
    u8 bank;            // where the routine's sample bytes lie
    unsigned address;
    unsigned samples;   // sample bytes
    unsigned first_out; // the set-up's first register write, from the routine's entry
    unsigned period;    // clocks per sample byte
    // The first of each nibble's three volume writes (then 18 clocks apart),
    // counted from the start of the sample byte.
    unsigned high_write, low_write;
};

class LogoVoice {
public:
    LogoVoice(const Assets& rom, const VoiceProfile& profile);

    struct Write {
        unsigned clock;
        std::uint8_t value;
    };

    std::vector<Write> writes;
    unsigned duration{}; // CPU clocks until the routine returns

    void start() {
        position_ = clock_ = 0;
        playing_ = true;
    }

    bool playing() const { return playing_; }

    // Plays the writes due within the next `clocks` CPU clocks.
    std::vector<float> render(gg::Psg&, unsigned clocks);

private:
    std::size_t position_{};
    unsigned clock_{};
    bool playing_{};
};

} // namespace gg_engine
