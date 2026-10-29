#include "sonic1_gg/audio/voice.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {
LogoVoice::LogoVoice(std::span<const std::uint8_t> packed) {
    if (packed.size() != 0x175f) {
        throw std::runtime_error("Incomplete logo voice");
    }
    unsigned clock = 28;
    auto out = [&](unsigned delay, unsigned value, unsigned port = 0x7f) {
        clock += delay;
        writes.push_back({clock, port, std::uint8_t(value)});
    };
    // 23C00-23C2F: silence all channels, set the stereo mask, and set every
    // tone channel to a fixed period so that only the volume shapes the
    // waveform. Times are the end of each OUT instruction, in CPU clocks.
    for (unsigned i = 0; i < 4; ++i) {
        out(i == 3 ? 16 : 21, 0x9f + i * 0x20);
    }
    out(38, 0x41, 6);
    for (unsigned value : {0xe7, 0x81, 0, 0xa0, 0, 0xc0, 0}) {
        out(18, value);
    }
    // Each level goes to the volumes of tone channels 0 and 2 (0x90 / 0xD0).
    // The delays are the routine's instruction timings between the writes.
    auto pair = [&](unsigned delay, int level) {
        out(delay, 0x90 + level);
        out(22, 0xd0 + level);
    };
    auto quarter = [](int value) {
        return value >= 0 ? value / 4 : -((-value + 3) / 4);
    };
    auto clamp = [](int value) {
        return unsigned(value) >= 15 ? 0 : value;
    };
    // A quarter of the step to the next level, rounded toward zero.
    // Levels that leave 0..14 are written as 0 (loudest), as the ROM does.
    for (unsigned i = 0; i < 0x175e; ++i) {
        int low = packed[i] & 15, high = packed[i] >> 4;
        int delta = quarter(high - low), level = low;
        pair(84, level);
        level += delta;
        pair(92, level);
        for (int j = 0; j < 2; ++j) {
            int raw = level + delta;
            level = clamp(raw);
            pair(unsigned(raw) < 15 ? 87 : 90, level);
        }
        level = high;
        pair(92, level);
        delta = quarter(int(packed[i + 1] & 15) - high);
        int raw = level + delta;
        level = clamp(raw);
        pair(unsigned(raw) < 15 ? 95 : 94, level);
        raw = level + delta;
        level = clamp(raw);
        pair(unsigned(raw) < 15 ? 87 : 90, level);
        raw = level + delta;
        level = clamp(raw);
        pair(unsigned(raw) < 15 ? 79 : 82, level);
        clock += 24; // loop counter and conditional JP, 23D04-23D09
    }
    for (unsigned value : {0x9f, 0xbf, 0xdf, 0xff}) {
        out(18, value);
    }
    out(18, 255, 6);
    duration = clock + 14; // EI; RET
}

std::vector<float> LogoVoice::render(gg::Psg& psg, unsigned clocks) {
    const auto end = clock_ + clocks;
    std::vector<float> result;
    auto advance = [&](unsigned time) {
        auto chunk = psg.render(time - clock_);
        result.insert(result.end(), chunk.begin(), chunk.end());
        clock_ = time;
    };
    while (position_ < writes.size() && writes[position_].clock <= end) {
        const auto& w = writes[position_++];
        advance(w.clock);
        if (w.port == 6) {
            psg.stereo(w.value);
        } else {
            psg.write(w.value);
        }
    }
    advance(end);
    if (clock_ >= duration) {
        playing_ = false;
    }
    return result;
}
} // namespace sonic1_gg
