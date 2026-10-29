// The bank 2 sound driver; see sound_driver.hpp.

#include "gg_engine/audio/sound_driver.hpp"
#include <stdexcept>
#include <string>

namespace gg_engine {

namespace {
using namespace sound_channel;

// Two uppercase hexadecimal digits.
std::string hex2(u8 v) {
    const char* digits = "0123456789ABCDEF";
    return {digits[v >> 4], digits[v & 15]};
}

// A signed byte as the high byte of a 16-bit addend.
u8 sign_high(u8 v) {
    return (v & 0x80) ? 0xFF : 0x00;
}
} // namespace

u8 SoundDriver::data(unsigned address) const {
    address &= 0xFFFF;
    // While the driver runs, bank 2 is in slot 1 and the profile's bank in
    // slot 2.
    if (address < 0x4000) {
        return rom_.byte(0, address);
    }
    return rom_.byte(address < 0x8000 ? 2 : profile_.slot2_bank, address);
}

u8& SoundDriver::stack_byte(unsigned offset) {
    if (offset >= 0x2A && offset < 0x30) {
        return ch().stack_bytes[offset - 0x2A];
    }
    if (offset >= 0x27 && offset < 0x2A) {
        return ch().loops[offset - 0x27];
    }
    throw std::runtime_error("Sound call stack nested too deep");
}

u8 SoundDriver::iy_plus_3() const {
    switch (iy_.to) {
    case Pointer::To::requests: // DD03/DE03 + 3: the third request slot
        return state.requests[2];
    case Pointer::To::channel: // a record + 3: its position's low byte
        if (iy_.value >= state.channels.size()) {
            throw std::runtime_error("Sound vibrato read past the channel records");
        }
        return u8(state.channels[iy_.value].position);
    case Pointer::To::data:
        break;
    }
    return data(iy_.value + 3);
}

// S2 441E, TT 508F: the chip silenced (tones at period 0, all volumes off);
// idle.
void SoundDriver::init() {
    silence();
    state.current = 0x80;
}

void SoundDriver::silence() {
    for (unsigned i = 0; i < 10; ++i) {
        out(data(profile_.silence_writes + i));
    }
}

// S2 4402, TT 5070: everything from the current request on cleared (Sonic 2
// keeps its 50 Hz mode, which lies past the cleared range), the stereo
// register on; then silent and idle.
void SoundDriver::stop_all() {
    const u8 tempo_counter = state.tempo_counter, tempo = state.tempo;
    const u8 pal_mode = state.pal_mode, pal_counter = state.pal_counter;
    state = {};
    state.tempo_counter = tempo_counter;
    state.tempo = tempo;
    state.pal_mode = pal_mode;
    state.pal_counter = pal_counter;
    state.stereo = 0xFF;
    out_stereo(0xFF);
    init();
}

// S2 4000, TT 4C42
void SoundDriver::tick() {
    u8& pause = state.pause;
    if (pause) {
        if (!(pause & 0x80)) {
            return;
        }
        --pause; // silence once, then hold
        init();
        return;
    }
    take_requests();
    slow_tempo();
    fade();
    start_request();
    // S2 4018: in 50 Hz mode every fifth frame runs the channels twice.
    if (state.pal_mode) {
        u8& counter = state.pal_counter;
        const u8 n = u8(counter + 1);
        if (n != 5) {
            counter = n;
        } else {
            counter = 0;
            run_channels();
            run_channels();
            return;
        }
    }
    run_channels();
}

// S2 4038
void SoundDriver::run_channels() {
    for (unsigned i = 0; i < 7; ++i) {
        channel_ = i;
        if (!(ch().flags & 0x80)) {
            continue;
        }
        stop_ = Stop::none;
        noise_running_ = i == noise;
        if (i == noise) {
            noise_channel();
        } else {
            tone_channel();
        }
        if (stop_ == Stop::tick) {
            return;
        }
    }
}

// The request slots compete by priority for the request to start this
// frame.
void SoundDriver::take_requests() {
    iy_ = {Pointer::To::requests, 0};
    for (unsigned i = 0; i < profile_.request_slots; ++i) {
        take_request(state.requests[i]);
    }
}

void SoundDriver::take_request(u8& request) {
    const u8 n = request & 0x7F;
    if (n) {
        const u8 priority = data(profile_.priorities + (n - 1u));
        if (priority >= state.priority) {
            state.priority = priority & 0x7F;
            state.current = request;
        }
    }
    request = 0;
}

void SoundDriver::slow_tempo() {
    if (profile_.tempo == DriverProfile::Tempo::accumulate) {
        const unsigned sum = state.tempo_counter + state.tempo;
        state.tempo_counter = u8(sum);
        if (sum <= 0xFF) {
            return;
        }
    } else {
        u8& counter = state.tempo_counter;
        if (!counter) {
            return;
        }
        if (--counter) {
            return;
        }
        counter = state.tempo;
    }
    for (unsigned i = tone1; i <= noise; ++i) {
        ++state.channels[i].ticks;
    }
}

// A fade ends any effect (and in Triple Trouble the music noise channel),
// then lowers the music's volume one step (of `fade_step`) every
// fade_speed / fade_step frames, fade_steps / fade_step times; at the end
// everything stops.
void SoundDriver::fade() {
    if (!state.fade_steps) {
        return;
    }
    const u8 step = profile_.fade_step;
    auto& drums = state.channels[noise];
    if (profile_.fade_ends_drums && (drums.flags & 0x80)) {
        out(state.noise_mode);
        drums.flags = u8((drums.flags & ~0x04) | 0x10);
        out(u8(state.fade_steps | 0x1F)); // a data byte for the noise register
        state.priority = 0;
        drums.flags = 0;
        out(0xFF);
    }
    for (unsigned i = effect_tone2; i <= effect_noise; ++i) {
        auto& effect = state.channels[i];
        if (!(effect.flags & 0x80)) {
            continue;
        }
        const u8 channel = effect.id;
        if (channel == 0xA0) {
            state.channels[tone2].flags &= 0xFB;
        } else {
            if (channel == 0xE0) {
                state.channels[noise].flags &= 0xFB;
            }
            state.channels[tone3].flags &= 0xFB;
        }
        effect.flags = 0;
    }
    if (const u8 counter = u8(state.fade_counter - step)) {
        state.fade_counter = counter;
        return;
    }
    state.fade_counter = state.fade_speed;
    state.fade_steps = u8(state.fade_steps - step);
    if (!state.fade_steps) {
        stop_all();
        return;
    }
    auto quieter = [step](u8& volume) {
        if (u8(volume + step) < 0x0C) {
            volume = u8(volume + step);
        }
    };
    for (unsigned i = tone1; i <= tone3; ++i) {
        quieter(state.channels[i].volume);
    }
    quieter(state.drum_volume);
}

// The current request (bit 7 clear stops everything): songs from 81, then
// effects, then the commands fade out, stop, end the effects; any higher
// number stops.
void SoundDriver::start_request() {
    const u8 r = state.current;
    if (!(r & 0x80)) {
        stop_all();
        return;
    }
    if (r < profile_.first_effect) {
        start_music(r);
        return;
    }
    if (r < profile_.first_command) {
        start_effect(r);
        return;
    }
    switch (r - profile_.first_command) {
    case 0: // S2 4160, TT 4DB0
        state.fade_steps = 0x0C;
        state.fade_counter = 0x12;
        state.fade_speed = 0x12;
        state.current = 0x80;
        return;
    case 2: // S2 4170, TT 4DC0: the effect channels continue at an end command
        for (unsigned i = effect_tone2; i <= effect_noise; ++i) {
            state.channels[i].position = std::uint16_t(profile_.end_command);
        }
        iy_ = {Pointer::To::channel, 7}; // left one record past the last
        return;
    case 3: // no routine for it
        throw std::runtime_error("Sound request " + hex2(r) + " (no routine) not reconstructed");
    default: // stop
        stop_all();
        return;
    }
}

// S2 4188, TT 4DD8: a song (81 = 0). The header gives the channel count,
// the duration unit and the tempo, then four bytes per channel: address,
// transpose, volume. (Sonic 2 first takes a tempo from a table of its own,
// overwritten at once.)
void SoundDriver::start_music(u8 r) {
    if (r < 0x81) {
        return; // SUB 81; RET M (80 is the idle value)
    }
    const unsigned n = u8(r - 0x81);
    stop_all();
    if (profile_.song_tempos) {
        state.tempo_counter = data(profile_.song_tempos + n);
        state.tempo = data(profile_.song_tempos + n);
    }
    unsigned h =
        data(profile_.song_headers + n * 2) | (data(profile_.song_headers + n * 2 + 1) << 8);
    const unsigned count = data(h + 2);
    const u8 unit_value = data(h + 4);
    state.tempo_counter = data(h + 5);
    state.tempo = data(h + 5);
    iy_ = {Pointer::To::data, profile_.music_channel_ids};
    h += 6;
    channel_ = tone1;
    for (unsigned i = 0; i < count; ++i) {
        setup_music_channel(h, unit_value);
    }
    state.current = 0x80;
}

// S2 4254, TT 4EAF
void SoundDriver::setup_music_channel(unsigned& header, u8 unit_value) {
    ch().flags = 0x80;
    ch().id = data(iy_.value);
    ++iy_.value;
    ch().unit = unit_value;
    ch().position = std::uint16_t(data(header) | (data(header + 1) << 8));
    ch().transpose = data(header + 2);
    ch().volume = data(header + 3);
    header += 4;
    ch().modulation = 0;
    ch().envelope = 0;
    // C = 34 counts down with the four LDIs: the stack starts at offset 30.
    ch().stack = 0x30;
    ch().loops = {};
    ch().ticks = 1;
    ++channel_;
}

// S2 41C7, TT 4E1D: an effect (the first effect number = 0). Its header
// gives the unit and the channel count, then six bytes per channel (flags,
// channel, address, transpose, volume); each takes the effect channel
// matching its chip channel and marks the music channel as taken over.
void SoundDriver::start_effect(u8 r) {
    state.effect = r;
    const unsigned n = u8(r - profile_.first_effect);
    unsigned h =
        data(profile_.effect_headers + n * 2) | (data(profile_.effect_headers + n * 2 + 1) << 8);
    const u8 unit_value = data(h + 2);
    unsigned count = data(h + 3);
    h += 4;
    while (count--) {
        const u8 channel = data(h + 1);
        if (channel == 0xA0) {
            channel_ = effect_tone2;
            iy_ = {Pointer::To::channel, tone2};
        } else if (channel == 0xC0) {
            // S2 41ED, TT 4E49: tone 3 also silences the noise unless the
            // noise effect channel has its own rate.
            if (!(state.channels[effect_noise].flags & 0x40)) {
                state.channels[effect_noise].flags |= 0x04;
                out(0xFF);
            }
            channel_ = effect_tone3;
            iy_ = {Pointer::To::channel, tone3};
        } else {
            channel_ = effect_noise;
            iy_ = {Pointer::To::channel, noise};
        }
        setup_effect_channel(h, unit_value);
    }
    state.current = 0x80;
}

// S2 4217, TT 4E72 (the record's other fields keep what an earlier effect
// left)
void SoundDriver::setup_effect_channel(unsigned& header, u8 unit_value) {
    state.channels[iy_.value].flags |= 0x04; // the music channel is taken over
    ch().flags = data(header);
    ch().id = data(header + 1);
    ch().unit = unit_value;
    ch().position = std::uint16_t(data(header + 2) | (data(header + 3) << 8));
    ch().transpose = data(header + 4);
    ch().volume = data(header + 5);
    header += 6;
    ch().modulation = 0;
    ch().envelope = 0;
    // C = 36 counts down with the six LDIs: the stack starts at offset 30.
    ch().stack = 0x30;
    out_stereo(0xFF);
    ch().loops = {};
    ch().ticks = 1;
}

// S2 444F, TT 50C7: a duration times the channel's unit (8-bit).
u8 SoundDriver::scale(u8 a) {
    u8 b = ch().unit;
    if (!--b) {
        return a;
    }
    const u8 base = a;
    while (b--) {
        a = u8(a + base);
    }
    return a;
}

// S2 4438, TT 50A9: the period of a note plus transpose.
unsigned SoundDriver::note_frequency(u8 note) {
    const u8 i = u8((note - profile_.note_base + ch().transpose) & profile_.note_mask);
    return data(profile_.note_periods + i * 2u) | (data(profile_.note_periods + i * 2u + 1) << 8);
}

// S2 44E5, TT 51D1: a tone channel. A new event when the duration runs out;
// otherwise the gate, modulation and envelope go on. Output: the period
// (with modulation and detune) and the volume (with the envelope).
void SoundDriver::tone_channel() {
    bool send_frequency;
    if (--ch().ticks == 0) {
        next_event();
        if (stop_ != Stop::none) {
            return;
        }
        if (ch().flags & 0x10) {
            return;
        }
        if (ch().flags & 0x04) {
            return;
        }
        start_modulation();
        start_envelope();
        send_frequency = true;
    } else {
        if (ch().flags & 0x04) {
            return;
        }
        if (ch().gate) {
            if (--ch().gate == 0) {
                note_off();
            }
        }
        send_frequency = ch().modulation && !(ch().flags & 0x20);
    }
    if (send_frequency && !(ch().flags & 0x40)) {
        // S2 451C, TT 520E
        unsigned hl = modulate();
        if (stop_ != Stop::none) {
            return;
        }
        const u8 d = ch().detune;
        hl = (hl + unsigned(sign_high(d) << 8 | d)) & 0xFFFF;
        u8 channel = ch().id;
        if (channel == 0xE0) {
            channel = 0xC0; // the noise effect channel drives tone 3
        }
        out(u8((hl & 0x0F) | channel));
        const u8 a = u8((hl & 0xF0) | (hl >> 8));
        out(u8((a >> 4) | (a << 4)));
    }
    // S2 454B, TT 5237
    u8 level = 0;
    if (!envelope(level)) {
        return;
    }
    if (ch().flags & 0x04) {
        return;
    }
    if (ch().flags & 0x10) {
        return;
    }
    u8 a = u8(level + ch().volume);
    if (a & 0x10) {
        a = 0x0F;
    }
    out(u8((a | ch().id) + 0x10));
}

// S2 436A, TT 4FCC: reads events until a note, rest or duration.
void SoundDriver::next_event() {
    ch().flags &= u8(~(0x02 | 0x10));
    de_ = ch().position;
    for (;;) {
        const u8 a = data(de_++);
        if (a >= profile_.first_event_command) {
            command(a); // returns to INC DE
            if (stop_ != Stop::none) {
                return;
            }
            ++de_;
            continue;
        }
        if (ch().flags & 0x08) {
            // S2 43E4, TT 5052: raw period (two bytes; 0 means silence) plus
            // transpose
            unsigned hl = unsigned(a << 8) | data(de_++);
            if (hl) {
                const u8 t = ch().transpose;
                hl = (hl + unsigned(sign_high(t) << 8 | t)) & 0xFFFF;
            }
            ch().frequency = std::uint16_t(hl);
            read_duration(data(de_++));
            return;
        }
        if (profile_.volume_events && a >= 0xD8) {
            ch().volume = a & 0x07; // TT 4FEC
            continue;
        }
        if (a < 0x80) {
            read_duration(a);
            return;
        }
        if (a == 0x80) {
            note_off();
        } else {
            ch().env_state &= 0x7F;
            ch().frequency = std::uint16_t(note_frequency(a));
        }
        // S2 439F, TT 500D: a duration follows, or the last one is used again.
        const u8 d = data(de_++);
        if (!(d & 0x80)) {
            read_duration(d);
            return;
        }
        ch().ticks = ch().last_ticks;
        --de_;
        finish_event();
        return;
    }
}

// S2 43AE, TT 501C
void SoundDriver::read_duration(u8 a) {
    const u8 t = scale(a);
    ch().ticks = t;
    ch().last_ticks = t;
    finish_event();
}

// S2 43B7, TT 5025
void SoundDriver::finish_event() {
    ch().position = std::uint16_t(de_);
    if (ch().flags & 0x02) {
        return;
    }
    if (!(ch().flags & 0x40)) {
        ch().flags &= 0xDF;
    }
    ch().gate = ch().gate_reload;
    ch().mod_speed = 0;
    if (ch().envelope & 0x80) {
        return;
    }
    ch().env_level = 0;
}

// S2 4290, TT 4EEB: inline vibrato parameters restart with each note (not
// tied).
void SoundDriver::start_modulation() {
    if (!(ch().modulation & 0x80)) {
        return;
    }
    if (ch().flags & 0x02) {
        return;
    }
    const unsigned p = ch().mod_params;
    ch().mod_delay = data(p);
    ch().mod_speed = data(p + 1);
    ch().mod_step = data(p + 2);
    ch().mod_count = u8(data(p + 3) >> 1);
    ch().mod_offset = 0;
}

// S2 42BB, TT 4F16: envelope numbers 80 and above restart at full
// attenuation.
void SoundDriver::start_envelope() {
    if (!(ch().envelope & 0x80)) {
        return;
    }
    if (ch().flags & 0x02) {
        return;
    }
    if (ch().env_state & 0x80) {
        return;
    }
    ch().env_level = 0xFF;
    ch().env_state = u8(0x10 | ch().env_base);
}

// S2 42D8, TT 4F33: the period with vibrato.
//
// Inline parameters (80): after the delay the offset moves by `step` every
// `speed` frames and the step reverses every `count` moves. (Quirk: when the
// speed counter has not just run out, IY still holds what earlier code left
// in it; the original reads the count reload from there.)
//
// Tables (1-7F, Triple Trouble): one signed offset per frame; 80 starts
// again, 81 and 82 hold the period and end the channel routine, 83 jumps.
// (Quirk: the jump takes its target from the byte after the period's
// address, not from the table, and adds one to the period from then on.)
unsigned SoundDriver::modulate() {
    unsigned hl = ch().frequency;
    const u8 mode = ch().modulation;
    if (!mode) {
        return hl;
    }
    if (!(mode & 0x80)) {
        // TT 4F89
        if (!profile_.modulation_tables) {
            throw std::runtime_error("Sound modulation table " + std::to_string(mode) +
                                     " not reconstructed");
        }
        const unsigned tables = profile_.modulation_tables;
        const unsigned table =
            data(tables + (mode - 1u) * 2) | (data(tables + (mode - 1u) * 2 + 1) << 8);
        unsigned base = hl;
        for (;;) {
            const u8 a = data(table + ch().mod_speed);
            if (!(a & 0x80)) {
                ++ch().mod_speed;
                return (base + a) & 0xFFFF;
            }
            if (a > 0x83) {
                ++ch().mod_speed;
                return (base + (0xFF00u | a)) & 0xFFFF;
            }
            if (a == 0x83) {
                base = (base + 1) & 0xFFFF;
                if (base >= 0x8000) {
                    throw std::runtime_error("Sound modulation jump read outside the cartridge");
                }
                ch().mod_speed = data(base);
                continue;
            }
            if (a == 0x80) {
                ch().mod_speed = 0;
                continue;
            }
            ch().flags |= 0x20; // 81, 82
            stop_ = Stop::channel;
            return hl;
        }
    }
    if (--ch().mod_delay) {
        return hl;
    }
    ++ch().mod_delay;
    unsigned offset = ch().mod_offset;
    if (--ch().mod_speed == 0) {
        iy_ = {Pointer::To::data, ch().mod_params};
        ch().mod_speed = data(iy_.value + 1);
        const u8 s = ch().mod_step;
        offset = (offset + unsigned(sign_high(s) << 8 | s)) & 0xFFFF;
        ch().mod_offset = std::uint16_t(offset);
    }
    hl = (offset + hl) & 0xFFFF;
    if (--ch().mod_count) {
        return hl;
    }
    ch().mod_count = iy_plus_3();
    ch().mod_step = u8(-ch().mod_step);
    return hl;
}

// S2 4569, TT 5255: the envelope's attenuation. Tables step one entry per
// frame (82: silence and mute, 81: silence, 80: repeat); both silences end
// the channel routine (POP HL), which is what `false` means. Envelope
// numbers 80 and above are Sonic 2's ADSR envelope; Triple Trouble has no
// code for them: its note-off routine runs and the value it leaves in A is
// used as the attenuation.
bool SoundDriver::envelope(u8& level) {
    const u8 mode = ch().envelope;
    level = 0;
    if (!mode) {
        return true;
    }
    if (mode & 0x80) {
        if (profile_.high_envelopes == DriverProfile::HighEnvelopes::adsr) {
            return adsr_envelope(level);
        }
        level = note_off();
        if (ch().flags & 0x02) {
            level = mode; // TT 5263 returns at once: A still holds the number
        }
        return true;
    }
    // S2 4620, TT 527A
    const unsigned table = data(profile_.envelope_tables + (mode - 1u) * 2) |
                           (data(profile_.envelope_tables + (mode - 1u) * 2 + 1) << 8);
    for (;;) {
        const u8 a = data(table + ch().env_level);
        if (!(a & 0x80)) {
            ++ch().env_level;
            level = a;
            return true;
        }
        if (a == 0x82) {
            mute();
            return false;
        }
        if (a == 0x81) {
            ch().flags |= 0x10;
            return false;
        }
        if (a == 0x80) {
            ch().env_level = 0;
            continue;
        }
        throw std::runtime_error("Sound envelope command " + std::to_string(a) +
                                 " not reconstructed");
    }
}

// S2 4571: ADSR. Phases in env_state bits 4 (attack: level down by rate 0),
// 5 (decay toward the sustain level), 6 (sustain fade); none: release.
bool SoundDriver::adsr_envelope(u8& level) {
    u8& phase = ch().env_state;
    u8& l = ch().env_level;
    if (phase & 0x10) {
        const u8 d = ch().adsr[0];
        int v = int(l) - int(d);
        if (v < 0) {
            v = 0;
        }
        l = u8(v);
        if (!l) {
            phase ^= 0x30;
        }
    } else if (phase & 0x20) {
        const u8 d = ch().adsr[1], e = ch().adsr[2];
        unsigned v = l + d;
        u8 a = (v > 0xFF) ? e : u8(v);
        if (v <= 0xFF && a >= e) {
            a = e;
        }
        l = a;
        if (a == e) {
            phase ^= (phase & 0x08) ? 0x30 : 0x60;
        }
    } else if (phase & 0x40) {
        unsigned v = l + ch().adsr[3];
        const u8 a = v > 0xFF ? 0xFF : u8(v);
        l = a;
        if (a == 0xFF) {
            phase &= 0x8F;
        }
    } else {
        const unsigned v = l + ch().adsr[4];
        if (v > 0xFF) {
            phase &= 0x0F;
            l = 0xFF;
            mute();
            level = 0x0F;
            return true;
        }
        l = u8(v);
    }
    level = u8((l >> 4) & 0x0F);
    return true;
}

// S2 4609, TT 5263: the note ends: with an envelope number 80 and above its
// state releases, otherwise silence. Returns A as the routine leaves it.
u8 SoundDriver::note_off() {
    if (ch().flags & 0x02) {
        return 0;
    }
    if (ch().envelope & 0x80) {
        ch().env_state = u8((ch().env_state & 0x0F) | 0x80);
        return ch().env_state;
    }
    mute();
    return u8(0x1F + ch().id);
}

// S2 465C, TT 52BD
void SoundDriver::mute() {
    ch().flags |= 0x10;
    if (ch().flags & 0x04) {
        return;
    }
    out(u8(0x1F + ch().id));
}

// S2 466D, TT 52CE: the music noise channel: events are drum sounds. A new
// event writes nothing; the volume is written on the following frames.
void SoundDriver::noise_channel() {
    if (--ch().ticks) {
        u8 level = 0;
        if (!envelope(level) || (ch().flags & 0x14)) {
            return;
        }
        u8 a = u8(level + ch().volume);
        if (a & 0x10) {
            a = 0x0F;
        }
        out(u8((a | ch().id) + 0x10));
        return;
    }
    ch().flags &= 0xEF;
    de_ = ch().position;
    for (;;) {
        const u8 a = data(de_++);
        if (a >= 0xE0) {
            if (a == 0xE0 && profile_.high_envelopes != DriverProfile::HighEnvelopes::adsr) {
                throw std::runtime_error("Sound command E0 on the noise channel (no routine)");
            }
            command(a); // returns to INC DE
            if (stop_ != Stop::none) {
                return;
            }
            ++de_;
            continue;
        }
        if (a < 0x80) {
            read_duration(a);
            return;
        }
        drum(a);
        const u8 d = data(de_++);
        if (!(d & 0x80)) {
            read_duration(d);
            return;
        }
        ch().ticks = ch().last_ticks;
        --de_;
        finish_event();
        return;
    }
}

// S2 4697, TT 52F8: a drum sound: envelope, volume and noise mode (added to
// noise_base).
void SoundDriver::drum(u8 a) {
    const DriverProfile::Drum* drum = nullptr;
    for (unsigned bit = 0; bit < profile_.drums.size(); ++bit) {
        const bool match = profile_.drum_match == DriverProfile::DrumMatch::exact
                               ? a == (0x80 | (1u << bit))
                               : (a & (1u << bit)) != 0;
        if (match) {
            drum = &profile_.drums[bit];
            break;
        }
    }
    if (!drum) {
        mute();
        return;
    }
    ch().envelope = drum->envelope;
    ch().volume =
        profile_.drum_volume_added ? u8(state.drum_volume + drum->volume) : drum->volume;
    if (ch().flags & 0x04) {
        return;
    }
    state.noise_mode = u8(state.noise_base + drum->mode);
    out(state.noise_mode);
}

// S2 46F9, TT 5357: commands. DE points at the first argument; the caller
// then moves past one byte, so commands with other argument counts adjust
// DE.
void SoundDriver::command(u8 code) {
    const u8 a = data(de_);
    switch (code) {
    case 0xE0: // S2 4909: ADSR envelope, five rates
        if (profile_.high_envelopes != DriverProfile::HighEnvelopes::adsr) {
            return;
        }
        ch().envelope = 0x80;
        for (unsigned k = 0; k < 5; ++k) {
            ch().adsr[k] = data(de_ + k);
        }
        de_ += 4;
        return;
    case 0xE1: // S2 48FE, TT 555E
        ch().detune = a;
        return;
    case 0xE2: { // S2 4751, TT 53AD: stereo: the argument's channels are switched off
        const u8 old = ch().stereo;
        ch().stereo = a;
        set_stereo(old, u8(~a), ch().id);
        return;
    }
    case 0xE4: // S2 478E, TT 53F7
        state.drum_volume = u8((state.drum_volume + a) & 0x0F);
        return;
    case 0xE5: // S2 4923
        if (profile_.env_base_command) {
            ch().env_base = a ? 0x08 : 0x00;
        }
        return;
    case 0xE6: // S2 477E, TT 53DA: volume change (not while fading)
        if (state.fade_steps) {
            return;
        }
        ch().volume = u8((ch().volume + a) & 0x0F);
        return;
    case 0xE7: // S2 48F8, TT 5558: tie the next note
        ch().flags |= 0x02;
        --de_;
        return;
    case 0xE8: { // S2 48E0, TT 5540: note length (gate)
        const u8 g = scale(a);
        ch().gate = g;
        ch().gate_reload = g;
        return;
    }
    case 0xE9: // TT 53EA: volume (not while fading)
        if (!profile_.volume_command || state.fade_steps) {
            return;
        }
        ch().volume = a & 0x0F;
        return;
    case 0xED: // S2 4902, TT 5562: tempo
        state.tempo = a;
        state.tempo_counter = a;
        return;
    case 0xF0: // S2 48EA, TT 554A: inline vibrato parameters (4 bytes)
        ch().mod_params = std::uint16_t(de_);
        ch().modulation = 0x80;
        de_ += 3;
        return;
    case 0xF2: // S2 4819, TT 5482: end of this channel
        stop_channel();
        return;
    case 0xF3: // S2 47A4, TT 540D: noise mode
        // A noise channel asking for tone 3's period while an effect uses
        // tone 3 gives up instead (unless an effect was started last):
        // silenced and ended, ending the channel routine like the end of a
        // channel.
        if (!(state.effect & 0x80) && (a & 0x03) == 0x03 &&
            (state.channels[effect_tone3].flags & 0x80)) {
            state.channels[noise].flags = u8((state.channels[noise].flags & ~0x04) | 0x10);
            ch().flags = 0;
            out(0xFF);
            state.effect = 0xFF;
            stop_ = noise_running_ ? Stop::tick : Stop::channel;
            return;
        }
        // S2 47CD, TT 5436: with tone 3's period the noise takes tone 3
        // (muted, both tone 3 channels marked as taken over).
        out(a);
        iy_ = {Pointer::To::channel, effect_tone3};
        if ((a & 0x03) == 0x03) {
            out(0xDF);
            ch().flags &= u8(~0x40);
            state.channels[tone3].flags |= 0x04;
            state.channels[effect_tone3].flags |= 0x04;
        } else {
            ch().flags |= 0x40;
            if (state.channels[effect_tone3].flags & 0x80) {
                state.channels[effect_tone3].flags &= u8(~0x04);
            } else {
                state.channels[tone3].flags &= u8(~0x04);
            }
        }
        return;
    case 0xF4: // S2 4801, TT 546A
        ch().modulation = a;
        return;
    case 0xF5: // S2 47FD, TT 5466
        ch().envelope = a;
        return;
    case 0xF6: // S2 4805, TT 546E: jump
        de_ = (data(de_) | (data(de_ + 1) << 8)) - 1u;
        return;
    case 0xF7: { // S2 48C9, TT 5513: loop {counter, count, address}
        ++de_;
        u8& counter = stack_byte(0x27 + a);
        if (!counter) {
            counter = data(de_);
        }
        ++de_;
        if (--counter) {
            de_ = (data(de_) | (data(de_ + 1) << 8)) - 1u;
            return;
        }
        ++de_;
        return;
    }
    case 0xF8: { // S2 489C, TT 54E6: call; the return address goes on the record's stack
        const unsigned target = a | (data(de_ + 1) << 8);
        ++de_;
        --ch().stack;
        const u8 sp = ch().stack;
        --ch().stack;
        stack_byte(sp) = u8(de_ >> 8);
        stack_byte(sp - 1u) = u8(de_);
        de_ = target - 1;
        return;
    }
    case 0xF9: { // S2 48B6, TT 5500: return
        const u8 sp = ch().stack;
        de_ = stack_byte(sp) | (stack_byte(sp + 1u) << 8);
        ch().stack = u8(sp + 2);
        return;
    }
    case 0xFA: // S2 47A0, TT 5409
        ch().unit = a;
        return;
    case 0xFB: // S2 4799, TT 5402
        ch().transpose = u8(ch().transpose + a);
        return;
    case 0xFC: { // TT 552A: leave a loop on its last pass {counter, address}
        if (!profile_.loop_exit_command) {
            return;
        }
        ++de_;
        u8& counter = stack_byte(0x27 + a);
        if (counter == 1) {
            counter = 0;
            de_ = (data(de_) | (data(de_ + 1) << 8)) - 1u;
            return;
        }
        ++de_;
        return;
    }
    case 0xFD: // S2 480B, TT 5474: raw periods on/off
        if (a == 0x01) {
            ch().flags |= 0x08;
        } else {
            ch().flags &= 0xF7;
        }
        return;
    case 0xEF: // Sonic 2: its table entries point into data (unused)
    case 0xF1:
        if (profile_.unused_commands_throw) {
            throw std::runtime_error("Sound command " + std::to_string(code) +
                                     " not reconstructed");
        }
        return;
    default: // E3, EA-EC, EE, FE, FF: nothing
        return;
    }
}

// S2 475C, TT 53B8: the stereo register keeps (register | old) & ~new,
// shifted to the channel's bit.
void SoundDriver::set_stereo(u8 b, u8 cmask, u8 channel) {
    auto rlc = [](u8 v) {
        return u8((v << 1) | (v >> 7));
    };
    if (channel != 0x80) {
        b = rlc(b);
        cmask = rlc(cmask);
        if (channel != 0xA0) {
            b = rlc(b);
            cmask = rlc(cmask);
            if (channel != 0xC0) {
                b = rlc(b);
                cmask = rlc(cmask);
            }
        }
    }
    u8& reg = state.stereo;
    reg = u8((reg | b) & cmask);
    out_stereo(reg);
}

// S2 4819, TT 5482: a channel ends. An effect channel hands its chip channel
// back (silencing it and restoring the noise mode and the music channel's
// stereo bit); the priority clears (Sonic 2: when no effect plays any more).
void SoundDriver::stop_channel() {
    const u8 channel = ch().id;
    unsigned music;
    if (channel == 0xA0) {
        music = tone2;
    } else if (channel == 0xC0) {
        music = (state.channels[effect_noise].flags & 0x80) ? effect_noise : tone3;
    } else {
        if (!(ch().flags & 0x40) && !(state.channels[effect_tone3].flags & 0x80)) {
            u8& tone3_flags = state.channels[tone3].flags;
            tone3_flags = u8((tone3_flags & ~0x44) | 0x10);
            state.channels[effect_tone3].flags = 0;
        }
        out(state.noise_mode);
        music = noise;
    }
    auto& handed_back = state.channels[music];
    handed_back.flags = u8((handed_back.flags & ~0x04) | 0x10);
    out(u8(channel | 0x1F));
    ch().flags = 0;
    // The record the stereo routine takes the old bits from (IX).
    unsigned record = channel_;
    if (profile_.priority_waits_for_effects) {
        // S2 4867: IX steps through the effect records and stays on the
        // first one still playing (or the last).
        record = effect_tone2;
        while (record < effect_noise && !(state.channels[record].flags & 0x80)) {
            ++record;
        }
        if (!(state.channels[record].flags & 0x80)) {
            state.priority = 0;
        }
    } else {
        state.priority = 0;
    }
    // The two POPs end the channel routine (and, for the music noise channel,
    // the whole driver tick: its return address is the driver's).
    stop_ = noise_running_ ? Stop::tick : Stop::channel;
    iy_ = {Pointer::To::channel, music};
    set_stereo(state.channels[record].stereo, u8(~handed_back.stereo), handed_back.id);
}

} // namespace gg_engine
