#pragma once
// The sequencer that plays the first adventure's music and effects: the SDK
// sound driver on the DS's second processor (ARM7, code autoloaded to WRAM at
// 037f8000), rewritten from that code.
//
// How it works
// ------------
// Up to 16 players each run a sequence: byte code with up to 16 tracks
// (32 tracks in all) that play notes, wait, loop, call, jump and set
// parameters (volume, pan, transpose, pitch bend, envelope, vibrato, sweep).
// A note looks its instrument up in the player's bank (a sample from one of
// the bank's wave archives, a square-wave tone or noise) and takes one of the
// 16 hardware channels: free ones first, else the lowest priority, the
// quietest of equals, in a fixed channel order. Every channel runs an
// envelope (attack, decay to the sustain level, release), a sweep (glide
// towards the note) and an LFO (vibrato, tremolo or auto-pan); volumes are
// summed in centibels and turned into the hardware's volume and divider by
// the BIOS volume curve, pitches (64 per semitone) into timer values by the
// BIOS pitch curve.
//
// The driver's thread runs one pass (037ff008) on every expiry of its alarm
// (0xaa8 timer units of 64 clocks: 174,592 clocks, about 5.2 ms) and after
// every batch of commands from the game, without advancing time then:
//   1. write the channel changes the previous pass decided (037ff1c0);
//   2. take the game's commands (03801f1c);
//   3. players: on a tick, run as many sequence steps as the tempo allows
//      (240 units a step, tempo x ratio / 256 added per tick), then pass
//      track parameters to their channels (037fff54);
//   4. channels: envelopes, sweeps, LFOs, volume, timer and pan (037ff3ac);
//   5. report which channels play (03801c8c).
//
// The hardware itself is DsSound (ds_sound.hpp). Constant tables are read
// from the program (DriverTables); the two BIOS curves are computed.
#include "sound/ds_sound.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>

namespace picarat::sdat {

// Tables of the ARM7 program (addresses after its autoload).
struct DriverTables {
    std::array<std::int16_t, 128> decibels{};    // 03806e84: 0..127 in centibels
    std::array<std::uint8_t, 19> attack{};       // 03807098: rates for attack 109..127
    std::array<std::int8_t, 33> sine{};          // 03806e60: a quarter sine, 0..127
    std::array<std::uint8_t, 16> channelOrder{}; // 03807088: allocation order
    std::array<std::uint8_t, 4> volumeShift{};   // 03807084: divider code -> shift
    std::uint32_t randomSeed = 0;                // 03807404

    // `program` is the ARM7 program as the ROM header loads it (02380000);
    // its start-up code copies the autoload segments, listed at 023801d0.
    static DriverTables fromProgram(std::span<const std::uint8_t> program);
};

// BIOS SWI 0x1b: 65536 * (2^(i/768) - 1), rounded (i < 768).
std::uint16_t pitchCurve(unsigned i);
// BIOS SWI 0x1c: the hardware volume for centibel step i (0 = -72.3 dB ..
// 723 = 0 dB) within the range of its divider.
std::uint8_t volumeCurve(unsigned i);

// A bank as the driver reads it: the file and the wave archives the game
// linked into its four slots (empty when not loaded).
struct LoadedBank {
    std::span<const std::uint8_t> data;
    std::array<std::span<const std::uint8_t>, 4> waveArchives{};
};

// What a bank plays for a program and key (03801948): a sample from one of
// the bank's wave archives, a tone or noise, with its base key, envelope and
// pan. Programs can be one instrument, a drum set (one per key) or a key
// split (up to eight ranges).
struct Instrument {
    std::uint8_t type = 0; // 1 sample, 2 tone (duty in `wave`), 3 noise, 4 sample by address
    std::uint16_t wave = 0, archive = 0;
    std::uint8_t baseKey = 60, attack = 0, decay = 0, sustain = 0, release = 0, pan = 64;
};

std::optional<Instrument> findInstrument(std::span<const std::uint8_t> bank, int program, int key);

class Sequencer {
public:
    Sequencer(DsSound& hardware, DriverTables tables);

    // The game's commands (SDK command numbers). They wait for the next pass.
    void prepare(unsigned player, std::span<const std::uint8_t> code, std::uint32_t offset,
                 const LoadedBank* bank); // 2: 038000b4
    void start(unsigned player);          // 3: 038002f0
    void stop(unsigned player);           // 1: 03800340
    void pause(unsigned player, bool on); // 4: 0380039c
    // 6: 038006a4, a player field: +4 priority (1 byte), +6 volume in
    // centibels (2 bytes).
    void setPlayerPriority(unsigned player, std::uint8_t priority);
    void setPlayerVolume(unsigned player, std::int16_t centibels);
    // 9: 0380054c, the channels the given tracks may take.
    void setTrackChannels(unsigned player, std::uint16_t tracks, std::uint16_t channels);
    // 8: 038004e0.
    void muteTracks(unsigned player, std::uint16_t tracks, unsigned mode);
    // 10, 11: 03801c50 / 03801c70.
    void setVariable(unsigned player, unsigned index, std::int16_t value);
    void setGlobalVariable(unsigned index, std::int16_t value);
    // 0x1a, 0x1b, 0x1c: channels kept from sequences (037ffbe0, 037ffcc0,
    // 037ffb30).
    // `lockedOnly`: kept only from tracks without their own channel mask.
    void lockChannels(std::uint32_t channels, bool lockedOnly);
    void unlockChannels(std::uint32_t channels, bool lockedOnly);

    // 0x1e, 0x1f, 0x20 (038005bc, 03800648, 037ffd14): stop what plays from
    // data about to be unloaded: players whose tracks read the code, players
    // using the bank, channels playing samples of the wave archive.
    void stopUsingCode(std::span<const std::uint8_t> code);
    void stopUsingBank(const LoadedBank* bank);
    void stopUsingWaves(std::span<const std::uint8_t> waves);

    // Channels used directly (the first adventure's streams), without the
    // sequencer's allocation; the commands step writes them at once.
    // 0x0e (037fe804): set a channel up; `length` and `loopStart` in words.
    void setupWave(unsigned channel, unsigned format, std::span<const std::uint8_t> data, bool loop,
                   unsigned loopStart, unsigned length, unsigned volume, unsigned shift,
                   unsigned timer, unsigned pan);
    // 0x0c / 0x0d: start / stop channels and alarms (masks).
    void startDirect(std::uint32_t channels, std::uint32_t alarms);
    void stopDirect(std::uint32_t channels, std::uint32_t alarms);
    // 0x12 (03801d50): alarm `id` fires `tick` units of 64 clocks after it
    // starts, then every `period` units.
    void setupAlarm(unsigned id, std::uint32_t tick, std::uint32_t period);
    // 0x14 (037fea48): the volume of directly used channels.
    void setDirectVolume(std::uint32_t channels, unsigned volume, unsigned shift);
    // Told when the commands step starts or stops an alarm (the host owns
    // the timing).
    std::function<void(unsigned alarm, bool start, std::uint32_t tick, std::uint32_t period)>
        alarmChanged;

    // The commands given since the last flush go to the driver as one batch
    // (SDK 020a0428); the driver takes them in its next pass.
    void flush();
    // One pass of the driver's thread; `tick` on an alarm expiry.
    void pass(bool tick);

    // How many of the flushed batches have reached the driver (a host that
    // times the hand-over limits what a pass takes; default: all).
    void setArrived(std::uint32_t batches) { arrived_ = batches; }

    // The register writes a pass makes, in the order the driver makes them
    // (037ff1c0): a host that times them (Adventure1Sound) is told before
    // each one.
    enum class Write { Stop, Setup, Timer, Volume, Pan, Start, Direct };
    std::function<void(Write, unsigned channel)> beforeWrite;
    // And as the channel update (037ff3ac) reaches an active channel; `reads`
    // when it is about to read the channel's busy bit.
    std::function<void(unsigned channel, bool reads)> beforeChannelUpdate;
    // And after the writes, before the pass takes the arrived batches.
    std::function<void()> beforeCommands;
    // And before each command it takes (a host times the direct writes).
    enum class Step { Other, Setup, Start, Stop, Alarm, Volume };
    std::function<void(Step)> beforeCommand;

    // The work of the last pass (research: the driver's execution time).
    struct Work {
        unsigned commands = 0, playerTicks = 0, steps = 0, commandsRead = 0, notes = 0,
                 trackUpdates = 0, channelsActive = 0;
    };

    Work lastWork() const { return work_; }

    // Whether a delivered batch waits for a pass.
    bool commandsPending() const { return !commands_.empty(); }

    // Batches delivered and processed (the SDK's command tags, 020a067c).
    std::uint32_t batchesSent() const { return batchesSent_; }

    std::uint32_t batchesDone() const { return commandsDone_; }

    // Shared work the game reads: players running (+4), variables.
    std::uint32_t activePlayers() const { return activePlayers_; }

    std::int16_t variable(unsigned player, unsigned index) const;

private:
    struct Lfo {
        std::uint8_t type = 0, speed = 16, depth = 0, range = 1; // type 0 pitch, 1 volume, 2 pan
        std::uint16_t delay = 0, counter = 0, phase = 0;
    };

    struct Channel { // 0x54-byte channel records

        enum Type : std::uint8_t { Wave, Tone, Noise };

        enum Envelope : std::uint8_t { Attack, Decay, Sustain, Release };

        unsigned index = 0;
        Type type = Wave;
        Envelope state = Attack;
        bool active = false, startPending = false, autoSweep = false;
        std::uint8_t updates = 0; // 1 start, 2 stop, 4 timer, 8 volume, 0x10 pan
        std::uint8_t panRange = 127, baseKey = 60, key = 60, velocity = 127;
        std::int16_t volumeOffset = 0; // +6: the track's and player's extra centibels
        std::int8_t instrumentPan = 0, trackPan = 0;
        std::int16_t volume = 0; // +0xc: player, track and expression in centibels
        std::int16_t pitch = 0;  // +0xe: bend and the track's offset
        std::int32_t level = 0;  // envelope, centibels x 128
        std::int32_t sweepCounter = 0, sweepLength = 0;
        std::uint8_t attack = 0, sustain = 127;
        std::uint16_t decay = 0xffff, release = 0xffff;
        std::uint8_t priority = 0;
        std::uint8_t hardwarePan = 0;
        std::uint16_t hardwareVolume = 0, hardwareTimer = 0; // volume | divider << 8
        Lfo lfo;
        std::int16_t sweep = 0; // +0x32: pitch distance still to glide
        std::int32_t length = 0;
        // The sample: format, loop, the timer of its base key, loop start and
        // length in words; or the tone's duty.
        std::uint8_t format = 0, loop = 0;
        std::uint16_t baseTimer = 0, loopStart = 0;
        std::uint32_t loopLength = 0;
        std::span<const std::uint8_t> data;
        unsigned duty = 0;
        int owner = -1; // track whose notes it plays (its callback data)
        int nextInTrack = -1;
    };

    struct Track { // 0x40-byte track records
        bool active = false;
        bool noteWait = true, muted = false, tie = false, noteEndWait = false;
        bool portamento = false, condition = true, ownChannels = false;
        std::uint8_t panRange = 127;
        std::uint16_t program = 0;
        std::uint8_t volume = 127, expression = 127;
        std::int8_t pitchBend = 0;
        std::uint8_t bendRange = 2;
        std::int8_t pan = 0, extPan = 0;
        std::int16_t extVolume = 0, extPitch = 0;
        std::uint8_t attack = 0xff, decay = 0xff, sustain = 0xff, release = 0xff;
        std::uint8_t priority = 64;
        std::int8_t transpose = 0;
        std::uint8_t portamentoKey = 60, portamentoTime = 0;
        std::int16_t sweepPitch = 0;
        Lfo lfo;
        std::uint16_t channelMask = 0xffff;
        std::int32_t wait = 0;
        std::uint32_t base = 0, position = 0; // offsets into the player's code
        std::array<std::uint32_t, 3> stack{};
        std::array<std::uint8_t, 3> loops{};
        std::uint8_t depth = 0;
        int channels = -1; // first channel of its list
    };

    struct Player { // 0x24-byte player records
        bool active = false, started = false, paused = false;
        std::uint8_t priority = 64, volume = 127;
        std::int16_t volumeOffset = 0;
        std::array<std::uint8_t, 16> tracks{};
        std::uint16_t tempo = 120, tempoRatio = 256, tickCounter = 240;
        std::span<const std::uint8_t> code;
        const LoadedBank* bank = nullptr;
    };

    struct Command {
        enum Kind {
            Prepare,
            Start,
            Stop,
            Pause,
            Priority,
            Volume,
            Channels,
            Mute,
            Variable,
            Global,
            Lock,
            Unlock,
            StopCode,
            StopBank,
            StopWaves,
            SetupWave,
            StartDirect,
            StopDirect,
            SetupAlarm,
            DirectVolume,
            EndBatch
        } kind;

        unsigned player = 0;
        std::uint32_t a = 0, b = 0, c = 0, d = 0;
        std::span<const std::uint8_t> code;
        const LoadedBank* bank = nullptr;
    };

    DsSound& hardware_;
    DriverTables tables_;
    std::array<Channel, 16> channels_{};
    std::array<Track, 32> tracks_{};
    std::array<Player, 16> players_{};
    std::array<std::array<std::int16_t, 16>, 16> variables_{};
    std::array<std::int16_t, 16> globals_{};
    std::array<std::uint32_t, 16> playerTicks_{};
    std::uint32_t locked_ = 0, reserved_ = 0;

    struct Alarm {
        std::uint32_t tick = 0, period = 0;
    };

    std::array<Alarm, 8> alarms_{};
    std::uint32_t activePlayers_ = 0, random_ = 0;
    std::uint32_t commandsDone_ = 0, batchesSent_ = 0, arrived_ = 0xffffffff;
    std::deque<Command> building_, commands_;
    Work work_;

    std::uint32_t random();
    void post(Command::Kind kind, unsigned player, std::uint32_t a = 0, std::uint32_t b = 0);
    void execute(const Command& c);

    // Players and tracks.
    void preparePlayer(unsigned p, std::span<const std::uint8_t> code, std::uint32_t offset,
                       const LoadedBank* bank);
    void stopPlayer(unsigned p);
    void closeTracks(Player& player);
    void closeTrack(Player& player, unsigned slot);
    Track* track(const Player& player, unsigned slot);
    int allocateTrack();
    static void resetTrack(Track& t);
    void tickPlayers(bool tick);
    bool step(unsigned p, bool play);
    std::uint8_t read(const Player& player, Track& t);
    std::int32_t argument(const Player& player, unsigned p, Track& t, int type);
    std::int16_t* variablePointer(unsigned p, unsigned index);
    void updateTrack(Track& t, const Player& player, bool releaseFinished);
    void releaseTrack(Track& t, const Player& player, int release);
    void freeTrackChannels(Track& t);
    void muteTrack(Track& t, const Player& player, unsigned mode);
    void noteOn(unsigned p, Track& t, int key, int velocity, std::int32_t length);

    // Channels.
    int allocateChannel(std::uint32_t mask, int priority, bool ownChannels, int owner);
    void releasedBy(Channel& c, bool ended);
    bool startNote(Channel& c, int key, int velocity, std::int32_t length, const LoadedBank& bank,
                   const Instrument& instrument);
    void startEnvelope(Channel& c, std::int32_t length);
    std::int32_t envelope(Channel& c, bool tick);
    std::int32_t lfoValue(const Lfo& l) const;
    static void advanceLfo(Lfo& l);
    std::uint8_t attackRate(int attack) const;
    static std::uint16_t fallRate(int rate);
    int sine(int x) const;
    std::uint16_t hardwareVolume(int centibels) const;
    static std::uint16_t timer(std::uint16_t base, int pitch);
    void updateChannels(bool tick);
    void applyChannels();
    void stopChannels(std::uint32_t channels);
};

} // namespace picarat::sdat
