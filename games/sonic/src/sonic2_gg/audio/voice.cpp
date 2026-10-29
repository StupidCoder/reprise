// The logo voice (bank 3, 8000); see voice.hpp.

#include "sonic2_gg/audio/voice.hpp"

namespace sonic2_gg {

// The routine's instruction timing: the set-up's first OUT at 78; each of
// the 3E7F sample bytes (3:805E) takes 339 clocks, its volume writes end
// 127, 145, 163 (high nibble) and 263, 281, 299 (low nibble) clocks into it.
const gg_engine::VoiceProfile voice_profile{
    .bank = 0x03,
    .address = 0x805E,
    .samples = 0x3E7F,
    .first_out = 78,
    .period = 339,
    .high_write = 127,
    .low_write = 263,
};

} // namespace sonic2_gg
