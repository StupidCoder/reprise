#include "wizball_c64/game/music.hpp"
#include <algorithm>

namespace wizball::game {
namespace {
// Instrument fields (the stream's numbering, $87 + n for the first voice).
constexpr int freq_mode = 0x0D, pulse_mode = 0x11;
constexpr int pulse_lo = 0x16, pulse_hi = 0x17, waveform = 0x18, attack_decay = 0x19, sustain_release = 0x1A;
constexpr int gate_time = 0x1B, release_time = 0x1C;
// Working fields ($469D + n): 0-7 the four frequency steps, 8-11 their
// lengths (arpeggio: table address, base note, loop index), 12 the delay
// (arpeggio: index), 13 the mode; then the pulse sweep: 14/15 lengths, 16
// delay, 17 mode, 18-21 steps, 22/23 start; 24/25 the note's frequency;
// 26 the waveform; 27 gate time; 28 release time; 29-32 the frequency
// step counters; 33/34 the pulse step counters.
constexpr int w_mode = 0x0D, w_pulse_len = 0x0E, w_pulse_delay = 0x10, w_pulse_mode = 0x11, w_pulse_step = 0x12;
constexpr int w_pulse_start = 0x16, w_note_freq = 0x18, w_wave = 0x1A, w_gate = 0x1B, w_release = 0x1C;
constexpr int w_freq_count = 0x1D, w_pulse_count = 0x21;
constexpr int sid_base[3] = {0x00, 0x07, 0x0E};
constexpr std::uint16_t command_tables[3] = {0x4600, 0x462A, 0x4654};
} // namespace

Music::Music(const data::GameImage& image) : image_(image) {}

Music::Command Music::command_of(int voice, std::uint8_t code) const {
    const int index = (code - 0xC0) / 2;
    const auto handler = image_.word(std::uint16_t(command_tables[voice] + 2 * index));
    switch (handler) {
    case 0x4A09: case 0x4B2A: case 0x4C6E: return Command::end;
    case 0x48FF: case 0x4A4B: case 0x4B6C: return Command::call;
    case 0x49B4: case 0x4AEF: case 0x4C21: return Command::jump;
    case 0x48F9: case 0x4A45: case 0x4B66: return Command::transpose_call;
    case 0x49B1: case 0x4AEC: return Command::transpose_jump;
    case 0x49E9: case 0x4B08: case 0x4C4B: return Command::set_field;
    case 0x4981: case 0x4ABA: case 0x4BEE: return Command::loop;
    case 0x49F2: case 0x4B12: case 0x4C54: return Command::next;
    case 0x4967: case 0x4A9F: case 0x4BD4: return Command::instrument;
    case 0x4A31: case 0x4B52: case 0x4CA3: return Command::envelope;
    case 0x4A20: case 0x4B48: case 0x4C86: return Command::set_work;
    case 0x491D: case 0x4A69: case 0x4B8C: return Command::routine;
    case 0x4A2A: case 0x4B41: case 0x4C9C: return Command::transpose;
    case 0x492F: case 0x4A7B: case 0x4B9E: return Command::set_field2;
    case 0x493F: case 0x4A8D: case 0x4BAE: return Command::set_work2;
    case 0x49DA: case 0x4C42: return Command::filter_on;
    case 0x4951: case 0x4BBE: return Command::filter_params;
    case 0x49C1: case 0x4C2E: return Command::filter_off;
    case 0x49E1: case 0x4AFC: return Command::arpeggio5;
    case 0x49DE: return Command::arpeggio7;
    case 0x499B: case 0x4AD5: case 0x4C0B: return Command::modulation;
    case 0x4C8F: return Command::tempo;
    default: return Command::invalid;
    }
}

void Music::build_durations() {
    // $4834: entry n (from 1) is n * tempo, in eight bits.
    std::uint8_t a = 0;
    for (int n = 1; n <= 0x20; ++n) {
        a = std::uint8_t(a + tempo_);
        durations_[std::size_t(n)] = a;
    }
}

void Music::reset(c64::Sid& sid) {
    // $47D0
    for (int r = 0x17; r >= 0; --r) {
        sid.write(r, 8);
        sid.write(r, 0);
    }
    for (auto& v : voices_) {
        v.work[w_release] = 0;
        v.active = false;
    }
    filter_work_[0x0D] = 0; // the filter sweep's mode ($4E)
    filter_owner_ = 0xFF;
}

void Music::start(int tune) {
    // $45F1, $47F2
    filter_mode_ = tune == 0x1A ? 0x70 : 0x10;
    tempo_ = image_.byte(std::uint16_t(0x5526 + tune));
    for (int k = 2; k >= 0; --k) {
        auto& v = voices_[std::size_t(k)];
        v.pointer = image_.word(std::uint16_t(0x5520 + tune + 2 * k));
        v.transpose = 0;
        // $4818: the modes are cleared at $0094/$0098 + $4717[k]; for the
        // second voice that lands in the stack page, so its modes stay.
        if (k != 1) {
            v.instrument[freq_mode] = 0;
            v.instrument[pulse_mode] = 0;
        }
        v.depth = 4;
        v.duration = 1;
        v.active = true;
    }
    build_durations();
}

void Music::filter_route(c64::Sid& sid, std::uint8_t routing, int owner) {
    sid.write(0x17, routing);
    filter_owner_ = std::uint8_t(owner);
}

void Music::write_cutoff(c64::Sid& sid) {
    sid.write(0x15, std::uint8_t(cutoff_ & 7));
    sid.write(0x16, std::uint8_t(cutoff_ >> 3));
}

void Music::filter_reset(c64::Sid& sid) {
    // $4D3B: the sweep from its parameters, from its start.
    for (int i = 0; i < 16; ++i) filter_work_[std::size_t(i)] = filter_params_[std::size_t(i)];
    cutoff_ = std::uint16_t(filter_work_[0x0E] | filter_work_[0x0F] << 8);
    for (int i = 0; i < 4; ++i) filter_count_[std::size_t(i)] = filter_work_[std::size_t(8 + i)];
    write_cutoff(sid);
}

void Music::filter_sweep(c64::Sid& sid) {
    // $4CB7
    const auto mode = filter_work_[0x0D];
    if (mode == 0) return;
    auto& delay = filter_work_[0x0C];
    auto step = [&](int k) {
        cutoff_ = std::uint16_t(cutoff_ + (filter_work_[std::size_t(2 * k)] | filter_work_[std::size_t(2 * k + 1)] << 8));
    };
    if (delay != 0) {
        --delay;
        if (mode & 2) {
            step(3);
            write_cutoff(sid);
        }
        return;
    }
    for (int guard = 0; guard < 4; ++guard) {
        for (int k = 0; k < 4; ++k) {
            if (filter_count_[std::size_t(k)] != 0) {
                --filter_count_[std::size_t(k)];
                step(k);
                write_cutoff(sid);
                return;
            }
        }
        if ((mode & 0x81) == 0) break;
        if (mode & 0x80) cutoff_ = std::uint16_t(filter_work_[0x0E] | filter_work_[0x0F] << 8);
        for (int i = 0; i < 4; ++i) filter_count_[std::size_t(i)] = filter_work_[std::size_t(8 + i)];
    }
    write_cutoff(sid);
}

void Music::note(c64::Sid& sid, int k, std::uint8_t byte, std::uint8_t length) {
    auto& v = voices_[std::size_t(k)];
    const int s = sid_base[k];
    int a = byte >= 0x60 ? byte - 0x60 : byte;
    if (a != 0x5F) { // $5F: a rest
        const int index = (a + v.transpose) & 0xFF;
        if ((k == 0 && filter_owner_ == 0) || (k == 2 && filter_owner_ == 2)) filter_reset(sid);
        if (k != 0) sid.write(s + 4, 8); // the second and third voices restart the oscillator
        const auto lo = image_.byte(std::uint16_t(0x471A + index)), hi = image_.byte(std::uint16_t(0x4775 + index));
        v.work[w_note_freq] = lo;
        v.work[w_note_freq + 1] = hi;
        sid.write(s + 0, lo);
        sid.write(s + 1, hi);
        const auto plo = v.instrument[pulse_lo], phi = v.instrument[pulse_hi];
        sid.write(s + 2, plo);
        sid.write(s + 3, phi);
        sid.write(s + 5, v.instrument[attack_decay]);
        sid.write(s + 6, v.instrument[sustain_release]);
        v.work[w_wave] = v.instrument[waveform];
        sid.write(s + 4, std::uint8_t(v.instrument[waveform] & 0xF7));
        // $4848: the pulse from the instrument; its step counters from the
        // working lengths (the last note's, unless this one has a sweep).
        v.pulse = std::uint16_t(plo | phi << 8);
        v.work[w_pulse_count] = v.work[w_pulse_len];
        v.work[w_pulse_count + 1] = v.work[w_pulse_len + 1];
        v.work[w_pulse_mode] = v.instrument[pulse_mode];
        if (v.work[w_pulse_mode] != 0) {
            v.work[w_pulse_start] = plo;
            v.work[w_pulse_start + 1] = phi;
            for (int i = 0; i < 4; ++i) v.work[std::size_t(w_pulse_step + i)] = v.instrument[std::size_t(0x12 + i)];
            v.work[w_pulse_delay] = v.instrument[0x10];
            v.work[w_pulse_len] = v.instrument[0x0E];
            v.work[w_pulse_len + 1] = v.instrument[0x0F];
            v.work[w_pulse_count] = v.instrument[0x0E];
            v.work[w_pulse_count + 1] = v.instrument[0x0F];
        }
        v.work[w_mode] = v.instrument[freq_mode];
        if (v.work[w_mode] != 0) {
            for (int i = 0; i <= 0x0C; ++i) v.work[std::size_t(i)] = v.instrument[std::size_t(i)];
            bool reset = true;
            if (v.work[w_mode] & 8) {
                // $4E4D: the arpeggio's base note (tied notes come out two higher)
                int b = byte;
                int base = b < 0x60 ? b + v.transpose : (b - 0x5F) + v.transpose + 1;
                v.work[0x0A] = std::uint8_t(base);
                reset = v.work[0x0A] == 0;
            }
            if (reset) {
                v.frequency = std::uint16_t(v.work[w_note_freq] | v.work[w_note_freq + 1] << 8);
                for (int i = 0; i < 4; ++i) v.work[std::size_t(w_freq_count + i)] = v.work[std::size_t(8 + i)];
            }
        }
        v.work[w_gate] = v.instrument[gate_time];
        v.work[w_release] = v.instrument[release_time];
    }
    // $4E69: a note's length indexes the tempo table ($46E2 + n; entry 0 is
    // the byte before it, the second voice's last pulse counter); a tied
    // note's is the length itself.
    if (byte >= 0x60) v.duration = length;
    else if (length == 0) v.duration = voices_[1].work[w_pulse_count + 1];
    else v.duration = durations_[std::size_t(std::min<int>(length, 0x20))];
    v.pointer = std::uint16_t(v.pointer + 2);
}

void Music::sequence(c64::Sid& sid, int k) {
    // $4D4E: when the note runs out, read on to the next note.
    auto& v = voices_[std::size_t(k)];
    if (!v.active) return;
    if (--v.duration != 0) return;
    auto& transpose = v.transpose;
    for (int guard = 0; guard < 256; ++guard) {
        const auto code = image_.byte(v.pointer);
        const auto a1 = image_.byte(std::uint16_t(v.pointer + 1));
        const auto a2 = image_.byte(std::uint16_t(v.pointer + 2));
        const auto a3 = image_.byte(std::uint16_t(v.pointer + 3));
        if (code < 0xC0) {
            note(sid, k, code, a1);
            return;
        }
        auto push = [&](std::uint16_t back) {
            v.return_to[v.depth] = back;
            --v.depth;
        };
        switch (command_of(k, code)) {
        case Command::end:
            if (v.depth == 4) {
                v.active = false;
                return;
            }
            ++v.depth;
            v.pointer = v.return_to[v.depth];
            break;
        case Command::call:
            push(std::uint16_t(v.pointer + 3));
            v.pointer = std::uint16_t(a1 | a2 << 8);
            break;
        case Command::transpose_call:
            transpose = a1;
            push(std::uint16_t(v.pointer + 4));
            v.pointer = std::uint16_t(a2 | a3 << 8);
            break;
        case Command::jump: v.pointer = std::uint16_t(a1 | a2 << 8); break;
        case Command::transpose_jump:
            transpose = a1;
            v.pointer = std::uint16_t(a2 | a3 << 8);
            break;
        case Command::set_field:
            if (a1 < v.instrument.size()) v.instrument[a1] = a2;
            v.pointer = std::uint16_t(v.pointer + 3);
            break;
        case Command::set_field2:
            if (a1 + 1u < v.instrument.size()) {
                v.instrument[a1] = a2;
                v.instrument[a1 + 1u] = a3;
            }
            v.pointer = std::uint16_t(v.pointer + 4);
            break;
        case Command::set_work:
            if (a1 < v.work.size()) v.work[a1] = a2;
            v.pointer = std::uint16_t(v.pointer + 3);
            break;
        case Command::set_work2:
            if (a1 + 1u < v.work.size()) {
                v.work[a1] = a2;
                v.work[a1 + 1u] = a3;
            }
            v.pointer = std::uint16_t(v.pointer + 4);
            break;
        case Command::loop:
            v.return_to[v.depth] = std::uint16_t(v.pointer + 2);
            v.count[v.depth] = a1;
            --v.depth;
            v.pointer = std::uint16_t(v.pointer + 2);
            break;
        case Command::next:
            if (--v.count[std::size_t(v.depth + 1)] != 0) {
                v.pointer = v.return_to[std::size_t(v.depth + 1)];
            } else {
                ++v.depth;
                v.pointer = std::uint16_t(v.pointer + 1);
            }
            break;
        case Command::instrument: { // the first n+1 fields from a table
            const auto from = std::uint16_t(a2 | a3 << 8);
            for (int i = a1; i >= 0; --i)
                if (i < int(v.instrument.size())) v.instrument[std::size_t(i)] = image_.byte(std::uint16_t(from + i));
            v.pointer = std::uint16_t(v.pointer + 4);
            break;
        }
        case Command::envelope: { // waveform, envelope, gate and release from a table
            const auto from = std::uint16_t(a1 | a2 << 8);
            for (int i = 4; i >= 0; --i) v.instrument[std::size_t(waveform + i)] = image_.byte(std::uint16_t(from + i));
            v.pointer = std::uint16_t(v.pointer + 3);
            break;
        }
        case Command::modulation: { // the frequency sweep's fourteen fields
            const auto from = std::uint16_t(a1 | a2 << 8);
            for (int i = 0x0D; i >= 0; --i) v.instrument[std::size_t(i)] = image_.byte(std::uint16_t(from + i));
            v.pointer = std::uint16_t(v.pointer + 3);
            break;
        }
        case Command::routine: {
            // A routine of the game's code, by address (only these two occur).
            const auto at = std::uint16_t(a1 | a2 << 8);
            if (at == 0x5AFF) { // all three voices through the filter, its sweep restarted
                filter_route(sid, 0xF7, 3);
                sid.write(0x18, 0x1F);
                filter_reset(sid);
            } else if (at == 0x5F2E) {
                filter_owner_ = 3;
            }
            v.pointer = std::uint16_t(v.pointer + 3);
            break;
        }
        case Command::transpose:
            transpose = a1;
            v.pointer = std::uint16_t(v.pointer + 2);
            break;
        case Command::filter_on:
            if (k == 0) {
                filter_route(sid, 0xF1, 0);
                sid.write(0x18, 0x1F);
            } else {
                sid.write(0x18, 0x1F);
                filter_route(sid, 0xF4, 2);
            }
            v.pointer = std::uint16_t(v.pointer + 1);
            break;
        case Command::filter_params: {
            const auto from = std::uint16_t(a1 | a2 << 8);
            for (int i = 0; i < 16; ++i) filter_params_[std::size_t(i)] = image_.byte(std::uint16_t(from + i));
            v.pointer = std::uint16_t(v.pointer + 3);
            break;
        }
        case Command::filter_off:
            filter_work_[0x0D] = 0;
            filter_route(sid, 0xF0, 3);
            if (k == 0) sid.write(0x18, 0x1F);
            v.pointer = std::uint16_t(v.pointer + 1);
            break;
        case Command::arpeggio5:
            v.instrument[freq_mode] = 5;
            v.pointer = std::uint16_t(v.pointer + 1);
            break;
        case Command::arpeggio7:
            v.instrument[freq_mode] = 7;
            v.pointer = std::uint16_t(v.pointer + 1);
            break;
        case Command::tempo:
            tempo_ = a1;
            build_durations();
            v.pointer = std::uint16_t(v.pointer + 2);
            break;
        case Command::invalid:
            v.active = false;
            return;
        }
    }
}

void Music::sweeps(c64::Sid& sid, int k) {
    // $50F4: the gate and release times, then the pulse and frequency sweeps.
    auto& v = voices_[std::size_t(k)];
    auto& w = v.work;
    const int s = sid_base[k];
    if (w[w_release] == 0) return;
    bool gate_done = false;
    if (w[w_wave] & 8) { // gate off once the note has less left than the gate time
        if (v.duration < w[w_gate]) {
            w[w_gate] = 0;
            w[w_wave] &= 0xF6;
            if (w[w_wave] != 0) {
                sid.write(s + 4, w[w_wave]);
                gate_done = true;
            }
        } else {
            gate_done = true;
        }
    }
    if (!gate_done) {
        if (w[w_gate] != 0) {
            if (w[w_gate] != 0xFF && --w[w_gate] == 0) sid.write(s + 4, std::uint8_t(w[w_wave] & 0xF6));
        } else if (w[w_release] != 0xFF && --w[w_release] == 0) {
            for (int r = 0; r <= 6; ++r) sid.write(s + r, 0);
            if ((k == 0 && filter_owner_ == 0) || (k == 2 && filter_owner_ == 2)) filter_work_[0x0D] = 0;
            return;
        }
    }
    // Pulse width: two steps, then reload (bit 0) or restart (bit 7).
    if (w[w_pulse_mode] != 0) {
        if (w[w_pulse_delay] != 0) {
            --w[w_pulse_delay];
        } else {
            for (int guard = 0; guard < 4; ++guard) {
                if (w[w_pulse_count] != 0) {
                    --w[w_pulse_count];
                    v.pulse = std::uint16_t(v.pulse + (w[w_pulse_step] | w[w_pulse_step + 1] << 8));
                    break;
                }
                if (w[w_pulse_count + 1] != 0) {
                    --w[w_pulse_count + 1];
                    v.pulse = std::uint16_t(v.pulse + (w[w_pulse_step + 2] | w[w_pulse_step + 3] << 8));
                    break;
                }
                const auto mode = w[w_pulse_mode] & 0x81;
                if (mode == 0) break;
                if (mode & 0x80) v.pulse = std::uint16_t(w[w_pulse_start] | w[w_pulse_start + 1] << 8);
                w[w_pulse_count] = w[w_pulse_len];
                w[w_pulse_count + 1] = w[w_pulse_len + 1];
            }
            sid.write(s + 2, std::uint8_t(v.pulse));
            sid.write(s + 3, std::uint8_t(v.pulse >> 8));
        }
    }
    // Frequency: an arpeggio table, or four steps with a delay.
    const auto mode = w[w_mode];
    if (mode == 0) return;
    if (mode & 8) {
        if (--w[4] != 0) return;
        w[4] = w[6];
        std::uint8_t y = w[0x0C];
        if (y & 0x80) y = w[0x0B];
        const auto table = std::uint16_t(w[8] | w[9] << 8);
        const int n = std::uint8_t(w[0x0A] + image_.byte(std::uint16_t(table + y)));
        w[0x0C] = std::uint8_t(y - 1);
        sid.write(s + 0, image_.byte(std::uint16_t(0x471A + n)));
        sid.write(s + 1, image_.byte(std::uint16_t(0x4775 + n)));
        return;
    }
    auto add = [&](int step) {
        v.frequency = std::uint16_t(v.frequency + (w[std::size_t(2 * step)] | w[std::size_t(2 * step + 1)] << 8));
    };
    if (w[0x0C] != 0) {
        --w[0x0C];
        if (!(mode & 2)) return;
        add(3);
    } else {
        bool stepped = false;
        for (int guard = 0; guard < 4 && !stepped; ++guard) {
            for (int i = 0; i < 4; ++i) {
                if (w[std::size_t(w_freq_count + i)] != 0) {
                    --w[std::size_t(w_freq_count + i)];
                    add(i);
                    stepped = true;
                    break;
                }
            }
            if (stepped) break;
            const auto m = mode & 0x81;
            if (m == 0) break;
            if (m & 0x80) v.frequency = std::uint16_t(w[w_note_freq] | w[w_note_freq + 1] << 8);
            for (int i = 0; i < 4; ++i) w[std::size_t(w_freq_count + i)] = w[std::size_t(8 + i)];
        }
    }
    sid.write(s + 0, std::uint8_t(v.frequency));
    sid.write(s + 1, std::uint8_t(v.frequency >> 8));
}

void Music::tick(c64::Sid& sid, int first) {
    for (int k = first; k < 3; ++k) {
        sweeps(sid, k);
        sequence(sid, k);
    }
    filter_sweep(sid);
}

} // namespace wizball::game
