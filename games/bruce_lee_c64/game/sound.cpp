#include "bruce_lee_c64/game/sound.hpp"

namespace bruce_lee::game {
namespace {

// The effect player's tables in the high block.
constexpr std::uint16_t effect_streams = 0xEBE5; // word per effect
constexpr std::uint16_t command_kinds = 0xEC03;  // command byte -> jump table offset (3 per entry)
constexpr std::uint16_t envelopes = 0xEC11;      // attack/decay, sustain/release pairs
constexpr std::uint16_t pulse_widths = 0xEC1B;   // low, high pairs
constexpr std::uint16_t notes_lo = 0xE200, notes_hi = 0xE254; // frequency by note

// SID registers of a voice.
constexpr int freq_lo = 0, freq_hi = 1, pulse_lo = 2, pulse_hi = 3, control = 4, attack_decay = 5,
              sustain_release = 6, volume = 0x18;

} // namespace

EffectPlayer::EffectPlayer(const data::Assets& assets) : assets_(assets), priorities_(assets.effect_priorities) {}

void EffectPlayer::request(c64::Sid& sid, std::uint8_t effect) {
    sid.write(volume, 0x0F);
    const std::uint8_t priority = priorities_[effect & 0x0F];
    auto take = [&](Channel& c) {
        c.state = effect;
        c.priority = priority;
    };
    for (auto& c : channels_) {
        if (c.state == 0) {
            take(c);
            return;
        }
    }
    for (auto& c : channels_) {
        if (priority >= c.priority) {
            take(c);
            return;
        }
    }
}

void EffectPlayer::silence(c64::Sid& sid) {
    for (auto& c : channels_) {
        c.state = 0;
    }
    for (int reg = 0; reg <= 0x20; ++reg) {
        sid.write(reg, 0);
    }
}

void EffectPlayer::frame(c64::Sid& sid) {
    for (int voice = 0; voice < 3; ++voice) {
        run(sid, voice);
    }
}

// $EB03 for one voice. A command waits its frames, then the next one runs:
// 0 ends the effect, 1 plays a note for some frames, 2-6 also set the
// waveform/gate and the envelope or pulse width, 7 sets only the waveform
// (and, with no wait of its own, the next command comes 255 frames later).
void EffectPlayer::run(c64::Sid& sid, int voice) {
    Channel& c = channels_[std::size_t(voice)];
    const int base = 7 * voice;
    if (c.state == 0) {
        return;
    }
    if (c.state & 0x80) {
        const auto entry = std::uint16_t(effect_streams + std::uint8_t(c.state << 1));
        c.stream = std::uint16_t(at(entry) | at(std::uint16_t(entry + 1)) << 8);
        c.wait = 1;
        c.state = 1;
        sid.write(base + control, 0);
        c.position = 0;
    }
    if (--c.wait != 0) {
        return;
    }
    std::uint8_t pos = c.position;
    auto next = [&] { return at(std::uint16_t(c.stream + pos++)); };
    const std::uint8_t kind = std::uint8_t(at(std::uint16_t(command_kinds + at(std::uint16_t(c.stream + pos)))) / 3);
    auto note = [&] { // $EB52
        ++pos;
        c.wait = next();
        const std::uint8_t n = next();
        sid.write(base + freq_lo, at(std::uint16_t(notes_lo + n)));
        sid.write(base + freq_hi, at(std::uint16_t(notes_hi + n)));
    };
    auto envelope = [&] { // $EB6A
        const std::uint8_t i = next();
        sid.write(base + attack_decay, at(std::uint16_t(envelopes + i)));
        sid.write(base + sustain_release, at(std::uint16_t(envelopes + i + 1)));
    };
    auto pulse = [&] { // $EB91
        const std::uint8_t i = next();
        sid.write(base + pulse_lo, at(std::uint16_t(pulse_widths + i)));
        sid.write(base + pulse_hi, at(std::uint16_t(pulse_widths + i + 1)));
    };
    auto waveform = [&] { sid.write(base + control, next()); }; // $EB7B
    switch (kind) {
    case 0: // $EB82
        sid.write(base + freq_lo, 0);
        sid.write(base + freq_hi, 0);
        c.state = 0;
        c.position = 0;
        return;
    case 1:
        note();
        break;
    case 2:
        note();
        waveform();
        break;
    case 3:
        note();
        envelope();
        waveform();
        break;
    case 4:
        note();
        envelope();
        pulse();
        waveform();
        break;
    case 5:
    case 6:
        note();
        pulse();
        waveform();
        break;
    default: // 7 ($EBDD)
        ++pos;
        waveform();
        break;
    }
    c.position = pos;
}

} // namespace bruce_lee::game

namespace bruce_lee::game {
namespace {

// The music player's tables and initial values in the high block.
constexpr std::uint16_t tune_streams = 0xE8C0;  // 8 bytes per tune: three stream addresses
constexpr std::uint16_t tune_count = 0xE86B;
constexpr std::uint16_t voice_defaults = 0xE2A8; // $E100-$E12D as a tune starts
constexpr std::uint16_t lengths = 0xE6F9;       // note lengths (16 each for $E2E7 = 0 and 1)
constexpr std::uint16_t waveforms = 0xE71C;     // $A0-$A3: triangle, sawtooth, pulse, noise with the gate
constexpr std::uint16_t envelope_sets = 0xE80A; // $Bx: 8 bytes each (6 used); $Dx: filter sets from +$40
constexpr std::uint16_t initial_tempo = 0xE30D, initial_long_notes = 0xE2E7;
constexpr std::array<int, 3> register_base{0, 7, 14}; // $E719


} // namespace

MusicPlayer::MusicPlayer(const data::Assets& assets)
    : assets_(assets), tempo_(assets.sound_byte(initial_tempo)), long_notes_(assets.sound_byte(initial_long_notes)) {}

// $E3DA / $E3C5: nothing plays, the voices' registers cleared.
void MusicPlayer::silence(c64::Sid& sid) {
    playing_ = false;
    ended_ = false;
    transpose_ = 0;
    for (int reg = 0x17; reg >= 0; --reg) {
        sid.write(reg, 0);
    }
}

void MusicPlayer::stop(c64::Sid& sid) {
    silence(sid);
}

// $06CC: the title sets what comes at a tune's end and the list, then
// $E003 ($E479) starts the list from its start.
void MusicPlayer::start_list(c64::Sid& sid, std::uint8_t end_action, std::array<std::uint8_t, 2> list) {
    end_action_ = end_action;
    list_ = list;
    list_position_ = list_start_;
    play_list(sid, 1);
}

// $E45E: list entries: bit 7 back to the list's start (and on only with
// bit 0 of the action), bit 6 a transposition (the tune's start clears it
// again), else a tune.
void MusicPlayer::play_list(c64::Sid& sid, std::uint8_t action) {
    for (int guard = 0; guard < 256; ++guard) {
        const std::uint8_t entry = list_[list_position_ % list_.size()];
        if (entry & 0x80) {
            list_position_ = list_start_;
            if (!(action & 0x01)) {
                silence(sid);
                return;
            }
            continue;
        }
        ++list_position_;
        if (entry & 0x40) {
            transpose_ = entry & 0x3F;
            continue;
        }
        tune_ = entry;
        start_tune(sid);
        return;
    }
}

// $E438: Y = the end action. Bit 7: tune Y & $7F next; bit 1: the list
// goes on; else the next tune (bit 0: around to the first after the last).
void MusicPlayer::end_of_tune(c64::Sid& sid, std::uint8_t action) {
    if (action & 0x80) {
        tune_ = action & 0x7F;
        start_tune(sid);
        return;
    }
    if (action & 0x02) {
        play_list(sid, action);
        return;
    }
    std::uint8_t next = tune_;
    if (next >= at(tune_count)) {
        if (!(action & 0x01)) {
            silence(sid);
            return;
        }
        next = 0xFF;
    }
    tune_ = std::uint8_t(next + 1);
    start_tune(sid);
}

// $E352 / $E38C: the tune's streams, the voices' defaults, and each voice's
// leading commands.
void MusicPlayer::start_tune(c64::Sid& sid) {
    playing_ = false;
    const std::uint8_t tune = tune_ < at(tune_count) ? tune_ : at(tune_count); // $E360
    for (int v = 0; v < 3; ++v) {
        const auto entry = std::uint16_t(tune_streams + 8 * tune + 2 * v);
        voices_[std::size_t(v)].stream = std::uint16_t(at(entry) | at(std::uint16_t(entry + 1)) << 8);
    }
    // $E3A8
    filter_mode_ = 0;
    resonance_ = 0;
    auto def = [&](int offset) { return at(std::uint16_t(voice_defaults + offset)); };
    for (int v = 0; v < 3; ++v) {
        Voice& voice = voices_[std::size_t(v)];
        voice.legato = def(1 + v);
        voice.count = def(4 + v);
        voice.gap = def(7 + v);
        voice.length = def(10 + v);
        voice.next_gap = def(13 + v);
        voice.attack_decay = def(16 + v);
        voice.sustain_release = def(19 + v);
        voice.waveform = def(22 + v);
        voice.style = def(25 + v);
        for (int i = 0; i < 6; ++i) {
            voice.envelopes[std::size_t(i)] = def(28 + 6 * v + i);
        }
    }
    tempo_count_ = tempo_;
    silence(sid); // $E3C5 (transposition back to 0 too)
    for (int v = 0; v < 3; ++v) {
        look_ahead_ = 0xFF;
        commands(sid, v);
    }
    playing_ = true;
}

// $E3EA: the voice's next byte; the one after it is kept as the look-ahead.
std::uint8_t MusicPlayer::fetch(int v) {
    auto& stream = voices_[std::size_t(v)].stream;
    const std::uint8_t value = at(stream);
    ++stream;
    look_ahead_ = at(stream);
    return value;
}

// $E489
void MusicPlayer::apply_filter(c64::Sid& sid) {
    sid.write(0x18, std::uint8_t(filter_mode_ | volume_));
    sid.write(0x17, resonance_);
    filter_voice_ = 0;
}

// $E578: the voice's next note (0 is a rest), then its commands.
void MusicPlayer::next_note(c64::Sid& sid, int v) {
    Voice& voice = voices_[std::size_t(v)];
    const int base = register_base[std::size_t(v)];
    const std::uint8_t note = fetch(v);
    if (note == 0) {
        sid.write(base + 0, 0);
        sid.write(base + 1, 0);
    } else {
        const auto n = std::uint8_t(note + transpose_);
        sid.write(base + 1, at(std::uint16_t(0xE254 + n)));
        sid.write(base + 0, at(std::uint16_t(0xE200 + n)));
        if (!voice.legato) {
            sid.write(base + 5, voice.attack_decay);
            sid.write(base + 6, voice.sustain_release);
            sid.write(base + 4, voice.waveform);
        }
    }
    commands(sid, v);
}

// $E5BB: commands while the look-ahead byte has bit 7.
void MusicPlayer::commands(c64::Sid& sid, int v) {
    Voice& voice = voices_[std::size_t(v)];
    const int base = register_base[std::size_t(v)];
    for (int guard = 0; guard < 4096 && (look_ahead_ & 0x80); ++guard) {
        const std::uint8_t command = fetch(v);
        if (command == 0xFF) {
            ended_ = true;
            return;
        }
        switch ((command >> 4) & 7) {
        case 1: // $E617: volume, applied at this voice's next note
            volume_ = command & 0x0F;
            filter_voice_ = std::uint8_t(v + 1);
            break;
        case 2: { // $E630
            const int low = command & 0x0F;
            if (command >= 0xAC) {
                long_notes_ = command & 1;
            } else if (command == 0xAB) {
                voice.waveform = 0x51;
            } else if (low < 4) {
                voice.waveform = at(std::uint16_t(waveforms + low));
            } else if (low < 7) {
                voice.style = std::uint8_t(low & 3);
                const int pair = 2 * voice.style;
                voice.attack_decay = voice.envelopes[std::size_t(pair)];
                voice.sustain_release = voice.envelopes[std::size_t(pair + 1)];
            } else if (low < 9) {
                voice.legato = command & 1;
            } else {
                voice.waveform = std::uint8_t((voice.waveform & 0xFB) | (command & 1) << 2);
            }
            break;
        }
        case 3: { // $E720: an envelope set
            const auto from = std::uint16_t(envelope_sets + 8 * (command & 7));
            for (int i = 0; i < 6; ++i) {
                voice.envelopes[std::size_t(i)] = at(std::uint16_t(from + i));
            }
            break;
        }
        case 4: { // $E75E: pulse width
            const std::uint8_t lo = fetch(v);
            sid.write(base + 2, lo);
            const std::uint8_t hi = fetch(v);
            sid.write(base + 3, hi);
            break;
        }
        case 5: { // $E779: a filter set, applied at this voice's next note
            const auto from = std::uint16_t(envelope_sets + 0x40 + 4 * (command & 7));
            sid.write(0x15, at(from));
            sid.write(0x16, at(std::uint16_t(from + 1)));
            const std::uint8_t routing = at(std::uint16_t(from + 2));
            filter_mode_ = at(std::uint16_t(from + 3));
            filter_voice_ = std::uint8_t(v + 1);
            resonance_ = std::uint8_t(routing | fetch(v));
            break;
        }
        case 6: // $E7B2: tempo
            tempo_ = fetch(v);
            tempo_count_ = tempo_;
            break;
        default: { // 0 and 7 ($E5FD): note lengths from the table by style ($E6B6)
            int index = command & 0x0F;
            if (long_notes_) {
                index += 16;
            }
            const std::uint8_t length = at(std::uint16_t(lengths + index));
            std::uint8_t count, gap;
            if (voice.style == 2) {
                count = std::uint8_t(length - 1);
                gap = 0xFF;
            } else if (voice.style == 1) {
                count = std::uint8_t(length >> 1);
                gap = std::uint8_t(-count);
            } else {
                std::uint8_t released = std::uint8_t(length >> 3);
                if (released == 0) {
                    released = 1;
                }
                count = std::uint8_t(length - released);
                gap = std::uint8_t(-released);
            }
            voice.length = count;
            voice.next_gap = gap;
            break;
        }
        }
    }
}

// $E49E: once per frame from the title's interrupt.
void MusicPlayer::frame(c64::Sid& sid) {
    if (!playing_) {
        return;
    }
    if (ended_) {
        if (end_action_ == 0) {
            silence(sid);
            return;
        }
        end_of_tune(sid, end_action_);
        // The frame that starts the next tune plays no tick: so the
        // original's SID writes show (its first note comes two frames later,
        // compared over the whole title). The listing reads as if $E4B8's
        // JMP $E49E would tick at once; why it does not is not understood.
        return;
    }
    if (--tempo_count_ != 0) {
        return;
    }
    tempo_count_ = tempo_;
    for (int v = 0; v < 3; ++v) {
        Voice& voice = voices_[std::size_t(v)];
        --voice.count; // wraps as a byte; bit 7 is the sign the code tests
        if (voice.count & 0x80) {
            if (voice.count != voice.gap) {
                continue;
            }
            voice.count = voice.length;
            voice.gap = voice.next_gap;
            if (filter_voice_ == v + 1) {
                apply_filter(sid);
            }
            next_note(sid, v);
        } else if (voice.count == 0 && !voice.legato) {
            sid.write(register_base[std::size_t(v)] + 4, std::uint8_t(voice.waveform & 0xFE));
        }
    }
}

} // namespace bruce_lee::game
