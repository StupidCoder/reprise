#pragma once
#include "amiga/paula_channels.hpp"
#include "beach_volley_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The game's music: songs in the 15-sample SoundTracker layout without
// their samples (music1-13, musicfinal), all playing the one sample bank in
// `instruments`, and the player each code file carries a copy of (intro.code
// $24114-$24616; game.md "Music"). The player is called once per vertical
// blank and writes Paula's registers itself.
namespace beach_volley::sound {

// A song file: a 20-byte title, 15 sample records, the number of positions,
// a byte the player ignores, 128 positions of pattern numbers, then the
// patterns (64 rows x 4 channels x 4 bytes).
struct Song {
    struct Sample {
        std::uint16_t length{};        // words, played from the sample's start
        std::uint16_t volume{};        // 0-64
        std::uint16_t repeat_start{};  // bytes from the sample's start
        std::uint16_t repeat_length{}; // words; 1: no repeat
    };
    std::string title;
    std::array<Sample, 15> samples{}; // samples 1-15
    int length{};                     // positions played before the song starts again
    std::array<std::uint8_t, 128> positions{};
    std::vector<std::uint8_t> patterns;

    // Throws std::runtime_error when the file does not have this layout.
    // Takes as many patterns as the positions name, so a song can also be
    // read from inside a larger block (volley.code's jingles in the sprite
    // block).
    static Song parse(std::span<const std::uint8_t> file);
    // intro.code $230E4: the intro halves every sample's volume in its copy
    // of the song before playing it.
    void halve_volumes();
};

// What a code file's copy of the player plays from: where each of the 15
// samples starts in the sample memory, and the 36 note periods arpeggios
// step through (both tables follow the player in each code file).
struct PlayerTables {
    std::array<std::uint32_t, 15> sample_starts{};
    std::array<std::uint16_t, 36> periods{};
};

// Reads a code file's tables: `samples` the address of sample 1's start
// (a long each), `periods` the first of the 36 periods; the sample bank is
// loaded at data::instruments_address and is `bank_size` bytes long.
PlayerTables read_player_tables(const data::CodeFile& code, std::uint32_t samples, std::uint32_t periods,
                                std::size_t bank_size);

// One copy of the player. The copies differ in which channels play notes and
// which have their effects run (intro.code: notes on 0-2, effects on 0-3;
// volley.code: both on 0-2), and in what starting a song resets.
class Player {
public:
    Player(const PlayerTables& tables, unsigned note_channels, unsigned effect_channels,
           bool start_clears = true);

    // $24114: the song from its first position (the song must outlive the
    // player's use of it). intro.code's copy clears the channels' volumes
    // and what their notes left; volley.code's ($30E6C) keeps both. A song
    // played `once` ends the playing at its end instead of starting again
    // (volley.code's $3133A).
    void start(const Song& song, amiga::PaulaChannels& paula, bool once = false);
    // Channels whose notes are not read at a new row (volley.code holds
    // channel 1 while a spike's sound plays on it: $26A8A).
    void hold(unsigned channels) { held_ = channels; }
    // $24172: silence and stop.
    void stop(amiga::PaulaChannels& paula);
    // Rebinds the song after a quick save is read (the save keeps where the
    // player is, not the song).
    void bind(const Song* song) { song_ = song; }
    void set_tables(const PlayerTables& tables) { tables_ = tables; }
    bool playing() const { return song_ != nullptr; }

    // $24194: one vertical blank.
    void tick(amiga::PaulaChannels& paula);

    template <class A> void persist(A& ar) { ar(speed_, counter_, position_, row_, channels_, once_, held_); }

    // What a channel's note left, for research tools.
    struct Channel {
        std::uint16_t period{};                     // the row's note: period, sample and command, parameter
        std::uint8_t command{}, parameter{};
        std::uint32_t start{};                      // the block the next note plays
        std::uint16_t length{};
        std::uint32_t repeat_start{};
        std::uint16_t repeat_length{};              // 1: the sample does not repeat (cleared once applied)
        std::uint16_t note_period{};                // the period the note started with (arpeggio's base)
        std::uint16_t slide_period{};               // the period the slides change

        template <class A> void persist(A& ar) {
            ar(period, command, parameter, start, length, repeat_start, repeat_length, note_period, slide_period);
        }
    };

    const Channel& channel(int n) const { return channels_[std::size_t(n)]; }
    int speed() const { return speed_; }
    int counter() const { return counter_; }
    int position() const { return position_; }
    int row() const { return row_; }

private:
    void new_row(amiga::PaulaChannels& paula);
    void read_note(int channel, std::uint32_t at, amiga::PaulaChannels& paula, std::uint16_t& started);
    void effect(int channel, amiga::PaulaChannels& paula);

    PlayerTables tables_;
    unsigned note_channels_, effect_channels_;
    bool start_clears_;
    bool once_{};
    unsigned held_{};
    const Song* song_{};
    std::uint16_t speed_{6};   // ticks per row, as the code file is loaded (command F rewrites it)
    std::uint16_t counter_{};  // ticks since the row
    int position_{}, row_{};
    std::array<Channel, 4> channels_{};
};

} // namespace beach_volley::sound
