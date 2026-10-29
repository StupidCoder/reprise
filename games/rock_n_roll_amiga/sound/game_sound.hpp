#pragma once
#include "amiga/paula_channels.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/sound/tfmx.hpp"
#include <span>
#include <vector>

// The game's sound as its glue routines drive the player ($70A32-$70AEE)
// and its music files reach memory ($70E40, $70F96-$71038):
//   - the memory the player's samples are addressed in, from $3912C (the
//     program's own bytes there, the loaded files laid over them; what a
//     world does not load stays from before, so world 3, which loads only
//     song data, plays with the samples left by the world before it);
//   - the song data at $5740E, from a world's song file or, in world 1 and
//     on the title, one of the three song sets in file $40;
//   - the player and the four channels it drives.
namespace rocknroll::sound {

class GameSound {
public:
    explicit GameSound(const data::Assets&);

    // $70FB8 for a level's world: world 1 copies song set 1 and loads its
    // samples; the others load their song and sample files.
    void load_world(int world);
    // The title ($68F40, world 8): song set 8 after a game that reached the
    // hall of fame, else set 9 ($68E9C), and world 8's two sample files.
    void load_title(bool after_result);
    // The jukebox's title n ($68DE6): 8 and 9 are song sets 8 and 9 with
    // world 8's samples, 1-7 the worlds' music.
    void load_jukebox(int title);

    void start_music(int song = 0) { player_.init(song_data()); player_.start_song(song); } // $70A74
    void stop_channels() { for (int c = 0; c < 4; ++c) player_.stop_channel(c, paula_); } // $70A32
    void stop_all() { player_.stop(paula_); }  // $709E6
    void effect(int number) { if (effects_on_) player_.effect(number); } // $70A9C
    void tick() { player_.frame(paula_, memory_); } // $70AE4 (+$24)
    void render(std::span<float> stereo, int rate) { paula_.render(stereo, rate); }
    const Tfmx& player() const { return player_; } // research

private:
    void load_file(int file);               // to its address in the load table ($78136)
    void copy_song_set(int set);            // $70E40
    void place(std::uint32_t address, std::span<const std::uint8_t> bytes);
    std::vector<std::uint8_t> song_data() const;

    const data::Assets* assets_;
    std::vector<std::int8_t> memory_; // from Tfmx::sample_base to the program's end
    Tfmx player_;
    amiga::PaulaChannels paula_;
    bool effects_on_{true};           // $68AAC (the '&' key toggles it, $68AC2)
};

} // namespace rocknroll::sound
