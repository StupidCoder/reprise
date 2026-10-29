#pragma once
#include "gamegear/music_player.hpp"
#include "gg_engine/audio/sound_driver.hpp"
#include "gg_engine/audio/voice.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace gg_engine {

// A sound test track: a song request, or -1 for the logo voice.
struct SoundTestTrack {
    int song;
    const char* title;
    const char* description;
};

// What the sound test needs of a game: its cartridge, driver and voice, the
// request slot songs go to, and its tracks.
struct SoundTest {
    const CartridgeInfo& cartridge;
    const DriverProfile& driver;
    const VoiceProfile& voice;
    unsigned song_slot;
    std::span<const SoundTestTrack> tracks;
};

// The sound test's tracks on the game's sound driver.
class MusicPlayer final : public gg::MusicPlayer {
public:
    MusicPlayer(const SoundTest& game, const std::filesystem::path& pack);

private:
    SoundTest game_;
    Assets assets_;
    std::unique_ptr<SoundDriver> sound_;
    std::optional<LogoVoice> voice_;
    unsigned resting_{}; // frames with every music channel resting

    void start(unsigned track) override;
    std::vector<float> frame(gg::Psg& psg, bool& active) override;
    void release() override;
};

} // namespace gg_engine
