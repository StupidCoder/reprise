#pragma once
#include "gg_engine/data/assets.hpp"
#include "gg_engine/types.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The sound driver both cartridges run once per frame interrupt, from bank 2
// (Sonic 2: 4000-4922, Triple Trouble: 4C42-5569).
//
// Seven channels: music tone 1-3, music noise, and three effect channels
// that take over the matching music channel while they play. The game asks
// for a song or effect by its number (bit 7 set; see request()); a priority
// table gives each a priority. Songs and effects are bytecode in bank 2
// (addresses 4000-7FFF) and a second bank (8000-BFFF), interpreted here. The
// output is the list of writes to the PSG (port 7F) and its stereo register
// (port 06).
//
// Triple Trouble runs a later revision of the same driver. Where the two
// differ, DriverProfile says which way a cartridge does it; everything else
// is one routine in both. Hexadecimal numbers in comments are the original
// routines (S2 / TT) and the work-RAM addresses of the fields.
namespace gg_engine {

struct SoundWrite {
    std::uint8_t port; // 0x7F or 0x06
    std::uint8_t value;
};

// One channel (48 bytes at S2 DD40 / TT DE40 + 30 * n in the original; the
// record offsets are the same in both).
struct SoundChannel {
    // Bits: 7 on, 6 noise with its own rate (tone 3 not written), 5 held,
    // 4 silent, 3 raw periods, 2 taken over by an effect, 1 tie.
    u8 flags{};                 // +00
    u8 id{};                    // +01 80/A0/C0 tone 1-3, E0 noise
    u8 unit{};                  // +02 duration multiplier
    std::uint16_t position{};   // +03 bytecode address
    u8 transpose{};             // +05
    u8 volume{};                // +06 attenuation 0-15
    u8 modulation{};            // +07 0 none, 80 inline parameters (F0), else table entry
    u8 envelope{};              // +08 0 none, 1-7F table entry, 80-FF see envelope()
    u8 stack{};                 // +09 the call stack's top, as an offset into the record
    u8 ticks{};                 // +0A
    std::uint16_t frequency{};  // +0B
    u8 last_ticks{};            // +0D
    u8 gate{}, gate_reload{};   // +0E, +0F
    std::uint16_t mod_params{}; // +10 address of the inline vibrato parameters
    std::uint16_t mod_offset{}; // +12
    // +14-+17: inline vibrato delay, speed (a table's position), step, count
    u8 mod_delay{}, mod_speed{}, mod_step{}, mod_count{};
    u8 env_state{}, env_base{}, env_level{}; // +1D, +1E, +1F (env_level: a table's position)
    std::array<u8, 5> adsr{};                // (Sonic 2, +20-+24) the ADSR envelope's rates (E0)
    u8 detune{};                             // +25
    u8 stereo{};                             // +26
    std::array<u8, 3> loops{};               // +27-+29 loop counters
    // The call stack, record offsets 2A-2F: return addresses are pushed
    // downward from offset 30, so a fourth nested call would overwrite the
    // loop counters below it.
    std::array<u8, 6> stack_bytes{};
};

// The driver's state (S2 DD01-DE92, TT DE01-DF8F).
struct SoundState {
    u8 tempo_counter{}; // S2 DD01, TT DE01: see DriverProfile::Tempo
    u8 tempo{};         // S2 DD02, TT DE02
    u8 current{};       // S2 DD03, TT DE03: the request to start this frame (bit 7 clear: stop)
    // The game's request slots (Sonic 2 reads two; its third byte, DD06, is
    // never written but read through the vibrato quirk).
    std::array<u8, 3> requests{}; // S2 DD04, TT DE04
    u8 pause{};        // S2 DD08, TT DE08: 80 while paused
    u8 fade_steps{};   // S2 DD09, TT DE09: a music fade: steps left,
    u8 fade_counter{}; // S2 DD0A, TT DE0A: frames to the next step,
    u8 fade_speed{};   // S2 DD0B, TT DE0B: frames per step (see DriverProfile::fade_step)
    u8 effect{};       // S2 DD0D, TT DE0D: the last effect started (FF: the noise gave up)
    u8 priority{};     // S2 DD0F, TT DE0F
    u8 stereo{};       // S2 DD10, TT DE10: the stereo register
    u8 noise_mode{};   // S2 DD11, TT DE11: the last noise mode written
    u8 noise_base{};   // S2 DD15, TT DE15
    u8 drum_volume{};  // S2 DD16, TT DE16
    std::array<SoundChannel, 7> channels{}; // S2 DD40, TT DE40
    u8 pal_mode{};    // S2 DE91: 50 Hz (every fifth frame runs twice; never set by the game)
    u8 pal_counter{}; // S2 DE92
};

// How one cartridge's revision of the driver works where the revisions differ.
struct DriverProfile {
    // The tables in bank 2, and the bank in slot 2 (8000-BFFF) while the
    // driver runs.
    u8 slot2_bank;
    unsigned silence_writes;    // ten PSG writes: periods 0, volumes off
    unsigned end_command;       // an "end of channel" command
    unsigned music_channel_ids; // 80, A0, C0, E0
    unsigned note_periods;      // per note
    unsigned envelope_tables;   // per envelope number
    unsigned priorities;        // per request
    unsigned song_headers;      // per song
    unsigned effect_headers;    // per effect
    unsigned song_tempos;       // per song, read and then overwritten by the header (0: none)
    unsigned modulation_tables; // per modulation number (0: not reconstructed)

    // The game's request slots (from requests[0]) and the one request() uses
    // unless told otherwise.
    unsigned request_slots;
    unsigned default_slot;
    // Requests: songs from 81, effects from `first_effect`, then fade out,
    // stop, end the effects, and an unused number.
    u8 first_effect, first_command;

    // Countdown: every `tempo` frames (when nonzero) each music note lasts one
    // tick longer. Accumulate: `tempo` is added every frame and a carry
    // makes the notes last one tick longer.
    enum class Tempo { countdown, accumulate } tempo;
    // Volume steps per fade step (the fade counter and steps count in it too)
    // and whether a fade also ends the music noise channel.
    u8 fade_step;
    bool fade_ends_drums;

    // The note table's index: (note - note_base + transpose) & note_mask.
    u8 note_base, note_mask;
    // Event bytes from here on are commands; D8 up to it set the volume
    // when `volume_events`.
    u8 first_event_command;
    bool volume_events;
    // Envelope numbers 80 and above: an ADSR envelope (E0), or no envelope
    // code (the note-off routine's A is the attenuation).
    enum class HighEnvelopes { adsr, note_off_level } high_envelopes;

    // Drums (music noise channel events 81-FF): picked by the lowest bit set
    // or by the exact value 80 | bit; per bit 0-5 the envelope, volume and
    // noise mode (added to noise_base). Volumes are offsets from drum_volume
    // when `drum_volume_added`.
    enum class DrumMatch { lowest_bit, exact } drum_match;
    bool drum_volume_added;
    struct Drum {
        u8 envelope, volume, mode;
    };
    std::array<Drum, 6> drums;

    // Commands only one revision has.
    bool env_base_command;      // E5: the ADSR envelope's base
    bool volume_command;        // E9: set the volume
    bool loop_exit_command;     // FC: leave a loop on its last pass
    bool unused_commands_throw; // EF, F1: would run data as code (otherwise nothing)
    // A channel's end clears the priority only when no effect plays any
    // more, scanning the effect records (which leaves IX on one of them).
    bool priority_waits_for_effects;
};

class SoundDriver {
public:
    SoundDriver(const Assets& rom, const DriverProfile& profile) : rom_(rom), profile_(profile) {}

    SoundState state;

    // The game asks for a song or effect (in the profile's default slot
    // unless another is named). All slots compete by priority at the next
    // tick.
    void request(u8 id) { request(id, profile_.default_slot); }

    void request(u8 id, unsigned slot) { state.requests.at(slot) = id; }

    void set_pause(u8 v) { state.pause = v; } // 80 holds the sound (the game's pause)

    void init(); // S2 441E, TT 508F: silence the chip, idle
    void tick(); // S2 4000, TT 4C42

    std::vector<SoundWrite> take_writes() { return std::move(writes_); }

private:
    const Assets& rom_;
    const DriverProfile& profile_;
    std::vector<SoundWrite> writes_;
    unsigned channel_{}; // the channel being processed (IX)
    unsigned de_{};      // the bytecode position (DE)

    // IY as the routines leave it (see modulate): the request slots, a
    // channel's record, or a bytecode/table address.
    struct Pointer {
        enum class To { requests, channel, data } to{To::requests};
        unsigned value{}; // channel number or address
    } iy_;
    // Set by the routines that end with extra POPs (the end of a channel,
    // the noise giving up, the table envelope's 81/82 and the modulation
    // table's 81/82): the channel routine ends; on the music noise channel,
    // whose commands run with the driver's own return address below them,
    // the whole tick ends.
    enum class Stop { none, channel, tick } stop_{Stop::none};
    bool noise_running_{};

    SoundChannel& ch() { return state.channels[channel_]; }

    // A byte of the current record's stack area (offsets 27-2F).
    u8& stack_byte(unsigned offset);
    // The byte 3 past where IY points (the vibrato quirk).
    u8 iy_plus_3() const;

    void out(std::uint8_t value) { writes_.push_back({0x7F, value}); }

    void out_stereo(std::uint8_t value) { writes_.push_back({0x06, value}); }

    u8 data(unsigned address) const; // bytecode / tables

    void run_channels();                                  // S2 4038
    void take_requests();                                 // S2 409F, TT 4CC0
    void take_request(u8& slot);                          // S2 40B0, TT 4CD1
    void slow_tempo();                                    // S2 4086, TT 4CA8
    void fade();                                          // S2 40D0, TT 4CF1
    void start_request();                                 // S2 4139, TT 4D85
    void start_music(u8 id);                              // S2 4188, TT 4DD8
    void start_effect(u8 id);                             // S2 41C7, TT 4E1D
    void setup_effect_channel(unsigned& header, u8 unit); // S2 4217, TT 4E72
    void setup_music_channel(unsigned& header, u8 unit);  // S2 4254, TT 4EAF
    void stop_all();                                      // S2 4402, TT 5070
    void silence();                                       // S2 441E, TT 508F (without idling)
    void tone_channel();                                  // S2 44E5, TT 51D1
    void noise_channel();                                 // S2 466D, TT 52CE
    void next_event();                                    // S2 436A, TT 4FCC
    void read_duration(u8 a);                             // S2 43AE, TT 501C
    void finish_event();                                  // S2 43B7, TT 5025
    unsigned note_frequency(u8 note);                     // S2 4438, TT 50A9
    u8 scale(u8 a);                                       // S2 444F, TT 50C7
    void command(u8 code);                                // S2 46F9, TT 5357
    void start_modulation();                              // S2 4290, TT 4EEB
    void start_envelope();                                // S2 42BB, TT 4F16
    unsigned modulate();                                  // S2 42D8, TT 4F33
    bool envelope(u8& level); // S2 4569, TT 5255; false: the channel routine ends
    bool adsr_envelope(u8& level); // S2 4571
    u8 note_off();                 // S2 4609, TT 5263; returns A as the original leaves it
    void mute();                   // S2 465C, TT 52BD
    void drum(u8 a);               // S2 4697, TT 52F8
    void stop_channel();           // S2 4819, TT 5482
    void set_stereo(u8 old_mask, u8 new_mask_complement, u8 id); // S2 475C, TT 53B8
};

// Channel numbers.
namespace sound_channel {
inline constexpr unsigned tone1 = 0, tone2 = 1, tone3 = 2, noise = 3, effect_tone2 = 4,
                          effect_tone3 = 5, effect_noise = 6;
} // namespace sound_channel

} // namespace gg_engine
