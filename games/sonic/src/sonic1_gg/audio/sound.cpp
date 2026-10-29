// The game's sound driver (ROM bank 3, 4000-46FF), reconstructed as native code.
//
// Music and effects are bytecode scores. Each voice reads commands from its
// score; a byte below 0x80 is a note:
//
//   0x00-0x6F  pitched note: low nibble = note (base period table at 44D5),
//              high nibble = octave (period >> octave), followed by a length
//   0x70-0x7E  drum: low nibble selects a noise preset (43CE), plus a length
//   0x7F       rest, plus a length (0 = the voice's default length)
//
// and bytes from 0x80 are commands:
//
//   80 unit tempo   set the length unit and the tempo (both words)
//   81 v            set the volume       82 a d s d s r   set the envelope shape
//   83 d r n delta  vibrato: delay, rate, steps and step size
//   84 w            detune               85 x             (parameter ignored)
//   86 / 87 n addr  loop start / loop n times back to addr
//   88              mark the repeat point used by FF
//   89 n            noise mode           8A n             default note length
//   8B / 8C         volume up / down     8D               tie into the next note
//   FE / FF         end (FF jumps to the repeat point if one is set)
//
// Note lengths are counted in a 16-bit accumulator decremented by the tempo
// each frame; the deliberate wraparound keeps fractional timing.

#include "sonic1_gg/audio/sound.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sonic1_gg {
unsigned SoundData::byte(unsigned a) const {
    if (a >= 0x4716 && a < 0x7fbd) {
        return sequences.at(a - 0x4716);
    }
    if (a >= 0x44d5 && a < 0x44f3) {
        return notes.at(a - 0x44d5);
    }
    if (a >= 0x43ce && a < 0x43de) {
        return drums.at(a - 0x43ce);
    }
    throw std::runtime_error("Sound data pointer outside extracted tables: " + std::to_string(a));
}

SoundData extract_sound(const Rom& rom) {
    return {rom.slice(0xc716, 0x7fbd - 0x4716), rom.slice(0xc4d5, 30), rom.slice(0xc3ce, 16)};
}

void save_sound(const SoundData& d, const std::filesystem::path& p) {
    write_file(p / "music.bin", d.sequences);
    write_file(p / "notes.bin", d.notes);
    write_file(p / "drums.bin", d.drums);
}

SoundData load_sound(const std::filesystem::path& p) {
    SoundData d{read_file(p / "music.bin"), read_file(p / "notes.bin"), read_file(p / "drums.bin")};
    if (d.sequences.size() != 0x7fbd - 0x4716 || d.notes.size() != 30 || d.drums.size() != 16) {
        throw std::runtime_error("Incomplete sound assets. Import the ROM again.");
    }
    return d;
}

void SoundDriver::event(SoundEvent e) {
    switch (e.kind) {
    case SoundEvent::Kind::Music:
        music(e.value);
        break;
    case SoundEvent::Kind::Effect:
        effect(e.value);
        break;
    case SoundEvent::Kind::Tempo:
        speed_shoes(e.value != 0);
        break;
    case SoundEvent::Kind::Stop:
        stop();
        break;
    case SoundEvent::Kind::Fade:
        fade(e.value);
        break;
    }
}

// True while the music can still produce notes. Used only by the sound test to
// detect the end of a jingle; it never changes the sequencer state.
bool SoundDriver::playing() const {
    return std::any_of(voices_.begin(), voices_.begin() + 4, [this](const auto& v) {
        if (!v.active()) {
            return false;
        }
        // Player-only observation; never alter the original sequencer state.
        // Act-clear voices repeat [81 00, 7F 00, FF] (e.g. CPU 68F3);
        // emerald voices repeat [7F 00, FF] (e.g. 7AB5). Once resting
        // immediately before that FF, they can never produce another note.
        unsigned rest = v.repeat_position;
        if (rest >= 0x4716 && rest + 4 < 0x7fbd && data_.byte(rest) == 0x81 &&
            data_.byte(rest + 1) == 0) {
            rest += 2;
        }
        // Ending voices disable vibrato before their permanent rest (79C7/79FA).
        if (rest >= 0x4716 && rest + 5 < 0x7fbd && data_.byte(rest) == 0x83) {
            rest += 6;
        }
        const bool terminal_rest = v.gate == 0 && rest >= 0x4716 && rest + 2 < 0x7fbd &&
                                   v.position == rest + 2 && data_.byte(rest) == 0x7f &&
                                   data_.byte(rest + 2) == 0xff;
        return !terminal_rest;
    });
}

void SoundDriver::music(unsigned id) {
    if (id >= 21) {
        throw std::runtime_error("Unknown music ID");
    }
    const auto base = data_.word(0x4716 + 2 * id); // 46EB: song directory.
    // 4018: only these fields are reset. Envelopes/volume are set by score
    // commands; preserving the others also preserves the original transitions.
    for (unsigned i = 0; i < 5; ++i) {
        voices_[i].position = base + data_.word(base + 2 * i);
    }
    for (unsigned i = 0; i < 4; ++i) {
        auto& v = voices_[i];
        v.repeat_position = 0;
        v.loop_depth = 0;
        v.loop_initialized = true;
        v.remaining = 1;
        v.vibrato_delta = 0;
        v.delta = 0;
        v.detune = 0;
        v.tone_latch = 0x80 + 32 * i;
        v.volume_latch = 0x90 + 32 * i;
        v.flags = 2;
        v.vibrato_delay = 0;
        v.vibrato_rate = 0;
        v.index = i;
        v.song_base = base;
    }
    tempo_ = 1;
    voices_[4].flags = 0;
    priority_ = 0;
    flags_ = 0;
}

void SoundDriver::stop() { // 412D
    for (auto& v : voices_) {
        v.flags &= ~2;
    }
    priority_ = 0;
    for (unsigned i = 0; i < 4; ++i) {
        write(0x9f + 32 * i);
    }
    flags_ &= ~8;
}

// Starts a sound effect (46FF / 4171) on the fifth voice. A lower priority
// number wins; an effect does not interrupt a more important one. The effect
// borrows one of the four chip channels from the music, whose timers keep
// running underneath.
void SoundDriver::effect(unsigned id) {
    if (id >= 36) {
        throw std::runtime_error("Unknown effect ID");
    }
    const auto entry = 0x4740 + 4 * id, base = data_.word(entry), priority = data_.byte(entry + 2);
    if (priority_ && priority_ < priority) {
        return;
    }
    priority_ = priority;
    auto& v = voices_[4];
    v.song_base = base;
    write(v.volume_latch | 15); // The previous effect's channel is silenced first.
    effect_channel_ = data_.byte(base);
    if (effect_channel_ > 3) {
        throw std::runtime_error("Invalid effect channel");
    }
    v.unit = data_.word(base + 1);
    effect_tempo_ = data_.word(base + 3);
    v.position = base + 6;
    v.tone_latch = 0x80 + 32 * effect_channel_;
    v.volume_latch = v.tone_latch + 16;
    v.repeat_position = 0;
    v.vibrato_delta = 0;
    v.delta = 0;
    v.detune = 0;
    v.index = 4;
    v.remaining = 1;
    v.loop_depth = 0;
    v.loop_initialized = true;
    v.flags = 2;
}

void SoundDriver::speed_shoes(bool fast) {
    tempo_ = normal_tempo_ + unsigned(fast);
}

// Fades the music out: every frame each music voice's volume drops by `amount`
// + 1 (in 1/256 steps) until a 0x1000 budget is used up, then all sound stops.
void SoundDriver::fade(unsigned amount) {
    flags_ |= 8;
    fade_left_ = 0x1000;
    fade_step_ = amount;
}

Bytes SoundDriver::take_writes() {
    return std::exchange(writes_, {});
}

void SoundDriver::start_note(Voice& v) { // 436E: restart envelope and vibrato.
    v.delay_left = v.vibrato_delay;
    v.rate_left = v.vibrato_rate;
    v.steps_left = v.vibrato_steps / 2;
    v.delta = v.vibrato_delta;
    v.vibrato_offset = 0;
    v.stage = 0;
    v.envelope = 0;
    v.gate = 15;
}

void SoundDriver::sequence(Voice& v, unsigned tempo) { // 42F4..43CD, 44F3..46D7
    if (!v.active()) {
        return;
    }
    const bool due = v.remaining <= tempo;
    v.remaining -= tempo; // Deliberate 16-bit wrap: keeps fractional note timing.
    if (due) {
        for (unsigned guard = 0;; ++guard) {
            if (guard > 4096) {
                throw std::runtime_error("Sound score has no timed note");
            }
            const auto command = data_.byte(v.position);
            if (command < 0x80) {
                if (command == 0x7f) {
                    v.gate = 0; // Rest: envelope/vibrato keep their state.
                } else if (command >= 0x70) {
                    const auto preset = 0x43ce + (command & 15) * 8;
                    v.noise = data_.byte(preset);
                    for (unsigned i = 0; i < 6; ++i) {
                        v.shape[i] = data_.byte(preset + 1 + i);
                    }
                    start_note(v);
                } else {
                    v.period = data_.word(0x44d5 + 2 * (command & 15));
                    v.octave = command >> 4;
                    if (!(v.flags & 1)) {
                        start_note(v); // 8D ties the next pitched note.
                    }
                }
                auto length = data_.byte(v.position + 1);
                v.position += 2;
                if (!length) {
                    length = v.default_length;
                }
                v.remaining += (v.unit ? v.unit : unit_) * length;
                break;
            }
            if (command == 0xff || command == 0xfe) {
                if (command == 0xff && v.repeat_position) {
                    v.position = v.repeat_position;
                    continue;
                }
                if (command == 0xfe) {
                    priority_ = 0;
                }
                v.flags &= ~2;
                write(v.volume_latch | 15);
                return;
            }
            ++v.position;
            auto byte = [&]() {
                return data_.byte(v.position++);
            };
            auto word = [&]() {
                auto lo = byte();
                return lo | (byte() << 8);
            };
            switch (command) {
            case 0x80:
                v.unit = unit_ = word();
                tempo_ = normal_tempo_ = word();
                break;
            case 0x81:
                v.target_volume = byte();
                if (v.index == 4 || !(flags_ & 8)) {
                    v.volume = unsigned(v.target_volume) << 8;
                }
                break;
            case 0x82:
                for (auto& value : v.shape) {
                    value = byte();
                }
                break;
            case 0x83:
                v.vibrato_delay = byte();
                v.vibrato_rate = byte();
                v.vibrato_steps = byte();
                v.vibrato_delta = word();
                break;
            case 0x84:
                v.detune = word();
                break;
            case 0x85:
                byte();
                break; // 462D reads and discards this parameter.
            case 0x86:
                if (v.loop_depth >= v.loops.size()) {
                    throw std::runtime_error("Sound loop stack overflow");
                }
                v.loops[v.loop_depth++] = 0;
                break;
            case 0x87: {
                if (!v.loop_depth) {
                    throw std::runtime_error("Sound loop stack underflow");
                }
                auto& remaining = v.loops[v.loop_depth - 1];
                const auto count = byte(), target = word();
                if (!remaining) {
                    remaining = count - 1;
                } else {
                    --remaining;
                }
                if (remaining) {
                    v.position = v.song_base + target;
                } else {
                    --v.loop_depth;
                }
                break;
            }
            case 0x88:
                v.repeat_position = v.position;
                break;
            case 0x89:
                v.noise = byte();
                break;
            case 0x8a:
                v.default_length = byte();
                break;
            case 0x8b:
                v.target_volume = std::min(unsigned(std::uint8_t(v.target_volume + 1)), 15u);
                if (!(flags_ & 8)) {
                    v.volume = (v.volume & 255) | (unsigned(v.target_volume) << 8);
                }
                break;
            case 0x8c:
                --v.target_volume;
                if (v.target_volume >= 16) {
                    v.target_volume = 0;
                }
                if (!(flags_ & 8)) {
                    v.volume = (v.volume & 255) | (unsigned(v.target_volume) << 8);
                }
                break;
            case 0x8d:
                v.flags |= 1;
                break;
            default:
                throw std::runtime_error("Unknown sound command " + std::to_string(command));
            }
        }
    }
    v.flags &= ~1;
}

// Produces one frame of PSG writes for a voice (43DE): the ADSR-like envelope
// (attack, two decay/sustain stages, release; 4545-45AB), vibrato, the tone
// period (or noise mode) and the attenuation.
void SoundDriver::synthesize(Voice& v) {
    if (!v.active()) {
        return;
    }
    if (v.stage == 0) {
        const unsigned next = unsigned(v.envelope) + v.shape[0];
        v.envelope = std::min(next, 255u);
        if (next > 255) {
            ++v.stage;
        }
    } else if (v.stage == 1 || v.stage == 2) {
        const unsigned i = v.stage == 1 ? 1 : 3;
        const int next = int(v.envelope) - v.shape[i];
        v.envelope = std::max(next, int(v.shape[i + 1]));
        // The ROM advances only on strict underflow/below-sustain, not equality.
        if (next < v.shape[i + 1]) {
            ++v.stage;
        }
    } else if (v.stage == 3) {
        const int next = int(v.envelope) - v.shape[5];
        v.envelope = std::max(next, 0);
        if (next < 0) {
            ++v.stage;
        }
    }
    if (v.tone_latch == 0xe0) {
        if (last_noise_ != v.noise) {
            last_noise_ = v.noise;
            write(v.noise | 0xe0);
        }
    } else {
        if (v.delay_left) {
            --v.delay_left;
        } else if (--v.rate_left == 0) {
            v.rate_left = v.vibrato_rate;
            if (--v.steps_left == 0) {
                v.steps_left = v.vibrato_steps;
                v.delta = 0 - v.delta;
            } else {
                v.vibrato_offset += v.delta;
            }
        }
        const auto period = std::uint16_t(v.period + v.detune + v.vibrato_offset) >> v.octave;
        write(v.tone_latch | (period & 15));
        write(period >> 4);
    }
    // 448F: rounded product of envelope (0..255) and volume (0..15).
    const auto amplitude = ((v.volume >> 8) * v.envelope + 128) >> 8;
    write(v.volume_latch | ((amplitude & v.gate) ^ 15));
    if ((flags_ & 8) && v.index != 4) {
        // CP 4 leaves carry set for music voice indices 0..3 (44BA).
        const int volume = int(v.volume) - fade_step_ - 1;
        v.volume = std::max(volume, 0);
    }
}

// One frame of the driver (4000), run once per video interrupt: advance the
// four music voices and the effect voice, then write each chip channel from the
// voice that currently owns it.
void SoundDriver::tick() {
    output_initialized_ = true;
    for (unsigned i = 0; i < 4; ++i) {
        sequence(voices_[i], tempo_);
        output_[i] = i;
    }
    sequence(voices_[4], effect_tempo_);
    if (voices_[4].active()) {
        output_[effect_channel_] = 4;
    }
    for (auto index : output_) {
        synthesize(voices_[index]);
    }
    if (flags_ & 8) {
        const bool ended = fade_left_ < fade_step_;
        fade_left_ -= fade_step_;
        if (ended) {
            stop();
        }
    }
}

} // namespace sonic1_gg
