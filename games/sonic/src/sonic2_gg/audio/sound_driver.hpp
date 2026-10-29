#pragma once
#include "gg_engine/audio/sound_driver.hpp"
#include "sonic2_gg/data/assets.hpp"
#include "sonic2_gg/game/types.hpp"

// Sonic 2's revision of the shared sound driver (gg_engine/audio/
// sound_driver.hpp): bank 2, 4000-4922, the second bytecode bank 1B.
namespace sonic2_gg {

using gg_engine::SoundChannel;
using gg_engine::SoundState;
using gg_engine::SoundWrite;
namespace sound_channel = gg_engine::sound_channel;

extern const gg_engine::DriverProfile sound_profile;

class SoundDriver : public gg_engine::SoundDriver {
public:
    explicit SoundDriver(const Assets& rom) : gg_engine::SoundDriver(rom, sound_profile) {}
};

} // namespace sonic2_gg
