#include "mindwalker_amiga/psyche/psyche_sound.hpp"
#include <algorithm>

namespace mindwalker::psyche {
namespace {

constexpr std::uint32_t patterns = 0x502E14;   // 4 pointers to note bytes
constexpr std::uint32_t fast_notes = 0x502E04; // 4 notes per voice
constexpr std::uint32_t fast_wave = 0x502E24;  // 8 samples
constexpr std::uint32_t envelope = 0x502E2C;   // 17 volume words
constexpr std::uint32_t periods = 0x503490;    // h18: a period per note
constexpr std::uint32_t finale_lists = 0x504492;  // per voice: pattern numbers, 0 = round again
constexpr std::uint32_t finale_patterns = 0x504314; // pattern pointers
constexpr std::uint32_t finale_curves = 0x50354A;   // volume curves, 16 words each
constexpr std::uint32_t finale_first = 0x504318;    // the voices' first pattern
constexpr std::uint32_t wave8 = 0x50350E, wave4 = 0x50350A, ramp = 0x503516; // h17's small waves
constexpr int frame_us = 20000;

} // namespace

PsycheSound::PsycheSound(const amiga::LoadedImage& p, std::vector<std::int8_t> sample, std::vector<std::int8_t> finale_sample,
                         const std::vector<std::vector<std::int8_t>>& waves)
    : p_(p), sample_(std::move(sample)) {
    // h17's waves: 8 and 4 samples from P's data, the finale's sample, a
    // 32-sample blend of a ramp with itself, the six waves.
    for (std::uint32_t i = 0; i < 8; ++i) finale_waves_[0].push_back(std::int8_t(p_.u8(wave8 + i)));
    for (std::uint32_t i = 0; i < 4; ++i) finale_waves_[1].push_back(std::int8_t(p_.u8(wave4 + i)));
    finale_waves_[2] = std::move(finale_sample);
    for (std::uint32_t i = 0; i < 32; ++i)
        finale_waves_[3].push_back(std::int8_t(std::int8_t(p_.u8(ramp + i)) / 2 + std::int8_t(p_.u8(ramp + (i & 15))) / 2));
    for (std::size_t w = 0; w < 6 && w < waves.size(); ++w) finale_waves_[4 + w] = waves[w];
    for (std::uint32_t i = 0; i < 0x800; ++i) wave_.push_back(std::int8_t(p_.u8(fast_wave + (i & 7))));
    // $50265A: the four voices on the sample, silent, DMA on.
    for (int v = 0; v < 4; ++v) {
        voices_.set_samples(v, sample_);
        voices_.set_volume(v, 0);
        voices_.set_period(v, 200);
    }
    voices_.set_dma(true);
}

void PsycheSound::frame(int hint) {
    timer_left_ -= frame_us;
    if (timer_left_ <= 0) {
        timer_left_ = std::max(timer_left_, -frame_us);
        if (finale_) {
            finale_ticks_ = (finale_ticks_ + 1) & 0xFF; // $503146
            for (int v = 0; v < 4; ++v) finale_tick(v);
            timer_left_ += 60000;
        } else {
            tick(hint);
            timer_left_ += fast_ ? 20000 : 60000;
        }
    }
}

void PsycheSound::tick(int hint) {
    if (hint != 0) { // $502A3E: the hint's hum
        const int volume = std::max(0x3F - hint * 5, 0);
        for (int v = 0; v < 4; ++v) {
            voices_.set_period(v, v * 20 + hint * 100 + 0x32);
            voices_.set_volume(v, volume);
        }
        return;
    }
    ticks_ = (ticks_ + 1) & 0xFF; // $502748
    for (int v = 0; v < 4; ++v) voice_tick(v);
}

// $502840.
void PsycheSound::voice_tick(int v) {
    auto& voice = voice_[std::size_t(v)];
    if (voice.wait == 0) {
        int note = 0;
        const std::uint32_t pattern = p_.u32(patterns + std::uint32_t(v * 4));
        if (!fast_) {
            voice.wait = 8;
            note = p_.u8(pattern + std::uint32_t(++voice.step));
        } else {
            voice.wait = 2;
            note = p_.u8(fast_notes + std::uint32_t(v * 4 + ((ticks_ & 6) >> 1)));
        }
        if (note == 0x7F) {
            voice.step = 1;
            note = p_.u8(pattern + 1);
        }
        if (note != 0) {
            voice.envelope = fast_ ? 3 : 0;
            voice.period = p_.u16(periods + std::uint32_t(note * 2));
        }
    }
    voice.volume = p_.u16(envelope + std::uint32_t(voice.envelope * 2));
    if (voice.envelope < 0x10 && (v < 2 || (ticks_ & 1) != 0)) ++voice.envelope;
    --voice.wait;
    voices_.set_period(v, voice.period);
    voices_.set_volume(v, voice.volume);
}

void PsycheSound::set_fast(bool fast) {
    fast_ = fast;
    if (fast) { // $50277C: the figure's wave, every voice due at once
        for (auto& voice : voice_) voice.wait = 0;
        for (int v = 0; v < 4; ++v) voices_.set_samples(v, wave_);
    } else { // $5027DC: the envelopes run out, the sample back
        for (auto& voice : voice_) voice.envelope = 0x10;
        for (int v = 0; v < 4; ++v) voices_.set_samples(v, sample_);
    }
}

// h17: the finale's voices on the first wave, silent.
void PsycheSound::start_finale() {
    stop();
    finale_ = true;
    for (auto& v : finale_voice_) v = FinaleVoice{p_.u32(finale_first)};
    for (int v = 0; v < 4; ++v) {
        voices_.set_samples(v, finale_waves_[0]);
        voices_.set_volume(v, 0);
        voices_.set_period(v, 200);
    }
    voices_.set_dma(true);
}

// $503178.
void PsycheSound::finale_tick(int v) {
    auto& voice = finale_voice_[std::size_t(v)];
    if (voice.wait == 0) {
        std::uint32_t at = voice.pattern + std::uint32_t(++voice.step * 4);
        int note = p_.u8(at);
        if (note == 0x7F) { // the voice's next pattern
            const std::uint32_t list = p_.u32(finale_lists + std::uint32_t(v * 4));
            int number = p_.u8(list + std::uint32_t(++voice.list));
            if (number == 0) {
                voice.list = 1;
                finale_quieter_ = 2;
                number = p_.u8(list + 1);
            }
            voice.pattern = p_.u32(finale_patterns + std::uint32_t(number * 4));
            voice.step = 0;
            at = voice.pattern;
            note = p_.u8(at);
        }
        voice.wait = p_.u8(at + 1);
        if (note != 0) {
            voice.envelope = 0;
            voice.period = p_.u16(periods + std::uint32_t(note * 2));
            const int flags = p_.u8(at + 2), wave = p_.u8(at + 3);
            voice.slide = (flags & 0x80) != 0;
            voice.curve = flags & 0x7F;
            voice.quieter = wave & 0xF;
            voices_.set_samples(v, finale_waves_[std::size_t(std::min(wave >> 4, 9))]);
        }
    }
    const int volume = p_.u16(finale_curves + std::uint32_t(voice.envelope * 2 + voice.curve * 0x20)) >>
                       ((voice.quieter + finale_quieter_) & 0x3F);
    if (voice.envelope < 0xF) ++voice.envelope;
    if (voice.slide) voice.period += 10;
    --voice.wait;
    voices_.set_period(v, voice.period);
    voices_.set_volume(v, volume);
}

void PsycheSound::stop() {
    for (int v = 0; v < 4; ++v) voices_.set_volume(v, 0);
    voices_.set_dma(false);
}

} // namespace mindwalker::psyche
