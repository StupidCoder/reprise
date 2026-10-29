// The logo voice; see voice.hpp.

#include "gg_engine/audio/voice.hpp"

namespace gg_engine {

// The three PUSHes and set-up end with the first OUT, then 18 clocks per
// register write; the samples follow.
LogoVoice::LogoVoice(const Assets& rom, const VoiceProfile& p) {
    static constexpr std::uint8_t setup[6] = {0x81, 0x00, 0xA1, 0x00, 0xC1, 0x00};
    unsigned clock = p.first_out;
    for (auto v : setup) {
        writes.push_back({clock, v});
        clock += 18;
    }
    unsigned start = p.first_out + 90;
    for (unsigned i = 0; i < p.samples; ++i) {
        const std::uint8_t b = rom.byte(p.bank, p.address + i);
        const std::uint8_t high = std::uint8_t((~b >> 4) & 0x0F);
        const std::uint8_t low = std::uint8_t(~b & 0x0F);
        writes.push_back({start + p.high_write, std::uint8_t(0x90 | high)});
        writes.push_back({start + p.high_write + 18, std::uint8_t(0xB0 | high)});
        writes.push_back({start + p.high_write + 36, std::uint8_t(0xD0 | high)});
        writes.push_back({start + p.low_write, std::uint8_t(0x90 | low)});
        writes.push_back({start + p.low_write + 18, std::uint8_t(0xB0 | low)});
        writes.push_back({start + p.low_write + 36, std::uint8_t(0xD0 | low)});
        start += p.period;
    }
    duration = start + 57; // XOR A; LD (nn),A; three POPs; RET
}

std::vector<float> LogoVoice::render(gg::Psg& psg, unsigned clocks) {
    const unsigned end = clock_ + clocks;
    std::vector<float> result;
    auto advance = [&](unsigned time) {
        auto chunk = psg.render(time - clock_);
        result.insert(result.end(), chunk.begin(), chunk.end());
        clock_ = time;
    };
    while (position_ < writes.size() && writes[position_].clock <= end) {
        const auto& w = writes[position_++];
        advance(w.clock);
        psg.write(w.value);
    }
    advance(end);
    if (clock_ >= duration) {
        playing_ = false;
    }
    return result;
}

} // namespace gg_engine
