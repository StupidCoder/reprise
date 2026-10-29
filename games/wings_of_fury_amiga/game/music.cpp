#include "wings_of_fury_amiga/game/music.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wings::game {
namespace {

constexpr std::uint32_t songplay_base = 0x180000;
constexpr std::uint32_t wofsongs_base = 0x190000;
constexpr std::uint32_t period_table = 0x180BC4;  // 72 longs
constexpr std::uint32_t duration_table = 0x180CE4; // 20 pairs of bytes
constexpr double cia_clock = 709379.0;            // PAL E clock
constexpr int lowest_note = 0x1B;
constexpr std::uint32_t vhdr_tag = 0x56484452, body_tag = 0x424F4459; // "VHDR", "BODY"

enum Command : int { next_entry = 0xD9, end_track = 0xDA, restart = 0xDB, instrument = 0xDC, tempo_high = 0xDD,
                     tempo_low = 0xDE, set_volume = 0xDF, set_hold = 0xE0 };
enum State : int { idle = 0, starting = 1, playing = 2, stopping = 3, fading = 4 };

} // namespace

MusicPlayer::MusicPlayer(const std::vector<std::uint8_t>& songplay, const std::vector<std::uint8_t>& wofsongs)
    : image_(amiga::read_load_file(wofsongs), wofsongs_base), player_(amiga::read_load_file(songplay), songplay_base) {
    const auto& player = player_;
    // The tracks as the data hunk holds them (Track0Info.. at $180D3A, $4A
    // bytes each): the fields not set when a song starts keep these.
    for (std::size_t n = 0; n < tracks_.size(); ++n) {
        const std::uint32_t at = 0x180D3A + std::uint32_t(n * 0x4A);
        auto& t = tracks_[n];
        t.active = player.u16(at) != 0;
        t.left = player.u16(at + 0xE);
        t.release = player.u16(at + 0x10);
        t.stage = player.s16(at + 0x12);
        t.volume = player.u16(at + 0x1E);
        t.period = player.u16(at + 0x20);
        t.loop_words = player.u16(at + 0x22);
        t.hold = player.u16(at + 0x38);
        t.tie = player.u16(at + 0x3A);
        t.note = player.u16(at + 0x40);
        t.slid = player.u16(at + 0x42);
        t.slide_step = player.s16(at + 0x44);
    }
    for (std::uint32_t a = image_.base(); a < image_.end(); ++a) memory_.push_back(image_.s8(a));
    songs_ = image_.hunk(1); // `songs`, the data hunk's start
    channels_.set_memory(memory_);
    channels_.block_started = [this](int channel) { block_started(channel); };
}

void MusicPlayer::play(int index) {
    // $180946 (`_ReadInstruments`): the song's voice table from +$10 (the
    // entries from +$14 to the first empty one set up: their 8SVX file's
    // VHDR and BODY found by scanning, and $53 / octaves).
    song_ = image_.u32(songs_ + std::uint32_t(index * 4));
    instrument_table_.clear();
    for (std::uint32_t a = song_ + 0x10; image_.contains(a, 4) && instrument_table_.size() < 16; a += 4) {
        instrument_table_.push_back(image_.u32(a));
    }
    for (std::size_t i = 1; i < instrument_table_.size() && instrument_table_[i] != 0; ++i) {
        const std::uint32_t voice = instrument_table_[i];
        const std::uint32_t file = image_.u32(voice + 0xA);
        if (file == 0) continue;
        Instrument in;
        in.address = voice;
        std::uint32_t a = file;
        while (image_.u32(a) != vhdr_tag) a += 2;
        in.vhdr = a + 8;
        a = file;
        while (image_.u32(a) != body_tag) a += 2;
        in.body = a + 8;
        const int octaves = std::max<int>(image_.u8(in.vhdr + 0xE), 1);
        in.notes_per_octave = 0x53 / octaves;
        known_[voice] = in;
    }
    // $18092A: `_PlaySong` only when none plays.
    if (state_ == idle && song_ != 0) state_ = starting;
}

void MusicPlayer::fade(int speed) {
    // $180084: (paused: stop at once.)
    if (state_ == idle) return;
    fade_count_ = fade_speed_ = speed;
    state_ = fading;
}

void MusicPlayer::stop() {
    if (state_ != idle) state_ = stopping;
}

void MusicPlayer::hardware_off() {
    // $1804DA: the audio interrupts and all four channels off.
    channels_.write_dmacon(0x000F);
}

// $1802A6, each CIA-B timer A interrupt.
void MusicPlayer::timer_tick() {
    switch (state_) {
    case idle: return;
    case stopping:
        state_ = idle;
        hardware_off();
        return;
    case fading:
        if (--fade_count_ < 0) {
            fade_count_ = fade_speed_;
            bool any = false;
            for (auto& t : tracks_) {
                if (t.volume != 0) {
                    --t.volume;
                    any = true;
                }
            }
            if (!any) {
                state_ = idle;
                hardware_off();
                return;
            }
        }
        break;
    case starting:
        start_tracks();
        state_ = playing;
        channels_.write_dmacon(0x000F);
        break;
    default: break;
    }
    for (int n = 0; n < 4; ++n) step_track(n);
    if (!alive_) {
        state_ = idle;
        hardware_off();
    }
}

// $18035A: each track from its list's first entry, volume $20.
void MusicPlayer::start_tracks() {
    for (int n = 0; n < 4; ++n) {
        auto& t = tracks_[std::size_t(n)];
        t.active = true;
        t.list = image_.u32(song_ + std::uint32_t(n * 4));
        t.pattern = image_.u32(t.list);
        t.entry = 0;
        t.left = 0;
        t.volume = 0x20;
    }
}

const MusicPlayer::Instrument* MusicPlayer::instrument_of(const Track& t) const {
    const auto it = known_.find(t.voice);
    return it == known_.end() ? nullptr : &it->second;
}

// $1804F4: one track's tick.
void MusicPlayer::step_track(int n) {
    auto& t = tracks_[std::size_t(n)];
    if (!t.active) return;
    const std::uint16_t bit = std::uint16_t(1u << n);
    int arpeggio = 0;
    if (t.left != 0) {
        const std::uint32_t v = t.voice;
        const bool arpeggio_on = v != 0 && word(v + 0x22) != 0;
        if (arpeggio_on) {
            auto& vs = voice_state_[v];
            const int step = vs.arpeggio_step++;
            arpeggio = std::int16_t(word(v + 0x24 + std::uint32_t((step & 3) * 2)));
        }
        if (--t.left == t.release) {
            // The note's release: the channel stops (unless held or tied).
            if (t.hold == 1 || t.tie != 0) return;
            channels_.write_dmacon(bit);
            channels_.set_volume(n, 0);
            channels_.set_period(n, 0x7C);
            t.stage = -1;
            return;
        }
        if (arpeggio_on) {
            int octave = 0;
            set_note(t, n, t.note + arpeggio, octave);
            return;
        }
        // The slide: the period swings between the voice's bounds.
        if (v == 0 || word(v + 0x1E) == 0) return;
        auto& vs = voice_state_[v];
        if (--vs.slide_counter > 0) return;
        vs.slide_counter = std::int16_t(word(v + 0x18));
        const int from = t.slid;
        const std::uint16_t distance = std::uint16_t(from - t.period);
        if (t.slide_step < 0) {
            if (!(distance > word(v + 0x14))) t.slide_step = -t.slide_step;
        } else if (distance > word(v + 0x12)) {
            t.slide_step = -t.slide_step;
        }
        t.slid = std::uint16_t(from + t.slide_step);
        channels_.set_period(n, std::uint16_t(t.slid));
        return;
    }
    for (int guard = 0; guard < 4096; ++guard) {
        const int b1 = image_.u8(t.pattern), b2 = image_.u8(t.pattern + 1);
        if (b1 < next_entry) {
            // A note: bit 7 ties it to the last; the duration from the table.
            if (b1 & 0x80) {
                if (t.tie == 0) t.tie = 2;
            } else {
                t.tie = 0;
            }
            const int note = (b1 & 0x7F) + std::int16_t(word(t.list + t.entry + 4));
            t.note = note;
            t.pattern += 2;
            const std::uint32_t at = duration_table + std::uint32_t(b2 * 2);
            const int length = player_.contains(at, 2) ? player_.u8(at) : 1;
            const int before = player_.contains(at, 2) ? player_.u8(at + 1) : 0;
            t.left = std::uint16_t(length - 1) & 0xFFFF;
            t.release = ((length - 1) - before) & 0xFF;
            const auto* in = instrument_of(t);
            if (in != nullptr) {
                bool sound = true;
                if (t.tie != 0) {
                    if (t.tie == 1) sound = false;
                    else --t.tie;
                }
                if (sound) {
                    int octave = 0;
                    set_note(t, n, note, octave);
                    // The sample: past the higher octaves' (one-shot + loop,
                    // doubling each octave down), then this octave's.
                    std::uint32_t start = in->body;
                    std::uint32_t both = image_.u32(in->vhdr) + image_.u32(in->vhdr + 4);
                    for (int k = 0; k < octave; ++k) {
                        start += both;
                        both <<= 1;
                    }
                    channels_.set_location(n, start - image_.base());
                    const std::uint32_t one_shot = image_.u32(in->vhdr) << octave;
                    t.loop_start = start + one_shot;
                    t.stage = 1;
                    const std::uint32_t total = (image_.u32(in->vhdr) + image_.u32(in->vhdr + 4)) << octave;
                    channels_.set_length(n, std::uint16_t(total >> 1));
                    t.loop_words = std::uint16_t((image_.u32(in->vhdr + 4) << octave) >> 1);
                    channels_.set_volume(n, std::uint16_t(t.volume));
                    channels_.write_dmacon(std::uint16_t(0x8000 | bit));
                }
            }
            alive_ = true;
            return;
        }
        switch (b1) {
        case next_entry:
            t.entry += 6;
            t.pattern = image_.u32(t.list + t.entry);
            continue;
        case restart:
            t.entry = 0;
            t.pattern = image_.u32(t.list);
            continue;
        case end_track:
            t.active = false;
            return;
        case instrument: {
            const std::size_t i = std::size_t(b2);
            t.voice = i < instrument_table_.size() ? instrument_table_[i] : 0;
            break;
        }
        case tempo_high: timer_latch_ = std::uint16_t((timer_latch_ & 0x00FF) | b2 << 8); break;
        case tempo_low: timer_latch_ = std::uint16_t((timer_latch_ & 0xFF00) | b2); break;
        case set_volume:
            if (state_ != fading) {
                channels_.set_volume(n, std::uint16_t(b2));
                t.volume = b2;
            }
            break;
        case set_hold: t.hold = b2; break;
        default: break;
        }
        t.pattern += 2;
    }
}

// $1807EA: the note's period. The octave whose samples suit it (the
// highest octave's samples are the shortest), and the period from the table
// over the samples per cycle in that octave.
void MusicPlayer::set_note(Track& t, int n, int note, int& octave) {
    const auto* in = instrument_of(t);
    if (in == nullptr) return;
    const int span = std::max(in->notes_per_octave, 1);
    octave = int(image_.u8(in->vhdr + 0xE)) - std::uint16_t(note) / span - 1;
    if (octave < 0) octave = 0;
    const std::uint32_t at = period_table + std::uint32_t((note - lowest_note) * 4);
    const std::uint32_t base = player_.contains(at, 4) ? player_.u32(at) : 0;
    const std::uint16_t cycle = std::uint16_t(image_.u32(in->vhdr + 8) << octave);
    const std::uint32_t quotient = cycle == 0 ? 0 : base / cycle;
    const std::uint16_t period = std::uint16_t(quotient);
    channels_.set_period(n, period);
    t.period = period;
    t.slid = period;
    if (t.voice != 0) voice_state_[t.voice].slide_counter = std::int16_t(word(t.voice + 0x1A));
    t.slide_step = std::int16_t(word(t.voice + 0x16));
}

// $180848: a channel took its block. A note's one-shot part playing: the
// loop is set up for the next block (or, without a loop, the next block
// stops the channel).
void MusicPlayer::block_started(int n) {
    auto& t = tracks_[std::size_t(n)];
    if (t.stage < 0) return;
    if (t.stage > 0) {
        if (t.loop_words != 0) {
            channels_.set_length(n, t.loop_words);
            channels_.set_location(n, t.loop_start - image_.base());
            t.stage = -1;
        } else {
            t.stage = 0;
        }
        return;
    }
    channels_.set_volume(n, 0);
    channels_.set_period(n, 0x7C);
    channels_.write_dmacon(std::uint16_t(1u << n));
    t.stage = -1;
}

void MusicPlayer::render(std::span<float> stereo, int rate) {
    std::size_t frame = 0;
    const std::size_t frames = stereo.size() / 2;
    while (frame < frames) {
        if (until_tick_ <= 0) {
            timer_tick();
            until_tick_ += std::max<double>(timer_latch_, 1) / cia_clock * rate;
        }
        const std::size_t chunk = std::min<std::size_t>(frames - frame, std::size_t(std::ceil(until_tick_)));
        channels_.render(stereo.subspan(frame * 2, chunk * 2), rate);
        frame += chunk;
        until_tick_ -= double(chunk);
    }
}

} // namespace wings::game
