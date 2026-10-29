#pragma once
#include "gba/apu.hpp"
#include "sonic_advance_gba/audio/sound_engine.hpp"
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <memory>
#include <utility>
#include <vector>

// The game's side of sound: the requests the game makes to the cartridge's
// sound engine (audio/sound_engine.hpp), which plays them on the GBA sound
// hardware (gba/apu.hpp). The requests are also recorded for the lockstep
// harness, which compares them with the original's.
//
// The four music players: 0 music, 1-2 effects (the ring sound alternates
// between them), 3 voices and jingles.
namespace sonic_advance_gba {

struct SoundRequest {
    enum class Kind : u8 { start_song, stop_song, restart_tracks, fade_out, play_header } kind;
    u16 number;
};

class Sound {
public:
    // 08097760 at power-on: the engine and its players.
    void attach(const Assets& a) { engine_ = std::make_unique<audio::SoundEngine>(a, apu_); }

    // 080977E4: starts song or sound effect `number`.
    void start(u16 number);

    // 0809785C: starts song `number` unless its player already holds it and
    // is playing (a paused one resumes).
    void start_or_continue(u16 number) { engine_->start_or_continue(number); }

    // 08097810: starts song `number` unless it is playing already.
    void start_unless_playing(u16 number) { engine_->start_unless_playing(number); }

    // 080978B0: stops song `number` if its player is playing it.
    void stop(u16 number);
    // 080979D4: the playing tracks of music player `player` start over
    // (used with player 0, the music, before a screen change).
    void restart_tracks(u8 player);
    // 08097F18 + 080979D4 + volume and pan: a song by its header on player
    // `player` (the ring sound alternates sides this way).
    void play_on(u8 player, u32 header, s8 pan);
    // 08097F18: the song with header `header` on music player `player`.
    void start_header(u8 player, u32 header);

    // 0809878C + 080979AC: the tracks of `player` set quiet (volume 4), then
    // fading in at `speed`.
    void fade_in(u8 player, u16 speed) {
        engine_->set_volume(player, 0xFF, 4);
        engine_->fade_in(player, speed);
    }

    // 08098764: the tempo of music player `player` (100h normal).
    void set_tempo(u8 player, u16 tempo) { engine_->set_tempo(player, tempo); }

    // 080987F4: the pitch of the chosen tracks (key shift in the high byte).
    void set_pitch(u8 player, u16 tracks, s16 pitch) { engine_->set_pitch(player, tracks, pitch); }

    // 0809797C: player `player` fades out.
    void fade_out(u8 player, u16 speed);

    // 08097FFC: music player `player` stops (its song kept).
    void stop_player(u8 player) { engine_->stop(player); }

    // 08097950: every music player goes on where it stopped.
    void resume_all() {
        for (u8 n = 0; n < 4; ++n) {
            engine_->resume(n);
        }
    }

    // 08097DE8 + 08097E64 (around a save write): the output starts over.
    void restart_output() { engine_->restart_output(); }

    // 08097918: every player stops.
    void stop_all() { engine_->stop_all(); }

    // 0803CB4C: the music's tracks start over, fading in from silence; with
    // `fast`, at double tempo.
    void restore_music(bool fast);

    // 080977D8 at the start of each frame: the players, the mix.
    void main() { engine_->main(); }

    // 08097104 in the vertical blank.
    void vertical_blank() { engine_->vertical_blank(); }

    u32 song(u8 player) const { return engine_->player(player).song; }

    // The player's status shows tracks playing and no pause.
    bool playing(u8 player) const {
        const u32 status = engine_->player(player).status;
        return (status & 0xFFFF) != 0 && !(status & 0x80000000);
    }

    const audio::SoundEngine& engine() const { return *engine_; }

    gba::Apu& hardware() { return apu_; }

    std::vector<SoundRequest> take_requests() { return std::exchange(requests_, {}); }

private:
    gba::Apu apu_;
    std::unique_ptr<audio::SoundEngine> engine_;
    std::vector<SoundRequest> requests_;
};

} // namespace sonic_advance_gba
