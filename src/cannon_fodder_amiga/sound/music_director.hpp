#pragma once
#include "cannon_fodder_amiga/data/assets.hpp"
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/sound/rjp1.hpp"
#include <optional>
#include <vector>

// The music during a phase (.analysis/cannon_fodder_amiga/sound.md "Music
// during a mission"): the map marks up to ten tiles with a song (the top
// three bits of the tile word); the nearest of songs 1-4 within 15 tiles of
// the leader plays the louder the closer he is, song 1 (the quiet landscape
// tune) takes over further away. A change of song fades the first two
// channels down, starts the new song and fades them up again.
namespace fodder::sound {

class MusicDirector {
public:
    // $8772C at the phase's start: the map's marked tiles, in map order. No
    // song chosen yet (the reference clears these variables when a phase
    // ends; seen in a forced loss on mission 1).
    void start_phase(const data::MissionMap& map);

    // $87774, once a tick while the mission runs: the song and volume for the
    // leader's tile ($87860 asks for them).
    void choose(int leader_tile_x, int leader_tile_y, const game::GeometryTables& geometry, Rjp1& player,
                bool engine_sound);
    // $878A2, every fourth frame while the mission runs: one step of a
    // change (the volume of channel 0, and channel 1 unless the engine sound
    // has it).
    void fade(Rjp1& player, bool engine_sound);

    struct Point {
        int x{}, y{}, song{};
    };
    const std::vector<Point>& points() const { return points_; }
    int song() const { return song_; }

private:
    void request(int song, int volume, Rjp1& player, bool engine_sound);
    static void set_music_volume(Rjp1& player, int volume, bool engine_sound);

    std::vector<Point> points_;   // $827FA
    int song_{};                  // $82854: the song last started (0 none yet)
    int wanted_{};                // $828B8: the song a change goes to (0 none)
    int target_volume_{};         // $828BA
};

} // namespace fodder::sound
