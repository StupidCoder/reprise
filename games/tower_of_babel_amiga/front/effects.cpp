#include "tower_of_babel_amiga/front/effects.hpp"
#include "tower_of_babel_amiga/front/screen.hpp"
#include <algorithm>

namespace babel::front {
namespace {

constexpr std::uint32_t sample_base = 0x8200;        // waveforms are offsets from here ($79FA(a6))
constexpr std::uint32_t main_base = 0x1C5F0;
constexpr std::uint32_t periods = var(0xD2);         // 432 periods
constexpr std::uint32_t lift_hum = var(0x2D5A), lift_stop = var(0x2DB6);
constexpr std::uint32_t backgrounds = var(0xAC);     // per setting two offsets from A6
constexpr int lift_priority = 0x141, background_priority = 0x29;

std::int16_t word(int v) { return std::int16_t(v); }

} // namespace

Effects::Effects(const data::Assets& assets) : assets_(assets) {
    // The main data from its base, as signed samples.
    for (std::uint32_t a = main_base; assets.main.contains(a); ++a) memory_.push_back(std::int8_t(assets.main.u8(a)));
    paula_.set_memory(memory_);
}

int Effects::period(int pitch) const {
    const int i = std::clamp((word(pitch) >> 6), 0, 0x1AF);
    return assets_.program.u16(periods + std::uint32_t(i) * 2);
}

int Effects::loudness(int volume) const {
    return std::clamp((word(volume) >> 6) + volume_offset_, 0, 0x3F);
}

void Effects::play_waveform(int channel, std::uint32_t waveform, bool now) {
    // A waveform starts with its length in words; the samples follow.
    if (!assets_.main.contains(waveform, 2)) return;
    if (now) paula_.write_dmacon(std::uint16_t(1 << channel));
    paula_.set_location(channel, waveform + 2 - main_base);
    paula_.set_length(channel, assets_.main.u16(waveform));
}

void Effects::silence(int channel) {
    channels_[std::size_t(channel)].priority = 0;
    paula_.set_volume(channel, 0);
    paula_.write_dmacon(std::uint16_t(1 << channel));
}

bool Effects::start_one(std::uint32_t record) {
    const auto& p = assets_.program;
    // The channel with the lowest priority (the first of equals) is taken
    // when the new sound's priority is above it.
    int chosen = 0;
    for (int c = 1; c < 4; ++c)
        if (unsigned(channels_[std::size_t(c)].priority) < unsigned(channels_[std::size_t(chosen)].priority)) chosen = c;
    const int priority = p.u16(record + 0x24);
    auto& ch = channels_[std::size_t(chosen)];
    if (unsigned(ch.priority) >= unsigned(priority)) return false;
    ch.priority = 0;
    ch.record = record;
    ch.pitch_step = word(p.u16(record + 0x1C));
    ch.pitch_wait = word(p.u16(record + 0x1E));
    ch.volume_step = word(p.u16(record + 0x20));
    ch.volume_wait = word(p.u16(record + 0x22));
    ch.pitch_repeats = word(p.u16(record + 0x26));
    ch.volume_repeats = word(p.u16(record + 0x28));
    ch.pitch = word(p.u16(record + 0x2A));
    ch.volume = word(p.u16(record + 0x2C));
    ch.pitch_segment = ch.volume_segment = 0x18;
    play_waveform(chosen, sample_base + p.u32(record + 0x18), true);
    paula_.set_period(chosen, std::uint16_t(period(ch.pitch)));
    paula_.set_volume(chosen, std::uint16_t(loudness(ch.volume)));
    paula_.write_dmacon(std::uint16_t(0x8200 | (1 << chosen)));
    ch.priority = priority;
    return true;
}

void Effects::start(std::uint32_t record) {
    if (start_one(record) && (assets_.program.u16(record + 0x24) & 2)) start_one(record + 0x2E);
}

void Effects::stop() {
    for (int c = 0; c < 4; ++c) silence(c);
    paused_ = false;
    volume_offset_ = 0;
}

void Effects::pause() {
    paused_ = true;
    for (int c = 0; c < 4; ++c) {
        paula_.write_dmacon(std::uint16_t(1 << c));
        paula_.set_volume(c, 0);
    }
}

void Effects::resume() {
    std::uint16_t on = 0;
    for (int c = 0; c < 4; ++c)
        if (channels_[std::size_t(c)].priority != 0) on = std::uint16_t(on | 1 << c);
    if (on) paula_.write_dmacon(std::uint16_t(0x8000 | on));
    paused_ = false;
}

void Effects::frame(const Frame& f) {
    for (const auto r : f.queued) start(r);
    if (f.silent) return;
    const auto humming = [&] {
        return std::any_of(channels_.begin(), channels_.end(), [](const Channel& c) { return unsigned(c.priority) >= lift_priority; });
    };
    if (f.lifts_moving) {
        if (!humming()) start(lift_hum);
    } else {
        bool stopped = false;
        for (int c = 0; c < 4; ++c)
            if (unsigned(channels_[std::size_t(c)].priority) >= lift_priority) {
                silence(c);
                stopped = true;
            }
        if (stopped) start(lift_stop);
    }
    const int type = f.background & 3;
    if (type != 0 && std::none_of(channels_.begin(), channels_.end(), [](const Channel& c) { return c.priority == background_priority; })) {
        const std::uint32_t at = backgrounds + std::uint32_t(type * 8 + background_toggle_);
        background_toggle_ ^= 4;
        start(0x806 + assets_.program.u32(at));
    }
}

void Effects::tick() {
    if (paused_) return;
    const auto& p = assets_.program;
    for (int c = 0; c < 4; ++c) {
        auto& ch = channels_[std::size_t(c)];
        if (!(ch.priority & 1)) continue;
        // The pitch: the next segment when this one's length is over.
        ch.pitch_wait = word(ch.pitch_wait - 1);
        if (ch.pitch_wait < 0) {
            ch.pitch_segment -= 12;
            if (ch.pitch_segment < 0) {
                ch.pitch_repeats = word(ch.pitch_repeats - 1);
                if (ch.pitch_repeats < 0) {
                    silence(c);
                    continue;
                }
                ch.pitch_segment = 0x18;
            }
            const std::uint32_t seg = ch.record + std::uint32_t(ch.pitch_segment);
            const auto waveform = std::int32_t(p.u32(seg));
            if (waveform >= 0) play_waveform(c, sample_base + std::uint32_t(waveform), false);
            ch.pitch_wait = word(p.u16(seg + 6));
            ch.pitch_step = word(p.u16(seg + 4));
        }
        ch.pitch = word(ch.pitch + ch.pitch_step);
        paula_.set_period(c, std::uint16_t(period(ch.pitch)));
        // The volume likewise.
        ch.volume_wait = word(ch.volume_wait - 1);
        if (ch.volume_wait < 0) {
            ch.volume_segment -= 12;
            if (ch.volume_segment < 0) {
                ch.volume_repeats = word(ch.volume_repeats - 1);
                if (ch.volume_repeats < 0) {
                    silence(c);
                    continue;
                }
                ch.volume_segment = 0x18;
            }
            const std::uint32_t seg = ch.record + std::uint32_t(ch.volume_segment);
            ch.volume_wait = word(p.u16(seg + 10));
            ch.volume_step = word(p.u16(seg + 8));
        }
        ch.volume = word(ch.volume + ch.volume_step);
        paula_.set_volume(c, std::uint16_t(loudness(ch.volume)));
    }
}

} // namespace babel::front
