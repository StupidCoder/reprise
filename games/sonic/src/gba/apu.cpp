#include "gba/apu.hpp"
#include <algorithm>
#include <cmath>

namespace gba {

namespace {

// Register offsets.
constexpr std::uint32_t nr10 = 0x60, nr11 = 0x62, nr12 = 0x63, nr13 = 0x64, nr14 = 0x65;
constexpr std::uint32_t nr21 = 0x68, nr22 = 0x69, nr23 = 0x6C, nr24 = 0x6D;
constexpr std::uint32_t nr30 = 0x70, nr31 = 0x72, nr32 = 0x73, nr33 = 0x74, nr34 = 0x75;
constexpr std::uint32_t nr41 = 0x78, nr42 = 0x79, nr43 = 0x7C, nr44 = 0x7D;
constexpr std::uint32_t cnt_l = 0x80, cnt_h = 0x82, cnt_x = 0x84;

constexpr int duty_pattern[4][8] = {{0, 0, 0, 0, 0, 0, 0, 1},
                                    {1, 0, 0, 0, 0, 0, 0, 1},
                                    {1, 0, 0, 0, 0, 1, 1, 1},
                                    {0, 1, 1, 1, 1, 1, 1, 0}};

} // namespace

std::uint8_t Apu::read8(std::uint32_t offset) const {
    if (offset >= 0x90 && offset < 0xA0) {
        // The CPU sees the bank that is not playing.
        const int bank = (regs_[nr30 - 0x60] & 0x40) ? 0 : 1;
        return wave_ram_[std::size_t(bank * 16 + (offset - 0x90))];
    }
    if (offset < 0x60 || offset >= 0xA8) {
        return 0;
    }
    return regs_[offset - 0x60];
}

void Apu::write8(std::uint32_t offset, std::uint8_t value) {
    if (offset >= 0x90 && offset < 0xA0) {
        const int bank = (regs_[nr30 - 0x60] & 0x40) ? 0 : 1;
        wave_ram_[std::size_t(bank * 16 + (offset - 0x90))] = value;
        return;
    }
    if (offset < 0x60 || offset >= 0xA8) {
        return;
    }
    regs_[offset - 0x60] = value;
    switch (offset) {
    case nr11:
        square_[0].length = 64 - (value & 0x3F);
        break;
    case nr21:
        square_[1].length = 64 - (value & 0x3F);
        break;
    case nr31:
        wave_.length = 256 - value;
        break;
    case nr41:
        noise_.length = 64 - (value & 0x3F);
        break;
    case nr12:
        if ((value & 0xF8) == 0) {
            square_[0].on = false;
        }
        break;
    case nr22:
        if ((value & 0xF8) == 0) {
            square_[1].on = false;
        }
        break;
    case nr42:
        if ((value & 0xF8) == 0) {
            noise_.on = false;
        }
        break;
    case nr30:
        if (!(value & 0x80)) {
            wave_.on = false;
        }
        break;
    case nr13:
        square_[0].frequency = (square_[0].frequency & 0x700) | value;
        break;
    case nr23:
        square_[1].frequency = (square_[1].frequency & 0x700) | value;
        break;
    case nr33:
        wave_.frequency = (wave_.frequency & 0x700) | value;
        break;
    case nr14:
    case nr24: {
        Square& s = square_[offset == nr14 ? 0 : 1];
        s.frequency = (s.frequency & 0xFF) | ((value & 7) << 8);
        s.length_on = value & 0x40;
        if (value & 0x80) {
            trigger_square(offset == nr14 ? 0 : 1);
        }
        break;
    }
    case nr34:
        wave_.frequency = (wave_.frequency & 0xFF) | ((value & 7) << 8);
        wave_.length_on = value & 0x40;
        if (value & 0x80) {
            trigger_wave();
        }
        break;
    case nr44:
        noise_.length_on = value & 0x40;
        if (value & 0x80) {
            trigger_noise();
        }
        break;
    case cnt_x:
        if (!(value & 0x80)) {
            square_[0].on = square_[1].on = wave_.on = noise_.on = false;
        }
        break;
    default:
        break;
    }
}

void Apu::trigger_square(int n) {
    Square& s = square_[std::size_t(n)];
    const std::uint8_t env = regs_[(n == 0 ? nr12 : nr22) - 0x60];
    s.on = (env & 0xF8) != 0;
    if (s.length == 0) {
        s.length = 64;
    }
    s.envelope.start(env);
    if (n == 0) {
        const std::uint8_t sweep = regs_[nr10 - 0x60];
        s.shadow = s.frequency;
        s.sweep_counter = (sweep >> 4) & 7;
        s.sweep_on = (sweep & 0x77) != 0;
    }
}

void Apu::trigger_wave() {
    wave_.on = regs_[nr30 - 0x60] & 0x80;
    if (wave_.length == 0) {
        wave_.length = 256;
    }
    wave_.position = 0;
}

void Apu::trigger_noise() {
    const std::uint8_t env = regs_[nr42 - 0x60];
    noise_.on = (env & 0xF8) != 0;
    if (noise_.length == 0) {
        noise_.length = 64;
    }
    noise_.envelope.start(env);
    noise_.lfsr = 0x7FFF;
}

void Apu::sequencer_tick() {
    // 512 Hz: length on even steps (256 Hz), sweep on steps 2 and 6 (128
    // Hz), envelopes on step 7 (64 Hz).
    const int step = sequencer_step_;
    sequencer_step_ = (sequencer_step_ + 1) & 7;
    if ((step & 1) == 0) {
        for (auto& s : square_) {
            if (s.length_on && s.length > 0 && --s.length == 0) {
                s.on = false;
            }
        }
        if (wave_.length_on && wave_.length > 0 && --wave_.length == 0) {
            wave_.on = false;
        }
        if (noise_.length_on && noise_.length > 0 && --noise_.length == 0) {
            noise_.on = false;
        }
    }
    if (step == 2 || step == 6) {
        Square& s = square_[0];
        const std::uint8_t sweep = regs_[nr10 - 0x60];
        const int time = (sweep >> 4) & 7;
        if (s.sweep_on && time != 0 && --s.sweep_counter <= 0) {
            s.sweep_counter = time;
            const int delta = s.shadow >> (sweep & 7);
            const int next = (sweep & 8) ? s.shadow - delta : s.shadow + delta;
            if (next > 0x7FF) {
                s.on = false;
            } else if ((sweep & 7) != 0 && next >= 0) {
                s.shadow = next;
                s.frequency = next;
            }
        }
    }
    if (step == 7) {
        square_[0].envelope.tick();
        square_[1].envelope.tick();
        noise_.envelope.tick();
    }
}

std::array<int, 2> Apu::psg_sample() {
    // Each generator: +volume or -volume (or 0 when off), per GBATEK's 4-bit
    // levels made symmetric.
    std::array<int, 4> out{};
    constexpr double dt = 1.0 / output_rate;
    for (int n = 0; n < 2; ++n) {
        Square& s = square_[std::size_t(n)];
        if (!s.on) {
            continue;
        }
        const int duty = regs_[(n == 0 ? nr11 : nr21) - 0x60] >> 6;
        const int bit = duty_pattern[duty][int(s.phase * 8) & 7];
        out[std::size_t(n)] = bit ? s.envelope.volume : -s.envelope.volume;
        s.phase += square_frequency(s) * dt;
        s.phase -= std::floor(s.phase);
    }
    if (wave_.on) {
        const std::uint8_t c0 = regs_[nr30 - 0x60];
        const bool both = c0 & 0x20;
        const int bank = (c0 & 0x40) ? 1 : 0;
        const int count = both ? 64 : 32;
        const int index = int(wave_.position) % count;
        const int b = both ? (bank + index / 32) & 1 : bank;
        const int i = index % 32;
        const std::uint8_t byte = wave_ram_[std::size_t(b * 16 + i / 2)];
        const int nibble = (i & 1) ? (byte & 0xF) : (byte >> 4);
        const std::uint8_t c2 = regs_[nr32 - 0x60];
        // Volume: 100%, 50%, 25% or mute (bits 5-6), or 75% (bit 7).
        static constexpr int quarters[4] = {0, 4, 2, 1};
        const int q = (c2 & 0x80) ? 3 : quarters[(c2 >> 5) & 3];
        out[2] = (2 * nibble - 15) * q / 4;
        wave_.position += 2097152.0 / (2048 - wave_.frequency) * dt;
        if (wave_.position >= count) {
            wave_.position = std::fmod(wave_.position, double(count));
        }
    }
    if (noise_.on) {
        const std::uint8_t c3 = regs_[nr43 - 0x60];
        const double r = (c3 & 7) == 0 ? 0.5 : double(c3 & 7);
        const double freq = 524288.0 / r / double(1u << (((c3 >> 4) & 0xF) + 1));
        noise_.clock += freq * dt;
        while (noise_.clock >= 1) {
            noise_.clock -= 1;
            const int bit = (noise_.lfsr ^ (noise_.lfsr >> 1)) & 1;
            noise_.lfsr = std::uint16_t((noise_.lfsr >> 1) | (bit << 14));
            if (c3 & 8) {
                noise_.lfsr = std::uint16_t((noise_.lfsr & ~0x40) | (bit << 6));
            }
        }
        out[3] = (noise_.lfsr & 1) ? -noise_.envelope.volume : noise_.envelope.volume;
    }
    const std::uint8_t l = regs_[cnt_l - 0x60], enables = regs_[cnt_l + 1 - 0x60];
    std::array<int, 2> side{};
    for (int ch = 0; ch < 4; ++ch) {
        if (enables & (1 << ch)) {
            side[0] += out[std::size_t(ch)]; // right
        }
        if (enables & (0x10 << ch)) {
            side[1] += out[std::size_t(ch)]; // left
        }
    }
    side[0] *= (l & 7) + 1;
    side[1] *= ((l >> 4) & 7) + 1;
    return side;
}

void Apu::queue_direct(std::span<const std::int8_t> a, std::span<const std::int8_t> b,
                       double rate) {
    const std::array<std::span<const std::int8_t>, 2> in{a, b};
    for (std::size_t i = 0; i < 2; ++i) {
        Direct& d = direct_[i];
        // Keep what has not been played yet.
        const std::size_t done = std::min(d.samples.size(), std::size_t(d.position));
        d.samples.erase(d.samples.begin(), d.samples.begin() + std::ptrdiff_t(done));
        d.position -= double(done);
        d.samples.insert(d.samples.end(), in[i].begin(), in[i].end());
        d.rate = rate;
    }
}

std::vector<float> Apu::render(std::uint32_t cycles) {
    // Whole output samples; the fraction carries over to the next call.
    owed_ += double(cycles) * output_rate / clock_rate;
    const std::size_t n = std::size_t(owed_);
    owed_ -= double(n);
    std::vector<float> out(2 * n, 0.0f);
    const std::uint16_t h = std::uint16_t(regs_[cnt_h - 0x60] | regs_[cnt_h + 1 - 0x60] << 8);
    const bool master = regs_[cnt_x - 0x60] & 0x80;
    static constexpr double psg_ratio[4] = {0.25, 0.5, 1.0, 0.0};
    for (std::size_t i = 0; i < n; ++i) {
        sequencer_ += 512.0 / output_rate;
        while (sequencer_ >= 1) {
            sequencer_ -= 1;
            sequencer_tick();
        }
        if (!master) {
            continue;
        }
        const auto psg = psg_sample();
        double side[2] = {psg[0] * psg_ratio[h & 3], psg[1] * psg_ratio[h & 3]};
        for (std::size_t f = 0; f < 2; ++f) {
            Direct& d = direct_[f];
            int sample = 0;
            if (std::size_t(d.position) < d.samples.size()) {
                sample = d.samples[std::size_t(d.position)];
            }
            d.position += d.rate / output_rate;
            const double v = sample * ((h & (4u << f)) ? 2.0 : 1.0);
            if (h & (0x100u << (4 * f))) {
                side[0] += v;
            }
            if (h & (0x200u << (4 * f))) {
                side[1] += v;
            }
        }
        // The output is AC-coupled: a high-pass (about 20 Hz) removes the
        // constant part, as the coupling capacitors do.
        for (std::size_t c = 0; c < 2; ++c) {
            const double x = side[c] / 512.0;
            dc_[c] += (x - dc_[c]) * (1.0 - std::exp(-2.0 * 3.14159265358979 * 20.0 / output_rate));
            side[c] = x - dc_[c];
        }
        out[2 * i] = float(std::clamp(side[1], -1.0, 1.0));     // left
        out[2 * i + 1] = float(std::clamp(side[0], -1.0, 1.0)); // right
    }
    return out;
}

} // namespace gba
