#include "triple_trouble_gg/music_player.hpp"
#include "triple_trouble_gg/audio/sound_driver.hpp"
#include "triple_trouble_gg/audio/voice.hpp"
#include "triple_trouble_gg/catalog.hpp"
#include "triple_trouble_gg/data/rom.hpp"

namespace triple_trouble_gg {

std::unique_ptr<gg::MusicPlayer> make_music_player(const std::filesystem::path& pack) {
    // Songs go to request slot 2.
    static const gg_engine::SoundTest game{cartridge, sound_profile, voice_profile, 2,
                                           music_tracks};
    return std::make_unique<gg_engine::MusicPlayer>(game, pack);
}

} // namespace triple_trouble_gg
