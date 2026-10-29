#pragma once
#include "gg_engine/audio/sound_driver.hpp"
#include "triple_trouble_gg/data/assets.hpp"
#include "triple_trouble_gg/game/types.hpp"

// Triple Trouble's revision of the shared sound driver (gg_engine/audio/
// sound_driver.hpp): bank 2, 4C42-5569, the second bytecode bank 3.
namespace triple_trouble_gg {

using gg_engine::SoundChannel;
using gg_engine::SoundState;
using gg_engine::SoundWrite;
namespace sound_channel = gg_engine::sound_channel;

extern const gg_engine::DriverProfile sound_profile;

class SoundDriver : public gg_engine::SoundDriver {
public:
    explicit SoundDriver(const Assets& rom) : gg_engine::SoundDriver(rom, sound_profile) {}
};

} // namespace triple_trouble_gg
