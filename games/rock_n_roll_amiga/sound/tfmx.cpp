#include "rock_n_roll_amiga/sound/tfmx.hpp"

namespace rocknroll::sound {
namespace {

// DMACON values per channel ($38E7A: on, off).
constexpr std::uint16_t dma_on(int channel) { return std::uint16_t(0x8200 | (1 << channel)); }
constexpr std::uint16_t dma_off(int channel) { return std::uint16_t(1 << channel); }

} // namespace

void Tfmx::set_periods(std::span<const std::uint16_t> periods) {
    for (std::size_t i = 0; i < periods_.size() && i < periods.size(); ++i) periods_[i] = periods[i];
}

std::uint16_t Tfmx::note_period(int note) const { return periods_[std::size_t(note & 0xFF)]; }

void Tfmx::init(std::vector<std::uint8_t> song_data) {
    data_ = std::move(song_data);
    initialised_ = true;
    fade_stopped_ = false;
    // $38DC6: the per-song start steps and speeds become the resume points
    // (the copy stops after the first negative start step, DBMI).
    for (int i = 0; i < 32; ++i) {
        resume_speed_[std::size_t(i)] = u16(0x180 + std::uint32_t(i) * 2);
        resume_step_[std::size_t(i)] = u16(0x100 + std::uint32_t(i) * 2);
        if (resume_step_[std::size_t(i)] & 0x8000) break;
    }
}

void Tfmx::stop(amiga::PaulaChannels& paula) {
    playing_ = false;
    for (int c = 0; c < 4; ++c) paula.set_volume(c, 0);
    paula.write_dmacon(0x000F);
    resume_step_[std::size_t(song_ & 31)] = step_;
    for (auto& c : channels_) c.state = 0;
}

void Tfmx::stop_channel(int channel, amiga::PaulaChannels& paula) {
    channel &= 3;
    paula.write_dmacon(dma_off(channel));
    channels_[std::size_t(channel)].state = 0;
}

void Tfmx::effect(int number) {
    if (data_.size() < 0x600) return;
    const std::uint32_t table = u32(0x5FC);
    const int n = number & 0x7F;
    if (n == last_effect_) effect_time_ = -1;
    const std::uint32_t entry = table + std::uint32_t(n) * 8;
    const auto priority = std::int8_t(u8(entry + 5));
    if (priority < effect_priority_ && effect_time_ >= 0) return;
    effect_priority_ = priority;
    effect_time_ = std::int16_t(u16(entry + 6));
    note(u32(entry));
    last_effect_ = std::uint8_t(n);
}

void Tfmx::frame(amiga::PaulaChannels& paula, std::span<std::int8_t> memory) {
    if (request_ >= 0) {
        start(paula, memory);
        return;
    }
    if (!initialised_ || !playing_) return;
    if (!half_speed_) {
        sequencer(paula);
        for (int c = 0; c < 4; ++c) channel_tick(c, paula);
    } else if (++frame_count_ & 1) {
        sequencer(paula);
    } else {
        for (int c = 0; c < 4; ++c) channel_tick(c, paula);
    }
    if (effect_time_ >= 0) --effect_time_;
    else effect_priority_ = 0;
}

void Tfmx::start(amiga::PaulaChannels& paula, std::span<std::int8_t> memory) {
    if (!(request_ & 0x20)) stop(paula);
    playing_ = false;
    track8_reserved_ = false;
    for (std::size_t i = 0; i < 4 && i < memory.size(); ++i) memory[i] = 0; // $38AFE: an empty sample at the base
    const int song = request_ & 31;
    if (request_ & 0x40) { // $38B1A: the current song's place is kept
        const auto s = std::size_t(song_ & 31);
        resume_step_[s] = step_;
        resume_speed_[s] = std::uint16_t((half_speed_ ? 1 : 0) << 8 | (speed_ & 0xFF));
    }
    half_speed_ = false;
    step_ = first_step_ = u16(0x100 + std::uint32_t(song) * 2);
    last_step_ = u16(0x140 + std::uint32_t(song) * 2);
    speed_ = u16(0x180 + std::uint32_t(song) * 2);
    if (request_ & 0x40) {
        step_ = resume_step_[std::size_t(song)];
        half_speed_ = (resume_speed_[std::size_t(song)] >> 8) != 0;
        speed_ = std::uint16_t((speed_ & 0xFF00) | (resume_speed_[std::size_t(song)] & 0xFF));
    }
    load_step();
    speed_count_ = 0;
    // $38B84: the output filter off (the LED), no modulation.
    song_ = request_;
    request_ = -1;
    master_ = fade_target_ = 0x40;
    fade_direction_ = 0;
    fade_busy_ = false;
    fade_stopped_ = false;
    effect_priority_ = 0;
    effect_time_ = 0;
    playing_ = true;
}

void Tfmx::sequencer(amiga::PaulaChannels& paula) {
    if (speed_count_ != 0) {
        --speed_count_;
        return;
    }
    speed_count_ = half_speed_ ? std::uint16_t(speed_ >> 1) : speed_;
    for (int t = 0; t < 8; ++t) tracks_[std::size_t(t)].muted = u16(0x1C0 + std::uint32_t(t) * 2) != 0;
    // When a track's pattern ends the steps move on and all tracks are
    // gone through again from the first ($38004).
    for (int t = 0; t < 8; ++t)
        if (track_tick(t, paula)) t = -1;
}

bool Tfmx::track_tick(int t, amiga::PaulaChannels& paula) {
    auto& k = tracks_[std::size_t(t)];
    if (k.status >= 0x90) {
        if (k.status == 0xFE) { // stop a channel ($38086)
            k.status = 0xFF;
            if (!k.muted) stop_channel(k.transpose, paula);
        }
        return false;
    }
    if (k.wait != 0) {
        --k.wait;
        return false;
    }
    for (int guard = 0; guard < 4096; ++guard) {
        std::uint32_t command = u32(k.pattern + std::uint32_t(k.position) * 4);
        const std::uint8_t b0 = std::uint8_t(command >> 24);
        const std::uint8_t b1 = std::uint8_t(command >> 16);
        const std::uint16_t word = std::uint16_t(command);
        if (b0 < 0xF0) { // a note, transposed
            command = (command & 0x00FFFFFF) | std::uint32_t(std::uint8_t(b0 + k.transpose)) << 24;
            if (!k.muted) note(command);
            ++k.position;
            continue;
        }
        switch (b0 & 15) {
        case 0x0: // end of the pattern: the next step ($38102)
            if (k.status == 0x81) {
                k.status = 0xFF;
                return false;
            }
            step_ = step_ == last_step_ ? first_step_ : std::uint16_t(step_ + 1);
            load_step();
            return true;
        case 0x1: // loop ($38130)
            if (k.loop == 0) {
                k.loop = 0xFF;
                ++k.position;
                continue;
            }
            if (k.loop == 0xFF) k.loop = std::uint8_t(b1 - 1);
            else --k.loop;
            k.position = word;
            continue;
        case 0x2: // continue in another pattern ($38164)
            k.status = b1 & 0x7F;
            k.pattern = u32(0x400 + std::uint32_t(b1 & 0x7F) * 4);
            k.position = word;
            continue;
        case 0x3: // wait ($3818E): the low byte only
            k.wait = std::uint16_t((k.wait & 0xFF00) | b1);
            ++k.position;
            return false;
        case 0x4: // stop the track ($3819C)
            k.status = 0xFF;
            return false;
        case 0x5: case 0x6: case 0x7: // key up, vibrato, envelope ($381A4)
            if (!k.muted) note(command);
            ++k.position;
            continue;
        case 0x8: // gosub ($381B6)
            k.saved_pattern = k.pattern;
            k.saved_position = k.position;
            k.status = b1 & 0x7F;
            k.pattern = u32(0x400 + std::uint32_t(b1 & 0x7F) * 4);
            k.position = word;
            continue;
        case 0x9: { // return ($381EC): restores the first track's place (A5, not A0), then goes on here
            auto& first = tracks_[0];
            first.pattern = first.saved_pattern;
            first.position = first.saved_position;
            ++k.position;
            continue;
        }
        case 0xA: // fade ($381FC)
            start_fade(std::uint8_t(word & 0xFF), b1);
            ++k.position;
            continue;
        case 0xE: // stop, and the eighth track is free again ($38198)
            track8_reserved_ = false;
            k.status = 0xFF;
            return false;
        case 0xF: // nothing ($380F2)
            ++k.position;
            continue;
        default: // $FB-$FD ($380FA): the next long next time
            ++k.position;
            return false;
        }
    }
    return false;
}

void Tfmx::start_fade(std::uint8_t target, std::uint8_t speed) {
    if (fade_busy_) return;
    fade_busy_ = true;
    fade_target_ = target;
    fade_count_ = fade_speed_ = speed;
    if (speed == 0) {
        master_ = fade_target_;
        fade_direction_ = 0;
        fade_busy_ = false;
        return;
    }
    fade_direction_ = 1;
    if (master_ == fade_target_) {
        fade_direction_ = 0;
        fade_busy_ = false;
    } else if (master_ > fade_target_) {
        fade_direction_ = -1;
    }
}

void Tfmx::load_step() {
    for (int guard = 0; guard < 1024; ++guard) {
        const std::uint32_t at = 0x800 + std::uint32_t(step_) * 16;
        if (u16(at) == 0xEFFE) {
            std::uint16_t command = u16(at + 2);
            if (command >= 5) command = 0;
            switch (command) {
            case 0: // stop ($38400 → $38C3C)
                fade_stopped_ = true;
                fade_busy_ = false;
                playing_ = false;
                return;
            case 1: // loop to a step
                step_ = u16(at + 4);
                continue;
            case 2: // speed
                speed_ = speed_count_ = u16(at + 4);
                ++step_;
                continue;
            case 3: // half speed on or off
                half_speed_ = u16(at + 4) != 0;
                ++step_;
                continue;
            case 4: // fade ($3843E)
                ++step_;
                start_fade(u8(at + 7), u8(at + 5));
                continue;
            }
        }
        for (int t = 0; t < 8; ++t) {
            if (t == 7 && track8_reserved_) break;
            const std::uint16_t word = u16(at + std::uint32_t(t) * 2);
            auto& k = tracks_[std::size_t(t)];
            k.status = std::uint8_t(word >> 8);
            k.transpose = std::uint8_t(word);
            if (word & 0x8000) continue;
            k.pattern = u32(0x400 + std::uint32_t((word >> 8) & 0x7F) * 4);
            k.position = 0;
            k.wait = 0;
            k.loop = 0xFF;
        }
        return;
    }
}

void Tfmx::note(std::uint32_t command) {
    const std::uint8_t b0 = std::uint8_t(command >> 24), b1 = std::uint8_t(command >> 16);
    const std::uint8_t b2 = std::uint8_t(command >> 8), b3 = std::uint8_t(command);
    auto& c = channels_[b2 & 3];
    if (b0 == 0xF7) { // envelope
        c.envelope_speed = 1;
        c.envelope_step = b1;
        c.envelope_count = 1;
        c.envelope_target = b3;
    } else if (b0 == 0xF6) { // vibrato
        c.vibrato_width = b1 & 0xFE;
        c.vibrato_count = std::uint8_t((b1 & 0xFE) >> 1);
        c.vibrato_delta = std::int8_t(b3);
        c.vibrato_offset = 0;
    } else if (b0 == 0xF5) { // key up
        c.key_down = false;
    } else if (b0 >= 0xBF) { // a note sliding from the current period ($38A8A)
        c.porta_speed = b1;
        c.porta_count = 1;
        c.porta_step = b3;
        c.porta_period = c.period;
        c.note = b0 & 0x3F;
        c.period = note_period(b0 & 0x3F);
    } else if (b0 < 0x40) { // a note: its macro starts
        c.detune = std::int8_t(b3);
        c.volume_level = b2 >> 4;
        c.macro_number = b1;
        c.note = b0;
        c.macro = u32(0x600 + std::uint32_t(b1) * 4);
        c.state = 1;
        c.key_down = true;
    }
}

void Tfmx::channel_tick(int channel, amiga::PaulaChannels& paula) {
    const auto& c = channels_[std::size_t(channel)];
    if (c.state <= 0) { // idle or running: the effects first ($384EE)
        vibrato(channel, paula);
        portamento(channel, paula);
        volume(channel, paula);
    }
    macro_tick(channel, paula);
}

void Tfmx::macro_tick(int channel, amiga::PaulaChannels& paula) {
    auto& c = channels_[std::size_t(channel)];
    if (c.state == 0) return;
    if (c.state > 0) {
        c.macro_position = 0;
        c.macro_wait = 0;
        c.state = -1;
        c.macro_loop = 0xFF;
    }
    if (c.macro_wait != 0) {
        --c.macro_wait;
        return;
    }
    const auto set_period = [&](std::uint16_t period) {
        c.period = period;
        if (c.porta_step == 0) paula.set_period(channel, period);
    };
    for (int guard = 0; guard < 4096; ++guard) {
        const std::uint32_t command = u32(c.macro + std::uint32_t(c.macro_position) * 4);
        const std::uint8_t op = std::uint8_t(command >> 24), b1 = std::uint8_t(command >> 16);
        const std::uint8_t b3 = std::uint8_t(command);
        const std::uint16_t word = std::uint16_t(command);
        bool next = true; // go on with the next command in this frame ($38562)
        switch (op) {
        case 0x00: // DMA off, effects reset
            c.envelope_speed = 0;
            c.vibrato_width = 0;
            c.porta_step = 0;
            paula.write_dmacon(dma_off(channel));
            break;
        case 0x01: // DMA on
            paula.write_dmacon(dma_on(channel));
            break;
        case 0x02: // sample start
            c.sample = command & 0xFFFFFF;
            paula.set_location(channel, c.sample);
            break;
        case 0x03: // sample length (words)
            c.length = word;
            paula.set_length(channel, word);
            break;
        case 0x04: // wait
            c.macro_wait = word;
            next = false;
            break;
        case 0x05: // loop ($385B6)
            if (c.macro_loop == 0) {
                c.macro_loop = 0xFF;
                break;
            }
            if (c.macro_loop == 0xFF) c.macro_loop = std::uint8_t(b1 - 1);
            else --c.macro_loop;
            c.macro_position = word;
            continue;
        case 0x06: // continue in another macro
            c.macro = u32(0x600 + std::uint32_t(b1 & 0x7F) * 4);
            c.macro_position = word;
            continue;
        case 0x07: // stop
            c.state = 0;
            return;
        case 0x08: // the note plus an offset, detuned
            set_period(std::uint16_t(note_period(std::uint8_t(b1 + c.note)) + c.detune + word));
            next = false;
            break;
        case 0x09: // a fixed note
            set_period(std::uint16_t(note_period(b1) + word));
            next = false;
            break;
        case 0x0A: // effects reset
            c.envelope_speed = 0;
            c.vibrato_width = 0;
            c.porta_step = 0;
            break;
        case 0x0B: // portamento
            c.porta_speed = b1;
            c.porta_count = 1;
            c.porta_step = word;
            c.porta_period = c.period;
            break;
        case 0x0C: // vibrato
            c.vibrato_width = b1 & 0xFE;
            c.vibrato_count = std::uint8_t((b1 & 0xFE) >> 1);
            c.vibrato_delta = std::int8_t(b3);
            if (c.porta_step == 0) paula.set_period(channel, c.period);
            c.vibrato_offset = 0;
            break;
        case 0x0D: // volume from the note's level
            c.volume = std::uint8_t(c.volume_level * 3 + word);
            break;
        case 0x0E: // volume
            c.volume = b3;
            break;
        case 0x0F: // envelope
            c.envelope_speed = std::uint8_t(word >> 8);
            c.envelope_step = b1;
            c.envelope_count = std::uint8_t(word >> 8);
            c.envelope_target = b3;
            break;
        case 0x10: // loop while the key is down
            if (c.key_down) {
                if (c.macro_loop == 0) {
                    c.macro_loop = 0xFF;
                    break;
                }
                if (c.macro_loop == 0xFF) c.macro_loop = std::uint8_t(b1 - 1);
                else --c.macro_loop;
                c.macro_position = word;
                continue;
            }
            break;
        case 0x11: // sample start + offset
            c.sample += std::uint32_t(std::int32_t(std::int16_t(word)));
            paula.set_location(channel, c.sample);
            break;
        case 0x12: // sample length + offset
            c.length = std::uint16_t(c.length + word);
            paula.set_length(channel, c.length);
            break;
        case 0x13: // DMA off
            paula.write_dmacon(dma_off(channel));
            break;
        case 0x14: // wait for the key's release
            if (c.key_down) return;
            break;
        case 0x15: // gosub
            c.saved_macro = c.macro;
            c.saved_position = c.macro_position;
            c.macro = u32(0x600 + std::uint32_t(b1 & 0x7F) * 4);
            c.macro_position = word;
            continue;
        case 0x16: // return
            c.macro = c.saved_macro;
            c.macro_position = c.saved_position;
            break;
        case 0x17: // a fixed period
            set_period(word);
            next = false;
            break;
        case 0x18: // sample start and length move on by an offset
            c.sample += word;
            paula.set_location(channel, c.sample);
            c.length = std::uint16_t(c.length - (word >> 1));
            paula.set_length(channel, c.length);
            break;
        case 0x19: // the empty sample at the base
            c.sample = 0;
            paula.set_location(channel, 0);
            c.length = 1;
            paula.set_length(channel, 1);
            break;
        default: // $1A on: skipped
            next = false;
            break;
        }
        ++c.macro_position;
        if (!next) return;
    }
}

void Tfmx::vibrato(int channel, amiga::PaulaChannels& paula) {
    auto& c = channels_[std::size_t(channel)];
    if (c.vibrato_width == 0) return;
    c.vibrato_offset = std::int16_t(c.vibrato_offset + c.vibrato_delta);
    if (c.porta_step == 0) paula.set_period(channel, std::uint16_t(c.vibrato_offset + c.period));
    if (--c.vibrato_count == 0) {
        c.vibrato_count = c.vibrato_width;
        c.vibrato_delta = std::int8_t(-c.vibrato_delta);
    }
}

void Tfmx::portamento(int channel, amiga::PaulaChannels& paula) {
    auto& c = channels_[std::size_t(channel)];
    if (c.porta_step == 0) return;
    if (--c.porta_count != 0) return;
    c.porta_count = c.porta_speed;
    const std::uint16_t target = c.period;
    std::uint16_t now = c.porta_period;
    if (now == target) return;
    bool done;
    if (now < target) {
        now = std::uint16_t(now + c.porta_step);
        done = now >= target;
    } else {
        now = std::uint16_t(now - c.porta_step);
        done = now <= target;
    }
    if (done) {
        c.porta_step = 0;
        now = c.period;
    }
    now &= 0x7FF;
    c.porta_period = now;
    paula.set_period(channel, now);
}

void Tfmx::volume(int channel, amiga::PaulaChannels& paula) {
    auto& c = channels_[std::size_t(channel)];
    // The master fade, stepped in each channel's turn (four times a frame).
    if (fade_direction_ != 0 && --fade_count_ == 0) {
        fade_count_ = fade_speed_;
        master_ = std::uint8_t(master_ + fade_direction_);
        if (master_ == fade_target_) {
            fade_direction_ = 0;
            fade_busy_ = false;
        }
    }
    unsigned v = c.volume;
    if (master_ != 0x40) v = ((v * 4 * master_) & 0xFFFF) >> 8; // (and $3893E masks the word at 0, not this)
    paula.set_volume(channel, std::uint16_t(v));
    // The envelope, after the register was written.
    if (c.envelope_speed == 0) return;
    if (c.envelope_count != 0) {
        --c.envelope_count;
        return;
    }
    c.envelope_count = c.envelope_speed;
    const std::uint8_t target = c.envelope_target;
    if (target >= c.volume) { // up ($38990)
        c.volume = std::uint8_t(c.volume + c.envelope_step);
        if (target < c.volume) {
            c.volume = target;
            c.envelope_speed = 0;
        }
    } else { // down ($3896E): stops when the target is reached or passed
        c.volume = std::uint8_t(c.volume - c.envelope_step);
        const std::int8_t difference = std::int8_t(std::uint8_t(target - c.volume));
        if (difference >= 0 || target >= c.volume) {
            c.volume = target;
            c.envelope_speed = 0;
        }
    }
}

std::vector<std::pair<std::string, std::int64_t>> Tfmx::snapshot(std::uint32_t at) const {
    std::vector<std::pair<std::string, std::int64_t>> v;
    const auto add = [&](std::string name, std::int64_t value) { v.emplace_back(std::move(name), value); };
    const auto address = [&](std::uint32_t base, std::uint32_t offset) -> std::int64_t { return offset == unset ? 0 : base + offset; };
    add("step", step_);
    add("speedcount", speed_count_);
    add("master", master_);
    add("playing", playing_);
    for (int t = 0; t < 8; ++t) {
        const auto& k = tracks_[std::size_t(t)];
        const auto n = "t" + std::to_string(t);
        add(n + "status", k.status);
        add(n + "transpose", k.transpose);
        add(n + "pattern", address(at, k.pattern));
        add(n + "pos", k.position);
        add(n + "wait", k.wait);
    }
    for (int c = 0; c < 4; ++c) {
        const auto& h = channels_[std::size_t(c)];
        const auto n = "c" + std::to_string(c);
        add(n + "state", std::int16_t(h.state));
        add(n + "macro", address(at, h.macro));
        add(n + "mpos", h.macro_position);
        add(n + "mwait", h.macro_wait);
        add(n + "vol", h.volume);
        add(n + "period", h.period);
        add(n + "sample", address(sample_base, h.sample));
        add(n + "len", h.length);
        add(n + "vib", h.vibrato_offset);
        add(n + "porta", h.porta_step);
        add(n + "portaper", h.porta_period);
        add(n + "key", h.key_down);
    }
    return v;
}

} // namespace rocknroll::sound
