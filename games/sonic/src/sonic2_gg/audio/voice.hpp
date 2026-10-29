#pragma once
#include "gg_engine/audio/voice.hpp"
#include "sonic2_gg/data/assets.hpp"

// The sampled "SEGA" voice on the logo screen (gg_engine/audio/voice.hpp),
// played by the routine at bank 3, 8000.
namespace sonic2_gg {

extern const gg_engine::VoiceProfile voice_profile;

class LogoVoice : public gg_engine::LogoVoice {
public:
    explicit LogoVoice(const Assets& rom) : gg_engine::LogoVoice(rom, voice_profile) {}
};

} // namespace sonic2_gg
