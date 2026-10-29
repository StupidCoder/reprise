#include "spindizzy_worlds_amiga/sound/music.hpp"

namespace spindizzy::sound {
namespace {

constexpr std::uint32_t base = 0x432A8;
constexpr std::uint32_t tunes = 0x4429A;        // 16 bytes: 4 sequence offsets, 8 tempo counts
constexpr std::uint32_t sequences = 0x442DA;
constexpr std::uint32_t pattern_offsets = 0x43D5C; // words from $43DEE
constexpr std::uint32_t patterns = 0x43DEE;
constexpr std::uint32_t periods = 0x43A20;      // a word per note
constexpr std::uint32_t instruments = 0x43C7C;  // 16 bytes
constexpr std::uint32_t envelopes = 0x43BDC;    // 16 bytes: (ticks, change, repeats) triples
constexpr std::uint32_t sample_offsets = 0x43ACA; // longs from $4445C
constexpr std::uint32_t sample_loops = 0x43B3A;   // 6 bytes: length, repeat offset, repeat length
constexpr std::uint32_t samples = 0x4445C;
constexpr std::uint32_t vibrato_depths = 0x4397A; // $4390E + $6C, by the instrument's byte $E
constexpr std::uint32_t vibrato_shifts = 0x4398D; // $4390E + $7F
constexpr std::uint32_t first_positions = 0x43A1C; // $439A0 + $7C
// The state blocks: $4390E (Paula's side) and $439A0.

} // namespace

MusicPlayer::MusicPlayer(std::vector<std::uint8_t> file) : file_(std::move(file)) {
    memory_.assign(file_.begin(), file_.end());
    load_state(std::span<const std::uint8_t>(file_).subspan(0x4390E - base, 0x112));
}

void MusicPlayer::load_state(std::span<const std::uint8_t> region) {
    // The state blocks $4390E (Paula's side) and $439A0 (+ $92), as a file
    // or a recording holds them.
    auto r = [&](int o) { return int(region[std::size_t(o)]); };
    auto rw = [&](int o) { return r(o) << 8 | r(o + 1); };
    auto rl = [&](int o) { return std::uint32_t(rw(o)) << 16 | std::uint32_t(rw(o + 2)); };
    for (int n = 0; n < 4; ++n) {
        auto& v = voices_[std::size_t(n)];
        const auto c = std::uint32_t(n);
        auto at = [&](int offset) { return r(0x92 + offset + int(c)); };
        v.off = at(0x02) & 0x80;
        v.position = at(0x18); v.row = at(0x1C); v.repeats = at(0x20); v.pattern = at(0x24);
        v.in_block = at(0x2C) & 0x80; v.block_count = at(0x30); v.block_start = at(0x34);
        v.countdown = at(0x3C); v.sample = at(0x40); v.note = at(0x44); v.held = at(0x48) & 0x80;
        v.instrument = at(0x4C); v.slide = at(0x50); v.vibrato_down = at(0x54) & 0x80; v.delay = at(0x58);
        v.vibrato_init = at(0x5C); v.vibrato_back = at(0x60) & 0x80; v.vibrato_count = at(0x64);
        v.vibrato_depth = at(0x68); v.note_byte = at(0x6C); v.flags = at(0x70); v.phase = at(0x74);
        v.tick = at(0x78);
        v.sequence = rw(0x92 + 8 + int(c) * 2);
        v.start = rl(int(c) * 16); v.length = rw(int(c) * 16 + 4);
        v.repeat = rl(int(c) * 16 + 6); v.repeat_length = rw(int(c) * 16 + 0x0A);
        v.volume = rw(int(c) * 16 + 0x0C);
        v.envelope = rw(0x40 + int(c) * 2); v.env_position = r(0x48 + int(c));
        v.env_countdown = r(0x4C + int(c)); v.env_repeats = r(0x50 + int(c));
        v.vibrato_step = rw(0x5C + int(c) * 2); v.period = rw(0x64 + int(c) * 2);
    }
    const int fade = r(0x92);
    fade_period = fade >= 0x80 ? -1 : fade;
    volume_ = rw(0x92 + 6);
    fade_countdown_ = r(0x92 + 0x28);
    tempo_countdown_ = r(0x92 + 0x29);
    tempo_index_ = r(0x92 + 0x2A);
    for (int i = 0; i < 8; ++i) tempo_[std::size_t(i)] = r(0x92 + 0x10 + i);
    playing_ = r(0x92 + 1) & 0x80;
}

int MusicPlayer::period_of(int note) const { return word(periods + std::uint32_t(note & 0xFF) * 2); }

void MusicPlayer::start(int tune, amiga::PaulaChannels& paula) {
    const std::uint32_t header = tunes + std::uint32_t(tune & 0x0F) * 16;
    for (int i = 0; i < 8; ++i) tempo_[std::size_t(i)] = byte(header + 8 + std::uint32_t(i));
    for (int n = 3; n >= 0; --n) { // $432C8: only these are reset
        auto& v = voices_[std::size_t(n)];
        v.sequence = word(header + std::uint32_t(n) * 2);
        v.off = false;
        v.in_block = false;
        v.row = 0;
        v.countdown = 1;
        v.instrument = 0xFF;
        v.position = byte(first_positions + std::uint32_t(n));
        next_sequence(v);
    }
    tempo_countdown_ = fade_countdown_ = 0;
    fade_period = -1;
    volume_ = 0x40;
    playing_ = true;
    tune_ = tune;
    for (int n = 0; n < 4; ++n) paula.set_volume(n, 0);
}

void MusicPlayer::stop(amiga::PaulaChannels& paula) {
    playing_ = false;
    for (int n = 0; n < 4; ++n) paula.set_volume(n, 0);
    paula.write_dmacon(0x000F);
}

void MusicPlayer::next_sequence(Voice& v) {
    // $43876: $FD n sets the fade, $FE / $FF n a stop or a jump (to n),
    // a byte with bit 6 opens a block repeated (byte & $3F) times, else a
    // pair (pattern repeats, pattern).
    const std::uint32_t at = sequences + std::uint32_t(v.sequence);
    for (int guard = 0; guard < 512; ++guard) {
        v.position = (v.position + 1) & 0xFF;
        const int b = byte(at + std::uint32_t(v.position));
        if (b >= 0xFD) {
            if (b == 0xFD) {
                v.position = (v.position + 1) & 0xFF;
                const int period = byte(at + std::uint32_t(v.position));
                fade_period = period >= 0x80 ? -1 : period;
                continue;
            }
            v.pattern = b;
            v.position = (byte(at + std::uint32_t(v.position) + 1) - 1) & 0xFF;
            return;
        }
        int pair = v.position;
        if (b & 0x40) {
            if (!v.in_block) {
                v.in_block = true;
                v.block_count = b & 0x3F;
                v.position = (v.position + 1) & 0xFF;
                v.block_start = v.position;
                pair = v.position;
            } else if (--v.block_count != 0) {
                v.position = v.block_start;
                pair = v.position;
            } else {
                v.in_block = false;
                continue;
            }
        }
        v.repeats = byte(at + std::uint32_t(pair)); // $438FA
        v.position = (pair + 1) & 0xFF;
        v.pattern = byte(at + std::uint32_t(pair) + 1);
        return;
    }
}

void MusicPlayer::new_row(int n, Voice& v, amiga::PaulaChannels& paula) {
    const std::uint32_t at = patterns + word(pattern_offsets + std::uint32_t(v.pattern) * 2);
    instrument_change_ = -1;
    const int b = byte(at + std::uint32_t(v.row));
    v.countdown = b & 0x3F;
    v.slide = 0;
    v.held = false;
    if (b & 0x40) { // the envelope taken up again at its position 9
        v.env_countdown = 1;
        v.env_repeats = 1;
        v.env_position = 9;
    }
    if (b & 0x80) {
        v.row = (v.row + 1) & 0xFF;
        const int c = byte(at + std::uint32_t(v.row));
        if (c < 0x80) instrument_change_ = c;
        else v.slide = c;
    }
    v.row = (v.row + 1) & 0xFF;
    v.note_byte = byte(at + std::uint32_t(v.row));
    v.note = v.note_byte & 0x7F;
    v.period = period_of(v.note);
    if (instrument_change_ >= 0 && instrument_change_ != v.instrument) {
        v.instrument = instrument_change_;
        v.note_byte = 0; // a new instrument always sounds
    }
    if (!(v.note_byte & 0x80) && v.instrument != 0xFF) { // not tied: the instrument's sample from its start
        const std::uint32_t inst = instruments + std::uint32_t(v.instrument) * 16;
        v.flags = byte(inst + 2);
        v.phase = byte(inst + 7);
        v.delay = byte(inst + 0x0F);
        v.sample = byte(inst);
        const std::uint32_t loop = sample_loops + std::uint32_t(v.sample) * 6;
        v.start = samples + longword(sample_offsets + std::uint32_t(v.sample) * 4);
        v.length = word(loop);
        v.repeat = v.start + word(loop + 2);
        v.repeat_length = word(loop + 4);
        paula.write_dmacon(std::uint16_t(1 << n));
        paula.set_period(n, std::uint16_t(v.period));
        paula.set_location(n, v.start - base);
        paula.set_length(n, std::uint16_t(v.length));
    }
    v.row = (v.row + 1) & 0xFF;
    if (byte(at + std::uint32_t(v.row)) == 0xFF) { // the pattern's end
        v.row = 0;
        v.repeats = (v.repeats - 1) & 0xFF;
        if (v.repeats == 0) next_sequence(v);
    }
}

void MusicPlayer::effects(int, Voice& v, bool after_new_row) {
    const std::uint32_t inst = instruments + std::uint32_t(v.instrument & 0xFF) * 16;
    auto arpeggio = [&](int index) { // $436EE
        int value = byte(inst + 9 + std::uint32_t(index));
        int note;
        if (value < 0x80) note = value;
        else if ((value &= 0x7F) < 0x40) note = (value + v.note) & 0xFF;
        else note = v.note - (value & 0x3F);
        v.period = period_of(note);
    };
    bool arpeggio_done = false;
    if (!after_new_row) {
        if (v.delay != 0) {
            --v.delay;
        } else if (v.phase == 0 && byte(inst + 0x0E) != 0) { // vibrato ($43566)
            const int kind = byte(inst + 0x0E);
            if (!(v.vibrato_init & 0x80)) {
                v.vibrato_depth = byte(vibrato_depths + std::uint32_t(kind));
                v.vibrato_init = byte(vibrato_shifts + std::uint32_t(kind));
                v.vibrato_back = false;
                v.vibrato_count = 0;
                v.period = period_of(v.note);
                v.vibrato_step = (period_of(v.note) - period_of(v.note + 1)) & 0xFFFF;
                for (;;) {
                    v.vibrato_init = (v.vibrato_init - 1) & 0xFF;
                    if (v.vibrato_init & 0x80) break;
                    v.vibrato_step >>= 1;
                    if (v.vibrato_step == 0) {
                        v.vibrato_step = 1;
                        v.vibrato_init = 0xFF;
                        break;
                    }
                }
            } else {
                v.period = (v.period + (v.vibrato_down ? -v.vibrato_step : v.vibrato_step)) & 0xFFFF;
                if (!v.vibrato_back) {
                    if (++v.vibrato_count >= v.vibrato_depth) {
                        v.vibrato_back = true;
                        v.vibrato_down = !v.vibrato_down;
                    }
                } else if (--v.vibrato_count == 0) {
                    v.vibrato_back = false;
                }
            }
        }
        if (v.slide != 0) { // $4360E
            if (v.slide >= 0xC0) v.period = (v.period + (v.slide & 0x3F)) & 0xFFFF;
            else v.period = (v.period - (v.slide & 0x3F)) & 0xFFFF;
        }
        if (v.flags & 2) { // a second note every other tick ($43632)
            int note = v.note;
            if (v.tick & 1) {
                const int d = byte(inst + 0x0D);
                note = d >= 0x80 ? ((d & 0x7F) + v.note) & 0xFF : d;
            }
            v.period = period_of(note);
        }
        if (v.flags & 4) { // $4366A
            v.sample = (v.tick & 1) ? 2 : byte(inst);
            const int bend = byte(inst + 4);
            v.period = (v.period + bend) & 0xFFFF;
            if (v.period & 0x8000) v.period = (v.period - bend) & 0xFFFF;
        }
        if (v.flags & 8) { // four notes in turn ($4369A)
            arpeggio(v.tick & 3);
            arpeggio_done = true;
        }
    }
    if (!arpeggio_done) {
        if (v.flags & 0x10) { // $436B0
            if (v.phase != 0) v.sample = byte(inst + 8);
            else if (!v.held) v.sample = byte(inst);
        }
        if (v.flags & 0x20) { // $436D6
            if (v.phase != 0) arpeggio(v.tick);
            else if (!v.held) v.period = period_of(v.note);
        }
    }
    if (!(v.note_byte & 0x80) && v.tick == 0) { // $43730: the envelope from its start
        v.envelope = byte(inst + 1) * 16;
        v.env_repeats = byte(envelopes + std::uint32_t(v.envelope) + 2);
        v.env_position = 0;
        v.env_countdown = 1;
        v.volume = 0;
    }
    envelope(v);
    if (v.phase == 0) v.held = true; // $437EA
    else --v.phase;
    v.tick = (v.tick + 1) & 0xFF;
}

void MusicPlayer::envelope(Voice& v) { // $43764, as the effects player's but $FF → 0
    if (v.env_countdown & 0x80) return;
    v.env_countdown = (v.env_countdown - 1) & 0xFF;
    if (v.env_countdown != 0) return;
    const std::uint32_t entry = envelopes + std::uint32_t(v.envelope);
    int volume = (v.volume + byte(entry + std::uint32_t(v.env_position) + 1)) & 0xFF;
    if (volume >= 0x41) volume = volume < 0xFF ? 0x40 : 0;
    v.volume = volume;
    v.env_repeats = (v.env_repeats - 1) & 0xFF;
    if (v.env_repeats != 0) {
        v.env_countdown = byte(entry + std::uint32_t(v.env_position));
        return;
    }
    v.env_position += 3;
    int ticks = byte(entry + std::uint32_t(v.env_position));
    if (ticks >= 0xC0) {
        v.env_position = ticks & 0x3F;
        ticks = byte(entry + std::uint32_t(v.env_position));
    }
    v.env_countdown = ticks;
    if (v.env_position != 0x0F) v.env_repeats = byte(entry + std::uint32_t(v.env_position) + 2);
}

void MusicPlayer::tick(amiga::PaulaChannels& paula) {
    if (!playing_) return;
    if (fade_period >= 0 && --fade_countdown_ < 0) { // $43364: the fade ($FD)
        fade_countdown_ = fade_period;
        if (--volume_ == 0) {
            stop(paula);
            return;
        }
    }
    for (int n = 3; n >= 0; --n) {
        auto& v = voices_[std::size_t(n)];
        bool row = false;
        if (tempo_countdown_ == 0 && --v.countdown == 0) { // $4338C
            row = true;
            if (v.pattern == 0xFE) { stop(paula); return; } // $433A0
            if (v.pattern == 0xFF) { v.row = 0; next_sequence(v); }
            new_row(n, v, paula);
        }
        if (v.off) continue;
        if (row) {
            v.tick = 0;
            v.vibrato_init = 0;
        }
        effects(n, v, row);
    }
    // $4380E: the channels on; the tempo's next count ($43816).
    for (int n = 0; n < 4; ++n)
        if (!voices_[std::size_t(n)].off && !paula.dma(n) && voices_[std::size_t(n)].length)
            paula.write_dmacon(std::uint16_t(0x8000 | (1 << n)));
    if (--tempo_countdown_ < 0) {
        if (--tempo_index_ < 0) tempo_index_ = 7;
        tempo_countdown_ = tempo_[std::size_t(tempo_index_)];
    }
    for (int n = 0; n < 4; ++n) { // $4383A
        const auto& v = voices_[std::size_t(n)];
        if (v.off || !v.length) continue;
        paula.set_location(n, v.repeat - base);
        paula.set_length(n, std::uint16_t(v.repeat_length));
        paula.set_period(n, std::uint16_t(v.period));
        paula.set_volume(n, std::uint16_t(v.volume * volume_ / 0x40));
    }
}

} // namespace spindizzy::sound
