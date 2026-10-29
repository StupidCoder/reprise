#include "sonic_advance_gba/audio/sound_engine.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace sonic_advance_gba::audio {

namespace {

// The engine's tables.
constexpr u32 song_table = 0x086D76AC;     // per song: header, player (u16), (u16)
constexpr u32 lengths = 0x086D5764;        // note and wait lengths by command
constexpr u32 key_notes = 0x086D5580;      // per key: shift (high nibble) and note (low)
constexpr u32 note_rates = 0x086D5634;     // 12 rates of the highest octave
constexpr u32 psg_key_notes = 0x086D567C;  // tone generator keys from 24h
constexpr u32 psg_note_rates = 0x086D5700; // 12 register values
constexpr u32 noise_keys = 0x086D5718;     // noise settings from key 15h
constexpr u32 wave_volumes = 0x086D5754;   // the wave generator's volume codes by level
constexpr u32 frame_samples = 0x086D5664;  // samples per frame by rate index

// Registers (offsets from 04000000).
constexpr u32 nr10 = 0x60, nr12 = 0x63, nr14 = 0x65, nr22 = 0x69, nr24 = 0x6D;
constexpr u32 nr30 = 0x70, nr42 = 0x79, nr44 = 0x7D, sound_l = 0x80, sound_h = 0x82, sound_x = 0x84,
              bias = 0x89;

// The tone generators' registers: control 0 (sweep or wave enable), length,
// envelope, frequency, trigger.
struct PsgRegisters {
    u32 zero, length, envelope, frequency, control;
};

constexpr PsgRegisters psg_registers[4] = {
    {0x60, 0x62, 0x63, 0x64, 0x65},
    {0x61, 0x68, 0x69, 0x6C, 0x6D},
    {0x70, 0x72, 0x73, 0x74, 0x75},
    {0x71, 0x78, 0x79, 0x7C, 0x7D},
};

u32 high_product(u32 a, u32 b) {
    return u32((std::uint64_t(a) * b) >> 32);
}

} // namespace

void Track::clear() {
    const u32 keep_command = command;
    const auto keep_patterns = patterns;
    const int keep_order = order;
    *this = Track{};
    command = keep_command;
    patterns = keep_patterns;
    order = keep_order;
}

SoundEngine::SoundEngine(const Assets& assets, gba::Apu& apu) : assets_(assets), apu_(apu) {
    // 08097760 (m4aSoundInit): 08097B60 the mixer, 08097A1C the tone
    // generators, the sound mode, the four players.
    reg(sound_x, 0x8F);
    reg(sound_h, 0x0E);
    reg(sound_h + 1, 0xA9);
    reg(bias, u8((apu_.read8(bias) & 0x3F) | 0x40));
    max_channels = 8;
    master_volume = 15;
    set_sample_rate(4);

    reg(sound_x, 0x8F);
    reg(sound_l, 0);
    reg(sound_l + 1, 0);
    reg(nr12, 8);
    reg(nr22, 8);
    reg(nr42, 8);
    reg(nr14, 0x80);
    reg(nr24, 0x80);
    reg(nr44, 0x80);
    reg(nr30, 0);
    reg(sound_l, 0x77);
    for (u8 n = 0; n < 4; ++n) {
        psg_[n] = PsgChannel{};
        psg_[n].type = u8(n + 1);
        psg_[n].pan_mask = u8(0x11 << n);
    }

    sound_mode(0x93F500);

    for (std::size_t n = 0; n < players_.size(); ++n) {
        MusicPlayer& p = players_[n];
        p.track_count = 16;
        p.status = 0x80000000;
        for (std::size_t t = 0; t < p.tracks.size(); ++t) {
            p.tracks[t].order = int(n * 16 + t);
        }
    }
}

void SoundEngine::sound_mode(u32 mode) {
    if (mode & 0xFF) {
        reverb_ = u8(mode & 0x7F);
    }
    if (mode & 0xF00) {
        max_channels = u8((mode & 0xF00) >> 8);
        for (auto& c : pcm_) {
            c.status = 0;
        }
    }
    if (mode & 0xF000) {
        master_volume = u8((mode & 0xF000) >> 12);
    }
    if (mode & 0xB00000) {
        reg(bias, u8((apu_.read8(bias) & 0x3F) | ((mode & 0x300000) >> 14)));
    }
    if (mode & 0xF0000) {
        // 08097DE8: the ring buffer is cleared, then the new rate.
        buffer_.fill(0);
        set_sample_rate((mode & 0xF0000) >> 16);
    }
}

void SoundEngine::set_sample_rate(u32 index) {
    samples_per_frame_ = assets_.u16(frame_samples + (index - 1) * 2);
    dma_period_ = u8(0x630 / samples_per_frame_);
    sample_rate_ = (samples_per_frame_ * 0x91D1B + 5000) / 10000;
    step_divisor_ = (0x1000000 / sample_rate_ + 1) >> 1;
    dma_counter_ = 0; // 08097E64
}

// ---- Song services ----------------------------------------------------

void SoundEngine::start_song(u16 number) {
    const u32 e = song_table + 8u * number;
    start(u8(assets_.u16(e + 4)), read32(e));
}

void SoundEngine::start_or_continue(u16 number) {
    const u32 e = song_table + 8u * number;
    const u8 n = u8(assets_.u16(e + 4));
    const u32 header = read32(e);
    MusicPlayer& p = players_.at(n);
    if (p.song != header) {
        start(n, header);
    } else if ((p.status & 0xFFFF) == 0) {
        start(n, p.song);
    } else if (p.status & 0x80000000) {
        resume(n);
    }
}

void SoundEngine::start_unless_playing(u16 number) {
    const u32 e = song_table + 8u * number;
    const u8 n = u8(assets_.u16(e + 4));
    const u32 header = read32(e);
    MusicPlayer& p = players_.at(n);
    if (p.song != header || (p.status & 0xFFFF) == 0 || (p.status & 0x80000000)) {
        start(n, header);
    }
}

void SoundEngine::stop_song(u16 number) {
    const u32 e = song_table + 8u * number;
    const u8 n = u8(assets_.u16(e + 4));
    if (players_.at(n).song == read32(e)) {
        stop(n);
    }
}

void SoundEngine::stop_all() {
    for (u8 n = 0; n < 4; ++n) {
        stop(n);
    }
}

void SoundEngine::start(u8 n, u32 header) {
    MusicPlayer& p = players_.at(n);
    const u8 priority = read(header + 2);
    if (p.priority_check) {
        const bool busy = (p.song != 0 && (p.tracks[0].flags & track_flag::start)) ||
                          ((p.status & 0xFFFF) != 0 && !(p.status & 0x80000000));
        if (busy && priority < p.priority) {
            return;
        }
    }
    p.status = 0;
    p.song = header;
    p.voicegroup = read32(header + 4);
    p.priority = priority;
    p.clock = 0;
    p.tempo_d = 150;
    p.tempo_i = 150;
    p.tempo_u = 0x100;
    p.tempo_c = 0;
    p.fade_interval = 0;
    const u8 count = read(header);
    std::size_t i = 0;
    if (count != 0 && p.track_count != 0) {
        for (; i < count && i < p.track_count; ++i) {
            Track& t = p.tracks[i];
            track_stop(t);
            t.flags = track_flag::active | track_flag::start;
            t.channels = nullptr;
            t.command = read32(header + 8 + 4 * u32(i));
        }
    }
    for (; i < p.track_count; ++i) {
        track_stop(p.tracks[i]);
        p.tracks[i].flags = 0;
    }
    if (read(header + 3) & 0x80) {
        sound_mode(read(header + 3));
    }
}

void SoundEngine::stop(u8 n) {
    MusicPlayer& p = players_.at(n);
    p.status |= 0x80000000;
    for (std::size_t i = 0; i < p.track_count; ++i) {
        track_stop(p.tracks[i]);
    }
}

void SoundEngine::resume(u8 n) {
    players_.at(n).status &= 0x7FFFFFFF;
}

void SoundEngine::fade_out(u8 n, u16 speed) {
    MusicPlayer& p = players_.at(n);
    p.fade_counter = speed;
    p.fade_interval = speed;
    p.fade_volume = 0x100;
}

void SoundEngine::fade_in(u8 n, u16 speed) {
    MusicPlayer& p = players_.at(n);
    p.fade_counter = speed;
    p.fade_interval = speed;
    p.fade_volume = 2;
    p.status &= 0x7FFFFFFF;
}

void SoundEngine::set_tempo(u8 n, u16 tempo) {
    MusicPlayer& p = players_.at(n);
    p.tempo_u = tempo;
    p.tempo_i = u16((p.tempo_d * tempo) >> 8);
}

void SoundEngine::set_volume(u8 n, u16 tracks, u16 volume) {
    MusicPlayer& p = players_.at(n);
    u16 bit = 1;
    for (std::size_t i = 0; i < p.track_count; ++i, bit = u16(bit << 1)) {
        Track& t = p.tracks[i];
        if ((tracks & bit) && (t.flags & track_flag::active)) {
            t.volume_x = u8(volume >> 2);
            t.flags |= track_flag::volume;
        }
    }
}

void SoundEngine::set_pitch(u8 n, u16 tracks, s16 pitch) {
    MusicPlayer& p = players_.at(n);
    u16 bit = 1;
    for (std::size_t i = 0; i < p.track_count; ++i, bit = u16(bit << 1)) {
        Track& t = p.tracks[i];
        if ((tracks & bit) && (t.flags & track_flag::active)) {
            t.key_shift_x = s8(u16(pitch) >> 8);
            t.pitch_x = u8(pitch);
            t.flags |= track_flag::pitch;
        }
    }
}

void SoundEngine::set_pan(u8 n, u16 tracks, s8 pan) {
    MusicPlayer& p = players_.at(n);
    u16 bit = 1;
    for (std::size_t i = 0; i < p.track_count; ++i, bit = u16(bit << 1)) {
        Track& t = p.tracks[i];
        if ((tracks & bit) && (t.flags & track_flag::active)) {
            t.pan_x = pan;
            t.flags |= track_flag::volume;
        }
    }
}

void SoundEngine::reset_tracks(u8 n) {
    MusicPlayer& p = players_.at(n);
    for (std::size_t i = 0; i < p.track_count; ++i) {
        Track& t = p.tracks[i];
        if ((t.flags & track_flag::active) && (t.flags & track_flag::start)) {
            track_reset(t);
        }
    }
}

// ---- The frame ---------------------------------------------------------

void SoundEngine::main() {
    // The players were opened 0 to 3; each first runs the one opened
    // before it, so they play in that order.
    for (auto& p : players_) {
        play_player(p);
    }
    psg_update();
    mix();
}

void SoundEngine::vertical_blank() {
    // When the counter runs out, the hardware's sample transfer starts over
    // at the buffer's beginning.
    const int counter = dma_counter_ - 1;
    dma_counter_ = counter > 0 ? u8(counter) : dma_period_;
}

std::span<const s8> SoundEngine::mixed(int side) const {
    return {buffer_.data() + (side ? 0x630 : 0) + mixed_at_, samples_per_frame_};
}

// ---- Music players ----------------------------------------------------

void SoundEngine::play_player(MusicPlayer& p) {
    if (p.status & 0x80000000) {
        return;
    }
    fade_step(p);
    if (p.status & 0x80000000) {
        return;
    }
    p.tempo_c = u16(p.tempo_c + p.tempo_i);
    while (p.tempo_c >= 150) {
        u32 bit = 1, playing = 0;
        for (std::size_t i = 0; i < p.track_count; ++i, bit <<= 1) {
            Track& t = p.tracks[i];
            if (!(t.flags & track_flag::active)) {
                continue;
            }
            playing |= bit;
            for (Channel* c = t.channels; c; c = c->next) {
                if (c->status & channel_status::active) {
                    if (c->gate_time != 0 && --c->gate_time == 0) {
                        c->status |= channel_status::release;
                    }
                } else {
                    unlink(*c);
                }
            }
            if (t.flags & track_flag::start) {
                track_reset(t);
            }
            run_commands(p, t);
        }
        ++p.clock;
        if (playing == 0) {
            p.status = 0x80000000;
            return;
        }
        p.status = playing;
        p.tempo_c = u16(p.tempo_c - 150);
    }
    // Volume and pitch changes reach the notes.
    for (std::size_t i = 0; i < p.track_count; ++i) {
        Track& t = p.tracks[i];
        if (!(t.flags & track_flag::active) || !(t.flags & 0xF)) {
            continue;
        }
        volume_pitch(t);
        for (Channel* c = t.channels; c; c = c->next) {
            if (!(c->status & channel_status::active)) {
                unlink(*c);
                continue;
            }
            const u8 gen = c->type & 7;
            if (t.flags & track_flag::volume) {
                channel_volume(*c, t);
                if (gen) {
                    static_cast<PsgChannel*>(c)->modified |= 1;
                }
            }
            if (t.flags & track_flag::pitch) {
                s32 key = c->key + t.key_m;
                if (key < 0) {
                    key = 0;
                }
                if (gen) {
                    auto* pc = static_cast<PsgChannel*>(c);
                    pc->frequency = key_to_psg(gen, u8(key), t.pitch_m);
                    pc->modified |= 2;
                } else {
                    static_cast<PcmChannel*>(c)->frequency =
                        key_to_frequency(c->wave, u8(key), t.pitch_m);
                }
            }
        }
        t.flags &= 0xF0;
    }
}

void SoundEngine::run_commands(MusicPlayer& p, Track& t) {
    while (t.wait == 0) {
        u8 cmd = read(t.command);
        if (cmd < 0x80) {
            cmd = t.running_status; // arguments repeat the last command
        } else {
            ++t.command;
            if (cmd >= 0xBD) {
                t.running_status = cmd;
            }
        }
        if (cmd >= 0xCF) {
            note(p, t, u8(cmd - 0xCF));
        } else if (cmd > 0xB0) {
            p.command = u8(cmd - 0xB1);
            command(p, t, cmd);
            if (t.flags == 0) {
                return; // the track ended
            }
        } else {
            t.wait = read(lengths + cmd - 0x80);
        }
    }
    --t.wait;
    // The LFO.
    if (t.lfo_speed == 0 || t.mod == 0) {
        return;
    }
    if (t.lfo_delay_counter != 0) {
        --t.lfo_delay_counter;
        return;
    }
    // The phase is kept as a byte, but the falling half of the triangle is
    // taken from the sum before it wraps (so a wrap gives a large swing).
    const s32 sum = t.lfo_phase + t.lfo_speed;
    t.lfo_phase = u8(sum);
    const s32 wave = s8(t.lfo_phase - 0x40) < 0 ? s32(s8(t.lfo_phase)) : 0x80 - sum;
    const s32 value = (t.mod * wave) >> 6;
    if (u8(t.mod_m ^ value) != 0) {
        t.mod_m = s8(value);
        t.flags |= t.mod_type == 0 ? track_flag::pitch : track_flag::volume;
    }
}

void SoundEngine::command(MusicPlayer& p, Track& t, u8 cmd) {
    switch (cmd) {
    case 0xB2: // GOTO
        t.command = read32(t.command);
        return;
    case 0xB3: // PATTERN
        if (t.pattern_level < 3) {
            t.patterns[t.pattern_level++] = t.command + 4;
            t.command = read32(t.command);
        } else {
            track_end(t);
        }
        return;
    case 0xB4: // PATTERN END
        if (t.pattern_level != 0) {
            t.command = t.patterns[--t.pattern_level];
        }
        return;
    case 0xB5: { // REPEAT count, address
        const u32 at = t.command;
        const u8 count = read(at);
        if (count == 0) {
            t.command = read32(at + 1);
            return;
        }
        ++t.repeat_count;
        if (t.repeat_count >= count) {
            t.repeat_count = 0;
            t.command = at + 5;
        } else {
            t.command = read32(at + 1);
        }
        return;
    }
    case 0xB9:
        memory_access(p, t);
        return;
    case 0xBA:
        t.priority = next_byte(t);
        return;
    case 0xBB: // TEMPO
        p.tempo_d = u16(next_byte(t) * 2);
        p.tempo_i = u16((p.tempo_d * p.tempo_u) >> 8);
        return;
    case 0xBC:
        t.key_shift = s8(next_byte(t));
        t.flags |= track_flag::pitch;
        return;
    case 0xBD: { // VOICE
        const u32 v = p.voicegroup + next_byte(t) * 12u;
        t.voice = {read(v), read(v + 1), read(v + 2), read(v + 3), read32(v + 4), read32(v + 8)};
        return;
    }
    case 0xBE:
        t.volume = next_byte(t);
        t.flags |= track_flag::volume;
        return;
    case 0xBF:
        t.pan = s8(next_byte(t) - 0x40);
        t.flags |= track_flag::volume;
        return;
    case 0xC0:
        t.bend = s8(next_byte(t) - 0x40);
        t.flags |= track_flag::pitch;
        return;
    case 0xC1:
        t.bend_range = next_byte(t);
        t.flags |= track_flag::pitch;
        return;
    case 0xC2: // LFO speed
        t.lfo_speed = next_byte(t);
        if (t.lfo_speed == 0) {
            lfo_reset(t);
        }
        return;
    case 0xC3:
        t.lfo_delay = next_byte(t);
        return;
    case 0xC4: // modulation depth
        t.mod = next_byte(t);
        if (t.mod == 0) {
            lfo_reset(t);
        }
        return;
    case 0xC5: {
        const u8 type = next_byte(t);
        if (t.mod_type != type) {
            t.mod_type = type;
            t.flags |= 0xF;
        }
        return;
    }
    case 0xC8:
        t.tune = s8(next_byte(t) - 0x40);
        t.flags |= track_flag::pitch;
        return;
    case 0xCC: { // a sound register
        const u8 offset = next_byte(t);
        reg(0x60u + offset, next_byte(t));
        return;
    }
    case 0xCD:
        extended(t);
        return;
    case 0xCE:
        note_off(t);
        return;
    default:
        // FINE (B1) and the commands this engine leaves out (B6-B8, C6, C7,
        // C9-CB) end the track.
        track_end(t);
        return;
    }
}

void SoundEngine::memory_access(MusicPlayer&, Track& t) {
    const u8 op = read(t.command);
    u8& m = shared_memory_[read(t.command + 1)];
    const u8 data = read(t.command + 2);
    t.command += 3;
    const auto branch = [&](bool taken) {
        if (taken) {
            t.command = read32(t.command);
        } else {
            t.command += 4;
        }
    };
    const u8 other = shared_memory_[data];
    switch (op) {
    case 0:
        m = data;
        break;
    case 1:
        m = u8(m + data);
        break;
    case 2:
        m = u8(m - data);
        break;
    case 3:
        m = other;
        break;
    case 4:
        m = u8(m + other);
        break;
    case 5:
        m = u8(m - other);
        break;
    case 6:
        branch(m == data);
        break;
    case 7:
        branch(m != data);
        break;
    case 8:
        branch(m > data);
        break;
    case 9:
        branch(m >= data);
        break;
    case 10:
        branch(m <= data);
        break;
    case 11:
        branch(m < data);
        break;
    case 12:
        branch(m == other);
        break;
    case 13:
        branch(m != other);
        break;
    case 14:
        branch(m > other);
        break;
    case 15:
        branch(m >= other);
        break;
    case 16:
        branch(m <= other);
        break;
    case 17:
        branch(m < other);
        break;
    default:
        break;
    }
}

void SoundEngine::extended(Track& t) {
    const u8 n = next_byte(t);
    const auto set_byte = [](u32& word, unsigned i, u8 v) {
        word = (word & ~(0xFFu << (8 * i))) | (u32(v) << (8 * i));
    };
    switch (n) {
    case 0:
    case 3:
        track_end(t);
        return;
    case 1:
        t.voice.data = read32(t.command);
        t.command += 4;
        return;
    case 2:
        t.voice.type = next_byte(t);
        return;
    case 4:
        set_byte(t.voice.data2, 0, next_byte(t));
        return;
    case 5:
        set_byte(t.voice.data2, 1, next_byte(t));
        return;
    case 6:
        set_byte(t.voice.data2, 2, next_byte(t));
        return;
    case 7:
        set_byte(t.voice.data2, 3, next_byte(t));
        return;
    case 8:
        t.echo_volume = next_byte(t);
        return;
    case 9:
        t.echo_length = next_byte(t);
        return;
    case 10:
        t.voice.length = next_byte(t);
        return;
    case 11:
        t.voice.pan_sweep = next_byte(t);
        return;
    default:
        throw std::runtime_error("sound command CD " + std::to_string(n) +
                                 " (no handler in the original's table)");
    }
}

void SoundEngine::fade_step(MusicPlayer& p) {
    if (p.fade_interval == 0 || --p.fade_counter != 0) {
        return;
    }
    p.fade_counter = p.fade_interval;
    if (!(p.fade_volume & 2)) {
        p.fade_volume = u16(p.fade_volume - 0x10);
        if (s16(p.fade_volume) <= 0) {
            for (std::size_t i = 0; i < p.track_count; ++i) {
                track_stop(p.tracks[i]);
                if (!(p.fade_volume & 1)) {
                    p.tracks[i].flags = 0;
                }
            }
            p.status = (p.fade_volume & 1) ? p.status | 0x80000000 : 0x80000000;
            p.fade_interval = 0;
            return;
        }
    } else {
        p.fade_volume = u16(p.fade_volume + 0x10);
        if (p.fade_volume > 0xFF) {
            p.fade_volume = 0x100;
            p.fade_interval = 0;
        }
    }
    for (std::size_t i = 0; i < p.track_count; ++i) {
        Track& t = p.tracks[i];
        if (t.flags & track_flag::active) {
            t.volume_x = u8(p.fade_volume >> 2);
            t.flags |= track_flag::volume;
        }
    }
}

void SoundEngine::track_reset(Track& t) {
    t.clear();
    t.flags = track_flag::active;
    t.bend_range = 2;
    t.volume_x = 0x40;
    t.lfo_speed = 0x16;
    t.voice.type = 1;
}

void SoundEngine::track_stop(Track& t) {
    if (!(t.flags & track_flag::active)) {
        return;
    }
    for (Channel* c = t.channels; c; c = c->next) {
        if (c->status != 0) {
            if (c->type & 7) {
                psg_off(c->type & 7);
            }
            c->status = 0;
        }
        c->track = nullptr;
    }
    t.channels = nullptr;
}

void SoundEngine::track_end(Track& t) {
    for (Channel* c = t.channels; c; c = c->next) {
        if (c->status & channel_status::active) {
            c->status |= channel_status::release;
        }
        unlink(*c);
    }
    t.flags = 0;
}

void SoundEngine::lfo_reset(Track& t) {
    t.mod_m = 0;
    t.lfo_phase = 0;
    t.flags |= t.mod_type == 0 ? track_flag::pitch : track_flag::volume;
}

void SoundEngine::unlink(Channel& c) {
    if (!c.track) {
        return;
    }
    Channel* next = c.next;
    Channel* previous = c.previous;
    if (!previous) {
        c.track->channels = next;
    } else {
        previous->next = next;
    }
    if (next) {
        next->previous = previous;
    }
    c.track = nullptr;
}

void SoundEngine::volume_pitch(Track& t) {
    if (t.flags & 1) {
        u32 v = (t.volume_x * t.volume) >> 5;
        if (t.mod_type == 1) {
            v = (v * u32(t.mod_m + 0x80)) >> 7;
        }
        s32 pan = t.pan * 2 + t.pan_x;
        if (t.mod_type == 2) {
            pan += t.mod_m;
        }
        pan = std::clamp(pan, -0x80, 0x7F);
        t.volume_right = u8(((pan + 0x80) * v) >> 8);
        t.volume_left = u8(((0x7F - pan) * v) >> 8);
    }
    if (t.flags & 4) {
        s32 pitch = (t.bend * t.bend_range + t.tune) * 4 + t.key_shift * 256 + t.key_shift_x * 256 +
                    t.pitch_x;
        if (t.mod_type == 0) {
            pitch += t.mod_m * 16;
        }
        t.key_m = s8(pitch >> 8);
        t.pitch_m = u8(pitch);
    }
    t.flags &= 0xFA;
}

void SoundEngine::channel_volume(Channel& c, const Track& t) {
    const u32 right = (c.velocity * u32(c.rhythm_pan + 0x80) * t.volume_right) >> 14;
    const u32 left = (c.velocity * u32(0x7F - c.rhythm_pan) * t.volume_left) >> 14;
    c.right_volume = u8(std::min(right, 0xFFu));
    c.left_volume = u8(std::min(left, 0xFFu));
}

u32 SoundEngine::key_to_frequency(u32 wave, u8 key, u8 fine) const {
    u32 k = key, f = u32(fine) << 24;
    if (k > 0xB2) {
        k = 0xB2;
        f = 0xFF000000;
    }
    const u8 a = read(key_notes + k), b = read(key_notes + k + 1);
    const u32 low = read32(note_rates + (a & 0xF) * 4u) >> (a >> 4);
    const u32 high = read32(note_rates + (b & 0xF) * 4u) >> (b >> 4);
    return high_product(read32(wave + 4), low + high_product(high - low, f));
}

u32 SoundEngine::key_to_psg(u8 type, u8 key, u8 fine) const {
    if (type == 4) {
        u32 k = key <= 0x14 ? 0 : std::min<u32>(key - 0x15u, 0x3B);
        return read(noise_keys + k);
    }
    u32 k, f = fine;
    if (key <= 0x23) {
        k = 0;
        f = 0;
    } else {
        k = key - 0x24u;
        if (k > 0x82) {
            k = 0x82;
            f = 0xFF;
        }
    }
    const u8 a = read(psg_key_notes + k), b = read(psg_key_notes + k + 1);
    const s32 low = assets_.s16(psg_note_rates + (a & 0xF) * 2u) >> (a >> 4);
    const s32 high = assets_.s16(psg_note_rates + (b & 0xF) * 2u) >> (b >> 4);
    return u32(low + (((high - low) * s32(f)) >> 8) + 0x800);
}

// ---- Notes --------------------------------------------------------------

void SoundEngine::note(MusicPlayer& p, Track& t, u8 length) {
    t.gate_time = read(lengths + length);
    if (read(t.command) < 0x80) {
        t.key = read(t.command++);
        if (read(t.command) < 0x80) {
            t.velocity = read(t.command++);
            if (read(t.command) < 0x80) {
                t.gate_time = u8(t.gate_time + read(t.command++));
            }
        }
    }
    s8 rhythm_pan = 0;
    Voice v;
    u8 key;
    if (t.voice.type & 0xC0) {
        // A drum kit or a key split: the voice by key from its sub-group.
        const u8 index = (t.voice.type & 0x40) ? read(t.voice.data2 + t.key) : t.key;
        const u32 at = t.voice.data + index * 12u;
        v = {read(at), read(at + 1), read(at + 2), read(at + 3), read32(at + 4), read32(at + 8)};
        if (v.type & 0xC0) {
            return;
        }
        key = t.key;
        if (t.voice.type & 0x80) {
            if (v.pan_sweep & 0x80) {
                rhythm_pan = s8((v.pan_sweep - 0xC0) * 2);
            }
            key = v.key;
        }
    } else {
        v = t.voice;
        key = t.key;
    }
    const u32 priority = std::min<u32>(t.priority + p.priority, 0xFF);
    const u8 gen = v.type & 7;
    const auto order = [](const Channel& c) {
        return c.track ? c.track->order : -1;
    };
    Channel* c = nullptr;
    if (gen) {
        PsgChannel& pc = psg_[gen - 1];
        const bool busy =
            (pc.status & channel_status::active) && !(pc.status & channel_status::release);
        if (busy && (pc.priority > priority || (pc.priority == priority && order(pc) < t.order))) {
            return;
        }
        c = &pc;
    } else {
        // A free channel, else a releasing one of the lowest priority, else
        // the lowest priority (ties: the later track).
        u32 lowest = priority;
        int latest = t.order;
        bool releasing = false;
        for (std::size_t i = 0; i < max_channels; ++i) {
            PcmChannel& ch = pcm_[i];
            if (!(ch.status & channel_status::active)) {
                c = &ch;
                break;
            }
            if (ch.status & channel_status::release) {
                if (!releasing) {
                    releasing = true;
                    lowest = ch.priority;
                    latest = order(ch);
                    c = &ch;
                    continue;
                }
            } else if (releasing) {
                continue;
            }
            if (ch.priority < lowest) {
                lowest = ch.priority;
                latest = order(ch);
                c = &ch;
            } else if (ch.priority == lowest && order(ch) >= latest) {
                latest = order(ch);
                c = &ch;
            }
        }
        if (!c) {
            return;
        }
    }
    unlink(*c);
    c->previous = nullptr;
    c->next = t.channels;
    if (t.channels) {
        t.channels->previous = c;
    }
    t.channels = c;
    c->track = &t;
    t.lfo_delay_counter = t.lfo_delay;
    if (t.lfo_delay != 0) {
        lfo_reset(t);
    }
    volume_pitch(t);
    c->gate_time = t.gate_time;
    c->midi_key = t.key;
    c->velocity = t.velocity;
    c->priority = u8(priority);
    c->key = key;
    c->rhythm_pan = rhythm_pan;
    c->type = v.type;
    c->wave = v.data;
    c->attack = u8(v.data2);
    c->decay = u8(v.data2 >> 8);
    c->sustain = u8(v.data2 >> 16);
    c->release = u8(v.data2 >> 24);
    c->echo_volume = t.echo_volume;
    c->echo_length = t.echo_length;
    channel_volume(*c, t);
    s32 k = c->key + t.key_m;
    if (k < 0) {
        k = 0;
    }
    if (gen) {
        auto* pc = static_cast<PsgChannel*>(c);
        pc->length = v.length;
        pc->sweep = ((v.pan_sweep & 0x80) || !(v.pan_sweep & 0x70)) ? 8 : v.pan_sweep;
        pc->frequency = key_to_psg(gen, u8(k), t.pitch_m);
    } else {
        static_cast<PcmChannel*>(c)->frequency = key_to_frequency(v.data, u8(k), t.pitch_m);
    }
    c->status = channel_status::start;
    t.flags &= 0xF0;
}

void SoundEngine::note_off(Track& t) {
    u8 key = read(t.command);
    if (key < 0x80) {
        t.key = key;
        ++t.command;
    } else {
        key = t.key;
    }
    for (Channel* c = t.channels; c; c = c->next) {
        if ((c->status & 0x83) && !(c->status & channel_status::release) && c->midi_key == key) {
            c->status |= channel_status::release;
            return;
        }
    }
}

// ---- Tone generators -------------------------------------------------------

void SoundEngine::psg_off(u8 n) {
    switch (n) {
    case 1:
        reg(nr12, 8);
        reg(nr14, 0x80);
        break;
    case 2:
        reg(nr22, 8);
        reg(nr24, 0x80);
        break;
    case 3:
        reg(nr30, 0);
        break;
    default:
        reg(nr42, 8);
        reg(nr44, 0x80);
        break;
    }
}

void SoundEngine::psg_volume(PsgChannel& c) {
    const u32 r = c.right_volume, l = c.left_volume;
    const u32 sum = (l + r) >> 4;
    bool clamp = true;
    if (r >= l) {
        if ((r >> 1) >= l) {
            c.pan = 0x0F; // right only
        } else {
            c.pan = 0xFF;
            clamp = false;
        }
    } else if ((l >> 1) >= r) {
        c.pan = 0xF0; // left only
    } else {
        c.pan = 0xFF;
        clamp = false;
    }
    c.envelope_goal = u8(clamp ? std::min(sum, 15u) : sum);
    c.sustain_goal = u8((c.envelope_goal * c.sustain + 0xF) >> 4);
    c.pan &= c.pan_mask;
}

void SoundEngine::psg_update() {
    psg_counter_ = psg_counter_ == 0 ? 0xE : u8(psg_counter_ - 1);
    for (u8 n = 1; n <= 4; ++n) {
        PsgChannel& c = psg_[n - 1];
        if (!(c.status & channel_status::active)) {
            continue;
        }
        const PsgRegisters& r = psg_registers[n - 1];
        s32 frame = psg_counter_;
        u8 env_reg = apu_.read8(r.envelope);
        enum class Step { tick, step, apply, attack_done, sustain_check, echo_or_stop, stop } next;
        u8 counter = 0;
        bool set_counter = false;
        if (c.status & channel_status::start) {
            if (c.status & channel_status::release) {
                next = Step::stop;
            } else {
                c.status = 3;
                c.modified = 3;
                psg_volume(c);
                switch (n) {
                case 3:
                    if (c.wave != c.current_wave) {
                        reg(r.zero, 0x40);
                        for (u32 i = 0; i < 16; ++i) {
                            reg(0x90 + i, read(c.wave + i));
                        }
                        c.current_wave = c.wave;
                    }
                    reg(r.zero, 0);
                    reg(r.length, c.length);
                    c.control = c.length ? 0xC0 : 0x80;
                    break;
                case 4:
                    reg(r.length, c.length);
                    reg(r.frequency, u8(c.wave << 3));
                    env_reg = u8(c.attack + 8);
                    c.control = c.length ? 0x40 : 0;
                    break;
                default:
                    if (n == 1) {
                        reg(r.zero, c.sweep);
                    }
                    reg(r.length, u8((c.wave << 6) + c.length));
                    env_reg = u8(c.attack + 8);
                    c.control = c.length ? 0x40 : 0;
                    break;
                }
                c.envelope_counter = c.attack;
                if (c.attack != 0) {
                    c.envelope = 0;
                    next = Step::tick;
                } else {
                    next = Step::attack_done;
                }
            }
        } else if (c.status & channel_status::echo) {
            --c.echo_length;
            next = s8(c.echo_length) > 0 ? Step::apply : Step::stop;
        } else if ((c.status & channel_status::release) && (c.status & channel_status::phase)) {
            c.status &= 0xFC;
            c.envelope_counter = c.release;
            if (c.release == 0) {
                next = Step::echo_or_stop;
            } else {
                c.modified |= 1;
                if (n != 3) {
                    env_reg = c.release;
                }
                next = Step::tick;
            }
        } else {
            next = Step::step;
        }
        for (;;) {
            if (set_counter) {
                c.envelope_counter = counter;
                set_counter = false;
                next = Step::tick;
            }
            if (next == Step::step) {
                if (c.envelope_counter != 0) {
                    next = Step::tick;
                    continue;
                }
                if (n == 3) {
                    c.modified |= 1;
                }
                psg_volume(c);
                switch (c.status & channel_status::phase) {
                case 0: // release
                    --c.envelope;
                    if (s8(c.envelope) > 0) {
                        counter = c.release;
                        set_counter = true;
                    } else {
                        next = Step::echo_or_stop;
                    }
                    break;
                case 1: // sustain
                    c.envelope = c.sustain_goal;
                    counter = 7;
                    set_counter = true;
                    break;
                case 2: // decay
                    --c.envelope;
                    if (s8(c.envelope) > s8(c.sustain_goal)) {
                        counter = c.decay;
                        set_counter = true;
                    } else {
                        next = Step::sustain_check;
                    }
                    break;
                default: // attack
                    ++c.envelope;
                    if (c.envelope < c.envelope_goal) {
                        counter = c.attack;
                        set_counter = true;
                    } else {
                        next = Step::attack_done;
                    }
                    break;
                }
                continue;
            }
            if (next == Step::attack_done) {
                --c.status;
                c.envelope_counter = c.decay;
                if (c.decay == 0) {
                    next = Step::sustain_check;
                    continue;
                }
                c.modified |= 1;
                c.envelope = c.envelope_goal;
                if (n != 3) {
                    env_reg = c.decay;
                }
                next = Step::tick;
                continue;
            }
            if (next == Step::sustain_check) {
                if (c.sustain == 0) {
                    c.status &= 0xFC;
                    next = Step::echo_or_stop;
                    continue;
                }
                --c.status;
                c.modified |= 1;
                if (n != 3) {
                    env_reg = 8;
                }
                c.envelope = c.sustain_goal;
                counter = 7;
                set_counter = true;
                continue;
            }
            if (next == Step::echo_or_stop) {
                c.envelope = u8((c.echo_volume * c.envelope_goal + 0xFF) >> 8);
                if (c.envelope == 0) {
                    next = Step::stop;
                    break;
                }
                c.status |= channel_status::echo;
                c.modified |= 1;
                if (n != 3) {
                    env_reg = 8;
                }
                next = Step::apply;
                break;
            }
            if (next == Step::tick) {
                --c.envelope_counter;
                if (frame == 0) {
                    // On the envelope frame the step runs once more.
                    frame = -1;
                    next = Step::step;
                    continue;
                }
                next = Step::apply;
            }
            break;
        }
        if (next == Step::stop) {
            psg_off(n);
            c.status = 0;
            c.modified = 0;
            continue;
        }
        // Apply what changed.
        if (c.modified & 2) {
            if (n <= 3 && (c.type & 8)) {
                const u8 b = apu_.read8(bias);
                if (b <= 0x3F) {
                    c.frequency = (c.frequency + 2) & 0x7FC;
                } else if (b <= 0x7F) {
                    c.frequency = (c.frequency + 1) & 0x7FE;
                }
            }
            if (n != 4) {
                reg(r.frequency, u8(c.frequency));
            } else {
                reg(r.frequency, u8((apu_.read8(r.frequency) & 8) | u8(c.frequency)));
            }
            c.control = u8((c.control & 0xC0) + u8(c.frequency >> 8));
            reg(r.control, c.control);
        }
        if (c.modified & 1) {
            reg(sound_l + 1, u8((apu_.read8(sound_l + 1) & ~c.pan_mask) | c.pan));
            if (n == 3) {
                reg(r.envelope, read(wave_volumes + c.envelope));
                if (c.control & 0x80) {
                    reg(r.zero, 0x80);
                    reg(r.control, c.control);
                    c.control &= 0x7F;
                }
            } else {
                env_reg &= 0xF;
                reg(r.envelope, u8((c.envelope << 4) + env_reg));
                reg(r.control, u8(c.control | 0x80));
                if (n == 1 && !(apu_.read8(nr10) & 8)) {
                    reg(r.control, u8(c.control | 0x80));
                }
            }
        }
        c.modified = 0;
    }
}

// ---- The mixer --------------------------------------------------------------

void SoundEngine::mix() {
    const u32 spf = samples_per_frame_;
    u32 pos = 0;
    if (dma_counter_ > 1) {
        pos = (dma_period_ - (dma_counter_ - 1u)) * spf;
    }
    mixed_at_ = pos;
    s8* right = buffer_.data() + pos;
    s8* left = buffer_.data() + 0x630 + pos;
    if (reverb_) {
        u32 other = dma_counter_ == 2 ? 0 : pos + spf;
        for (u32 i = 0; i < spf; ++i, ++other) {
            s32 v = left[i] + right[i] + buffer_[0x630 + other] + buffer_[other];
            v = (v * reverb_) >> 9;
            if (v & 0x80) {
                ++v;
            }
            left[i] = right[i] = s8(v);
        }
    } else {
        std::fill(right, right + spf, s8(0));
        std::fill(left, left + spf, s8(0));
    }
    for (std::size_t n = 0; n < max_channels; ++n) {
        PcmChannel& c = pcm_[n];
        if (!(c.status & channel_status::active)) {
            continue;
        }
        u32 env;
        bool envelope_done = false;
        if (c.status & channel_status::start) {
            if (c.status & channel_status::release) {
                c.status = 0;
                continue;
            }
            c.status = 3;
            c.current = c.wave + 0x10;
            c.count = read32(c.wave + 0xC);
            c.envelope = 0;
            c.fraction = 0;
            if (read(c.wave + 3) & 0xC0) {
                c.status |= channel_status::loop;
            }
            env = 0;
        } else {
            env = c.envelope;
            if (c.status & channel_status::echo) {
                if (--c.echo_length == 0) {
                    c.status = 0;
                    continue;
                }
                envelope_done = true;
            } else if (c.status & channel_status::release) {
                env = (env * c.release) >> 8;
                if (env <= c.echo_volume) {
                    env = c.echo_volume;
                    if (env == 0) {
                        c.status = 0;
                        continue;
                    }
                    c.status |= channel_status::echo;
                }
                envelope_done = true;
            } else if ((c.status & channel_status::phase) == 2) {
                env = (env * c.decay) >> 8;
                if (env <= c.sustain) {
                    env = c.sustain;
                    if (env == 0) {
                        env = c.echo_volume;
                        if (env == 0) {
                            c.status = 0;
                            continue;
                        }
                        c.status |= channel_status::echo;
                    } else {
                        --c.status;
                    }
                }
                envelope_done = true;
            } else if ((c.status & channel_status::phase) != 3) {
                envelope_done = true;
            }
        }
        if (!envelope_done) {
            // The attack.
            env += c.attack;
            if (env >= 0xFF) {
                env = 0xFF;
                --c.status;
            }
        }
        c.envelope = u8(env);
        const u32 v = ((master_volume + 1u) * env) >> 4;
        c.envelope_right = u8((c.right_volume * v) >> 8);
        c.envelope_left = u8((c.left_volume * v) >> 8);
        u32 loop_start = 0;
        s32 loop_length = 0;
        if (c.status & channel_status::loop) {
            loop_start = c.wave + 0x10 + read32(c.wave + 8);
            loop_length = s32(read32(c.wave + 0xC) - read32(c.wave + 8));
        }
        const s32 vr = c.envelope_right, vl = c.envelope_left;
        const auto add = [&](u32 i, s32 sample) {
            right[i] = s8(u8(right[i] + u8((vr * sample) >> 8)));
            left[i] = s8(u8(left[i] + u8((vl * sample) >> 8)));
        };
        s32 count = s32(c.count);
        u32 at = c.current;
        bool ended = false;
        if (c.type & 8) {
            // Fixed rate: one sample per output sample.
            for (u32 i = 0; i < spf; ++i) {
                add(i, s8(read(at++)));
                if (--count == 0) {
                    if (loop_length == 0) {
                        ended = true;
                        break;
                    }
                    count = loop_length;
                    at = loop_start;
                }
            }
        } else {
            // Resampled with linear interpolation; the position's fraction
            // in 23 bits.
            u32 fw = c.fraction;
            const u32 step = step_divisor_ * c.frequency;
            s32 s0 = s8(read(at));
            s32 delta = s8(read(++at)) - s0;
            for (u32 i = 0; i < spf; ++i) {
                add(i, s0 + (s32(fw * u32(delta)) >> 23));
                fw += step;
                u32 advance = fw >> 23;
                if (advance == 0) {
                    continue;
                }
                fw &= ~0x3F800000u;
                count -= s32(advance);
                if (count <= 0) {
                    if (loop_length == 0) {
                        ended = true;
                        break;
                    }
                    s32 over = -count;
                    at = loop_start;
                    for (;;) {
                        count = loop_length + count;
                        if (count > 0) {
                            break;
                        }
                        over -= loop_length;
                    }
                    at += u32(over);
                    s0 = s8(read(at));
                    delta = s8(read(++at)) - s0;
                    continue;
                }
                --advance;
                if (advance == 0) {
                    s0 += delta;
                } else {
                    at += advance;
                    s0 = s8(read(at));
                }
                delta = s8(read(++at)) - s0;
            }
            if (!ended) {
                --at;
                c.fraction = fw;
            }
        }
        if (ended) {
            c.status = 0;
            continue;
        }
        c.count = u32(count);
        c.current = at;
    }
}

} // namespace sonic_advance_gba::audio
