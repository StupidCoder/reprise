// The music player. See music_player.hpp.
#include "shell/music_player.hpp"
#include "graphics/a3_assets.hpp"
#include "graphics/text.hpp"
#include "menus/a1_bonus.hpp"
#include "platform/host.hpp"
#include "puzzles/a3_puzzle_loop.hpp"
#include "shell/library.hpp"
#include "sound/a1_sound.hpp"
#include "sound/a3_sound.hpp"
#include "system/resources.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace picarat::shell {

namespace {

// Second adventure: overlay 23 (loaded at 020efda0), its track list.
constexpr std::uint32_t kBonusOverlayLoad = 0x020efda0, kTrackList2 = 0x020f3b80;
constexpr int kTracks2 = 19; // 020f2200: the page's loops end at 19

// Third adventure: overlay 31 (loaded at 0211df00), its {kind, ID} pairs.
constexpr int kSoundOverlay = 31;
constexpr std::uint32_t kSoundOverlayLoad = 0x0211df00, kTrackList3 = 0x0211e54c;
constexpr int kTracks3 = 26;           // 0211e02c: the page's loops end at 26
constexpr int kTrack3StartFrames = 30; // 0211e388: 30 frames before the start

std::uint32_t word(const Bytes& code, std::uint32_t load, std::uint32_t address) {
    const std::size_t i = address - load;
    if (i + 4 > code.size()) {
        throw std::runtime_error("track list outside the program");
    }
    return std::uint32_t(code[i]) | std::uint32_t(code[i + 1]) << 8 |
           std::uint32_t(code[i + 2]) << 16 | std::uint32_t(code[i + 3]) << 24;
}

std::string lineOf(const Bytes& bytes) {
    std::string text(bytes.begin(), bytes.end());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == 0)) {
        text.pop_back();
    }
    return text;
}

} // namespace

std::vector<MusicTrack> adventure2Music(const std::filesystem::path& pack) {
    const auto overlay = pack / "code" / "overlay23.bin";
    if (!fileExists(overlay)) {
        return {};
    }
    const Bytes code = readFile(overlay);
    const GameText text(pack);
    std::vector<MusicTrack> tracks;
    for (int i = 0; i < kTracks2; ++i) {
        MusicTrack track;
        track.id = int(word(code, kBonusOverlayLoad, kTrackList2 + 4 * i));
        // 020f26fc: ID 1 and 29..31 are streamed pieces.
        track.streamed = track.id == 1 || (track.id >= 29 && track.id <= 31);
        // 020f2638: the title, music_<index>.txt.
        const auto title = pack / "text" / ("music_" + std::to_string(i) + ".txt");
        track.title = fileExists(title) ? utf8FromCp1252(text.expand(lineOf(readFile(title))))
                                        : "Track " + std::to_string(i + 1);
        tracks.push_back(track);
    }
    return tracks;
}

std::vector<MusicTrack> adventure3Music(const std::filesystem::path& pack) {
    adventure3::Assets assets(pack);
    const GameText text(pack);
    // The titles: rows {id, -, title} of txt/?/40 part 1400, keyed 1..26.
    std::map<int, std::string> titles;
    const std::string table = "txt/?/40/40_001400.lbin";
    if (assets.exists(table)) {
        const auto script = assets.script(table);
        for (const auto& c : script.commands) {
            if (c.opcode == 6001 && c.operands.size() >= 3) {
                titles[int(std::int32_t(c.operands[0].value))] = script.text(c.operands[2]);
            }
        }
    }
    std::vector<MusicTrack> tracks;
    for (int i = 0; i < kTracks3; ++i) {
        const std::uint32_t entry = kTrackList3 + 8 * std::uint32_t(i);
        MusicTrack track;
        track.streamed = assets.codeWord(kSoundOverlay, kSoundOverlayLoad, entry) == 1;
        track.id = int(assets.codeWord(kSoundOverlay, kSoundOverlayLoad, entry + 4));
        const auto title = titles.find(i + 1); // 0211e224: row index + 1
        track.title = title != titles.end() ? utf8FromCp1252(text.expand(title->second))
                                            : "Track " + std::to_string(i + 1);
        tracks.push_back(track);
    }
    return tracks;
}

std::vector<MusicTrack> adventure1Music(const std::filesystem::path& pack,
                                        const std::string& language) {
    const GameText text(pack);
    std::vector<MusicTrack> tracks;
    for (int i = 0; i < 19; ++i) {
        MusicTrack track;
        track.streamed = i == 0;
        track.id = i == 0 ? 0 : adventure1::kMusicRoomSequences[i];
        // 02030f3c: the title, otext music<n>.txt.
        const auto title =
            pack / "otext" / language / "otext" / ("music" + std::to_string(i + 1) + ".txt");
        track.title = fileExists(title) ? utf8FromCp1252(text.expand(lineOf(readFile(title))))
                                        : "Track " + std::to_string(i + 1);
        tracks.push_back(track);
    }
    return tracks;
}

std::unique_ptr<MusicPlayer> MusicPlayer::adventure1(const std::filesystem::path& pack,
                                                     const std::string& language) {
    std::unique_ptr<MusicPlayer> player(new MusicPlayer());
    player->adventure1_ = true;
    player->tracks_ = adventure1Music(pack, language);
    if (player->tracks_.empty() || !fileExists(pack / "stream" / "BGM_09.SAD")) {
        throw std::runtime_error("the imported game lacks its music");
    }
    player->sound1_ = std::make_unique<picarat::Adventure1Sound>(pack);
    MusicPlayer* self = player.get();
    player->device1_ =
        std::make_unique<platform::AudioStream>(32728, [self](std::int16_t* stereo, int frames) {
            if (self->silent_) {
                std::fill(stereo, stereo + std::ptrdiff_t(frames) * 2, std::int16_t(0));
            } else {
                self->sound1_->render({stereo, std::size_t(frames) * 2});
            }
            std::lock_guard lock(self->recentMutex_);
            for (int i = 0; i < frames; ++i) {
                self->recent_[self->recentAt_] =
                    (float(stereo[2 * i]) + float(stereo[2 * i + 1])) / 65536.0f;
                self->recentAt_ = (self->recentAt_ + 1) % self->recent_.size();
            }
        });
    // 0203090c: the room's sounds (kind 3).
    picarat::Adventure1Sound& sound = *player->sound1_;
    sound.setupScene(3, [&sound] {
        sound.frame();
        std::this_thread::sleep_for(std::chrono::microseconds(16715));
    });
    return player;
}

std::vector<float> MusicPlayer::waveform(std::size_t count) {
    if (!adventure1_) {
        return audio_->recent(count);
    }
    std::lock_guard lock(recentMutex_);
    count = std::min(count, recent_.size());
    std::vector<float> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = recent_[(recentAt_ + recent_.size() - count + i) % recent_.size()];
    }
    return out;
}

void MusicPlayer::play1(const MusicTrack& t) {
    // 02030f3c, the waits left out: the stream (channels 10 / 11, full
    // volume), or the sequence loaded (with the room's sounds) and played.
    picarat::Adventure1Sound& sound = *sound1_;
    // Loading waits for the sounds to end: a game frame at a time.
    const auto wait = [&sound] {
        sound.frame();
        std::this_thread::sleep_for(std::chrono::microseconds(16715));
    };
    if (t.streamed) {
        sound.playStream("BGM_09.SAD", 10, 0xb, 0x7f);
        return;
    }
    sound.forgetLoadedMusic();
    sound.loadMusic(t.id, wait);
    sound.setupScene(3, wait);
    sound.playMusic(t.id);
    sound.restoreMusicVolume();
}

std::unique_ptr<MusicPlayer> MusicPlayer::adventure2(const std::filesystem::path& pack) {
    std::unique_ptr<MusicPlayer> player(new MusicPlayer());
    player->tracks_ = adventure2Music(pack);
    player->audio_ = std::make_unique<AudioOutput>(pack);
    return player;
}

std::unique_ptr<MusicPlayer> MusicPlayer::adventure3(const std::filesystem::path& pack) {
    std::unique_ptr<MusicPlayer> player(new MusicPlayer());
    player->adventure3_ = true;
    player->tracks_ = adventure3Music(pack);
    player->audio_ = std::make_unique<AudioOutput>(picarat::SoundLayout::adventure3(pack));
    adventure3::Assets assets(pack);
    player->sound3_ = std::make_unique<picarat::Adventure3Sound>(*player->audio_->sequenced(),
                                                                 adventure3::soundSets(assets));
    return player;
}

MusicPlayer::~MusicPlayer() = default;

void MusicPlayer::stopTrack() {
    if (track_ < 0) {
        return;
    }
    if (state_ == State::Paused && !adventure1_) {
        audio_->sequenced()->resumeSound();
    }
    if (adventure1_) {
        silent_ = false;
        sound1_->closeStream();
        sound1_->stopMusic(1);
        state_ = State::Stopped;
        return;
    }
    if (adventure3_) {
        // 0211e1e0: the current kind's stop, at once.
        if (tracks_[std::size_t(track_)].streamed) {
            sound3_->stopStream(0);
        } else {
            sound3_->stopMusic(0);
        }
    } else {
        // 020f2568 / 020f2574: the music and the stream, at once.
        audio_->sequenced()->stopBgm(0);
        audio_->sequenced()->stopStream(0);
    }
    state_ = State::Stopped;
}

void MusicPlayer::play(int index) {
    if (index < 0 || index >= int(tracks_.size())) {
        return;
    }
    stopTrack();
    track_ = index;
    frames_ = 0;
    const MusicTrack& t = tracks_[std::size_t(index)];
    if (adventure1_) {
        play1(t);
        state_ = State::Playing;
        return;
    }
    if (adventure3_) {
        // 0211e350: open the stream or load the music, start it later.
        if (t.streamed) {
            sound3_->openStream(picarat::Adventure3Sound::Music, t.id);
        } else {
            sound3_->loadMusic(t.id, kTrack3StartFrames);
        }
        countdown_ = kTrack3StartFrames;
        state_ = State::Starting;
        return;
    }
    // 020f26f4: a streamed piece on route 1, or sequenced music.
    if (t.streamed) {
        audio_->sequenced()->playStream(1, t.id, -1, 1.0f, 0, false);
    } else {
        audio_->sequenced()->playBgm(t.id, 1.0f, 0);
    }
    state_ = State::Playing;
}

void MusicPlayer::togglePause() {
    if (adventure1_ && (state_ == State::Playing || state_ == State::Paused)) {
        silent_ = state_ == State::Playing;
        state_ = silent_ ? State::Paused : State::Playing;
        return;
    }
    if (state_ == State::Playing) {
        audio_->sequenced()->pauseSound();
        state_ = State::Paused;
    } else if (state_ == State::Paused) {
        audio_->sequenced()->resumeSound();
        state_ = State::Playing;
    } else if (state_ == State::Stopped) {
        play(track_ < 0 ? 0 : track_);
    }
}

void MusicPlayer::stop() {
    stopTrack();
    frames_ = 0;
}

void MusicPlayer::tick() {
    if (adventure1_) {
        // The game's sound frame (020964e0) once a frame, not while paused.
        if (state_ != State::Paused) {
            sound1_->frame();
        }
        if (state_ == State::Playing) {
            ++frames_;
        }
        return;
    }
    if (state_ == State::Starting && --countdown_ <= 0) {
        // 0211e4bc: start the opened stream or the loaded music. The game
        // also waits for its file loader (02063334); Picarat loads at once.
        if (tracks_[std::size_t(track_)].streamed) {
            sound3_->startStream(1.0f, 0, 0);
        } else {
            sound3_->playMusic(1.0f, 0, 1);
        }
        state_ = State::Playing;
    }
    if (state_ == State::Playing) {
        ++frames_;
    }
    if (state_ != State::Paused) {
        audio_->sequenced()->frame();
    }
    audio_->check();
}

} // namespace picarat::shell
