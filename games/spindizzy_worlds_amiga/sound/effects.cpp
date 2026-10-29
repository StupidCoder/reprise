#include "spindizzy_worlds_amiga/sound/effects.hpp"

namespace spindizzy::sound {
namespace {

constexpr std::uint32_t base = 0x3400;
constexpr std::uint32_t effect_table = 0x3850;   // 16 bytes: sample, envelope, period, duration
constexpr std::uint32_t envelope_table = 0x3800; // 16 bytes: (ticks, change, repeats) triples
constexpr std::uint32_t sample_offsets = 0x36EE; // longs from $3AB0
constexpr std::uint32_t sample_loops = 0x375E;   // 6 bytes: length, repeat offset, repeat length
constexpr std::uint32_t samples = 0x3AB0;
constexpr int busy_time = 0x32;                  // $3560

} // namespace

EffectsPlayer::EffectsPlayer(std::vector<std::uint8_t> file) : file_(std::move(file)) {
    memory_.assign(file_.begin(), file_.end());
}

void EffectsPlayer::request(int channel, int effect, int volume) {
    auto& c = channels_.at(std::size_t(channel & 3));
    c.requested = effect & 0xFF;
    c.volume = volume;
}

void EffectsPlayer::reset(amiga::PaulaChannels& paula) {
    for (auto& c : channels_) {
        c.requested = c.playing = -1;
        c.busy = c.duration = 0;
    }
    for (int n = 0; n < 4; ++n) paula.set_volume(n, 0);
    paula.write_dmacon(0x000F); // $3436: $FF to DMACON (the channels' bits off)
}

void EffectsPlayer::tick(amiga::PaulaChannels& paula) {
    for (int n = 3; n >= 0; --n) {
        auto& c = channels_[std::size_t(n)];
        bool take = false;
        if (c.requested >= 0 && c.requested < 0x80) {
            // $3474: priorities by effect number; a free channel takes any.
            if (c.requested < 8) take = true;
            else if (c.requested < 0x0E) take = c.playing >= 8 && c.playing < 0x80;
            else take = c.playing >= 0x0E && c.playing < 0x80;
            if (!take) take = c.busy == 0;
            if (!take) c.requested = -1;
        }
        if (take) { // $34B6
            c.playing = c.requested;
            const std::uint32_t effect = effect_table + std::uint32_t(c.playing) * 16;
            c.sample = byte(effect);
            c.period = word(effect + 2);
            c.duration = byte(effect + 4);
            c.envelope = byte(effect + 1) * 16;
            c.repeats = byte(envelope_table + std::uint32_t(c.envelope) + 2);
            c.position = 0;
            c.countdown = 1;
            c.envelope_volume = 0;
            const std::uint32_t loop = sample_loops + std::uint32_t(c.sample) * 6;
            c.start = samples + longword(sample_offsets + std::uint32_t(c.sample) * 4);
            c.length = word(loop);
            c.repeat = c.start + word(loop + 2);
            c.repeat_length = word(loop + 4);
            paula.write_dmacon(std::uint16_t(1 << n)); // the channel off
            paula.set_period(n, std::uint16_t(c.period));
            paula.set_location(n, c.start - base);
            paula.set_length(n, std::uint16_t(c.length));
            c.busy = busy_time;
            c.requested = -1;
        } else {
            if (c.duration == 0) { // $34AA: nothing playing
                c.busy = 0;
                continue;
            }
            if (c.busy > 0) --c.busy; // $356E
            if (c.duration != 0xFF && --c.duration == 0) {
                paula.set_volume(n, 0);
                continue;
            }
        }
        // $3590: the envelope.
        if (c.countdown & 0x80) continue;
        c.countdown = (c.countdown - 1) & 0xFF;
        if (c.countdown != 0) continue;
        const std::uint32_t entry = envelope_table + std::uint32_t(c.envelope);
        int volume = (c.envelope_volume + byte(entry + std::uint32_t(c.position) + 1)) & 0xFF;
        if (volume >= 0x41) volume = volume >= 0x80 ? 0 : 0x40;
        c.envelope_volume = volume;
        c.repeats = (c.repeats - 1) & 0xFF;
        if (c.repeats != 0) {
            c.countdown = byte(entry + std::uint32_t(c.position));
            continue;
        }
        c.position += 3;
        int ticks = byte(entry + std::uint32_t(c.position));
        if (ticks >= 0xC0) { // a jump back
            c.position = ticks & 0x3F;
            ticks = byte(entry + std::uint32_t(c.position));
        }
        c.countdown = ticks;
        if (c.position != 0x0F) c.repeats = byte(entry + std::uint32_t(c.position) + 2);
    }
    // $3624: every channel's DMA on; then the loop part, period and volume
    // of those still playing (Paula takes the loop after the first pass).
    for (int n = 0; n < 4; ++n)
        if (!paula.dma(n) && channels_[std::size_t(n)].duration != 0) paula.write_dmacon(std::uint16_t(0x8000 | (1 << n)));
    for (int n = 0; n < 4; ++n) {
        const auto& c = channels_[std::size_t(n)];
        if (c.duration == 0) continue;
        paula.set_location(n, c.repeat - base);
        paula.set_length(n, std::uint16_t(c.repeat_length));
        paula.set_period(n, std::uint16_t(c.period));
        paula.set_volume(n, std::uint16_t(c.envelope_volume * c.volume / 0x40));
    }
}

} // namespace spindizzy::sound
