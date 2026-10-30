#include "beach_volley_amiga/sound/soundtracker.hpp"
#include <algorithm>
#include <stdexcept>

namespace beach_volley::sound {
namespace {

constexpr std::size_t header_size = 600; // title 20, records 15 x 30, length, ignored byte, positions 128
constexpr std::size_t pattern_size = 1024;

std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t at) { return std::uint16_t(b[at] << 8 | b[at + 1]); }

} // namespace

Song Song::parse(std::span<const std::uint8_t> file) {
    // As many patterns as the positions name (the player's start finds the
    // highest of all 128, $30E6C); some files carry bytes after them
    // (music11, music12: 200).
    if (file.size() < header_size + pattern_size) throw std::runtime_error("A song file does not have the game's song layout.");
    int highest = 0;
    for (std::size_t i = 0; i < 128; ++i) highest = std::max(highest, int(file[472 + i]));
    const std::size_t size = header_size + std::size_t(highest + 1) * pattern_size;
    if (file.size() < size) throw std::runtime_error("A song file is cut short.");
    Song song;
    for (std::size_t i = 0; i < 20 && file[i] != 0; ++i) song.title.push_back(char(file[i]));
    for (std::size_t n = 0; n < 15; ++n) {
        const std::size_t at = 20 + n * 30 + 22; // after the 22-byte name
        song.samples[n] = {be16(file, at), be16(file, at + 2), be16(file, at + 4), be16(file, at + 6)};
    }
    song.length = file[470];
    for (std::size_t i = 0; i < 128; ++i) song.positions[i] = file[472 + i];
    song.patterns.assign(file.begin() + header_size, file.begin() + std::ptrdiff_t(size));
    if (song.length == 0) throw std::runtime_error("A song file has no positions.");
    return song;
}

PlayerTables read_player_tables(const data::CodeFile& code, std::uint32_t samples, std::uint32_t periods,
                                std::size_t bank_size) {
    PlayerTables tables;
    for (std::size_t n = 0; n < 15; ++n) {
        const std::uint32_t address = code.u32(samples + std::uint32_t(n) * 4);
        if (address < data::instruments_address || address - data::instruments_address >= bank_size)
            throw std::runtime_error("The music's sample table is not as expected.");
        tables.sample_starts[n] = address - data::instruments_address;
    }
    for (std::size_t i = 0; i < 36; ++i) tables.periods[i] = code.u16(periods + std::uint32_t(i) * 2);
    return tables;
}

void Song::halve_volumes() {
    for (auto& sample : samples) sample.volume /= 2;
}

Player::Player(const PlayerTables& tables, unsigned note_channels, unsigned effect_channels, bool start_clears)
    : tables_(tables), note_channels_(note_channels), effect_channels_(effect_channels), start_clears_(start_clears) {}

void Player::start(const Song& song, amiga::PaulaChannels& paula, bool once) {
    if (start_clears_) {
        for (int n = 0; n < 4; ++n) paula.set_volume(n, 0);
        channels_ = {};
    }
    song_ = &song;
    once_ = once;
    counter_ = 0; // (the speed stays: command F changes it in the program itself)
    position_ = 0;
    row_ = 0;
}

void Player::stop(amiga::PaulaChannels& paula) {
    for (int n = 0; n < 4; ++n) paula.set_volume(n, 0);
    paula.write_dmacon(0x000F);
    song_ = nullptr;
}

void Player::tick(amiga::PaulaChannels& paula) {
    if (!song_) return;
    // The counter is compared with the speed for equality only: a speed of
    // 0 (command F00 is skipped, but $F10 sets it) would wait for the
    // 16-bit counter to come round.
    if (++counter_ == speed_) {
        counter_ = 0;
        new_row(paula);
        return;
    }
    for (int n = 0; n < 4; ++n) {
        if ((effect_channels_ >> n & 1) && channels_[std::size_t(n)].parameter != 0) effect(n, paula);
    }
}

// $24200: the effects between rows, for a channel whose parameter is not 0.
void Player::effect(int n, amiga::PaulaChannels& paula) {
    auto& c = channels_[std::size_t(n)];
    switch (c.command & 0x0F) {
    case 0x0: { // arpeggio: the note, +x, +y semitones over the row's ticks (no song uses it)
        int step = 0;
        switch (counter_) {
        case 1: case 4: step = c.parameter >> 4; break;
        case 2: case 5: step = c.parameter & 0x0F; break;
        case 3: paula.set_period(n, c.note_period); return;
        default: return;
        }
        // The note's period is looked up in the table and the one `step`
        // notes higher is taken. The original searches without a limit (a
        // period not in the table hangs it); the port leaves the period.
        for (std::size_t i = 0; i < tables_.periods.size(); ++i) {
            if (tables_.periods[i] != c.note_period) continue;
            if (i + std::size_t(step) < tables_.periods.size()) paula.set_period(n, tables_.periods[i + std::size_t(step)]);
            return;
        }
        return;
    }
    case 0x1: // slide up to the period $71 at most
        c.slide_period = std::uint16_t(c.slide_period - (c.parameter & 0x0F));
        if (std::int16_t(c.slide_period - 0x71) < 0) c.slide_period = 0x71;
        paula.set_period(n, c.slide_period);
        return;
    case 0x2: // slide down to $358 at most
        c.slide_period = std::uint16_t(c.slide_period + (c.parameter & 0x0F));
        if (std::int16_t(c.slide_period - 0x358) >= 0) c.slide_period = 0x358;
        paula.set_period(n, c.slide_period);
        return;
    case 0xC: // volume (a byte written to the volume register: bits 0-6 count)
        paula.set_volume(n, c.parameter & 0x7F);
        return;
    case 0xF: // speed: the low four bits
        speed_ = c.parameter & 0x0F;
        return;
    default: // E (the filter) and the rest: nothing
        return;
    }
}

// $24336: the next row on the note channels, all of them started together.
void Player::new_row(amiga::PaulaChannels& paula) {
    const std::uint32_t at =
        std::uint32_t(song_->positions[std::size_t(position_)]) * 1024u + std::uint32_t(row_) * 16u;
    std::uint16_t started = 0;
    for (int n = 0; n < 4; ++n) {
        if ((note_channels_ & ~held_) >> n & 1) read_note(n, at + std::uint32_t(n) * 4, paula, started);
    }
    paula.write_dmacon(std::uint16_t(0x8000 | started));
    // A sample without repeat plays once: its length register is set to one
    // word right after the start, so that afterwards the channel repeats the
    // sample's first word (the samples start with silence).
    for (int n = 0; n < 4; ++n) {
        auto& c = channels_[std::size_t(n)];
        if ((note_channels_ >> n & 1) && c.repeat_length == 1) {
            c.repeat_length = 0;
            paula.set_length(n, 1);
        }
    }
    if (++row_ == 64) {
        row_ = 0;
        if (++position_ == song_->length) {
            if (once_) song_ = nullptr; // $31172: the music is switched off
            else position_ = 0;
        }
    }
}

// $2445A: one channel's note.
void Player::read_note(int n, std::uint32_t at, amiga::PaulaChannels& paula, std::uint16_t& started) {
    auto& c = channels_[std::size_t(n)];
    const auto& data = song_->patterns;
    c.period = be16(data, at);
    c.command = data[at + 2];
    c.parameter = data[at + 3];
    if (const int sample = c.command >> 4; sample != 0) {
        const auto& s = song_->samples[std::size_t(sample - 1)];
        c.start = tables_.sample_starts[std::size_t(sample - 1)];
        c.length = s.length;
        paula.set_volume(n, (c.command & 0x0F) == 0xC ? c.parameter & 0x7F : s.volume);
        c.repeat_start = c.start + s.repeat_start;
        c.repeat_length = s.repeat_length;
        if (c.repeat_length != 1) { // a repeating sample plays its repeat only
            c.start = c.repeat_start;
            c.length = s.repeat_length;
        }
    }
    if (c.period == 0) return;
    paula.write_dmacon(std::uint16_t(1u << n));
    if (c.repeat_length == 0) c.repeat_length = 1;
    c.slide_period = c.period;
    c.note_period = c.period;
    paula.set_location(n, c.start);
    paula.set_length(n, c.length);
    paula.set_period(n, c.period);
    started = std::uint16_t(started | 1u << n);
}

} // namespace beach_volley::sound
