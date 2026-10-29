#pragma once
#include "gba/apu.hpp"
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <array>

// The cartridge's sound engine (08096AB0-08098B64, its mixer copied to
// 03000000): four music players read songs (byte-coded tracks) and play
// their notes on up to five sampled channels, which the engine mixes into
// 8-bit stereo every frame, and on the four tone generators of the sound
// hardware.
//
// Songs, voicegroups, samples and the engine's tables come from the
// cartridge (the song table 086D76AC, the player table 086D767C); this is
// only the engine.
namespace sonic_advance_gba::audio {

struct Track;

// What a playing note keeps, sampled or tone generator (the original's
// 0x40-byte channel records).
struct Channel {
    u8 status{}; // +00 channel_status bits
    u8 type{};   // +01 the voice type (bits 0-2: generator 1-4, 0 sampled; bit 3 fixed rate)
    u8 right_volume{}, left_volume{};           // +02, +03
    u8 attack{}, decay{}, sustain{}, release{}; // +04-+07
    u8 key{};                                   // +08 the note played
    u8 envelope{};                              // +09 the envelope's level
    u8 echo_volume{};                           // +0C level held after the release
    u8 echo_length{};                           // +0D frames it is held
    u8 gate_time{};                             // +10 ticks to the release (0: until a note-off)
    u8 midi_key{};                              // +11 the key as written (for note-off)
    u8 velocity{};                              // +12
    u8 priority{};                              // +13
    s8 rhythm_pan{};                            // +14 a drum's own pan
    u32 wave{};          // +24 sample address (generators: duty, wave address or noise mode)
    Track* track{};      // +2C
    Channel* previous{}; // +30
    Channel* next{};     // +34
};

// Bits of Channel::status. Bits 0-1: the envelope phase (3 attack, 2
// decay, 1 sustain, 0 release).
namespace channel_status {
inline constexpr u8 phase = 0x03, echo = 0x04, loop = 0x10, release = 0x40, start = 0x80;
inline constexpr u8 active = 0xC7;
} // namespace channel_status

struct PcmChannel : Channel {
    u8 envelope_right{}, envelope_left{}; // +0A, +0B
    u32 count{};                          // +18 samples left before the end (or the loop)
    u32 fraction{};                       // +1C position between samples (23 bits)
    u32 frequency{};                      // +20
    u32 current{};                        // +28 address of the next sample
};

struct PsgChannel : Channel {
    u8 envelope_goal{};    // +0A
    u8 envelope_counter{}; // +0B frames to the next envelope step
    u8 sustain_goal{};     // +19
    u8 control{};       // +1A the generator's trigger register (length bit 6, frequency high bits)
    u8 pan{};           // +1B output enables
    u8 pan_mask{};      // +1C this generator's enable bits
    u8 modified{};      // +1D bit 0: volume, bit 1: frequency to set
    u8 length{};        // +1E
    u8 sweep{};         // +1F
    u32 frequency{};    // +20 the 11-bit register value
    u32 current_wave{}; // +28 the wave in the generator's memory
};

// An instrument (12 bytes of a voicegroup).
struct Voice {
    u8 type{};      // +0: bits 0-2 generator; 40h key split, 80h drum kit
    u8 key{};       // +1 a drum's fixed key
    u8 length{};    // +2 (generators)
    u8 pan_sweep{}; // +3 a drum's pan (bit 7) or the sweep
    u32 data{};     // +4 sample, generator setting or sub-voicegroup
    u32 data2{};    // +8 attack, decay, sustain, release; or the key split table
};

struct Track {
    u8 flags{};                       // +00 track_flag bits
    u8 wait{};                        // +01 ticks to the next command
    u8 pattern_level{};               // +02
    u8 repeat_count{};                // +03
    u8 gate_time{};                   // +04 of the next note
    u8 key{};                         // +05
    u8 velocity{};                    // +06
    u8 running_status{};              // +07 the last command, repeated by bare arguments
    s8 key_m{};                       // +08 key offset from pitch settings
    u8 pitch_m{};                     // +09 and its fraction
    s8 key_shift{};                   // +0A
    s8 key_shift_x{};                 // +0B
    s8 tune{};                        // +0C
    u8 pitch_x{};                     // +0D
    s8 bend{};                        // +0E
    u8 bend_range{};                  // +0F
    u8 volume_right{}, volume_left{}; // +10, +11
    u8 volume{};                      // +12
    u8 volume_x{};                    // +13 (fades)
    s8 pan{};                         // +14
    s8 pan_x{};                       // +15
    s8 mod_m{};                       // +16 the LFO's current value
    u8 mod{};                         // +17 its depth
    u8 mod_type{};                    // +18 0 pitch, 1 volume, 2 pan
    u8 lfo_speed{};                   // +19
    u8 lfo_phase{};                   // +1A
    u8 lfo_delay{};                   // +1B
    u8 lfo_delay_counter{};           // +1C
    u8 priority{};                    // +1D
    u8 echo_volume{};                 // +1E
    u8 echo_length{};                 // +1F
    Channel* channels{};              // +20
    Voice voice;                      // +24
    u32 command{};                    // +40 address of the next command
    std::array<u32, 3> patterns{};    // +44 return addresses
    int order{};                      // the record's place in memory (priority ties)

    // Clear64byte (08096ED8): +00-+3F to zero.
    void clear();
};

namespace track_flag {
inline constexpr u8 volume = 0x03, pitch = 0x0C, start = 0x40, active = 0x80;
} // namespace track_flag

struct MusicPlayer {
    u32 song{};                     // +00 header address
    u32 status{};                   // +04 bits 0-15 tracks playing, bit 31 paused
    u8 track_count{};               // +08
    u8 priority{};                  // +09
    u8 command{};                   // +0A
    u8 priority_check{};            // +0B (0 for every player of this game)
    u32 clock{};                    // +0C
    u16 tempo_d = 150;              // +1C
    u16 tempo_u = 0x100;            // +1E
    u16 tempo_i = 150;              // +20
    u16 tempo_c{};                  // +22
    u16 fade_interval{};            // +24
    u16 fade_counter{};             // +26
    u16 fade_volume{};              // +28
    std::array<Track, 16> tracks{}; // +2C
    u32 voicegroup{};               // +30
};

class SoundEngine {
public:
    SoundEngine(const Assets& assets, gba::Apu& apu);

    // 080977E4 (m4aSongNumStart in the original's terms is not used here):
    // song `number` from the song table on its player.
    void start_song(u16 number);
    // 0809785C: starts it unless its player holds it with tracks playing;
    // a paused player resumes.
    void start_or_continue(u16 number);
    // 08097810: starts it unless its player holds it, playing.
    void start_unless_playing(u16 number);
    // 080978B0: stops it if its player is playing it.
    void stop_song(u16 number);
    // 08097918: every player stops.
    void stop_all();

    // Music player services (by player 0-3).
    void start(u8 player, u32 header);                  // 08097F18
    void stop(u8 player);                               // 08097FFC
    void resume(u8 player);                             // 08097724
    void fade_out(u8 player, u16 speed);                // 08097740
    void fade_in(u8 player, u16 speed);                 // 080979AC
    void set_tempo(u8 player, u16 tempo);               // 08098764
    void set_volume(u8 player, u16 tracks, u16 volume); // 0809878C
    void set_pitch(u8 player, u16 tracks, s16 pitch);   // 080987F4
    void set_pan(u8 player, u16 tracks, s8 pan);        // 08098868
    void reset_tracks(u8 player);                       // 080979D4

    // 08097DE8 + 08097E64: the output stops (the ring buffer cleared) and
    // starts again from the buffer's beginning.
    void restart_output() {
        buffer_.fill(0);
        dma_counter_ = 0;
    }

    // 080977D8 (SoundMain): the players, the tone generators, the mix.
    void main();
    // 08097104 (in the vertical blank): the mixing position moves on.
    void vertical_blank();

    const MusicPlayer& player(u8 n) const { return players_.at(n); }

    // The frame's samples just mixed (right, left).
    std::span<const s8> mixed(int side) const;

    // The whole ring buffer (right half, then left), as the original keeps it.
    const std::array<s8, 0x630 * 2>& buffer() const { return buffer_; }

    u8 dma_counter() const { return dma_counter_; }

    u32 samples_per_frame() const { return samples_per_frame_; }

    u32 sample_rate() const { return sample_rate_; }

private:
    void sound_mode(u32 mode);        // 08097CFC
    void set_sample_rate(u32 index);  // 08097C58
    void play_player(MusicPlayer& p); // 08097150
    void fade_step(MusicPlayer& p);   // 0809803C
    void run_commands(MusicPlayer& p, Track& t);
    void command(MusicPlayer& p, Track& t, u8 cmd);
    void note(MusicPlayer& p, Track& t, u8 length);        // 0809742C
    void note_off(Track& t);                               // 0809762C
    void track_stop(Track& t);                             // 080973B8
    void track_end(Track& t);                              // 08096F10
    void track_reset(Track& t);                            // MPlayMain's start
    void lfo_reset(Track& t);                              // 0809766C
    void volume_pitch(Track& t);                           // 08098104
    void channel_volume(Channel& c, const Track& t);       // 080973FC
    void unlink(Channel& c);                               // 08096EF0
    u32 key_to_frequency(u32 wave, u8 key, u8 fine) const; // 080976BC
    u32 key_to_psg(u8 type, u8 key, u8 fine) const;        // 080981B8
    void psg_off(u8 n);                                    // 08098260
    void psg_volume(PsgChannel& c);                        // 080982B0
    void psg_update();                                     // 08098318
    void mix();                                            // 08096B34
    void memory_access(MusicPlayer& p, Track& t);          // 080989D8
    void extended(Track& t);                               // 08098B30

    u8 read(u32 address) const { return assets_.u8(address); }

    u32 read32(u32 address) const { return assets_.u32(address); }

    u8 next_byte(Track& t) const { return read(t.command++); }

    void reg(u32 offset, u8 v) { apu_.write8(offset, v); }

    const Assets& assets_;
    gba::Apu& apu_;
    std::array<MusicPlayer, 4> players_{};
    std::array<PcmChannel, 12> pcm_{};
    std::array<PsgChannel, 4> psg_{};
    std::array<u8, 256> shared_memory_{}; // 02002600: memory access commands' bytes

    u8 dma_counter_{};                   // +04
    u8 reverb_{};                        // +05
    u8 max_channels = 8;                 // +06
    u8 master_volume = 15;               // +07
    u8 psg_counter_{};                   // +0A envelope frames (0-14)
    u8 dma_period_{};                    // +0B frames in the ring buffer
    u32 samples_per_frame_{};            // +10
    u32 sample_rate_{};                  // +14
    u32 step_divisor_{};                 // +18
    std::array<s8, 0x630 * 2> buffer_{}; // +350 right, +980 left
    u32 mixed_at_{};                     // where the last frame was mixed
};

} // namespace sonic_advance_gba::audio
