#include "marble_madness_amiga/sound/music.hpp"

namespace marble_madness::sound {
namespace {

// Program tables.
constexpr std::uint32_t semitone_periods = 0x1200CA; // 12 words, C to B in the instrument's top octave
constexpr std::uint32_t durations = 0x1201EE;        // 16 longs: note lengths (16.16 ticks of tempo)
constexpr std::uint32_t first_transpose = 0x1201ED;  // byte: added to every note

} // namespace

Music::Music(const Memory& memory, Effects& effects) : memory_(&memory), effects_(&effects) {
    transpose_ = memory.s8(first_transpose);
    set_tempo(0x4B00 >> 7); // $11FBB4 starts at tempo word $4B00
}

void Music::start(std::uint32_t entry) {
    const auto& m = *memory_;
    const std::uint32_t instrument = m.u32(entry + 4);
    for (int v = 0; v < 8; ++v) {
        set_instrument(v, m.u32(instrument + 4), m.u32(instrument), m.u32(instrument + 8), m.u32(instrument + 12));
        voices_[std::size_t(v)].volume = std::uint8_t(m.s8(instrument + 16));
    }
    set_tempo(std::uint16_t(m.s16(entry + 8)));
    play(m.u32(entry), 99);
}

void Music::stop() {
    running_ = false;
    for (int v = 0; v < 8; ++v) {
        release(v);
    }
}

// $11FB72: the tempo word, times 128, kept as a 16-bit value, gives the
// step per tick: (that << 9) / 150.
void Music::set_tempo(std::uint16_t tempo) {
    const auto word = std::uint16_t(std::uint32_t(tempo) << 7);
    step_ = (std::uint32_t(word) << 9) / 150;
}

// $11FF02: the first octave whose length bit is set in the one-shot or
// repeat length (from 1; 9 at most).
std::uint32_t Music::octave_shift(std::uint32_t header) const {
    std::uint32_t k = 1;
    while (true) {
        const std::uint32_t bit = 1u << k;
        if ((bit & memory_->u32(header)) != 0 || (bit & memory_->u32(header + 4)) != 0 || k > 8) {
            return k;
        }
        ++k;
    }
}

// $11FF42: an 8SVX voice header (one-shot and repeat samples of the top
// octave, samples per cycle, octaves at +$E) and the samples, octave after
// octave, each twice the one before.
void Music::set_instrument(int v, std::uint32_t samples, std::uint32_t header, std::uint32_t volume_envelope,
                           std::uint32_t pitch_envelope) {
    const auto& m = *memory_;
    auto& voice = voices_[std::size_t(v)];
    voice.volume_envelope = volume_envelope;
    voice.pitch_envelope = pitch_envelope;
    const std::uint32_t per_cycle = m.u32(header + 8);
    std::uint32_t k;
    if (per_cycle == 0) {
        k = octave_shift(header);
    } else {
        k = 1;
        while ((1u << k) < per_cycle) {
            ++k;
        }
        if ((1u << k) != per_cycle) {
            k = octave_shift(header);
        }
    }
    std::uint32_t octaves = m.u8(header + 0xE);
    if (octaves > 9) {
        octaves = 9;
    }
    if (octaves == 0) {
        octaves = 1;
    }
    const std::uint32_t lowest = 10 - octaves;
    if (lowest < k) {
        k = lowest;
    }
    voice.top_octave = std::uint8_t(9 - k);
    voice.bottom_octave = std::uint8_t(lowest - k);
    voice.repeat_words = std::uint16_t(m.u32(header + 4) >> k);
    voice.total_words = std::uint16_t((m.u32(header) >> k) + voice.repeat_words);
    voice.base = samples - std::uint32_t(voice.total_words) * (1u << (k - 1)) * 2;
}

// $11FD96.
void Music::play(std::uint32_t score, std::int16_t repeats) {
    stop();
    score_ = score;
    score_repeats_ = repeats;
    for (int v = 0; v < 8; ++v) {
        auto& voice = voices_[std::size_t(v)];
        voice.time = 0;
        voice.event = 0;
        voice.pattern = 0;
        const std::uint32_t list = memory_->u32(score + std::uint32_t(v) * 8);
        voice.repeats = list != 0 ? memory_->s16(list) : 0;
        voice.last_note = std::int8_t(0x80);
        voice.restart = true;
    }
    finished_ = false;
    running_ = true;
}

void Music::vertical_blank() {
    ++blanks_;
    if (!running_) {
        return;
    }
    std::uint32_t steps = blanks_ - played_to_;
    played_to_ = blanks_;
    if (steps > 5) {
        steps = 5;
    }
    while (steps-- != 0) {
        tick();
    }
}

// $11FA46: each voice's next events when its time has run out; with flag
// bit 6 a note ends one step before its time.
void Music::tick() {
    if (!running_) {
        return;
    }
    for (int v = 0; v < 8; ++v) {
        auto& voice = voices_[std::size_t(v)];
        const std::uint32_t record = score_ + std::uint32_t(v) * 8;
        if (memory_->u32(record) != 0) {
            if (voice.time < 1) {
                next_events(v, record);
            } else if (voice.time <= std::int32_t(step_) && (voice.flags & 0x40) != 0) {
                release(v);
            }
        }
        voice.time -= std::int32_t(step_);
    }
}

void Music::next_events(int v, std::uint32_t record) {
    const auto& m = *memory_;
    auto& voice = voices_[std::size_t(v)];
    while (!finished_) {
        const std::uint32_t list = m.u32(record);
        const std::uint32_t data = m.u32(list + std::uint32_t(voice.pattern) * 6 + 2);
        const std::uint8_t b = m.u8(data + std::uint32_t(std::int32_t(voice.event++)));
        if ((b & 0x80) == 0) {
            voice.time += m.s32(durations + std::uint32_t(voice.flags & 0xF) * 4);
            if (b == 0x7F) {
                release(v);
                return;
            }
            const auto pitch = std::int8_t(b + transpose_);
            if (pitch < 108) {
                note(v, pitch, record);
            }
            return;
        }
        if ((b & 0xF) == 0) {
            pattern_end(v, record);
        } else {
            voice.flags = b;
        }
    }
    release(v);
}

void Music::pattern_end(int v, std::uint32_t record) {
    const auto& m = *memory_;
    auto& voice = voices_[std::size_t(v)];
    voice.event = 0;
    if (--voice.repeats > 0) {
        return;
    }
    const std::uint32_t list = m.u32(record);
    ++voice.pattern;
    voice.repeats = m.s16(list + std::uint32_t(voice.pattern) * 6);
    if (voice.repeats == 0) {
        voice.pattern = 0;
        voice.repeats = m.s16(list);
        if (v == 0 && --score_repeats_ < 1) {
            running_ = false;
            for (int i = 0; i < 8; ++i) {
                release(i);
            }
            finished_ = true;
        }
    }
}

// $11F64A: a note n = octave * 12 + semitone. The instrument's octave
// holding it (clamped to those stored; below them the lowest one at twice
// the period) is played once in full, then its repeat part loops.
void Music::note(int v, int n, std::uint32_t record) {
    const auto& m = *memory_;
    auto& voice = voices_[std::size_t(v)];
    int octave = 0;
    int semitone = n;
    while (semitone - 12 >= 0) {
        semitone -= 12;
        ++octave;
    }
    if (voice.top_octave < octave) {
        octave = voice.top_octave;
    }
    const bool below = octave < voice.bottom_octave;
    if (below) {
        octave = voice.bottom_octave;
    }
    Chain chain(1);
    auto& once = chain[0];
    once.count = 1;
    once.wave.words = std::uint32_t(voice.total_words) << (8 - octave);
    once.wave.data = once.wave.words * 2 + voice.base;
    once.wave.period = std::uint16_t(m.u16(semitone_periods + std::uint32_t(semitone) * 2) << (below ? 1 : 0));
    once.wave.volume = voice.volume;
    if (voice.repeat_words != 0) {
        Segment loop;
        loop.count = -2;
        loop.wave.words = std::uint32_t(voice.repeat_words) << (8 - octave);
        loop.wave.data = (once.wave.words - loop.wave.words) * 2 + once.wave.data;
        loop.wave.period = once.wave.period;
        loop.wave.volume = once.wave.volume;
        once.next = 1;
        chain.push_back(loop);
    }
    if (voice.restart || voice.last_note != n) {
        release(v);
        voice.sound = effects_->play(chain, voice.volume_envelope, voice.pitch_envelope, 0xFFFF, m.s8(record + 6),
                                     m.s16(record + 4));
    }
    voice.last_note = std::int8_t(n);
    voice.restart = (voice.flags & 0x40) != 0;
}

void Music::release(int v) {
    auto& voice = voices_[std::size_t(v)];
    if (voice.sound.valid()) {
        effects_->stop(voice.sound);
        voice.sound = {};
    }
}

} // namespace marble_madness::sound
