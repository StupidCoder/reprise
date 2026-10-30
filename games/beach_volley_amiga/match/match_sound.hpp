#pragma once
#include "amiga/paula_channels.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include <cstdint>
#include <utility>
#include <vector>

// volley.code's sound (game.md "Match sound"): its copy of the music player
// ($30EB2) run by the vertical blank while the music is on, the songs a
// match plays, the volume the vertical blank holds the music at, and the
// effects, each a routine that programs channel 3 (the spike: channel 1)
// itself. The effects' samples lie in the sprite block.
namespace beach_volley::match {

struct MatchData;

class MatchSound {
public:
    enum class Song : std::uint8_t {
        none,
        venue, // the venue's song ($7BD1C)
        hurry, // music8 ($796C4): from the clock's ninth minute
        a_won, // the jingles in the sprite block ($78A14, $7906C)
        b_won,
    };
    enum class Effect : std::uint8_t {
        crowd,         // $29292: the crowd after a point (repeats until faded)
        serve,         // $292BE: side A's serve (the hit, lower)
        whistle,       // $292FA
        net_whistle,   // $2934A: a spike into the net (the whistle, higher)
        spike,         // $29392: side A's jump spike, on channel 1
        hit,           // $293DA
        landing,       // $29422: the ball in the sand
        smash,         // $294BA: side B's jump spike (the hit, higher)
    };

    explicit MatchSound(amiga::PaulaChannels& paula) : paula_(&paula) {}

    // The venue's songs and samples (again after a quick save is read).
    void bind(const MatchData& data);

    // $30E6C: a song from its start; `once` ($3133A) ends the music at its
    // end. The music plays only while it is on ($31B88).
    void play(Song song, bool once = false);
    void set_music(bool on) { music_on_ = on; }
    bool music_on() const { return music_on_; }

    // The vertical blank's music part ($284DE): the player, then the music
    // channels held at the volume (channel 1 left alone while `hold_1`).
    void vblank(bool hold_1);

    void effect(Effect e);
    // $26172: the crowd's fade steps channel 3's volume down; at its end the
    // sample is left to stop.
    void set_effect_volume(int volume) { paula_->set_volume(3, std::uint16_t(volume)); }
    void end_effect() { paula_->set_length(3, 1); }
    // $25CDA: every channel's DMA off.
    void silence() { paula_->write_dmacon(0x000F); }

    // $28698: the music's volume (keypad + and - change it, 2-40).
    int volume() const { return volume_; }
    void set_volume(int v) { volume_ = v; }

    Song song() const { return song_; }
    // Research: what was started since the last call (effects as their
    // number, songs as 100 + theirs).
    std::vector<int> take_heard() { return std::exchange(heard_, {}); }
    const sound::Player& player() const { return player_; }

    // Needs bind() first when reading.
    template <class A> void persist(A& ar) {
        bool playing = player_.playing();
        ar(song_, music_on_, volume_, player_, playing);
        if constexpr (A::reading) player_.bind(playing ? current() : nullptr);
    }

private:
    const sound::Song* current() const;
    // An effect's sample: its address in the original's memory.
    std::uint32_t at(std::uint32_t address) const;

    amiga::PaulaChannels* paula_;
    const MatchData* data_{};
    std::vector<std::int8_t> memory_; // the instruments, then the sprite block
    sound::Player player_{{}, 0x7, 0x7, false};
    Song song_{Song::none};
    bool music_on_{};
    int volume_{10};   // as volley.code is loaded
    std::vector<int> heard_;
};

} // namespace beach_volley::match
