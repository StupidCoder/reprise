#include "sound/ds_sound.hpp"
#include <algorithm>

namespace picarat {
namespace {
// IMA-ADPCM step tables (general DS hardware documentation).
constexpr std::int16_t adpcmSteps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::int8_t adpcmIndexStep[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr unsigned dividerShift[4] = {0, 1, 2, 4};
} // namespace

void DsSound::reset() {
    channels_ = {};
    masterVolume_ = 0x7f;
}

std::uint8_t DsSound::Channel::byte(std::uint32_t offset) const {
    if (!data || limit <= data || offset >= std::size_t(limit - data)) {
        return 0;
    }
    return data[offset];
}

void DsSound::Channel::start() {
    busy = true;
    counter = 0;
    position = 0;
    sample = 0;
    psgStep = 0;
    noise = 0x7fff;
    delay = format == Psg || format == 4 ? 0 : format == Adpcm ? 10 : 2;
    if (format == Adpcm) {
        std::uint32_t header =
            byte(0) | byte(1) << 8 | byte(2) << 16 | std::uint32_t(byte(3)) << 24;
        adpcmValue = std::int16_t(header & 0xffff);
        adpcmIndex = std::min<std::int32_t>((header >> 16) & 0x7f, 88);
        adpcmLoopValue = adpcmValue;
        adpcmLoopIndex = adpcmIndex;
        position = 8; // nibble position after the 32-bit header
    }
}

// One timer overflow: fetch the next sample.
void DsSound::Channel::step() {
    if (delay) {
        --delay;
        return;
    }
    if (format == Psg) {
        // Channels 8-13 PSG square wave; duty d is high for d+1 of 8 steps.
        psgStep = (psgStep + 1) & 7;
        sample = psgStep <= duty ? 0x7fff : -0x7fff;
        return;
    }
    if (format == 4) { // noise (channels 14-15)
        if (noise & 1) {
            noise = std::uint16_t((noise >> 1) ^ 0x6000);
            sample = -0x7fff;
        } else {
            noise >>= 1;
            sample = 0x7fff;
        }
        return;
    }
    const std::uint32_t end = (loopStart + loopLength) * 4; // bytes
    if (format == Adpcm) {
        std::uint32_t bytePosition = position >> 1;
        if (bytePosition >= end) {
            if (repeat == 1) {
                position = loopStart * 8;
                adpcmValue = adpcmLoopValue;
                adpcmIndex = adpcmLoopIndex;
                bytePosition = position >> 1;
            } else {
                busy = false;
                sample = 0;
                return;
            }
        }
        if ((position == loopStart * 8) && position) {
            adpcmLoopValue = adpcmValue;
            adpcmLoopIndex = adpcmIndex;
        }
        unsigned nibble = (byte(bytePosition) >> ((position & 1) * 4)) & 15;
        std::int32_t step = adpcmSteps[adpcmIndex];
        std::int32_t diff = step >> 3;
        if (nibble & 1) {
            diff += step >> 2;
        }
        if (nibble & 2) {
            diff += step >> 1;
        }
        if (nibble & 4) {
            diff += step;
        }
        adpcmValue = (nibble & 8) ? std::max(adpcmValue - diff, -0x7fff)
                                  : std::min(adpcmValue + diff, 0x7fff);
        adpcmIndex = std::clamp<std::int32_t>(adpcmIndex + adpcmIndexStep[nibble & 7], 0, 88);
        sample = adpcmValue;
        ++position;
        return;
    }
    const unsigned size = format == Pcm16 ? 2 : 1;
    if (position + size > end) {
        if (repeat == 1) {
            position = loopStart * 4;
        } else {
            busy = false;
            sample = 0;
            return;
        }
    }
    if (format == Pcm16) {
        sample = std::int16_t(byte(position) | byte(position + 1) << 8);
    } else {
        sample = std::int8_t(byte(position)) * 256;
    }
    position += size;
}

std::uint32_t DsSound::Channel::control() const {
    const unsigned f = format == 4 ? 3 : format;
    return volume | unsigned(shift) << 8 | unsigned(pan) << 16 |
           (f == Psg ? unsigned(duty) << 24 : 0) | unsigned(repeat) << 27 | f << 29 |
           (busy ? 0x80000000u : 0);
}

void DsSound::trace(unsigned channel, unsigned offset, unsigned size, std::uint32_t value) const {
    if (registerTrace) {
        registerTrace(0x04000400 + channel * 16 + offset, size, value);
    }
}

void DsSound::startChannels(std::uint32_t mask) {
    for (unsigned i = 0; i < 16; ++i) {
        if (mask >> i & 1) {
            trace(i, 3, 1, (channels_[i].control() >> 24) | 0x80);
            channels_[i].start();
        }
    }
}

void DsSound::stopChannels(std::uint32_t mask, bool) {
    for (unsigned i = 0; i < 16; ++i) {
        if (mask >> i & 1) {
            trace(i, 0, 4, channels_[i].control() & 0x7fffffff);
            channels_[i].busy = false;
            channels_[i].sample = 0;
        }
    }
}

void DsSound::setupPcm(unsigned ch, unsigned format, const std::uint8_t* data,
                       const std::uint8_t* limit, unsigned repeat, unsigned loopStart,
                       unsigned loopLength, unsigned volume, unsigned shift, unsigned timer,
                       unsigned pan) {
    auto& c = channels_.at(ch);
    // ARM7 0x037fe804 writes SOUNDxCNT without the busy bit, which stops the channel.
    c.busy = false;
    c.format = std::uint8_t(format & 3);
    c.data = data;
    c.limit = limit;
    c.repeat = std::uint8_t(repeat & 3);
    c.loopStart = loopStart & 0xffff;
    c.loopLength = loopLength & 0x3fffff;
    c.volume = std::uint8_t(volume & 0x7f);
    c.shift = std::uint8_t(shift & 3);
    c.pan = std::uint8_t(pan & 0x7f);
    c.reload = std::uint16_t(-timer);
    trace(ch, 0, 4, c.control());
    trace(ch, 8, 2, c.reload);
    trace(ch, 0xa, 2, c.loopStart);
    trace(ch, 0xc, 4, c.loopLength);
    trace(ch, 4, 4, std::uint32_t(reinterpret_cast<std::uintptr_t>(data)));
}

void DsSound::setupPsg(unsigned ch, unsigned duty, unsigned volume, unsigned shift, unsigned timer,
                       unsigned pan) {
    auto& c = channels_.at(ch);
    c.busy = false;
    c.format = Psg;
    c.duty = std::uint8_t(duty & 7);
    c.repeat = 0;
    c.volume = std::uint8_t(volume & 0x7f);
    c.shift = std::uint8_t(shift & 3);
    c.pan = std::uint8_t(pan & 0x7f);
    c.reload = std::uint16_t(-timer);
    c.data = c.limit = nullptr;
    trace(ch, 0, 4, c.control());
    trace(ch, 8, 2, c.reload);
}

void DsSound::setupNoise(unsigned ch, unsigned volume, unsigned shift, unsigned timer,
                         unsigned pan) {
    auto& c = channels_.at(ch);
    c.busy = false;
    c.format = 4;
    c.repeat = 0;
    c.volume = std::uint8_t(volume & 0x7f);
    c.shift = std::uint8_t(shift & 3);
    c.pan = std::uint8_t(pan & 0x7f);
    c.reload = std::uint16_t(-timer);
    c.data = c.limit = nullptr;
    trace(ch, 0, 4, c.control());
    trace(ch, 8, 2, c.reload);
}

void DsSound::setTimer(std::uint32_t mask, unsigned timer) {
    for (unsigned i = 0; i < 16; ++i) {
        if (mask >> i & 1) {
            channels_[i].reload = std::uint16_t(-timer);
            trace(i, 8, 2, channels_[i].reload);
        }
    }
}

void DsSound::setVolume(std::uint32_t mask, unsigned volume, unsigned shift) {
    for (unsigned i = 0; i < 16; ++i) {
        if (mask >> i & 1) {
            channels_[i].volume = std::uint8_t(volume & 0x7f);
            channels_[i].shift = std::uint8_t(shift & 3);
            trace(i, 0, 2, channels_[i].control() & 0xffff);
        }
    }
}

void DsSound::setPan(std::uint32_t mask, unsigned pan) {
    for (unsigned i = 0; i < 16; ++i) {
        if (mask >> i & 1) {
            channels_[i].pan = std::uint8_t(pan & 0x7f);
            trace(i, 2, 1, channels_[i].pan);
        }
    }
}

std::uint16_t DsSound::busy() const {
    std::uint16_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (channels_[i].busy) {
            bits |= std::uint16_t(1u << i);
        }
    }
    return bits;
}

// One output period: 1024 system clocks = 512 channel-timer ticks.
void DsSound::mix(std::int32_t& left, std::int32_t& right, bool produce) {
    std::int64_t l = 0, r = 0;
    for (auto& c : channels_) {
        if (!c.busy) {
            continue;
        }
        const std::uint32_t period = 0x10000u - c.reload;
        c.counter += 512;
        while (c.busy && c.counter >= period) {
            c.counter -= period;
            c.step();
        }
        if (!produce || !c.busy) {
            continue;
        }
        // GBATEK mixer steps: divider (16.4), volume x N/128 (16.11), panning
        // x N/128 (16.18), each channel rounded down to 16.8, then summed.
        const std::int64_t volume = c.volume == 127 ? 128 : c.volume;
        const std::int64_t pan = c.pan == 127 ? 128 : c.pan;
        const std::int64_t v =
            ((std::int64_t(c.sample) << 4) >> dividerShift[c.shift]) * volume; // .11
        l += (v * (128 - pan)) >> 10;
        r += (v * pan) >> 10; // .8
    }
    // Master volume x N/128, fraction stripped: the signed 16-bit scale of
    // the output stage (its upper 10 bits reach the DAC).
    const std::int64_t master = masterVolume_ == 127 ? 128 : masterVolume_;
    left = std::int32_t(std::clamp<std::int64_t>((l * master) >> 15, -32768, 32767));
    right = std::int32_t(std::clamp<std::int64_t>((r * master) >> 15, -32768, 32767));
}

void DsSound::render(std::span<std::int16_t> stereo) {
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        std::int32_t l, r;
        mix(l, r, true);
        if (dac10) { // (clip(x / 64 + 0x200, 0, 0x3ff) - 0x200) x 64
            l = std::clamp((l >> 6) + 0x200, 0, 0x3ff) - 0x200;
            r = std::clamp((r >> 6) + 0x200, 0, 0x3ff) - 0x200;
            l *= 64;
            r *= 64;
        }
        stereo[i] = std::int16_t(l);
        stereo[i + 1] = std::int16_t(r);
    }
}

void DsSound::advance(unsigned frames) {
    for (unsigned i = 0; i < frames; ++i) {
        std::int32_t l, r;
        mix(l, r, false);
    }
}

} // namespace picarat
