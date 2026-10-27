#include "mindwalker_amiga/landscape/sound.hpp"
#include <algorithm>

namespace mindwalker::landscape {
namespace {

// A's data (research layout).
constexpr std::uint32_t music_wave = 0x20C3DC;   // h60: 16 samples
constexpr std::uint32_t length_patterns = 0x20C648, note_patterns = 0x20C67C, period_tables = 0x20C6B4;
constexpr std::uint32_t song_parts = 0x20C710;   // 6 bytes each: end word, patterns A, B, C
constexpr std::uint32_t effect_parts = 0x20C7D0, effect_loops = 0x20C7DD; // by effect number
constexpr std::uint32_t ring_delays = 0x20C7EA, ring_shifts = 0x20C7FA;   // by channel (longs)
constexpr std::uint32_t key_multipliers = 0x20C80A, key_divisors = 0x20C812; // by key & 7
constexpr std::uint32_t noise_seed = 0x20BBB8;   // h57
constexpr std::uint32_t setup_frames = 0x702540; // S: the +$1C6 frames, 80 bytes each, contiguous
constexpr int music_songs = 0x10;                // parts above this are effects
constexpr int frame_us = 20000;

} // namespace

LandscapeSound::LandscapeSound(const amiga::LoadedImage& a, const amiga::LoadedImage& s, int noise_frame) : a_(a) {
    for (std::uint32_t i = 0; i < 16; ++i) wave_.push_back(std::int8_t(a.u8(music_wave + i)));
    for (std::uint32_t i = 0; i < 1000; ++i)
        noise_.push_back(std::int8_t(s.u8(setup_frames + std::uint32_t(noise_frame * 80) + i)));
    noise_random_.state = a.u32(noise_seed);
    // $20BCB8: the first parts' patterns; $20B744: the aim's wave.
    lengths_ = a.u32(length_patterns);
    notes_ = a.u32(note_patterns);
    periods_ = a.u32(period_tables);
    pulse_tick(0, 0);
    // $20BDD4: the four channels on the music's wave, DMA on.
    for (int c = 0; c < 4; ++c) music_channel(c);
    voices_.set_dma(true);
}

void LandscapeSound::music_channel(int c) {
    voices_.set_volume(c, 0);
    voices_.set_period(c, 0);
    voices_.set_samples(c, wave_);
    channel_kind_[std::size_t(c)] = 1;
}

void LandscapeSound::square_channel(int c) {
    voices_.set_volume(c, 0);
    voices_.set_period(c, 0x7C);
    voices_.set_samples(c, square_);
    channel_kind_[std::size_t(c)] = 3;
}

void LandscapeSound::aim_channel(int c) {
    voices_.set_volume(c, 0);
    voices_.set_period(c, c < 2 ? 0x9C4 : 0x7C);
    if (c < 2) voices_.set_samples(c, pulse_wave_);
    else voices_.set_samples(c, std::span(noise_).first(c == 2 ? 1000 : 800));
    channel_kind_[std::size_t(c)] = 2;
}

void LandscapeSound::stop() {
    for (int c = 0; c < 4; ++c) voices_.set_volume(c, 0);
    voices_.set_dma(false);
}

void LandscapeSound::bolt(int length) {
    if (bolt_ < 4) {
        bolt_ = (length >> 1) + 1;
        bolt_pitch_ = noise_random_.next() & 0x3F;
    }
}

void LandscapeSound::frame(int stage, int slow, bool stick, int cursor_x) {
    if (rumble_ != 0) { // h56 in place of the timer while it rumbles
        rumble_tick();
        return;
    }
    timer_left_ -= frame_us;
    // A request that came back is served once and sent again from then on
    // (missed ones are not caught up: $20BEE0 polls it many times a frame,
    // but not while it rumbles or pauses).
    if (timer_left_ <= 0) {
        timer_left_ = std::max(timer_left_, -frame_us);
        // $20BF26: the music (or an effect) unless the aim's sound plays.
        if (aim_ == 0 || song_ > music_songs || request_ != 0) {
            const int pace = 80000 - (stage * 0x618 & 0xFFFF); // $204A6E is a word
            int period = pace < 25001 ? 25000 : pace;
            if (slow != 0) period += slow * 200;
            sequencer_tick();
            timer_left_ += period;
        } else {
            aim_tick(stick, cursor_x, slow);
            timer_left_ += 25000;
        }
    }
}

// $20BFA6.
void LandscapeSound::sequencer_tick() {
    if (ticks_left_ == 0 || request_ != 0) {
        int length = a_.u8(lengths_ + std::uint32_t(++step_));
        if (length == 0 || request_ != 0) {
            step_ = 1;
            int part = song_;
            if (request_ == 0) {
                if (a_.u16(song_parts + std::uint32_t(song_ * 6)) == 0) {
                    ++part;          // on to the next part
                } else if (saved_song_ == 0) {
                    ++loops_;        // the music from the top; a new key every fourth time
                    part = 0;
                    if ((loops_ & 3) == 0) ++key_;
                } else {
                    part = saved_song_; // back from an effect
                    loops_ = saved_loops_;
                    saved_song_ = 0;
                }
            } else {
                if (song_ <= music_songs) {
                    saved_song_ = song_;
                    saved_loops_ = loops_;
                }
                part = std::int8_t(a_.u8(effect_parts + std::uint32_t(request_)));
                loops_ = a_.u8(effect_loops + std::uint32_t(request_));
                request_ = 0;
            }
            wave_[0] = (loops_ & 2) ? std::int8_t(0x81) : 0;
            song_ = part & 0xFF;
            const std::uint32_t entry = song_parts + std::uint32_t(song_ * 6);
            lengths_ = a_.u32(length_patterns + std::uint32_t(a_.u8(entry + 2) * 4));
            notes_ = a_.u32(note_patterns + std::uint32_t(a_.u8(entry + 3) * 4));
            periods_ = a_.u32(period_tables + std::uint32_t(a_.u8(entry + 4) * 4));
            length = a_.u8(lengths_ + 1);
        }
        ticks_left_ = length & 0xF;
        const int note = a_.u8(notes_ + std::uint32_t(step_));
        slide_ = (note & 0x80) != 0;
        volume_ = std::int8_t((length & 0xF0) >> ((loops_ & 2) ? 3 : 2));
        decay_ = slide_ ? 1 : 3;
        period_ = (a_.u16(periods_ + std::uint32_t((note & 0x7F) * 2)) << (loops_ & 1)) & 0xFFFF;
        if (song_ <= music_songs) {
            const unsigned scaled = unsigned(period_) * a_.u8(key_multipliers + std::uint32_t(key_ & 7));
            period_ = int((scaled & 0xFFFF) / a_.u8(key_divisors + std::uint32_t(key_ & 7))) & 0xFFFF;
        }
    }
    ring_at_ = (ring_at_ + 1) & 0xF;
    ring_periods_[std::size_t(ring_at_)] = period_;
    ring_volumes_[std::size_t(ring_at_)] = volume_;
    volume_ = volume_ - decay_ <= 0 ? 0 : volume_ - decay_;
    if (slide_) period_ = (period_ + 10) & 0xFFFF;
    --ticks_left_;
    int first = 0;
    if (blip_ != 0) { // the marking blip on channel 0
        first = 1;
        if (channel_kind_[0] != 3) square_channel(0);
        voices_.set_period(0, ((blip_ & 1) << 7) + blip_base_ + 200);
        voices_.set_volume(0, blip_);
        --blip_;
    }
    if (trace) trace->push_back(music_state());
    for (int c = first; c < 4; ++c) {
        if (channel_kind_[std::size_t(c)] != 1) music_channel(c);
        const int at = (a_.s32(ring_delays + std::uint32_t(c * 4)) + ring_at_) & 0xF;
        voices_.set_period(c, ring_periods_[std::size_t(at)]);
        voices_.set_volume(c, ring_volumes_[std::size_t(at)] >> (a_.s32(ring_shifts + std::uint32_t(c * 4)) & 0x3F));
    }
}

// $20B858 / $20B902: the aim's sound. Channels 0 and 1: a pulse wave whose
// width wanders, rising in pitch and loudness while the stick is held,
// panned by the cursor; channels 2 and 3: the bolt's or a hit's noise.
void LandscapeSound::aim_tick(bool stick, int x, int slow) {
    for (int c = 0; c < 4; ++c)
        if (channel_kind_[std::size_t(c)] != 2) aim_channel(c);
    if (hit_ == 0) --aim_;
    if (level_ < 0x10) ++level_;
    if (stick) {
        if (pitch_ < 0x28) pulse_ &= 0xFF9F;
        pitch_ = 0x40;
        if (level_ < 0x50) level_ += 3;
    }
    pulse_tick(x, slow);
}

// $20B902.
void LandscapeSound::pulse_tick(int x, int slow) {
    pulse_ = (pulse_ + (4 - (pulse_ >> 5 & 3))) & 0xFFFF;
    const int sign = (pulse_ & 0x80) ? 1 : -1;
    const int width = pulse_ & 0x1F;
    for (int i = 0; i < 34; ++i) pulse_wave_[std::size_t(i)] = std::int8_t(i > width ? -sign * 127 : sign * 127);
    voices_.set_samples(0, pulse_wave_);
    voices_.set_samples(1, pulse_wave_);
    const int base = 0x9C4 - pitch_;
    ++vibrato_;
    const int wobble = (vibrato_ >> 3) & 0x1F;
    voices_.set_period(0, base + wobble);
    voices_.set_period(1, base - wobble);
    const int limit = level_ >> 2;
    const int left = (0x13F - x) / 5 + 4, right = x / 5 + 4;
    voices_.set_volume(0, std::min(limit, left));
    voices_.set_volume(1, std::min(limit, right));
    if (pitch_ > 2) pitch_ -= 2;
    if (level_ > 0x10 && !(slow != 0 && (noise_random_.next() & 0x13) != 0)) --level_;
    const int noise_period = hit_ * 0x100 + bolt_pitch_ + 0xAA;
    const int amount = hit_ == 0 ? bolt_ * 4 : hit_ * 8;
    if (aim_ < 9) {
        for (int c = 2; c < 4; ++c) {
            voices_.set_volume(c, 0);
            channel_kind_[std::size_t(c)] = 0;
        }
    } else {
        voices_.set_period(3, noise_period);
        voices_.set_period(2, noise_period);
        voices_.set_volume(3, std::min(left >> 1, amount));
        voices_.set_volume(2, std::min(right >> 1, amount));
    }
    const int fade = slow == 0 ? 3 : 1;
    bolt_ = bolt_ - fade < 0 ? 0 : bolt_ - fade;
    if (hit_ != 0) --hit_;
}

// h56: the catch's rumble, a low drone growing louder.
void LandscapeSound::rumble_tick() {
    for (int c = 0; c < 4; ++c) {
        if (channel_kind_[std::size_t(c)] != 3) square_channel(c);
        if (rumble_ < 0x16) {
            voices_.set_period(c, (c << 8) + 30000);
            voices_.set_volume(c, 0x3F - rumble_ * 2);
        } else {
            voices_.set_period(c, (c << 2) - rumble_ * 0x100 + 0x307C);
            voices_.set_volume(c, rumble_);
        }
    }
}

} // namespace mindwalker::landscape
