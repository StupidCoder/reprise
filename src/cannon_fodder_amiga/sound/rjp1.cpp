#include "cannon_fodder_amiga/sound/rjp1.hpp"
#include <stdexcept>

namespace fodder::sound {
namespace {

int be16(std::span<const std::uint8_t> d, std::size_t at) {
    return at + 1 < d.size() ? (d[at] << 8) | d[at + 1] : 0;
}
std::int32_t be32(std::span<const std::uint8_t> d, std::size_t at) {
    return std::int32_t((std::uint32_t(be16(d, at)) << 16) | std::uint32_t(be16(d, at + 2)));
}
constexpr std::uint32_t period_table_address = 0xa4856;
constexpr std::size_t file_offset(std::uint32_t address) { return address - 0x80000 + 0x1a; }

} // namespace

Rjp1::Rjp1(std::span<const std::uint8_t> sng, std::span<const std::uint8_t> ins, std::span<const std::uint8_t> program)
    : sng_(sng.begin(), sng.end()) {
    if (sng.size() < 12 || ins.size() < 4) throw std::runtime_error("The song or instrument file is too short.");
    for (std::size_t i = 4; i < ins.size(); ++i) samples_.push_back(std::int8_t(ins[i]));
    // Seven sections after "RJP1SMOD", each a long length then the data.
    std::array<int, 7> start{};
    std::size_t at = 8;
    for (int s = 0; s < 7; ++s) {
        const auto length = std::size_t(be32(sng, at));
        start[std::size_t(s)] = int(at + 4);
        at += 4 + length;
    }
    sections_ = {start[0], start[1], start[2], start[3], start[4], start[5], start[6]};
    // Instruments, 32 bytes each ($A3E76 relocates them once: sample
    // locations from the .ins data, the attack offset doubled to bytes).
    for (int i = start[0]; i + 32 <= start[1] - 4; i += 32) {
        const auto u = std::size_t(i);
        instruments_.push_back({be32(sng, u), be32(sng, u + 4) ? be32(sng, u + 4) : -1, be32(sng, u + 8) ? be32(sng, u + 8) : -1,
                                be16(sng, u + 0xc), be16(sng, u + 0xe), be16(sng, u + 0x10) * 2, be16(sng, u + 0x12),
                                be16(sng, u + 0x14), be16(sng, u + 0x16), be16(sng, u + 0x18), be16(sng, u + 0x1a),
                                be16(sng, u + 0x1c), be16(sng, u + 0x1e)});
    }
    // The program's period table (notes are byte offsets into it).
    for (std::size_t i = 0; i < 0x24; ++i) periods_[i] = be16(program, file_offset(period_table_address) + i * 2);
}

std::int32_t Rjp1::sng_long(int offset) const { return be32(sng_, std::size_t(offset)); }

int Rjp1::pattern_offset(int number) const {
    return sections_.patterns + sng_long(sections_.pattern_table + number * 4);
}

int Rjp1::sequence_offset(int number) const {
    return sections_.sequences + sng_long(sections_.sequence_table + number * 4);
}

void Rjp1::start_song(int song) { // $A3FCC
    const int at = sections_.songs + (song & 0x3f) * 4;
    for (int n = 0; n < 4; ++n) {
        const int sequence = sng(at + n);
        if (sequence != 0) start_sequence(n, sequence);
    }
}

void Rjp1::start_sequence(int n, int sequence) { // $A400A
    auto& c = channels_[std::size_t(n)];
    busy_ |= 1 << n;
    if (sequence == 0) {
        c.active = false;
        // (The original also switches the channel's DMA off here.)
        return;
    }
    int at = sequence_offset(sequence);
    const int pattern = sng(at++);
    c.sequence = at;
    c.music.pattern = pattern_offset(pattern);
    c.music.note_length = 6;
    c.music.note_count = 1;
    c.music.row_count = 1;
    c.active = true;
}

void Rjp1::start_effect(int n, int pattern) { // $A4076
    if (!(mode_ & 4)) return;
    busy_ |= 1 << (n + 4);
    auto& e = channels_[std::size_t(n & 3)].effect;
    e.pattern = pattern_offset(pattern);
    e.note_length = 6;
    e.note_count = 1;
    e.row_count = 1;
}

void Rjp1::set_channel_volume(int n, int volume) { // $A3F60
    if (volume != 0x40) volume &= 0x3f;
    if (n < 0) channel_volume_.fill(volume);
    else channel_volume_[std::size_t(n & 3)] = volume;
}

void Rjp1::set_mode(int mode) { // $A3F96
    mode_ = mode & 0xf;
    half_step_ = false;
    music_on_ = mode_ & 8;
    effects_on_ = mode_ & 4;
}

void Rjp1::stop(amiga::PaulaChannels& paula) { // $A3F26
    on_ = false;
    mode_ = 0;
    paula.write_dmacon(0xf);
    for (int n = 0; n < 4; ++n) paula.set_volume(n, 0);
}

int Rjp1::frame(amiga::PaulaChannels& paula) { // $A40C2
    if (!on_) return busy_;
    int steps = mode_ & 3;
    if (steps == 0) {
        half_step_ = !half_step_; // speed 0: every second frame
        steps = 1;
    }
    for (int n = 0; n < 4; ++n) update_channel(n, paula, steps - 1);
    return busy_;
}

void Rjp1::update_channel(int n, amiga::PaulaChannels& paula, int steps) { // $A4144
    auto& c = channels_[std::size_t(n)];
    if (c.effect.pattern >= 0 && effects_on_) {
        // The effect plays; the music goes on counting underneath.
        key_and_loop(n, c.effect, paula);
        step_music(n, paula, steps);
        step_effect(n, paula);
        envelope(c.effect);
        apply_volume_table(c.effect);
        write_volume(n, c.effect, paula);
        write_period(n, c.effect, paula);
        return;
    }
    if (!music_on_ || !c.active) return;
    key_and_loop(n, c.music, paula);
    step_music(n, paula, steps);
    envelope(c.music);
    apply_volume_table(c.music);
    write_volume(n, c.music, paula);
    write_period(n, c.music, paula);
}

void Rjp1::key_and_loop(int n, Voice& v, amiga::PaulaChannels& paula) { // $A4186 + $A41AE
    if (v.loop_next) { // the attack has started: its loop follows it
        v.loop_next = false;
        paula.set_location(n, std::uint32_t(v.sample + v.loop_start * 2));
        paula.set_length(n, std::uint16_t(v.loop_length));
    }
    if (v.key_on) {
        v.key_on = false;
        v.loop_next = true;
        paula.write_dmacon(std::uint16_t(0x8200 | (1 << n)));
    }
}

void Rjp1::step_music(int n, amiga::PaulaChannels& paula, int steps) { // $A41D4
    auto& v = channels_[std::size_t(n)].music;
    for (int k = 0; k <= steps; ++k) {
        if (v.pattern < 0 || half_step_) continue;
        v.note_count = (v.note_count - 1) & 0xff;
        if (v.note_count != 0) continue;
        v.row_count = (v.row_count - 1) & 0xff;
        if (v.row_count != 0) {
            v.note_count = v.note_length;
            continue;
        }
        read_pattern(n, v, true, paula);
    }
}

void Rjp1::step_effect(int n, amiga::PaulaChannels& paula) { // $A455A
    auto& v = channels_[std::size_t(n)].effect;
    if (!(mode_ & 4) || v.pattern < 0 || half_step_) return;
    v.note_count = (v.note_count - 1) & 0xff;
    if (v.note_count != 0) return;
    v.row_count = (v.row_count - 1) & 0xff;
    if (v.row_count != 0) {
        v.note_count = v.note_length;
        return;
    }
    read_pattern(n, v, false, paula);
}

void Rjp1::read_pattern(int n, Voice& v, bool music, amiga::PaulaChannels& paula) { // $A41F6 / $A4584
    auto& c = channels_[std::size_t(n)];
    int at = v.pattern;
    const auto finish = [&] {
        v.pattern = at;
        v.row_count = v.rows;
        v.note_count = v.note_length;
    };
    while (true) {
        const int b = sng(at++);
        if (b < 0x80) { // a note
            const int period = periods_[std::size_t((b >> 1) % int(periods_.size()))];
            if (!music) note_on(n, c.effect, period, paula);
            else if (c.effect.pattern < 0) note_on(n, c.music, period, paula);
            return finish();
        }
        switch (b & 0x7f) {
        case 0: // end of the pattern
            if (!music) {
                at = -1;
                busy_ &= ~(1 << (n + 4));
                return finish();
            }
            v.rows = 1;
            // (The position is kept only when a pattern is found.)
            for (int cursor = c.sequence;;) {
                const int next = sng(cursor++);
                if (next != 0) {
                    c.sequence = cursor;
                    at = pattern_offset(next);
                    break;
                }
                const int jump = std::int8_t(sng(cursor));
                if (jump == 0) { // the sequence ends: the channel stops
                    at = -1;
                    c.active = false;
                    busy_ &= ~(1 << n);
                    return finish();
                }
                if (jump < 0) cursor = sequence_offset(sng(cursor + 1));
                else cursor -= jump;
            }
            continue;
        case 1:
            release(v);
            return finish();
        case 2:
            v.note_length = sng(at++);
            continue;
        case 3:
            v.rows = sng(at++);
            continue;
        case 4: {
            const int number = sng(at++);
            if (number != 0) select_instrument(v, number);
            continue;
        }
        case 5:
            v.volume_scale = sng(at++);
            continue;
        case 6:
            v.slide_count = sng(at);
            v.slide_step = std::int32_t((std::uint32_t(sng(at + 1)) << 24) | (std::uint32_t(sng(at + 2)) << 16) |
                                        (std::uint32_t(sng(at + 3)) << 8) | std::uint32_t(sng(at + 4)));
            v.slide = 0;
            at += 5;
            continue;
        default: // 7: a rest
            return finish();
        }
    }
}

void Rjp1::select_instrument(Voice& v, int number) { // $A44A6 / $A47E4
    if (number == v.instrument) return;
    v.instrument = number;
    if (std::size_t(number) >= instruments_.size()) return;
    const auto& i = instruments_[std::size_t(number)];
    v.attack_length = i.attack_length;
    v.loop_start = i.loop_start;
    v.loop_length = i.loop_length;
    v.volume_scale = i.volume_scale;
    v.period_loop = i.period_loop;
    v.period_position = 0;
    v.period_end = i.period_length * 2;
    v.volume_loop = i.volume_loop;
    v.volume_position = 0;
    v.volume_end = i.volume_length * 2;
    v.sample = i.sample;
    v.period_table = i.period_table;
    v.volume_table = i.volume_table;
}

void Rjp1::note_on(int n, Voice& v, int period, amiga::PaulaChannels& paula) { // $A4446 / $A4784
    const auto& i = instruments_[std::size_t(v.instrument) < instruments_.size() ? std::size_t(v.instrument) : 0];
    v.base_period = v.period = period;
    v.slide = 0;
    v.envelope = i.envelope;
    const int e = sections_.envelopes + i.envelope;
    v.envelope_target = sng(e + 1);
    v.envelope_delta = std::int8_t(sng(e + 1) - sng(e));
    v.envelope_length = v.envelope_count = sng(e + 2);
    v.envelope_stage = 4; // attack
    paula.write_dmacon(std::uint16_t(1 << n));
    const int start = v.sample + i.attack_offset;
    // The player clears the attack's first word (no click on the restart).
    for (int k = 0; k < 2; ++k) {
        if (start + k >= 0 && std::size_t(start + k) < samples_.size()) samples_[std::size_t(start + k)] = 0;
    }
    paula.set_location(n, std::uint32_t(start));
    paula.set_length(n, std::uint16_t(i.attack_length));
    v.key_on = true;
}

void Rjp1::release(Voice& v) { // $A43C4 / $A4702
    v.envelope_target = 0;
    v.envelope_delta = std::int8_t(0 - v.envelope_value);
    v.envelope_length = v.envelope_count = sng(sections_.envelopes + v.envelope + 5);
    v.envelope_stage = -1;
}

void Rjp1::envelope(Voice& v) { // $A4332 / $A4670
    if (v.envelope_stage != 0) {
        int drop = 0;
        if (v.envelope_delta != 0 && v.envelope_count != 0 && v.envelope_length != 0) {
            drop = (v.envelope_delta * v.envelope_count) / v.envelope_length; // (muls, then divs: towards 0)
        }
        v.envelope_value = (v.envelope_target - drop) & 0xff;
        v.envelope_count = (v.envelope_count - 1) & 0xff;
        if (v.envelope_count == 0xff) {
            if (v.envelope_stage == 4) { // attack done: decay
                const int e = sections_.envelopes + v.envelope;
                v.envelope_target = sng(e + 3);
                v.envelope_delta = std::int8_t(sng(e + 3) - sng(e + 1));
                v.envelope_length = v.envelope_count = sng(e + 4);
                v.envelope_stage = 2;
            } else { // decay or release done: hold
                v.envelope_stage = 0;
            }
        }
    }
    v.volume = v.envelope_value;
}

void Rjp1::apply_volume_table(Voice& v) { // $A4300 / $A463E
    if (v.volume_table < 0) return;
    const std::size_t at = std::size_t(v.volume_table + v.volume_position);
    const int value = at < samples_.size() ? samples_[at] : 0;
    v.volume = std::int16_t(v.volume + (std::int16_t(value * v.volume) >> 7));
    if (++v.volume_position == v.volume_end) v.volume_position = v.volume_loop * 2;
}

void Rjp1::write_volume(int n, Voice& v, amiga::PaulaChannels& paula) { // $A42D8 / $A4616
    int level = std::int16_t(std::int16_t(v.volume) * std::int16_t(v.volume_scale)) >> 6;
    level = std::int16_t(std::int16_t(level) * std::int16_t(channel_volume_[std::size_t(n)])) >> 6;
    v.volume = std::int16_t(level);
    const int out = std::uint16_t(level) <= 64 ? level : (std::int16_t(level) > 64 ? 64 : 0);
    paula.set_volume(n, std::uint16_t(out));
}

void Rjp1::write_period(int n, Voice& v, amiga::PaulaChannels& paula) { // $A43E8 / $A4726
    if (v.period_table >= 0) {
        const std::size_t at = std::size_t(v.period_table + v.period_position);
        const int value = at < samples_.size() ? samples_[at] : 0;
        int shift = std::int16_t(-std::int16_t((value * std::int16_t(v.base_period)) >> 7));
        if (shift < 0) shift = std::int16_t(shift) >> 1; // upwards half as far
        v.period = std::int16_t(shift + v.base_period);
        if (++v.period_position == v.period_end) v.period_position = v.period_loop * 2;
    }
    if (v.slide_count != 0) {
        v.slide += v.slide_step;
        v.slide_count = (v.slide_count - 1) & 0xff;
    }
    paula.set_period(n, std::uint16_t(std::int16_t(std::uint32_t(v.slide) >> 16) + v.period));
}

} // namespace fodder::sound
