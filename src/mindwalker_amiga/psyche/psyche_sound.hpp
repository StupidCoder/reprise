#pragma once
#include "amiga/hunk.hpp"
#include "amiga/paula_voices.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The psyche screen's music (P h14, $5026BC-$502B8C): timer.device paces a
// four-voice sequencer, a tick every 60 ms. Each voice steps through its
// own note pattern (P's data at $502E14, a note every 8 ticks, 0 a rest,
// $7F back to the start), the note's period from P's table (h18), its
// volume from a falling envelope ($502E2C; voices 2 and 3 fall at half
// speed). While a hint line climbs, the four voices instead hum at a pitch
// set by its height ($502A3E). While a tile flies home the ticks come every
// 20 ms, the voices play a short figure ($502E04) on an eight-sample wave
// ($502E24). Once the last picture is whole, a second sequencer plays the
// finale (h17 $502EC0, $503178): per voice a list of patterns ($504492),
// each a run of four-byte steps (note, length, slide and volume curve, wave
// and quietening), on ten waves; once round, everything a step quieter.
namespace mindwalker::psyche {

class PsycheSound {
public:
    // `sample` P's sample (h24, shifted), `finale_sample` h27 (shifted),
    // `waves` the six 64-sample waves (h30).
    PsycheSound(const amiga::LoadedImage& psyche, std::vector<std::int8_t> sample, std::vector<std::int8_t> finale_sample,
                const std::vector<std::vector<std::int8_t>>& waves);

    // One display frame (20 ms) of time. `hint` the climbing hint line's
    // height (0: none).
    void frame(int hint);
    void set_fast(bool fast);   // $50277C / $5027DC
    void stop();                // $502BCC: all voices silent, DMA off
    void start_finale();        // h17
    void start() { voices_.set_dma(true); }
    void render(std::span<float> stereo, int rate) { voices_.render(stereo, rate); }

    // For replays: each voice's period and volume, the tick count.
    struct Voice {
        int period{200}, volume{}, step{}, wait{}, envelope{0x10};
    };
    const std::array<Voice, 4>& voices() const { return voice_; }
    int ticks() const { return ticks_; }

private:
    void tick(int hint);        // $5026BC's work when the request comes back
    void voice_tick(int v);     // $502840
    void finale_tick(int v);    // $503178

    const amiga::LoadedImage& p_;
    amiga::PaulaVoices voices_;
    std::vector<std::int8_t> sample_, wave_;
    std::array<Voice, 4> voice_{};
    int ticks_{};               // $502EA4 (a byte)
    bool fast_{};               // $502EAA
    int timer_left_{60000};     // microseconds until the request comes back
    // The finale ($50361C on, 26 bytes a voice).
    struct FinaleVoice {
        std::uint32_t pattern{};
        int list{1}, step{}, wait{}, envelope{0xF}, curve{}, quieter{};
        bool slide{};
        int period{200};
    };
    bool finale_{};
    std::array<FinaleVoice, 4> finale_voice_{};
    int finale_ticks_{}, finale_quieter_{1};   // $503684, $503685
    std::array<std::vector<std::int8_t>, 10> finale_waves_; // h19 $5035F4 on
};

} // namespace mindwalker::psyche
