#pragma once
#include "amiga/hunk.hpp"
#include "amiga/paula_channels.hpp"
#include <array>
#include <map>
#include <cstdint>
#include <span>
#include <vector>

// The music: `songplay` (Kris Hatlelid's player, its symbols kept) playing
// the songs of `wofsongs`. A CIA-B timer interrupt (`SongInt`, $1802A6)
// steps the four tracks; each track reads its patterns (bytes: notes with a
// duration, and commands), sets the channel's period from the note (8SVX
// instruments with one sample per octave, `_ReadInstruments` $180946) and
// lets the audio interrupt (`SongIntHandler` $180848) switch a note's
// one-shot part to its loop. Rebuilt from songplay's code; the songs,
// instruments and the player's tables are read from the two files.
//
// The game uses it in the front end only ($1023DC: fade the playing song
// out, then load and play another; $102470: fade out and stop).
namespace wings::game {

class MusicPlayer {
public:
    // `songplay` and `wofsongs` as stored.
    MusicPlayer(const std::vector<std::uint8_t>& songplay, const std::vector<std::uint8_t>& wofsongs);

    // `_ReadInstruments` and `_PlaySong` (commands 1 and 2): song `index` of
    // the `songs` table, from its start.
    void play(int index);
    // `_FadeSong` (command 6): the tracks' volumes step down, one every
    // `speed` + 1 ticks; at 0 the song stops.
    void fade(int speed);
    // `_StopSong` (command 3).
    void stop();
    // At once: no song, the channels off (quick load).
    void silence() {
        state_ = 0;
        hardware_off();
    }
    // `_GetSongStat` (command 5): 0 when no song plays.
    int state() const { return state_; }

    // Adds the music to `stereo` (interleaved), the timer interrupts falling
    // between the samples where they come.
    void render(std::span<float> stereo, int rate);

    // Research: one timer interrupt, and the tracks as the player keeps them
    // (addresses in wofsongs as loaded at $190000).
    void tick() { timer_tick(); }
    int latch() const { return timer_latch_; }
    void set_stage(int track, int stage) { tracks_[std::size_t(track)].stage = stage; }
    // Research: the channels play on for the time to the next interrupt (at
    // the tempo now set), without the interrupt.
    void play_to_next_tick(int rate) {
        research_time_ += double(timer_latch_) / 709379.0 * rate;
        const auto whole = std::size_t(research_time_);
        research_time_ -= double(whole);
        std::vector<float> scratch(whole * 2);
        channels_.render(scratch, rate);
    }
    struct TrackView {
        bool active{};
        std::uint32_t pattern{}, entry{}, voice{}, loop_start{};
        int left{}, release{}, stage{}, volume{}, period{}, loop_words{}, hold{}, tie{}, note{}, slid{}, slide_step{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(active, pattern, entry, voice, loop_start, left, release, stage, volume, period, loop_words,
               hold, tie, note, slid, slide_step);
        }
    };
    std::array<TrackView, 4> view() const {
        std::array<TrackView, 4> out{};
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& t = tracks_[i];
            out[i] = {t.active, t.pattern, t.entry, t.voice, t.loop_start, t.left, t.release, t.stage, t.volume,
                      t.period, t.loop_words, t.hold, t.tie, t.note, t.slid, t.slide_step};
        }
        return out;
    }

private:
    struct Instrument {
        std::uint32_t vhdr{};   // +0: the 8SVX VHDR chunk's contents
        std::uint32_t body{};   // +4: its BODY
        int notes_per_octave{}; // +8: $53 / octaves
        std::uint32_t address{};// the voice struct (its slide and arpeggio settings)

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(vhdr, body, notes_per_octave, address); }
    };
    struct Track {
        bool active{};               // +0
        std::uint32_t list{};        // +2: the pattern list (6 bytes: pattern, transpose)
        std::uint32_t pattern{};     // +6: where the next byte is read
        std::uint32_t entry{};       // +$A: the list entry's offset
        int left{};                  // +$E: ticks left of the note
        int release{};               // +$10: the tick it is released at
        int stage{};                 // +$12: 1 one-shot playing, 0 to stop, -1 looping or off
        std::uint32_t voice{};       // +$3C: the voice struct (its instrument: +$14, +$18, +$1C)
        int volume{0x20};            // +$1E
        int period{};                // +$20: the note's period
        std::uint16_t loop_words{};  // +$22
        std::uint32_t loop_start{};  // +$24
        int hold{};                  // +$38: notes not released
        int tie{};                   // +$3A
        int note{};                  // +$40
        int slid{};                  // +$42: the period with the slide
        int slide_step{};            // +$44

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(active, list, pattern, entry, left, release, stage, voice, volume, period, loop_words,
               loop_start, hold, tie, note, slid, slide_step);
        }
    };

    // Per instrument (the voice struct, shared by the tracks using it).
    struct Voice {
        int slide_counter{};   // +$1C
        int arpeggio_step{};   // +$2C

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(slide_counter, arpeggio_step); }
    };

    void timer_tick();                 // SongInt
    void start_tracks();               // state 1
    void step_track(int n);            // $1804F4
    void set_note(Track&, int n, int note, int& octave); // $1807EA
    void block_started(int channel);   // SongIntHandler
    void hardware_off();               // $1804DA
    const Instrument* instrument_of(const Track&) const;
    std::uint16_t word(std::uint32_t a) const { return image_.u16(a); }

    amiga::LoadedImage image_;      // wofsongs at $190000
    std::vector<std::int8_t> memory_; // the same bytes, for the channels
    std::uint32_t songs_{};
    amiga::LoadedImage player_;     // songplay at $180000: the period table $180BC4 (by note - $1B;
                                    // lower notes read what lies before it) and the durations $180CE4
    std::vector<std::uint32_t> instrument_table_;   // the song's voice pointers ($180E66)
    std::map<std::uint32_t, Instrument> known_;     // voices set up by `_ReadInstruments` (they keep it)
    std::map<std::uint32_t, Voice> voice_state_;    // their counters
    std::array<Track, 4> tracks_{};
    std::uint32_t song_{};
    int state_ = 0;              // PlayState: 0 idle, 1 start, 2 playing, 3 stop, 4 fading
    int fade_count_ = 0, fade_speed_ = 0;
    bool alive_ = false;         // $180D0E: a track has played
    std::uint16_t timer_latch_ = 0xFFFF; // CIA-B timer A
    double until_tick_ = 0;
    double research_time_ = 0;      // samples (at the render rate) to the next interrupt
    amiga::PaulaChannels channels_;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(instrument_table_, known_, voice_state_, tracks_, song_, state_, fade_count_, fade_speed_, alive_,
           timer_latch_, until_tick_, research_time_, channels_);
    }
};

} // namespace wings::game
