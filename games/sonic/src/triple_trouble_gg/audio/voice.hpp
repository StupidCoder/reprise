#pragma once
#include "gg_engine/audio/voice.hpp"
#include "triple_trouble_gg/data/assets.hpp"

// The sampled "SEGA" voice on the logo screen (gg_engine/audio/voice.hpp),
// played by the routine at bank 1A, 8000.
namespace triple_trouble_gg {

extern const gg_engine::VoiceProfile voice_profile;

class LogoVoice : public gg_engine::LogoVoice {
public:
    explicit LogoVoice(const Assets& rom) : gg_engine::LogoVoice(rom, voice_profile) {}
};

} // namespace triple_trouble_gg
