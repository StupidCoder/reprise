// The logo voice (bank 1A, 8000); see voice.hpp.

#include "triple_trouble_gg/audio/voice.hpp"

namespace triple_trouble_gg {

// The routine's instruction timing: the set-up's first OUT at 88; each of
// the 7568 sample bytes (1A:8061) takes 703 clocks, its volume writes end
// 309, 327, 345 (high nibble) and 627, 645, 663 (low nibble) clocks into it.
const gg_engine::VoiceProfile voice_profile{
    .bank = 0x1A,
    .address = 0x8061,
    .samples = 0x1D90,
    .first_out = 88,
    .period = 703,
    .high_write = 309,
    .low_write = 627,
};

} // namespace triple_trouble_gg
