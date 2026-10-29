#pragma once
#include "gamegear/music_player.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/audio/voice.hpp"
#include <filesystem>
#include <memory>

namespace sonic1_gg {
// The sound test's tracks (catalog.hpp) on the game's sound driver.
class MusicPlayer final : public gg::MusicPlayer {
public:
    explicit MusicPlayer(const std::filesystem::path& assets);

private:
    SoundData data_;
    Bytes voice_data_;
    std::unique_ptr<SoundDriver> sound_;
    std::unique_ptr<LogoVoice> voice_;

    void start(unsigned track) override;
    std::vector<float> frame(gg::Psg& psg, bool& active) override;
    void release() override;
};
} // namespace sonic1_gg
