#pragma once
// Recovered resident ARM9 sound driver (sequenced music and effects).
//
// Source: ARM9 resident image 0x02000d64-0x0200ba7c (expanded image, ROM
// SHA-256 76ed9184...5598); event table 0x020436e8, CC defaults 0x020463a4+0x64,
// pitch tables 0x02043de8/0x02043ee0, envelope time tables 0x02043aa8/0x02043ba8,
// LFO waveform table 0x02043da8, pan curve 0x02043e60. The game drives it from
// overlay 2 (lt2SoundObject, 0x0207d000-0x02080400); see sound_object.hpp.
// Validated tick by tick against the ROM driver (research notes).
//
// Ownership model (native): the driver never owns file bytes or sample memory.
// Loaded files and copied samples live in caller-owned memory (overlay-2
// pools); the driver keeps pointers into it exactly as the original keeps RAM
// addresses. Original heap allocations (0x02001214) are accounted by a heap
// model with the same first-fit placement so out-of-memory outcomes match.
// Hardware is reached through SoundCommands, the SDK command boundary.
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace picarat::dse {

// SDK sound command boundary (ARM9 0x020210bc..0x02021338 -> 0x020213d0).
// Argument packing is described in ds_sound.hpp / sound_engine.cpp.
struct SoundCommands {
    virtual ~SoundCommands() = default;
    virtual void startTimers(std::uint32_t channels, std::uint32_t capture, std::uint32_t alarms,
                             std::uint32_t flags) = 0; // 0x0c
    virtual void stopTimers(std::uint32_t channels, std::uint32_t capture, std::uint32_t alarms,
                            std::uint32_t flags) = 0; // 0x0d
    virtual void setupPcm(unsigned channel, unsigned format, const std::uint8_t* data,
                          unsigned repeat, unsigned loopStart, unsigned loopLength, unsigned volume,
                          unsigned shift, unsigned timer, unsigned pan) = 0; // 0x0e
    virtual void setupPsg(unsigned channel, unsigned duty, unsigned volume, unsigned shift,
                          unsigned timer, unsigned pan) = 0; // 0x0f
    virtual void setupNoise(unsigned channel, unsigned volume, unsigned shift, unsigned timer,
                            unsigned pan) = 0;                                           // 0x10
    virtual void setTimer(std::uint32_t channels, unsigned timer) = 0;                   // 0x13
    virtual void setVolume(std::uint32_t channels, unsigned volume, unsigned shift) = 0; // 0x14
    virtual void setPan(std::uint32_t channels, unsigned pan) = 0;                       // 0x15
    virtual void setMasterVolume(unsigned volume) = 0;                                   // 0x17
    virtual std::uint16_t channelStatus() = 0; // SDK 0x02021c74 (ARM7 shared work, busy bits)

    // 0x12 via SDK 0x02021188: ARM7 alarm setup (tick and period in 64-clock
    // units); id is the per-alarm setup counter of 0x02021bd4.
    virtual void setupAlarm(unsigned alarm, std::uint32_t tick, std::uint32_t period,
                            std::uint8_t id) {
        (void)alarm;
        (void)tick;
        (void)period;
        (void)id;
    }
};

// Stream file reader (FS calls of 0x02006b28 / 0x02006b9c).
struct StreamFile {
    virtual ~StreamFile() = default;
    virtual bool read(std::uint32_t offset, std::uint8_t* destination, std::uint32_t length) = 0;
    virtual std::uint32_t size() const = 0;
};

// Stream system configuration (0x020049e4 defaults, overlay 2 0x0207e540).
struct StreamConfig {
    std::uint8_t threadPriority = 6;    // +0x00
    std::uint8_t streams = 1;           // +0x01 pool size
    std::uint8_t voices = 2;            // +0x02 reserved voices (0: 16 - first)
    std::uint8_t firstVoice = 12;       // +0x03
    std::uint32_t bufferBytes = 0x2000; // +0x0c ring budget (0: 0x2000)
};

// Stream event callback (+0x1e8): 1 created, 2 closed, 8 ended, 9 stopped, 0xd looped.
using StreamCallback = std::function<void(std::uint32_t id, std::int32_t event)>;

// Overlay-2 file reader for the main sample bank (FS_* calls of 0x02002354/0x02002bb4).
struct BankFile {
    virtual ~BankFile() = default;
    virtual bool read(std::uint32_t offset, std::uint8_t* destination, std::uint32_t length) = 0;
};

// Research trace hook: called before every sequence byte the sequencer reads
// as an opcode (0x02008278). Null in the game.
extern void (*traceStep)(const std::uint8_t* cursor, std::uint8_t op);

// Event callback (player +0xb8/+0xbc): (sequence id, event, value).
using PlayerCallback =
    std::function<void(std::uint32_t id, std::int32_t event, std::int32_t value)>;

// Parameter block built by 0x02002094 / consumed by 0x02003520 (stack layout
// of 0x02003000, 0x02003128, 0x02004214).
struct PlayParameters {
    std::uint8_t tempoValid = 0;   // +0x00
    std::uint8_t pitchValid = 0;   // +0x01
    std::uint8_t volumeValid = 0;  // +0x02
    std::uint8_t panValid = 0;     // +0x03
    std::int32_t startDelayMs = 0; // +0x04 (negative: no delay set)
    std::int16_t tempo = 0;        // +0x08
    std::uint16_t tempoMs = 0;     // +0x0a
    std::int16_t pitch = 0;        // +0x0c
    std::uint16_t pitchMs = 0;     // +0x0e
    std::int8_t volume = 0;        // +0x10
    std::int8_t volumeAux = 0;     // +0x11
    std::uint16_t volumeMs = 0;    // +0x12
    std::int8_t pan = 0;           // +0x14
    std::uint16_t panMs = 0;       // +0x16
};

// Overlay-2 request block (0x0207dab4 stack; consumed by 0x02002094).
struct RequestBlock {
    std::array<std::uint16_t, 0x18> half{};
};

PlayParameters buildParameters(const RequestBlock& request,
                               std::int32_t startDelayMs); // 0x02002094

// The driver's constant tables. They are data in the resident ARM9 program,
// so they come from the user's own ROM: the importer saves the expanded
// program in the pack (code/arm9.bin) and fromArm9() reads them from it.
struct SoundTables {
    std::array<std::uint16_t, 12 * 256> pitch{};    // 0x02043ee0: 12 semitones x 256 fractions
    std::array<std::int32_t, 128> envelopeMs{};     // 0x02043ba8: envelope times (ms)
    std::array<std::uint16_t, 128> envelopeUnits{}; // 0x02043aa8: envelope time units
    std::array<std::uint8_t, 128> panCurve{};       // 0x02043e60: pan position -> level

    // `program` is the expanded ARM9 program, loaded at 0x02000000.
    static SoundTables fromArm9(std::span<const std::uint8_t> program);
    // The same tables from any program loaded at `load`, at the given
    // addresses (the third adventure keeps its driver in overlay 1).
    static SoundTables read(std::span<const std::uint8_t> program, std::uint32_t load,
                            std::uint32_t pitch, std::uint32_t envelopeMs,
                            std::uint32_t envelopeUnits, std::uint32_t panCurve);
};

class Driver {
public:
    Driver(SoundCommands& hardware, const SoundTables& tables);
    ~Driver();
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    // 0x02000dac. heapSize: bytes of the driver heap (overlay 2 passes 0x20000
    // at 0x02108760); mode: init byte +0x0c (4 in this game).
    int initialize(std::uint32_t heapBase, std::uint32_t heapSize, std::uint8_t mode);
    void tick(); // one sound-thread iteration (0x02008130 loop body)
    // 0x02000fcc / 0x02001078: pause and resume the whole sound system (lid
    // close in ov2 0x0206aebc). Pausing stops ARM7 alarm 1, which paces the
    // sound thread, so the host must stop calling tick() until alarm 1 runs.
    int pause(int ms);
    int resume(int ms);

    // Main sample bank (0x02002260 / 0x0200229c).
    int openMainBank(std::unique_ptr<BankFile> file);
    void closeMainBank(int handle);
    // Program bank (0x020022c8 / 0x020022ec), sample sizing and copy (0x02002814).
    int registerProgramBank(std::uint8_t* data, std::size_t size);
    void unregisterProgramBank(int handle);
    int sampleBytes(int handle);                       // 0x02002318
    int loadSamples(int handle, std::uint8_t* buffer); // 0x02002334
    // SMD sequences (0x02002cfc .. 0x02002da8 wrappers).
    int loadSequence(const std::uint8_t* data, std::size_t size, PlayerCallback callback);
    void unloadSequence(int id);
    int sequencePlaying(int id);                                           // 0x02002fb8
    int playSequence(int id, std::uint8_t volume, std::uint16_t fadeMs);   // 0x02003000
    int resumeSequence(int id, std::uint8_t volume, std::uint16_t fadeMs); // 0x02003128
    int stopSequence(int id, int fadeMs);                                  // 0x020031dc
    int sequenceTempo(int id, int scale, int ms);                          // 0x02003268
    int sequenceVolume(int id, int volume, int ms);                        // 0x02003354
    void stopAllSequences();                                               // 0x0200321c
    // SED effects (0x02003e4c, 0x02003e68, 0x02003eac, 0x02003eec, 0x02003e94).
    int loadEffects(const std::uint8_t* data, std::size_t size);
    void unloadEffects(int handle);
    int playEffect(std::uint32_t id, std::uint16_t group,
                   const PlayParameters& parameters);                   // 0x02004214
    void stopEffect(std::uint32_t id, std::uint16_t group, int fadeMs); // 0x020046e4
    int effectQuery(std::uint32_t id, std::uint32_t argument);          // 0x020041b4
    void stopAllEffects();                                              // 0x02004768
    int configureEffectPlayers(int count, int tracks);                  // 0x02003df8 -> 0x02003f08
    // SAD streams (0x02004a30 .. 0x02006bdc). Streams play through reserved
    // driver voices; the caller schedules alarm 0 and calls streamAlarm().
    int streamInit(const StreamConfig& config); // 0x02004f8c
    void streamMonoOnTwoVoices(bool enable);    // 0x02004a48 (overlay 2 0x0207e594 passes 1)
    int streamCreate(std::unique_ptr<StreamFile> file,
                     int mode); // 0x020052b4 (file opened by the caller)
    int streamStart(std::uint32_t id, std::uint8_t volume, int fadeMs,
                    std::uint16_t user);                                 // 0x02005814
    int streamStop(std::uint32_t id, int fadeMs);                        // 0x02005970
    int streamVolume(std::uint32_t id, std::uint8_t volume, int fadeMs); // 0x02005a14
    int streamPlaying(std::uint32_t id);                                 // 0x020057e0
    int streamQuery(std::uint32_t id);
    // 0x02004bd8: level metering flag (+0x14); returns the previous flag or -0x40.
    int streamSetMeter(std::uint32_t id, std::uint8_t enable);
    // 0x02004c04: per-channel {min, max} of the last refilled half (+0x200),
    // zero while metering is off. Returns the channel count or -0x40.
    int streamLevels(std::uint32_t id, std::array<std::int16_t, 4>& levels);
    // 0x02004b60 status +8: bytes consumed from the payload (+0x44 - +0x3c), or -0x40.
    std::int32_t streamPosition(std::uint32_t id);
    int streamSeek(std::uint32_t id, std::int32_t bytes);               // 0x02005b34
    std::int32_t streamBytesToMs(std::uint32_t id, std::int32_t bytes); // 0x02004d90
    std::int32_t streamMsToBytes(std::uint32_t id,
                                 std::int32_t ms); // 0x02004cf8  // 0x02005270: handle while the
                                                   // file is open, 0 when not, -0xb1 unknown
    int streamClose(std::uint32_t id);             // 0x020056f0
    int streamSetCallback(std::uint32_t id, StreamCallback callback); // 0x02005788
    void streamAlarm(); // 0x020060e4 loop body (alarm 0)
    // End of the stream ring buffer containing p (hardware reads stop there), or nullptr.
    const std::uint8_t* streamMemoryLimit(const std::uint8_t* p) const;
    // Research labels: channel ring and byte offset of p, or false.
    bool streamLocate(const std::uint8_t* p, unsigned& channel, std::uint32_t& offset) const;
    // Research: channel ring of stream id (empty if none).
    std::span<const std::uint8_t> streamRing(std::uint32_t id, unsigned channel) const;

    std::int16_t lastError() const { return error_; }

    std::uint32_t counter() const { return counter_; }

    // Research/validation only: set a recovered global by its original
    // address (history-dependent state such as the persistent stop mask).
    void debugPoke(std::uint32_t address, std::uint32_t value);

    struct Impl; // recovered state (dse_driver.cpp)

private:
    std::unique_ptr<Impl> impl_;
    std::int16_t error_ = 0;
    std::uint32_t counter_ = 0;
};

} // namespace picarat::dse
