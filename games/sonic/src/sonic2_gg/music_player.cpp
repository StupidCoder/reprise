#include "sonic2_gg/music_player.hpp"
#include "sonic2_gg/audio/sound_driver.hpp"
#include "sonic2_gg/audio/voice.hpp"
#include "sonic2_gg/catalog.hpp"
#include "sonic2_gg/data/rom.hpp"

namespace sonic2_gg {

std::unique_ptr<gg::MusicPlayer> make_music_player(const std::filesystem::path& pack) {
    // Songs go to request slot 0.
    static const gg_engine::SoundTest game{cartridge, sound_profile, voice_profile, 0,
                                           music_tracks};
    return std::make_unique<gg_engine::MusicPlayer>(game, pack);
}

} // namespace sonic2_gg
