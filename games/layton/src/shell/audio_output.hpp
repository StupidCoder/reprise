#pragma once
// Plays the game's sound through the platform's sound device. The game
// thread calls the sound object through a proxy that takes a lock, while the
// audio thread asks SoundEngine for samples; the lock plays the role of the
// original's "interrupts off" sections between game and sound code. The
// output also works without a device for tests (renderOffline).
#include "platform/host.hpp"
#include "sound/sound_engine.hpp"
#include "system/audio_service.hpp"
#include <array>
#include <memory>
#include <mutex>
#include <string>

namespace picarat::shell {

class AudioOutput final : public picarat::AudioService {
public:
    explicit AudioOutput(std::filesystem::path assets, bool useDevice = true);
    // Any game's sound (the third adventure's layout, for instance).
    explicit AudioOutput(picarat::SoundLayout layout, bool useDevice = true);
    ~AudioOutput();
    void reset() override;

    picarat::GameSound* sequenced() override { return &proxy_; }

    void check();
    std::vector<std::int16_t> renderOffline(unsigned frames);
    std::vector<std::string> events();
    // The last `count` output frames (up to kHistory), mono, -1..1, oldest
    // first: what the music player's waveform shows.
    static constexpr std::size_t kHistory = 4096;
    std::vector<float> recent(std::size_t count);

private:
    // Serialises scene calls with the audio callback (the original's
    // interrupt-masked sections between the game and sound threads).
    struct Proxy final : picarat::GameSound {
        AudioOutput& o;

        explicit Proxy(AudioOutput& owner) : o(owner) {}

        template <class F, class... A>
        void call(const char* name, F&& f, A... args) {
            std::lock_guard lock(o.mutex_);
            std::string e = name;
            ((e += " " + std::to_string(args)), ...);
            o.events_.push_back(e);
            f(o.engine_->sound());
        }

        void frame() override {
            std::lock_guard lock(o.mutex_);
            o.engine_->sound().frame();
        }

        void setScene(std::uint8_t s) override {
            std::lock_guard lock(o.mutex_);
            o.engine_->sound().setScene(s);
        }

        void roomSound(int id, bool play) override {
            call("room", [&](auto& s) { s.roomSound(id, play); }, id, int(play));
        }

        void loadBank(unsigned g, int n) override {
            call("bank", [&](auto& s) { s.loadBank(g, n); }, g, n);
        }

        void unloadBank(unsigned g) override {
            call("unbank", [&](auto& s) { s.unloadBank(g); }, g);
        }

        void playEffect(int id) override {
            call("se", [&](auto& s) { s.playEffect(id); }, id);
        }

        void stopEffect(int id, float t) override {
            call("se-stop", [&](auto& s) { s.stopEffect(id, t); }, id, t);
        }

        bool effectPlaying(int id) override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().effectPlaying(id);
        }

        bool playTrackedEffect(int id) override {
            bool r = false;
            call("se-tracked", [&](auto& s) { r = s.playTrackedEffect(id); }, id);
            return r;
        }

        bool trackedBusy() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().trackedBusy();
        }

        bool playUniqueEffect(int id) override {
            bool r = false;
            call("se-unique", [&](auto& s) { r = s.playUniqueEffect(id); }, id);
            return r;
        }

        void stopUniqueEffect(float t) override {
            call("se-unique-stop", [&](auto& s) { s.stopUniqueEffect(t); }, t);
        }

        void playBgm(int track, float v, float t) override {
            call("bgm", [&](auto& s) { s.playBgm(track, v, t); }, track, v, t);
        }

        void stopBgm(float t) override {
            call("bgm-stop", [&](auto& s) { s.stopBgm(t); }, t);
        }

        void switchBgm(int track) override {
            call("bgm-switch", [&](auto& s) { s.switchBgm(track); }, track);
        }

        int currentBgm() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().currentBgm();
        }

        void restartBgm(float v, float t) override {
            call("bgm-restart", [&](auto& s) { s.restartBgm(v, t); }, v, t);
        }

        void fadeOutBgm(float t) override {
            call("bgm-fade", [&](auto& s) { s.fadeOutBgm(t); }, t);
        }

        void bgmVolume(float v, float t) override {
            call("bgm-volume", [&](auto& s) { s.bgmVolume(v, t); }, v, t);
        }

        float duration(int id) override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().duration(id);
        }

        bool playStream(int route, int id, int part, float v, float t, bool meter) override {
            bool r = false;
            o.streamEvent("play", route, id, part);
            call(
                "stream", [&](auto& s) { r = s.playStream(route, id, part, v, t, meter); }, route,
                id, part, v, t, int(meter));
            return r;
        }

        bool playTrackedStream(int route, int id, int part, float v, float t) override {
            bool r = false;
            o.streamEvent("play", route, id, part);
            call(
                "stream-tracked", [&](auto& s) { r = s.playTrackedStream(route, id, part, v, t); },
                route, id, part, v, t);
            return r;
        }

        void stopStream(float t) override {
            {
                std::lock_guard lock(o.mutex_);
                o.events_.push_back("stop " + std::to_string(t));
            }
            call("stream-stop", [&](auto& s) { s.stopStream(t); }, t);
        }

        bool streamBusy() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().streamBusy();
        }

        bool streamIdle() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().streamIdle();
        }

        bool streamLoud() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().streamLoud();
        }

        void pauseSound() override {
            call("pause", [&](auto& s) { s.pauseSound(); });
        }

        void resumeSound() override {
            call("resume", [&](auto& s) { s.resumeSound(); });
        }

        bool prepareStream(int route, int id, int part, bool meter) override {
            bool r = false;
            o.streamEvent("play", route, id, part);
            call(
                "stream-prepare", [&](auto& s) { r = s.prepareStream(route, id, part, meter); },
                route, id, part, int(meter));
            return r;
        }

        void startStream(float v, float t) override {
            call("stream-start", [&](auto& s) { s.startStream(v, t); }, v, t);
        }

        void stopStreamFade(float t) override {
            call("stream-stop-fade", [&](auto& s) { s.stopStreamFade(t); }, t);
        }

        void restartStream(float v, float t) override {
            call("stream-restart", [&](auto& s) { s.restartStream(v, t); }, v, t);
        }

        void streamVolume(float v, float t) override {
            call("stream-volume", [&](auto& s) { s.streamVolume(v, t); }, v, t);
        }

        void seekStream(float t) override {
            call("stream-seek", [&](auto& s) { s.seekStream(t); }, t);
        }

        float streamPosition() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().streamPosition();
        }

        bool streamOpen() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().streamOpen();
        }

        bool bgmSettled() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().bgmSettled();
        }

        bool streamOpening() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().streamOpening();
        }

        bool streamStarting() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().streamStarting();
        }

        bool bgmBusy() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().bgmBusy();
        }

        bool canSleep() override {
            std::lock_guard lock(o.mutex_);
            return o.engine_->sound().canSleep();
        }
    };

    picarat::SoundLayout layout_;
    std::mutex mutex_;
    std::unique_ptr<picarat::SoundEngine> engine_;
    Proxy proxy_{*this};
    bool closing_ = false;
    std::array<float, kHistory> history_{}; // a ring of mono output frames
    std::size_t historyAt_ = 0;
    std::string error_;
    std::vector<std::string> events_;
    void renderLocked(std::span<std::int16_t> stereo);
    // "play <path below /data_lt2/stream/>" for the self-test's stream check.
    void streamEvent(const char* name, int route, int id, int part);
    // Runs on the audio thread: fills the device's buffer.
    void fill(std::int16_t* stereo, int frames) noexcept;
    std::unique_ptr<platform::AudioStream> device_; // last: created after the engine
};

} // namespace picarat::shell
