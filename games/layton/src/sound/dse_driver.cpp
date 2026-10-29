// Recovered resident ARM9 sound driver. See dse_driver.hpp for provenance.
//
// Every function names its original entry point. Integer widths, signedness,
// truncating division (0x0203b744 signed / 0x0203b950 unsigned, both
// returning the numerator for a zero divisor) and multiply-high reciprocal
// divisions are kept as in the ARM code. Interrupt masking (REG_IME writes)
// has no native counterpart: the driver runs on one thread here.
#include "sound/dse_driver.hpp"
#include "sound/sad_stream.hpp"
#include "system/resources.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <list>
#include <stdexcept>

namespace picarat::dse {
namespace {

// ---- machine arithmetic ----------------------------------------------------
std::int32_t sdiv(std::int32_t a, std::int32_t b) { // ARM9 0x0203b744
    if (b == 0) {
        return a;
    }
    if (a == INT32_MIN && b == -1) {
        return INT32_MIN;
    }
    return a / b;
}

std::uint32_t udiv(std::uint32_t a, std::uint32_t b) {
    return b ? a / b : a;
} // ARM9 0x0203b950

std::int32_t mulhs(std::int32_t a, std::int32_t b) {
    return std::int32_t((std::int64_t(a) * b) >> 32);
}

std::uint32_t mulhu(std::uint32_t a, std::uint32_t b) {
    return std::uint32_t((std::uint64_t(a) * b) >> 32);
}

// smull/add/asr #n/add sign: signed reciprocal division used by the compiler.
std::int32_t magic(std::int32_t x, std::int32_t m, unsigned shift) {
    std::int32_t q = (mulhs(m, x) + x) >> shift;
    return q + std::int32_t(std::uint32_t(x) >> 31);
}

std::int32_t div127(std::int32_t x) {
    return magic(x, std::int32_t(0x81020409), 6);
} // x / 127

std::int32_t div16129(std::int32_t x) {
    return magic(x, std::int32_t(0x82061029), 13);
} // x / 16129

std::int16_t s16(std::int32_t x) {
    return std::int16_t(std::uint16_t(x));
}

std::int8_t s8(std::int32_t x) {
    return std::int8_t(std::uint8_t(x));
}

// Signed shift of a value toward zero (asr with the compiler's rounding fixup).
std::int32_t sar0(std::int32_t x, unsigned n) {
    return (x + std::int32_t(std::uint32_t(x >> (n - 1)) >> (32 - n))) >> n;
}

std::uint32_t tag(const char* s) {
    return std::uint32_t(s[0]) << 24 | std::uint32_t(s[1]) << 16 | std::uint32_t(s[2]) << 8 |
           std::uint32_t(s[3]);
}

std::uint32_t chunkTag(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
} // 0x02001150(le32)

// Chunk walk step used by all loaders: next = chunk + align(size + 0x10, chunk[8]).
const std::uint8_t* nextChunk(const std::uint8_t* chunk) {
    std::uint32_t align = chunk[8], size = le32(chunk + 0xc);
    std::uint32_t step = (size + 0x10 + (align - 1)) & ~(align - 1);
    return chunk + step;
}

// ---- tables (resident data) --------------------------------------------------
// 0x02043de8: semitone -> (octave << 4 | note) for the 117 semitones the
// pitch range covers; three zero entries follow. Pitches above 0x77ff index
// past the table, into the pan curve that the ROM stores right after it.
std::uint32_t keyCode(const SoundTables& t, unsigned semitone) {
    if (semitone < 117) {
        return (semitone / 12) << 4 | semitone % 12;
    }
    return semitone < 0x78 ? 0 : t.panCurve[semitone - 0x78];
}

constexpr std::uint8_t waitTable[16] = {96, 72, 64, 48, 36, 32, 24, 18,
                                        16, 12, 9,  8,  6,  4,  3,  2}; // 0x020436d4
constexpr std::uint16_t lfoDirty[4] = {0, 16, 32, 64};                  // 0x0203e3bc

} // namespace

// ---- recovered structures ---------------------------------------------------
// Byte offsets of the original objects are given in comments.

struct Envelope {               // voice +0x3c, 0x20 bytes (copied from split +0x20)
    std::uint8_t valid = 0;     // +0x00
    std::uint8_t timeUnits = 0; // +0x01 (0: millisecond table, else unit table x value)
    std::uint8_t raw[6] = {};   // +0x02..+0x07 (copied, unused by the driver)
    std::uint8_t attackLevel = 0, attack = 0, decay = 0; // +0x08 +0x09 +0x0a
    std::int8_t sustain = 0;                             // +0x0b
    std::uint8_t hold = 0, decay2 = 0, release = 0;      // +0x0c +0x0d +0x0e
    std::uint8_t raw2 = 0;                               // +0x0f
    std::int32_t level = 0;                              // +0x10 (level << 23)
    std::int32_t step = 0;                               // +0x14
    std::int32_t remaining = 0;                          // +0x18
    std::uint8_t state = 0;                              // +0x1c
    std::uint8_t target = 0;                             // +0x1d
    std::uint8_t active = 0;                             // +0x1e
};

struct LfoParameters { // 0x10 bytes (program +0x10 / channel +0x7c)
    std::uint8_t b0 = 0, kind = 0, destination = 0, waveform = 0; // +0..+3
    std::int32_t depth = 0;                                       // +4
    std::uint16_t rateMs = 0, delayMs = 0, fadeMs = 0;            // +8 +a +c
    std::uint8_t b14 = 0, b15 = 0;                                // +e +f
};

struct Lfo;
using Waveform = std::int32_t (*)(Lfo&);

struct Lfo {                               // voice +0x6c + 0x34*n
    std::uint8_t flags = 0, kind = 0;      // +0x00 +0x01
    std::uint16_t dirty = 0;               // +0x02
    std::uint16_t period = 0, counter = 0; // +0x04 +0x06
    std::int32_t value = 0, depth = 0;     // +0x08 +0x0c
    std::int32_t step = 0;                 // +0x14
    std::uint16_t delay = 0, fade = 0;     // +0x1a +0x1e
    std::int32_t gain = 0, gainStep = 0;   // +0x20 +0x24
    std::int16_t* output = nullptr;        // +0x28
    Waveform waveform = nullptr;           // +0x2c
    Lfo* next = nullptr;                   // +0x30
};

struct LfoSet {               // voice +0x5c
    std::int16_t out[6] = {}; // +0x00..+0x0b (pitch at +2, volume +4, pan +6)
    Lfo* head = nullptr;      // +0x0c
    Lfo lfo[4];               // +0x10
};

struct ChannelState;

struct Voice {                  // 0x020464d0 + 0x15c*n
    std::uint16_t flags = 0;    // +0x000 bit1: released
    std::uint16_t dirty = 0;    // +0x002 1 start, 2 stop, 0x10 timer, 0x20 volume, 0x40 pan
    std::uint8_t maskGroup = 0; // +0x004
    std::uint8_t index = 0;     // +0x005
    std::uint16_t hardware = 0; // +0x006 SDK channel number
    std::uint32_t owner = 0;    // +0x008 player id of the channel that started it
    std::uint8_t key = 0;       // +0x00d
    std::uint8_t sample = 0;    // +0x00e
    std::uint8_t bendRange = 0; // +0x00f
    std::int16_t bend = 0;      // +0x010
    std::int16_t pitch = 0;     // +0x012
    std::int16_t tune = 0;      // +0x014
    std::uint8_t velocityVolume = 0, pan = 0x40; // +0x016 +0x017
    std::uint16_t keyGroup = 0;                  // +0x01a
    // +0x01c..+0x03b: wave entry bytes +0x10..+0x2f.
    std::uint8_t wave[0x20] = {};
    const std::uint8_t* data = nullptr;   // +0x030 (wave +0x14 after relocation)
    Envelope envelope;                    // +0x03c
    LfoSet lfo;                           // +0x05c
    std::uint32_t timer = 0;              // +0x13c
    std::uint16_t volume = 0, panOut = 0; // +0x140 +0x142
    std::uint16_t unknown146 = 1;         // +0x146
    std::uint32_t priority = 0;           // +0x148 byte0 level, byte1 in use, high half priority
    std::int32_t duration = 0;            // +0x14c
    Voice* next = nullptr;                // +0x154
    ChannelState* channel = nullptr;      // +0x158

    std::uint8_t format() const { return wave[3]; } // +0x01f

    std::uint8_t loops() const { return wave[5]; } // +0x021

    std::uint8_t duty() const { return wave[9]; } // +0x025

    std::uint32_t loopStart() const { return le32(wave + 0x18); } // +0x034

    std::uint32_t loopLength() const { return le32(wave + 0x1c); } // +0x038
};

struct ProgramBank;
struct ChannelGroup;

struct ChannelState {                                               // group +0x14 + 0xcc*n
    std::uint32_t owner = 0;                                        // +0x00
    std::uint8_t flags = 0;                                         // +0x04 bit0 hold, bit1 legato
    std::uint8_t index = 0;                                         // +0x05
    std::uint8_t b6 = 0;                                            // +0x06
    std::uint8_t voiceMode = 1;                                     // +0x07 0 mono (reuse), 1 poly
    std::uint16_t dirty = 0;                                        // +0x08
    std::uint8_t b10 = 0, b11 = 0x10;                               // +0x0a +0x0b
    std::int16_t randomPitch = 0;                                   // +0x0c
    std::uint8_t keyLow = 0, keyHigh = 0;                           // +0x0e +0x0f
    std::uint16_t bank = 0;                                         // +0x10
    std::int16_t program = 0;                                       // +0x12
    std::uint16_t h16 = 0;                                          // +0x16
    std::uint16_t pitch = 0;                                        // +0x18
    std::uint16_t volume = 0;                                       // +0x1a
    std::uint16_t pan = 0x40;                                       // +0x1c
    std::uint16_t h1e = 0, h20 = 0;                                 // +0x1e +0x20
    std::uint8_t b22 = 0x40, b23 = 0;                               // +0x22 +0x23
    std::int32_t sweep = 0, sweepStep = 0, sweepTarget = 0;         // +0x24 +0x28 +0x2c
    std::uint16_t sweepCount = 0;                                   // +0x30
    std::int32_t volumeValue = 0, volumeStep = 0, volumeTarget = 0; // +0x34 +0x38 +0x3c
    std::uint16_t volumeCount = 0;                                  // +0x40
    std::int32_t panValue = 0, panStep = 0, panTarget = 0;          // +0x44 +0x48 +0x4c
    std::uint16_t panCount = 0;                                     // +0x50
    std::uint8_t expression = 0x7f;                                 // +0x58
    std::int8_t lfoScale = 0;                                       // +0x59
    std::int16_t bend = 0;                                          // +0x5a
    std::int16_t detune = 0;                                        // +0x5c
    std::int16_t fine = 0;                                          // +0x5e
    std::uint8_t velocityScale = 0x7f, bendRange = 0;               // +0x60 +0x61
    std::uint8_t b62[7] = {};                                       // +0x62..+0x68
    std::uint8_t lfoSlot = 0;                                       // +0x69
    std::uint8_t envelopeOverride[16] = {};    // +0x6c ([0] valid; 0xff = unset)
    LfoParameters lfo[4];                      // +0x7c
    Voice* voices = nullptr;                   // +0xbc
    ProgramBank* programBank = nullptr;        // +0xc0
    const std::uint8_t* programData = nullptr; // +0xc4
    ChannelGroup* group = nullptr;             // +0xc8
};

struct ChannelGroup { // 0x14 + 0xcc*n, heap
    std::uint8_t b0 = 0;
    std::uint8_t count = 0;           // +0x01
    std::int8_t baseVolume = 0x7f;    // +0x02
    std::uint8_t volumeGroup = 1;     // +0x03
    std::uint16_t dirty = 0;          // +0x04
    std::int16_t pitch = 0;           // +0x06
    std::int8_t volume = 0x7f;        // +0x08
    std::int8_t pan = 0x40;           // +0x09
    std::int8_t combined = 0;         // +0x0a
    std::uint8_t b11 = 0, b12 = 0x10; // +0x0b +0x0c
    ChannelGroup* next = nullptr;     // +0x10
    std::vector<ChannelState> channels;
    std::uint32_t heap = 0;
};

struct Track {                            // player +0xc4 + 0x5c*n
    std::uint16_t id = 0;                 // +0x00
    std::uint8_t active = 0;              // +0x02
    std::uint8_t channel = 0;             // +0x03
    std::int8_t octave = 4;               // +0x04
    std::uint8_t gate = 0x7f;             // +0x05
    std::uint8_t depth = 0;               // +0x06
    std::uint8_t lastKey = 0;             // +0x07
    std::int32_t wait = 0;                // +0x08
    std::int32_t lastWait = 0;            // +0x0c
    std::int32_t lastDuration = 0;        // +0x10
    const std::uint8_t* start = nullptr;  // +0x14
    const std::uint8_t* cursor = nullptr; // +0x18
    const std::uint8_t* loop = nullptr;   // +0x1c

    struct Frame {
        const std::uint8_t* start = nullptr;
        const std::uint8_t* end = nullptr;
        std::uint8_t count = 0;
        std::int8_t octave = 0;
    };

    Frame stack[3];                // +0x20 (0xc each)
    ChannelState* state = nullptr; // +0x50
    std::uint8_t muted = 0;        // +0x58
};

struct Player { // 0xc4 + 0x5c*n, heap
    std::uint8_t flags =
        0; // +0x00 bit0 ready, bit1 playing, bit2/3 stopping, bit4 loop, bit5 muted, bit6
    std::uint8_t fading = 0; // +0x01 bit0 tempo, bit1 pitch, bit2 volume, bit4 pan
    std::uint8_t activeTracks = 0, maxTracks = 0, trackCount = 0, channelCount = 0; // +0x02..+0x05
    std::uint8_t b6 = 0, priority = 0, volumeGroup = 0, limitGroup = 0;             // +0x06..+0x09
    std::uint8_t ppq8 = 6, marker = 0, tempo = 0x78;                       // +0x0a +0x0b +0x0c
    std::int16_t startDelay = 0;                                           // +0x0e
    const std::uint8_t* data = nullptr;                                    // +0x10
    const std::uint8_t* entry = nullptr;                                   // +0x14
    ChannelGroup* group = nullptr;                                         // +0x18
    std::uint32_t id = 0;                                                  // +0x1c
    std::uint16_t user = 0;                                                // +0x20
    std::uint32_t age = 0;                                                 // +0x24
    std::int32_t ticks = 0, accumulator = 0, increment = 0, beat = 500000; // +0x28..+0x34
    std::int32_t tempoValue = 0x1000000, tempoStep = 0, tempoTarget = 0;
    std::uint16_t tempoCount = 0; // +0x38..+0x44
    std::int32_t pitchValue = 0, pitchStep = 0, pitchTarget = 0;
    std::uint16_t pitchCount = 0; // +0x48..+0x54
    std::int32_t volumeValue = 0x7f0000, volumeStep = 0, volumeTarget = 0;
    std::uint16_t volumeCount = 0; // +0x58..+0x64
    std::int32_t panValue = 0x400000, panStep = 0, panTarget = 0;
    std::uint16_t panCount = 0; // +0x68..+0x74
    std::uint32_t w88 = 0;      // +0x88
    std::uint8_t volume = 0x7f; // +0x8c
    PlayerCallback callback;    // +0xb8/+0xbc
    Player* next = nullptr;     // +0xc0
    std::vector<Track> tracks;  // +0xc4
    std::uint32_t heap = 0;
};

struct MainBank {                    // heap 0x60 + wave chunk
    std::uint16_t id = 0, waves = 0; // +0x00 +0x02
    std::uint32_t pcmOffset = 0;     // +0x04 file offset of PCM data
    std::vector<std::uint8_t> wave;  // +0x08 -> +0x70 (wave chunk data, copied)
    std::unique_ptr<BankFile> file;  // +0x14
    MainBank* next = nullptr;        // +0x5c
    std::uint32_t heap = 0;
};

struct ProgramBank {                         // heap 0x28
    std::uint8_t* file = nullptr;            // +0x00
    std::uint16_t id = 0, mainId = 0;        // +0x04 +0x06
    std::int16_t waves = 0, programs = 0;    // +0x08 +0x0a
    std::uint8_t b0c = 0, type = 0;          // +0x0c +0x0d
    const std::uint8_t* wave = nullptr;      // +0x10
    const std::uint8_t* program = nullptr;   // +0x14
    const std::uint8_t* keyGroups = nullptr; // +0x18
    const std::uint8_t* pcm = nullptr;       // +0x1c
    std::int32_t sampleBytes = 0;            // +0x20
    ProgramBank* next = nullptr;             // +0x24
    std::uint32_t heap = 0;
    // Relocated wave entry +0x24 words written by 0x02002814 (absolute sample
    // address in the original; here the pointer into the caller's buffer).
    std::vector<const std::uint8_t*> loaded;
};

struct EffectBank {                          // heap 0x1c
    const std::uint8_t* data = nullptr;      // +0x00
    std::int16_t count = 0;                  // +0x04
    std::uint16_t id = 0;                    // +0x06
    EffectBank* next = nullptr;              // +0x08
    const std::uint8_t* sequences = nullptr; // +0x0c ('seq ' chunk data)
    const std::uint8_t* macros = nullptr;    // +0x10 ('mcrl')
    const std::uint8_t* bankLinks = nullptr; // +0x14 ('bnkl')
    std::uint8_t flag = 1;                   // +0x18
    std::uint32_t heap = 0;
};

// First-fit heap of 0x02001214/0x02001318 (addresses only; objects are native).
class Heap {
public:
    void initialize(std::uint32_t base, std::uint32_t size) { // 0x020011a8
        if (base & 0xf) {
            base = (base & ~0xfu) + 0x10;
            size -= 0x10;
        }
        base_ = base;
        end_ = base + size;
        blocks_.clear();
        blocks_.push_back({base, base + 0x10});
    }

    std::uint32_t allocate(std::uint32_t size, std::uint32_t align = 0) { // 0x02001214
        if (!base_) {
            return 0;
        }
        if (align == 0) {
            align = 0x10;
        }
        const std::uint32_t mask = ~(align - 1);
        const std::uint32_t need = ((size + align - 1) & mask) + 0x10;
        auto it = blocks_.begin();
        for (;;) {
            auto next = std::next(it);
            std::uint32_t space = (next == blocks_.end() ? end_ : next->start) - it->end;
            if (std::int32_t(space) >= std::int32_t(need)) {
                break;
            }
            if (next == blocks_.end()) {
                return 0;
            }
            it = next;
        }
        std::uint32_t data = (it->end + align + 0xf) & mask;
        blocks_.insert(std::next(it), {data - 0x10, data + size});
        return data;
    }

    void release(std::uint32_t data) { // 0x02001318
        if (!base_) {
            return;
        }
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
            if (it->start == data - 0x10) {
                blocks_.erase(it);
                return;
            }
        }
        throw std::logic_error("driver heap release of unknown block"); // original loops forever
    }

private:
    struct Block {
        std::uint32_t start, end;
    };

    std::list<Block> blocks_;
    std::uint32_t base_ = 0, end_ = 0;
};

// ---- driver state ------------------------------------------------------------
struct Driver::Impl {
    Driver& owner;
    SoundCommands& hw;
    const SoundTables tables;

    Impl(Driver& d, SoundCommands& h, const SoundTables& t) : owner(d), hw(h), tables(t) {}

    // Globals at 0x020463a0 (G0) / 0x020463a4 (G4).
    std::uint8_t paused = 0;         // 0x020463a0
    std::uint8_t initialized = 0;    // 0x020463a1
    std::int16_t warning = 0;        // 0x020463a8
    std::int16_t tickMicros = 10000; // 0x020463aa
    std::int32_t pausedTicks = 0;    // 0x020463b0
    std::uint32_t random = 0xbc614e; // 0x020463c8
    std::uint8_t streamTimeout = 1;  // 0x020463cc
    std::uint8_t velocityMode = 2;   // 0x020463cd (7 selects the /127^3 velocity path)
    std::uint8_t effectPlayers = 0, effectTracks = 0;  // 0x020463ca/cb
    std::uint8_t resetGuard = 1;                       // 0x020463d1 (cleared during 0x02009e20)
    std::uint8_t b30 = 1, b32 = 0;                     // 0x020463d0 / 0x020463d2
    std::array<std::int8_t, 16> groupVolume{};         // 0x020463d2 (index 0 never written)
    std::array<std::uint8_t, 128> controllerDefault{}; // 0x02046408 (G4+0x64)
    std::uint8_t mode = 0;                             // 0x02047aa0 (init byte +0x0c)

    ChannelGroup* groups = nullptr;      // 0x020463e8
    MainBank* mainBanks = nullptr;       // 0x020463f0
    ProgramBank* programBanks = nullptr; // 0x020463f4
    Player* players = nullptr;           // 0x020463f8 (sequences)
    Player* effects = nullptr;           // 0x020463fc (effect players)
    EffectBank* effectBanks = nullptr;   // 0x02046400
    Heap heap;

    std::array<Voice, 16> voices;                                // 0x020464d0
    std::uint32_t startMask = 0, stopMask = 0, releasedMask = 0; // 0x020464c4/c8/cc
    std::uint8_t alarmStart = 0, alarmStop = 0, captureStart = 0, captureStop = 0; // 0x02047a90..93
    std::uint16_t lastStarted = 0;  // 0x02047a94 (0x020479a4 + 0xf0)
    std::uint16_t activeVoices = 0; // 0x02047a96 (0x020479a4 + 0xf2)
    std::uint16_t peakVoices = 0;   // 0x02047a98 (0x020479a4 + 0xf4)
    std::int16_t peakHold = 0;      // 0x02047a9a

    // ---- SAD stream system (state 0x020490bc, objects 0x280 bytes) ----
    struct Stream {
        std::uint32_t id = 0;            // +0x000 handle (counter << 16)
        std::uint8_t fileOpen = 0;       // +0x004
        std::uint8_t closing = 0;        // +0x005
        std::uint8_t loaded = 0;         // +0x007 header and decoder state ready (020069a4)
        std::uint8_t playing = 0;        // +0x009
        std::uint8_t stopNow = 0;        // +0x00a
        std::uint8_t loops = 0;          // +0x00b
        std::uint8_t ended = 0;          // +0x00d first empty refill seen
        std::uint8_t closed = 1;         // +0x00e
        std::uint8_t voicesReserved = 0; // +0x00f
        std::uint8_t running = 0;        // +0x010 voices and alarm started (02006258)
        std::uint8_t paused = 0;         // +0x011 stopped by 0x020051d8
        std::uint8_t stopAfterFade = 0;  // +0x012
        std::uint8_t clearRing = 1;      // +0x013
        std::uint8_t meter = 0;          // +0x014 level metering (0x02004bd8)
        std::uint16_t user = 0;          // +0x016
        std::uint8_t loopSetting = 2;    // +0x018 0 never, 1 always, 2 header
        std::uint8_t channels = 0;       // +0x01d
        std::uint8_t voiceCount = 0;     // +0x01e
        std::uint8_t curve = 0x7f;       // +0x021 groupVolume[header +0x62]
        std::uint8_t targetVolume = 0;   // +0x026
        std::int32_t level = 0, step = 0, target = 0; // +0x028 +0x02c +0x030 (volume << 16)
        std::int16_t count = 0;                       // +0x034 fade ticks left
        std::uint32_t rate = 0x7fd8;                  // +0x038
        std::uint8_t volume = 0x7f;                   // +0x0b4 header +0x60 (0: 0x7f)
        std::uint32_t ringBytes = 0, channelBytes = 0, halfBytes = 0; // +0x1b0 +0x1b4 +0x1b8
        std::uint32_t ringHeap = 0;                                   // +0x1bc
        std::vector<std::uint8_t> ring;       // native storage of the +0x1bc allocation
        std::array<Voice*, 2> voices{};       // +0x1d0
        std::uint8_t half = 0;                // +0x1d8 half refilled next
        std::array<std::int16_t, 4> levels{}; // +0x200 per channel {min, max} (0x02006ef0)
        StreamCallback callback;              // +0x1e8 / +0x1ec
        std::unique_ptr<StreamFile> file;     // +0x0e8 FS file
        picarat::SadStream decoder;           // 020064f4 / 02006bdc / 02007cac decoder state
        Stream* next = nullptr;               // +0x27c
        std::uint32_t heap = 0;               // pool allocation (02004e6c)

        std::uint8_t* channel(unsigned c, unsigned h) {
            return ring.data() + c * channelBytes + h * halfBytes;
        } // 0x1b0[4..7]
    };

    StreamConfig streamConfig;                     // 0x020490bc
    std::uint8_t streamsReady = 0;                 // +0x004
    std::uint32_t streamCounter = 1;               // 0x020490bc +0x00 (handle counter)
    std::uint32_t streamWork = 0, streamStack = 0; // +0x01c work buffer, +0x134 thread stack
    std::uint8_t monoTwoVoices = 0;                // +0x00e (0x02004a48)
    Stream* streams = nullptr;                     // +0x148 list (pool order)
    std::vector<std::unique_ptr<Stream>> ownedStreams;
    std::array<std::uint8_t, 8> alarmIds{}; // SDK alarm table +8 counters (0x02052440 + 0xc*n):
                                            // setup 0x02021bd4 and stop 0x02021bb4 increment them

    void stopTimers(std::uint32_t ch, std::uint32_t cap, std::uint32_t alarms,
                    std::uint32_t flags) { // SDK 0x020210e4
        for (unsigned i = 0; i < 8 && (alarms >> i); ++i) {
            if (alarms >> i & 1) {
                ++alarmIds[i];
            }
        }
        hw.stopTimers(ch, cap, alarms, flags);
    }

    void soundThreadStart(); // 0x02008038: ARM7 alarm 1 = 0x1474 x 64 clocks
    void soundThreadStop();  // 0x02008090
    Stream* findStream(std::uint32_t id, bool report); // 0x02005fd8
    void streamHardwareStart(Stream&);                 // 0x02006258
    void streamHardwareStop(Stream&);                  // 0x0200647c
    void streamRefill(Stream&);                        // 0x02006bdc
    void streamDestroy(Stream&);                       // 0x02005648
    void streamUpdate();                               // 0x02005cd8
    void streamVoiceVolume(Stream&, bool allTwo);

    void streamEvent(Stream& s, std::int32_t e) {
        if (s.callback) {
            s.callback(s.id, e);
        }
    }

    // All objects allocated through the original heap are owned here.
    std::vector<std::unique_ptr<ChannelGroup>> ownedGroups;
    std::vector<std::unique_ptr<Player>> ownedPlayers;
    std::vector<std::unique_ptr<MainBank>> ownedMain;
    std::vector<std::unique_ptr<ProgramBank>> ownedPrograms;
    std::vector<std::unique_ptr<EffectBank>> ownedEffects;

    void error(std::int32_t code) { owner.error_ = std::int16_t(code); } // 0x02001130

    void warn(std::int32_t code) { warning = std::int16_t(code); } // 0x02001140

    std::uint32_t rand(); // 0x02001180

    template <class T>
    void release(std::vector<std::unique_ptr<T>>& owned, T* object) {
        heap.release(object->heap);
        owned.erase(
            std::find_if(owned.begin(), owned.end(), [&](auto& p) { return p.get() == object; }));
    }

    // Program banks / programs.
    ProgramBank* findProgramBank(std::uint32_t id);                                // 0x02002b18
    MainBank* findMainBank(std::uint32_t id);                                      // 0x02002ac8
    const std::uint8_t* waveEntry(ProgramBank&, int index, int* number = nullptr); // 0x020029d0
    const std::uint8_t* programEntry(ProgramBank&, int index);                     // 0x02002a08
    const std::uint8_t* split(const std::uint8_t* program, const std::uint8_t* previous,
                              unsigned key, int velocity);   // 0x02002a40
    int copySamples(std::uint32_t id, std::uint8_t* buffer); // 0x02002814

    // Players.
    Player* findSequence(std::uint32_t id);                                           // 0x02003b94
    Player* allocatePlayer(int tracks, int channels);                                 // 0x02003be0
    void resetTracks(Player&);                                                        // 0x02003d2c
    int parseEntry(Player&, const std::uint8_t* entry);                               // 0x02003a84
    int resetPlayer(Player&);                                                         // 0x020037c4
    void applyParameters(Player&, PlayParameters&);                                   // 0x02003520
    int start(Player&, std::uint16_t user, std::uint8_t limitGroup, PlayParameters&); // 0x02003434
    int stop(Player&, int fadeMs);                                                    // 0x02003924
    const std::uint8_t* findSong(const std::uint8_t* data, int number);               // 0x02003a18
    void freePlayer(Player*);
    EffectBank* findEffectBank(std::uint32_t id); // 0x02004698
    const std::uint8_t* effectEntry(const std::uint8_t* data, int index,
                                    const std::uint8_t* table);  // 0x020043a8
    int limitById(std::uint32_t id, int limit, int steal);       // 0x020043f0
    int limitByGroup(std::uint32_t group, int limit, int steal); // 0x02004504
    Player* freeEffectPlayer(std::uint8_t priority);             // 0x02004618
    int createEffectPlayers(int count, int tracks);              // 0x02003f08
    void stopEffectsOf(std::uint32_t handle);                    // 0x020047d4

    // Channel groups (0x02009b6c..).
    ChannelGroup* allocateGroup(int channels);                                 // 0x02009b6c
    void freeGroup(ChannelGroup*);                                             // 0x02009c38
    void resetGroup(ChannelGroup&);                                            // 0x02009cc4
    void prepareGroup(ChannelGroup&, int, std::uint32_t);                      // 0x02009c9c
    void releaseAll(ChannelGroup&);                                            // 0x02009d34
    void setGroupVolumeIndex(ChannelGroup&, int, int);                         // 0x02009d60
    void setGroupPitch(ChannelGroup&, std::int16_t, int);                      // 0x02009db0
    void setGroupVolume(ChannelGroup&, std::int8_t, int);                      // 0x02009dc8
    void setGroupPan(ChannelGroup&, std::uint8_t, int);                        // 0x02009e08
    void resetChannels(ChannelGroup&, std::int32_t bank, std::uint32_t owner); // 0x02009e20
    void resetChannel(ChannelState&, std::uint32_t owner);                     // 0x02009eb0
    void stopChannel(ChannelState&);                                           // 0x02009ffc
    void selectBank(ChannelState&, std::uint32_t bank);                        // 0x0200a040
    std::int32_t selectProgram(ChannelState&, std::uint32_t program);          // 0x0200a084
    void setLfoScale(ChannelState&, std::int8_t);                              // 0x0200a120
    void setBend(ChannelState&, std::int16_t);                                 // 0x0200a160
    void noteOn(ChannelState&, struct Note&);                                  // 0x0200a1f8
    void legato(ChannelState&, struct Note&, std::uint8_t previous);           // 0x0200a4a8
    void releaseHeld(ChannelState&);                                           // 0x0200ab9c
    void release(ChannelState&, Voice&);                                       // 0x0200abe4
    std::uint32_t freeVoice(Voice&);                                           // 0x0200ac28
    Voice* allocateVoice(ChannelState&, const std::uint8_t* group, std::uint16_t keyGroup,
                         std::uint8_t priority);               // 0x0200a9e8
    void attach(ChannelState&, Voice&, std::uint8_t priority); // 0x0200ab40
    int countSounding(ChannelState&);                          // 0x0200ada0

    // Voices.
    void voicesInit();      // 0x0200a74c
    void hardwareReset();   // 0x0200a80c
    void envelopes();       // 0x0200a870
    void update();          // 0x0200a500
    void output();          // 0x0200adcc
    void status();          // 0x0200a95c
    void setupDone(Voice&); // 0x0200ace8
    void stopped(Voice&);   // 0x0200ad54

    // Sequencer.
    void tickPlayers(Player*);    // 0x020083f8
    void countDurations(Player&); // 0x020081e4
    void step(Player&, Track&);   // 0x02008254
    void fades(Player&);          // 0x02008640
    const std::uint8_t* event(std::uint8_t op, const std::uint8_t* p, Player&, Track&,
                              ChannelState&); // 0x020436e8
};

void (*traceStep)(const std::uint8_t*, std::uint8_t) = nullptr;

struct Note {
    std::uint8_t flag = 0, b1 = 0, key = 0, velocity = 0;
    std::int32_t duration = 0;
}; // 0x02008254 stack

namespace {
// ---- envelope (0x0200b030..0x0200b458) ----
void envelopeClear(Envelope& e) {
    e.valid = 0;
    e.state = 0;
    e.level = 0;
} // 0x0200b030

void envelopeReset(Envelope& e) {
    e.state = 0;
    e.level = 0;
    e.remaining = 0;
    e.active = 0;
} // 0x0200b2c4 / 0x0200b2fc

void envelopeTarget(const SoundTables& t, Envelope& e, std::int32_t target, unsigned time,
                    std::int16_t tick) { // 0x0200b118
    if (time == 0x7f) {
        e.step = 0;
        e.remaining = 0x7fffffff;
        return;
    }
    e.target = std::uint8_t(target);
    if (e.timeUnits == 0) {
        e.remaining = std::int32_t(
            udiv(std::uint32_t(t.envelopeMs[time] * 1000), std::uint32_t(std::int32_t(tick))));
    } else {
        e.remaining = sdiv(std::int32_t(e.timeUnits * t.envelopeUnits[time] * 1000), tick);
    }
    if (e.remaining == 0) {
        e.step = 0;
        return;
    }
    e.step = sdiv(std::int32_t(std::uint32_t(target) << 23) - e.level, e.remaining);
}

void envelopeStart(const SoundTables& t, Envelope& e, std::int16_t tick) { // 0x0200b1cc
    if (e.valid) {
        if (e.attack) {
            e.level = std::int32_t(std::uint32_t(e.attackLevel) << 23);
            e.state = 3;
            envelopeTarget(t, e, 0x7f, e.attack, tick);
        } else {
            e.level = 0x3f800000;
            if (e.hold) {
                envelopeTarget(t, e, 0x7f, e.hold, tick);
                e.state = 4;
            } else if (e.decay) {
                envelopeTarget(t, e, e.sustain, e.decay, tick);
                e.state = 5;
            } else {
                envelopeTarget(t, e, 0, e.decay2, tick);
                e.state = 6;
            }
        }
        e.active = 1;
        return;
    }
    if (e.state == 1) {
        return;
    }
    e.state = 0;
    e.level = 0x3f800000;
}

void envelopeRelease(const SoundTables& t, Envelope& e, std::int16_t tick) { // 0x0200b298
    if (e.state == 0) {
        return;
    }
    envelopeTarget(t, e, 0, e.release, tick);
    e.state = 7;
}

std::int32_t envelopeStep(const SoundTables& t, Envelope& e, std::int16_t tick) { // 0x0200b314
    if (e.state > 2) {
        if (e.remaining == 0) {
            e.level = std::int32_t(std::uint32_t(e.target) << 23);
            switch (e.state) {
            case 3:
                if (e.hold) {
                    envelopeTarget(t, e, 0x7f, e.hold, tick);
                    e.state = 4;
                    break;
                }
                [[fallthrough]];
            case 4:
                if (e.decay) {
                    envelopeTarget(t, e, e.sustain, e.decay, tick);
                    e.state = 5;
                    break;
                }
                e.level = std::int32_t(std::uint32_t(std::uint8_t(e.sustain)) << 23);
                [[fallthrough]];
            case 5:
                if (e.decay2) {
                    envelopeTarget(t, e, 0, e.decay2, tick);
                    e.state = 6;
                    break;
                }
                [[fallthrough]];
            case 6:
                envelopeTarget(t, e, 0, 0, tick);
                e.state = 2;
                break;
            case 7:
                e.state = 8;
                e.level = 0;
                e.remaining = 0;
                break;
            default:
                break;
            }
        } else {
            std::int32_t level = std::int32_t(std::uint32_t(e.level) + std::uint32_t(e.step));
            if (level > 0x3fffffff) {
                level = 0x3fffffff;
            }
            --e.remaining;
            if (level < 0) {
                level = 0;
            }
            e.level = level;
        }
    }
    return s8(e.level >> 23);
}

// ---- LFOs (0x0200b45c..0x0200b9cc) ----
std::int32_t waveSquare(Lfo& l) { // 0x0200b790
    if (l.counter == 0) {
        l.counter = l.period;
        l.value = l.value ? 0 : l.depth;
    }
    --l.counter;
    return l.value;
}

std::int32_t waveBipolar(Lfo& l) { // 0x0200b7cc
    if (l.counter == 0) {
        l.counter = l.period;
        l.value = (l.flags & 2) ? -l.depth : l.depth;
        l.flags ^= 2;
    }
    --l.counter;
    return l.value;
}

std::int32_t waveTriangle(Lfo& l) { // 0x0200b814
    if (l.counter == 0) {
        l.counter = l.period;
        if (l.flags & 1) {
            l.step = -l.step;
        }
        l.flags |= 1;
    }
    --l.counter;
    l.value += l.step;
    return l.value;
}

std::int32_t waveTriangleCentered(Lfo& l) { // 0x0200b868
    std::uint16_t c = l.counter;
    if (c == 0) {
        c = l.period;
        if (l.flags & 1) {
            l.step = -l.step;
        } else {
            c = std::uint16_t((std::uint32_t(c) << 15) >> 16);
            if (c < 1) {
                c = 1;
            }
        }
        l.flags |= 1;
    }
    l.counter = std::uint16_t(c - 1);
    l.value += l.step;
    return l.value;
}

std::int32_t waveSawUp(Lfo& l) { // 0x0200b8cc
    if (l.counter != 0) {
        l.value += l.step;
    } else {
        l.counter = l.period;
        l.value = 0;
    }
    --l.counter;
    return l.value;
}

std::int32_t waveSawDown(Lfo& l) { // 0x0200b908
    if (l.counter != 0) {
        l.value -= l.step;
    } else {
        l.counter = l.period;
        l.value = l.depth;
    }
    --l.counter;
    return l.value;
}

std::int32_t waveOff(Lfo& l) {
    l.flags = 0;
    l.kind = 0;
    return 0;
} // 0x0200b77c
} // namespace

std::uint32_t Driver::Impl::rand() { // 0x02001180
    std::uint32_t x = random ^ (random << 17);
    x ^= std::uint32_t(std::int32_t(x) >> 15);
    random = x;
    return x & 0x7fff;
}

namespace {
struct RandomWaves { // waveforms 6/7 use the driver RNG (0x0200b944 / 0x0200b988)
    static thread_local Driver::Impl* driver;
};
} // namespace

thread_local Driver::Impl* RandomWaves::driver = nullptr;

namespace {
std::int32_t waveRandom(Lfo& l) { // 0x0200b944
    if (l.counter == 0) {
        l.counter = l.period;
        l.value = (l.depth >> 16) * std::int32_t(RandomWaves::driver->rand());
    }
    --l.counter;
    return l.value;
}

std::int32_t waveRandomSigned(Lfo& l) { // 0x0200b988
    if (l.counter == 0) {
        l.counter = l.period;
        std::int32_t d = l.depth;
        l.value = (d >> 15) * std::int32_t(RandomWaves::driver->rand()) - (d >> 1);
    }
    --l.counter;
    return l.value;
}

constexpr Waveform waveforms[16] = {waveSquare, waveBipolar, waveTriangle, waveTriangleCentered,
                                    waveSawUp,  waveSawDown, waveRandom,   waveRandomSigned,
                                    waveOff,    waveOff,     waveOff,      waveOff,
                                    waveOff,    waveOff,     waveOff,      waveOff}; // 0x02043da8

void lfoSetup(LfoSet& set, const LfoParameters* parameters, std::int8_t scale,
              std::int16_t tick) { // 0x0200b474
    const std::int32_t scaledGain = div127(std::int32_t(scale) << 8) << 16;
    Lfo** link = &set.head;
    for (int i = 0; i < 4; ++i) {
        const LfoParameters& p = parameters[i];
        if (!p.kind) {
            continue;
        }
        Lfo& l = set.lfo[i];
        l.kind = p.kind;
        l.flags = 0;
        l.value = 0;
        if (p.kind == 1) {
            l.delay = std::uint16_t(sdiv(p.delayMs * 1000, tick));
            l.fade = std::uint16_t(sdiv(p.fadeMs * 1000, tick));
            if (l.fade) {
                l.gain = 0;
                l.gainStep = sdiv(0x1000000, l.fade);
            } else {
                l.gain = 0x1000000;
                l.gainStep = 0;
            }
        } else {
            l.delay = 0;
            l.fade = 0;
            l.gain = scaledGain;
        }
        l.counter = 0;
        l.step = 0;
        if (p.rateMs == 0) {
            l.counter = 0;
            l.period = 0;
        } else {
            std::uint16_t period = std::uint16_t(sdiv(p.rateMs * 1000, tick));
            if (period == 0) {
                period = 1;
            }
            l.period = period;
            l.counter = 0;
            l.depth = std::int32_t(std::uint32_t(p.depth) << 10);
            l.step = sdiv(l.depth, period);
        }
        l.waveform = waveforms[p.waveform & 15];
        if (p.destination >= 6) {
            throw std::runtime_error(
                "LFO destination beyond the original output block (unsupported)");
        }
        l.output = &set.out[p.destination];
        l.dirty = p.destination < 4 ? lfoDirty[p.destination] : 0;
        *link = &l;
        link = &l.next;
    }
    *link = nullptr;
}

void lfoScale(LfoSet& set, std::int8_t scale) { // 0x0200b67c
    std::int32_t gain = div127(std::int32_t(scale) << 8) << 16;
    for (Lfo* l = set.head; l; l = l->next) {
        if (l->kind == 3) {
            l->gain = gain;
        }
    }
}

std::uint16_t lfoUpdate(LfoSet& set) { // 0x0200b6c8
    set.out[0] = set.out[1] = set.out[2] = set.out[3] = set.out[4] = set.out[5] = 0;
    std::uint16_t bits = 0;
    for (Lfo* l = set.head; l; l = l->next) {
        if (l->delay) {
            --l->delay;
            continue;
        }
        std::int32_t v = l->waveform(*l) >> 8;
        if (l->fade) {
            if (--l->fade == 0) {
                l->gain = 0x1000000;
            } else {
                l->gain += l->gainStep;
            }
        }
        std::int32_t product = std::int32_t(std::uint32_t(v) * (std::uint32_t(l->gain) >> 8));
        *l->output = std::int16_t(*l->output + (product >> 16));
        bits |= l->dirty;
    }
    return bits;
}

void lfoClear(LfoSet& set) {
    set.out[0] = set.out[1] = set.out[2] = set.out[3] = set.out[4] = set.out[5] = 0;
    set.head = nullptr;
} // 0x0200b45c

void overrideReset(std::uint8_t* o) {
    o[0] = 0;
    std::memset(o + 1, 0xff, 15);
} // 0x0200b044

void overrideValidate(std::uint8_t* o) { // 0x0200b060
    o[0] = 1;
    for (int i : {1, 8, 9, 10, 11, 12, 13, 14}) {
        if (o[i] <= 0x7f) {
            return;
        }
    }
    o[0] = 0;
}

void overrideApply(Envelope& e, const std::uint8_t* o) { // 0x0200b0b4
    if (o[1] < 0x80) {
        e.timeUnits = o[1];
    }
    if (o[8] < 0x80) {
        e.attackLevel = o[8];
    }
    if (o[9] < 0x80) {
        e.attack = o[9];
    }
    if (o[10] < 0x80) {
        e.decay = o[10];
    }
    if (o[11] < 0x80) {
        e.sustain = std::int8_t(o[11]);
    }
    if (o[12] < 0x80) {
        e.hold = o[12];
    }
    if (o[13] < 0x80) {
        e.decay2 = o[13];
    }
    if (o[14] < 0x80) {
        e.release = o[14];
    }
}

void envelopeLoad(Envelope& e, const std::uint8_t* p) { // 16 bytes from split +0x20
    e.valid = p[0];
    e.timeUnits = p[1];
    std::memcpy(e.raw, p + 2, 6);
    e.attackLevel = p[8];
    e.attack = p[9];
    e.decay = p[10];
    e.sustain = std::int8_t(p[11]);
    e.hold = p[12];
    e.decay2 = p[13];
    e.release = p[14];
    e.raw2 = p[15];
}

std::uint32_t pitchToFrequency(const SoundTables& t, std::uint32_t pitch) { // 0x0200afe4
    std::uint32_t key = keyCode(t, (pitch & 0x7fff) >> 8);
    std::uint32_t value = t.pitch[(pitch & 0xff) + ((key & 0xf) << 8)];
    std::int32_t shift = 4 - std::int32_t(key >> 4);
    return shift >= 0 ? value >> shift : value << -shift;
}
} // namespace

// ---- banks --------------------------------------------------------------------
namespace {
int checkHeader(Driver::Impl& d, const std::uint8_t* p, std::uint32_t expected,
                std::uint16_t version) { // 0x02001390
    if (!p) {
        d.error(-0x18);
        return -0x18;
    }
    if (chunkTag(p) != expected) {
        d.error(-0x11);
        return -0x11;
    }
    if (version && le16(p + 0xc) != version) {
        d.error(-0x13);
        return -0x13;
    }
    return le16(p + 0xe);
}
} // namespace

MainBank* Driver::Impl::findMainBank(std::uint32_t id) { // 0x02002ac8
    MainBank* b = mainBanks;
    while (b && b->id != id) {
        b = b->next;
    }
    return b;
}

ProgramBank* Driver::Impl::findProgramBank(std::uint32_t id) { // 0x02002b18
    ProgramBank* b = programBanks;
    while (b && b->id != id) {
        b = b->next;
    }
    return b;
}

const std::uint8_t* Driver::Impl::waveEntry(ProgramBank& b, int index, int*) { // 0x020029d0
    if (!b.wave) {
        return nullptr;
    }
    if (index >= b.waves) {
        return nullptr;
    }
    std::uint16_t offset = le16(b.wave + index * 2);
    return offset ? b.wave + offset : nullptr;
}

const std::uint8_t* Driver::Impl::programEntry(ProgramBank& b, int index) { // 0x02002a08
    if (!b.program) {
        return nullptr;
    }
    if (index >= b.programs) {
        return nullptr;
    }
    std::uint16_t offset = le16(b.program + index * 2);
    return offset ? b.program + offset : nullptr;
}

const std::uint8_t* Driver::Impl::split(const std::uint8_t* program, const std::uint8_t* previous,
                                        unsigned key, int velocity) { // 0x02002a40
    const int count = program[2];
    if (!count) {
        return nullptr;
    }
    const std::uint8_t* s;
    if (!previous) {
        s = program + 0x60;
    } else {
        if (previous[1] >= count - 1) {
            return nullptr;
        }
        s = previous + 0x30;
    }
    for (;;) {
        if (key >= s[4] && key <= s[5] && velocity >= s[8] && velocity <= s[9]) {
            return s;
        }
        if (s[1] >= count - 1) {
            return nullptr;
        }
        s += 0x30;
    }
}

int Driver::Impl::copySamples(std::uint32_t id, std::uint8_t* buffer) { // 0x02002814
    ProgramBank* bank = findProgramBank(id);
    if (!bank) {
        error(-0x41);
        return -0x41;
    }
    if (bank->type != 2) {
        return -0x4d;
    }
    MainBank* main = findMainBank(bank->mainId);
    if (!main) {
        error(-0x4c);
        return -0x4c;
    }
    std::int32_t total = 0;
    std::uint8_t* destination = buffer; // the original advances an address even when buffer is 0
    std::uintptr_t offset = 0;
    bank->loaded.assign(std::size_t(std::max<int>(bank->waves, 0)), nullptr);
    for (int j = 0; j < bank->waves; ++j) {
        const std::uint8_t* e = waveEntry(*bank, j);
        if (!e) {
            continue;
        }
        bank->loaded[std::size_t(j)] =
            buffer ? buffer + offset : nullptr; // entry +0x24 := current address
        std::uint16_t mainOffset = le16(main->wave.data() + 0x10 + j * 2);
        if (!mainOffset) {
            continue;
        }
        std::uint32_t size = (le32(e + 0x2c) + le32(e + 0x28)) * 4;
        if (buffer) {
            const std::uint8_t* m = main->wave.data() + 0x10 + mainOffset;
            main->file->read(le32(m + 0x24) + main->pcmOffset, destination, size);
        }
        total += std::int32_t(size);
        offset += size;
        if (buffer) {
            destination += size;
        }
    }
    bank->sampleBytes = total;
    return total;
}

int Driver::openMainBank(std::unique_ptr<BankFile> file) { // 0x02002260 -> 0x02002354
    auto& d = *impl_;
    if (!file) {
        d.error(-0x18);
        return -0x18;
    }
    std::uint8_t header[0x50];
    if (!file->read(0, header, sizeof header)) {
        std::memset(header, 0, sizeof header);
    }
    int id = checkHeader(d, header, tag("swdl"), 0x415);
    if (id < 0) {
        return id;
    }
    if (d.findMainBank(le16(header + 0xe))) {
        d.error(-0x48);
        return -0x48;
    }
    const std::uint32_t waveBytes = le32(header + 0x4c);
    std::uint32_t at = d.heap.allocate(waveBytes + 0x60);
    if (!at) {
        d.error(-0x80);
        return -0x80;
    }
    auto bank = std::make_unique<MainBank>();
    bank->heap = at;
    bank->id = le16(header + 0xe);
    bank->waves = std::uint16_t(le16(header + 0x46));
    bank->pcmOffset = waveBytes + 0x70;
    bank->wave.assign(waveBytes, 0);
    file->read(0x50, bank->wave.data(), waveBytes);
    if (bank->wave.size() < 4 || chunkTag(bank->wave.data()) != tag("wavi")) {
        d.ownedMain.push_back(std::move(bank)); // original leaks the block and returns -1
        return -1;
    }
    bank->file = std::move(file);
    MainBank** link = &d.mainBanks;
    while (*link) {
        link = &(*link)->next;
    }
    *link = bank.get();
    int result = bank->id;
    d.ownedMain.push_back(std::move(bank));
    return result;
}

void Driver::closeMainBank(int handle) { // 0x0200229c -> 0x0200257c
    auto& d = *impl_;
    if (handle < 0) {
        return;
    }
    MainBank** link = &d.mainBanks;
    while (*link && (*link)->id != std::uint32_t(handle & 0xffff)) {
        link = &(*link)->next;
    }
    if (!*link) {
        return; // -0x41
    }
    MainBank* bank = *link;
    *link = bank->next;
    bank->file.reset();
    d.release(d.ownedMain, bank);
}

int Driver::registerProgramBank(std::uint8_t* data, std::size_t) { // 0x020022c8 -> 0x02002604
    auto& d = *impl_;
    if (!data) {
        d.error(-0x18);
        return -0x18;
    }
    int id = checkHeader(d, data, tag("swdl"), 0x415);
    if (id < 0) {
        return id;
    }
    if (d.findProgramBank(std::uint32_t(id) & 0xffff)) {
        d.error(-0x48);
        return -0x48;
    }
    std::uint32_t at = d.heap.allocate(0x28);
    if (!at) {
        d.error(-0x80);
        return -0x80;
    }
    auto bank = std::make_unique<ProgramBank>();
    bank->heap = at;
    bank->file = data;
    bank->id = std::uint16_t(id);
    bank->mainId = 0;
    bank->waves = std::int16_t(le16(data + 0x46));
    bank->programs = std::int16_t(le16(data + 0x48));
    bank->b0c = data[0x4a];
    bank->type = data[0x4b];
    for (const std::uint8_t* c = data + 0x50; chunkTag(c) != tag("eod "); c = nextChunk(c)) {
        std::uint32_t t = chunkTag(c);
        if (t == tag("wavi")) {
            bank->wave = c + 0x10;
        } else if (t == tag("prgi")) {
            bank->program = c + 0x10;
        } else if (t == tag("kgrp")) {
            bank->keyGroups = c + 0x10;
        } else if (t == tag("pcmd")) {
            bank->pcm = c + 0x10;
        }
    }
    ProgramBank** link = &d.programBanks;
    while (*link) {
        link = &(*link)->next;
    }
    *link = bank.get();
    if (bank->type == 2) {
        bank->mainId = le16(data + 0x40);
    }
    int result = bank->id;
    d.ownedPrograms.push_back(std::move(bank));
    return result;
}

void Driver::unregisterProgramBank(int handle) { // 0x020022ec -> 0x0200295c
    auto& d = *impl_;
    if (handle < 0) {
        return;
    }
    ProgramBank** link = &d.programBanks;
    while (*link && (*link)->id != std::uint32_t(handle & 0xffff)) {
        link = &(*link)->next;
    }
    if (!*link) {
        return;
    }
    ProgramBank* bank = *link;
    *link = bank->next;
    d.release(d.ownedPrograms, bank);
}

int Driver::sampleBytes(int handle) {
    return impl_->copySamples(std::uint32_t(handle) & 0xffff, nullptr);
} // 0x02002318

int Driver::loadSamples(int handle, std::uint8_t* buffer) {
    return impl_->copySamples(std::uint32_t(handle) & 0xffff, buffer);
} // 0x02002334

// ---- channel groups (0x02009b6c..0x0200a4fc) --------------------------------
ChannelGroup* Driver::Impl::allocateGroup(int channels) { // 0x02009b6c
    std::uint32_t at = heap.allocate(std::uint32_t(channels) * 0xcc + 0x14);
    if (!at) {
        return nullptr;
    }
    auto g = std::make_unique<ChannelGroup>();
    g->heap = at;
    g->b0 = 0;
    g->count = std::uint8_t(channels);
    g->next = nullptr;
    resetGroup(*g);
    g->channels.resize(std::size_t(std::max(channels, 0)));
    for (int i = 0; i < channels; ++i) {
        auto& c = g->channels[std::size_t(i)];
        c.owner = 0;
        c.dirty = 0;
        c.index = std::uint8_t(i);
        c.voices = nullptr;
        c.programBank = nullptr;
        c.programData = nullptr;
        c.group = g.get();
    }
    ChannelGroup** link = &groups;
    while (*link) {
        link = &(*link)->next;
    }
    *link = g.get();
    ChannelGroup* result = g.get();
    ownedGroups.push_back(std::move(g));
    return result;
}

void Driver::Impl::freeGroup(ChannelGroup* g) { // 0x02009c38
    releaseAll(*g);
    ChannelGroup** link = &groups;
    while (*link && *link != g) {
        link = &(*link)->next;
    }
    if (*link == g) {
        *link = g->next;
    }
    release(ownedGroups, g);
}

void Driver::Impl::resetGroup(ChannelGroup& g) { // 0x02009cc4
    g.dirty = 0;
    g.baseVolume = groupVolume[1];
    g.volumeGroup = 1;
    g.pitch = 0;
    g.volume = 0x7f;
    g.pan = 0x40;
    g.combined = s8(div127(std::int16_t(std::uint8_t(g.baseVolume)) * std::int16_t(g.volume)));
    g.b11 = 0;
    g.b12 = 0x10;
}

void Driver::Impl::prepareGroup(ChannelGroup& g, int bank, std::uint32_t owner_) {
    resetGroup(g);
    resetChannels(g, bank, owner_);
} // 0x02009c9c

void Driver::Impl::releaseAll(ChannelGroup& g) {
    for (auto& c : g.channels) {
        stopChannel(c);
    }
} // 0x02009d34

void Driver::Impl::setGroupVolumeIndex(ChannelGroup& g, int index, int flag) { // 0x02009d60
    g.baseVolume = groupVolume[std::size_t(index) & 15];
    g.volumeGroup = std::uint8_t(index);
    g.combined = s8(div127(std::int16_t(std::uint8_t(g.baseVolume)) * std::int16_t(g.volume)));
    if (flag == 1) {
        g.dirty |= 0x20;
    }
}

void Driver::Impl::setGroupPitch(ChannelGroup& g, std::int16_t v, int flag) {
    g.pitch = v;
    if (flag == 1) {
        g.dirty |= 0x10;
    }
} // 0x02009db0

void Driver::Impl::setGroupVolume(ChannelGroup& g, std::int8_t v, int flag) { // 0x02009dc8
    g.volume = v;
    g.combined = s8(div127(std::int16_t(std::uint8_t(g.baseVolume)) * std::int16_t(g.volume)));
    if (flag == 1) {
        g.dirty |= 0x20;
    }
}

void Driver::Impl::setGroupPan(ChannelGroup& g, std::uint8_t v, int flag) {
    g.pan = std::int8_t(v);
    if (flag == 1) {
        g.dirty |= 0x40;
    }
} // 0x02009e08

void Driver::Impl::resetChannels(ChannelGroup& g, std::int32_t bank,
                                 std::uint32_t owner_) { // 0x02009e20
    std::uint8_t saved = resetGuard;
    resetGuard = 0;
    for (auto& c : g.channels) {
        stopChannel(c);
        resetChannel(c, owner_);
        if (bank >= 0) {
            selectBank(c, std::uint16_t(bank));
            selectProgram(c, std::uint16_t(bank));
        }
    }
    resetGuard = saved;
}

void Driver::Impl::resetChannel(ChannelState& c, std::uint32_t owner_) { // 0x02009eb0
    c.owner = owner_;
    c.voiceMode = 1;
    c.b6 = 0;
    c.flags = 0;
    c.dirty = 0;
    c.b10 = 0;
    c.b11 = 0x10;
    c.randomPitch = 0;
    c.keyLow = 0;
    c.keyHigh = 0;
    c.bank = std::uint16_t(std::int8_t(controllerDefault[0x20]) +
                           (std::int8_t(controllerDefault[0]) << 8));
    c.program = 0;
    c.h16 = 0;
    c.h1e = 0;
    c.h20 = 0;
    c.b22 = 0x40;
    c.b23 = 0;
    c.sweep = 0;
    c.sweepCount = 0;
    c.volumeValue =
        std::int32_t(std::uint32_t(std::int32_t(std::int8_t(controllerDefault[7]))) << 16);
    c.volumeCount = 0;
    c.panValue =
        std::int32_t(std::uint32_t(std::int32_t(std::int8_t(controllerDefault[10]))) << 16);
    c.panCount = 0;
    c.detune = 0;
    c.expression = std::uint8_t(std::int8_t(controllerDefault[11]));
    c.lfoScale = 0;
    c.bend = 0;
    c.fine = 0;
    c.velocityScale = 0x7f;
    c.bendRange = 0;
    std::memset(c.b62, 0, sizeof c.b62);
    for (auto& l : c.lfo) {
        l.kind = 0;
        l.destination = 0;
    }
    overrideReset(c.envelopeOverride);
    c.voices = nullptr;
    c.programBank = nullptr;
    c.programData = nullptr;
    c.pitch = 0;
    std::int32_t x = std::int16_t(std::int8_t(controllerDefault[7])) *
                     std::int16_t(std::int8_t(controllerDefault[11]));
    c.volume = std::uint16_t(div16129(x * 127));
    c.pan = 0x40;
}

void Driver::Impl::stopChannel(ChannelState& c) { // 0x02009ffc
    std::uint32_t bits = 0;
    while (c.voices) {
        bits |= freeVoice(*c.voices);
    }
    hw.stopTimers(bits, 0, 0, 0);
}

void Driver::Impl::selectBank(ChannelState& c, std::uint32_t bank) { // 0x0200a040
    c.bank = std::uint16_t(bank);
    c.programBank = findProgramBank(bank & 0xffff);
    if (!c.programBank && bank != 0) {
        c.programData = nullptr;
        return;
    }
    selectProgram(c, std::uint16_t(c.program));
}

std::int32_t Driver::Impl::selectProgram(ChannelState& c, std::uint32_t program) { // 0x0200a084
    if (!c.programBank) {
        c.programData = nullptr;
        c.program = 0;
    } else {
        c.program = s16(std::int32_t(program));
        const std::uint8_t* e = programEntry(*c.programBank, c.program);
        if (e) {
            c.programData = e;
            for (int i = 0; i < 4; ++i) {
                const std::uint8_t* q = e + 0x10 + 16 * i;
                if (q[1]) {
                    auto& l = c.lfo[i];
                    l.b0 = q[0];
                    l.kind = q[1];
                    l.destination = q[2];
                    l.waveform = q[3];
                    l.depth = std::int32_t(le32(q + 4));
                    l.rateMs = le16(q + 8);
                    l.delayMs = le16(q + 10);
                    l.fadeMs = le16(q + 12);
                    l.b14 = q[14];
                    l.b15 = q[15];
                } else {
                    c.lfo[i].kind = 0;
                }
            }
        } else {
            c.programData = nullptr;
        }
    }
    overrideReset(c.envelopeOverride);
    return std::int32_t(program);
}

void Driver::Impl::setLfoScale(ChannelState& c, std::int8_t v) { // 0x0200a120
    c.lfoScale = v;
    c.dirty |= 0x10;
    for (Voice* x = c.voices; x; x = x->next) {
        lfoScale(x->lfo, v);
    }
}

void Driver::Impl::setBend(ChannelState& c, std::int16_t bend) { // 0x0200a160
    c.bend = bend;
    c.dirty |= 0x10;
    for (Voice* v = c.voices; v; v = v->next) {
        v->bend = s16(sar0(std::int32_t(v->bendRange) * (std::int32_t(bend) << 8), 13));
        v->dirty |= 0x10;
    }
}

void Driver::Impl::noteOn(ChannelState& c, Note& note) { // 0x0200a1f8
    const std::uint8_t* program = c.programData;
    if (!program) {
        return;
    }
    ProgramBank& bank = *c.programBank;
    std::int16_t randomTune = 0;
    if (c.randomPitch) {
        std::int32_t x = std::int32_t(c.randomPitch) * 2 * std::int32_t(rand());
        randomTune = s16(((x + std::int32_t(std::uint32_t(x >> 14) >> 17)) >> 15) - c.randomPitch);
    }
    if (c.keyHigh) {
        std::int32_t span = std::int32_t(c.keyHigh) - c.keyLow + 1;
        std::int32_t x = span * std::int32_t(rand());
        note.key =
            std::uint8_t(c.keyLow + ((x + std::int32_t(std::uint32_t(x >> 14) >> 17)) >> 15));
    }
    for (const std::uint8_t* s = nullptr;;) {
        s = split(program, s, note.key, std::int8_t(note.velocity));
        if (!s) {
            return;
        }
        unsigned group = s[0x1a];
        if (group >= bank.b0c) {
            group = 0;
        }
        if (!bank.keyGroups) {
            throw std::runtime_error("program bank without kgrp chunk (original reads address 0)");
        }
        std::uint8_t keyGroup[8];
        std::memcpy(keyGroup, bank.keyGroups + group * 8, 8);
        const std::int16_t sampleNumber = std::int16_t(le16(s + 0x12));
        const std::uint8_t* w = waveEntry(bank, sampleNumber);
        if (!w) {
            continue;
        }
        const std::uint16_t keyGroupId = std::uint16_t(s[0x1a] + (c.index << 8));
        Voice* v = allocateVoice(c, keyGroup, keyGroupId, keyGroup[3]);
        if (!v) {
            continue;
        }
        v->flags = 0;
        v->owner = c.owner;
        v->keyGroup = keyGroupId;
        v->sample = std::uint8_t(sampleNumber);
        std::memcpy(v->wave, w + 0x10, 0x20);
        // +0x30 = wave +0x24 + bank pcm chunk address. For main-bank samples
        // 0x02002814 replaced +0x24 by the copied sample's address (pcm is 0);
        // otherwise +0x24 is an offset into the bank's own pcm chunk.
        const std::size_t index = std::size_t(sampleNumber);
        if (index < bank.loaded.size() && bank.loaded[index]) {
            if (bank.pcm) {
                throw std::runtime_error(
                    "relocated sample in a bank with its own pcm chunk (address sum unsupported)");
            }
            v->data = bank.loaded[index];
        } else {
            v->data = bank.pcm ? bank.pcm + le32(w + 0x24)
                               : nullptr; // without pcm: an unmapped low address
        }
        v->key = note.key;
        v->tune = s16(randomTune + (std::int16_t(le16(s + 0x14)) + (std::int8_t(s[0x17]) << 8)));
        v->pitch = s16(v->tune + (note.key << 8));
        v->pan = s[0x19];
        if (velocityMode == 7) {
            std::int32_t x = std::int16_t(c.velocityScale) * std::int16_t(note.velocity) *
                             std::int32_t(program[4]) * std::int32_t(s[0x18]);
            v->velocityVolume =
                std::uint8_t((mulhs(0x4186143d, x) >> 19) + std::int32_t(std::uint32_t(x) >> 31));
        } else {
            std::int32_t x =
                std::int16_t(note.velocity) * std::int16_t(program[4]) * std::int32_t(s[0x18]);
            v->velocityVolume = std::uint8_t(div16129(x));
        }
        std::uint8_t range = c.bendRange ? c.bendRange : s[2];
        v->bend = s16(sar0(std::int32_t(range) * (std::int32_t(c.bend) << 8), 13));
        v->bendRange = range;
        v->duration = note.duration;
        v->dirty |= 0xff0;
        lfoSetup(v->lfo, c.lfo, c.lfoScale, tickMicros);
        envelopeLoad(v->envelope, s + 0x20);
        v->envelope.valid = 1;
        if (c.envelopeOverride[0]) {
            overrideApply(v->envelope, c.envelopeOverride);
        }
        attach(c, *v, keyGroup[3]);
    }
}

void Driver::Impl::legato(ChannelState& c, Note& note, std::uint8_t previous) { // 0x0200a4a8
    Voice* v = c.voices;
    while (v && v->key != previous) {
        v = v->next;
    }
    if (!v) {
        return;
    }
    v->key = note.key;
    v->duration = note.duration;
    v->pitch = s16(v->tune + (note.key << 8));
    v->dirty |= 0x10;
}

Voice* Driver::Impl::allocateVoice(ChannelState& c, const std::uint8_t* group,
                                   std::uint16_t keyGroupId, std::uint8_t priority) { // 0x0200a9e8
    std::uint32_t request = (std::uint32_t(priority) << 16) + 0x1ff;
    int last = group[5];
    if (last > 15) {
        last = 15;
    }
    int first = group[4];
    if (c.voiceMode == 0) {
        if (c.voices) {
            return c.voices;
        }
    } else if (c.voiceMode != 1) {
        return nullptr;
    }
    if (group[0]) {
        int count = 0, chosen = last;
        std::uint32_t lowest = request;
        for (int i = first; i <= last; ++i) {
            Voice& v = voices[std::size_t(i)];
            if (v.owner == c.owner && v.keyGroup == keyGroupId) {
                ++count;
                if (lowest > v.priority) {
                    lowest = v.priority;
                    chosen = i;
                }
                if (count >= group[2]) {
                    break;
                }
            }
        }
        if (count >= group[2]) {
            Voice& v = voices[std::size_t(chosen)];
            freeVoice(v);
            return &v;
        }
    }
    int best = last + 1;
    for (int i = first; i <= last; ++i) {
        std::uint32_t p = voices[std::size_t(i)].priority;
        if (request > p) {
            best = i;
            if (p == 0) {
                break;
            }
            request = p;
        }
    }
    if (best > last) {
        return nullptr;
    }
    Voice& v = voices[std::size_t(best)];
    freeVoice(v);
    return &v;
}

void Driver::Impl::attach(ChannelState& c, Voice& v, std::uint8_t priority) { // 0x0200ab40
    v.dirty = std::uint16_t((v.dirty | 1) & ~2u);
    v.priority |= (std::uint32_t(priority) << 16) + 0x1ff;
    envelopeStart(tables, v.envelope, tickMicros);
    if (v.channel == &c) {
        return;
    }
    v.next = c.voices;
    c.voices = &v;
    v.channel = &c;
}

void Driver::Impl::releaseHeld(ChannelState& c) { // 0x0200ab9c
    if (c.flags & 1) {
        return;
    }
    for (Voice* v = c.voices; v; v = v->next) {
        if (v->flags & 2) {
            release(c, *v);
        }
    }
}

void Driver::Impl::release(ChannelState& c, Voice& v) { // 0x0200abe4
    v.flags |= 2;
    if (c.flags & 1) {
        return;
    }
    v.priority &= 0xff;
    envelopeRelease(tables, v.envelope, tickMicros);
    v.dirty &= std::uint16_t(~1u);
}

std::uint32_t Driver::Impl::freeVoice(Voice& v) { // 0x0200ac28
    if (ChannelState* c = v.channel) {
        if (c->voices == &v) {
            c->voices = v.next;
        } else {
            Voice* p = c->voices;
            while (p->next != &v) {
                p = p->next;
            }
            p->next = v.next;
        }
        v.channel = nullptr;
    }
    v.flags = 0;
    v.next = nullptr;
    envelopeReset(v.envelope);
    v.priority &= 0xff;
    v.owner = 0;
    v.keyGroup = 0;
    const std::uint32_t bit = 1u << v.index;
    startMask &= ~bit;
    releasedMask |= bit;
    return bit;
}

int Driver::Impl::countSounding(ChannelState& c) { // 0x0200ada0
    int n = 0;
    for (Voice* v = c.voices; v; v = v->next) {
        if (std::uint8_t(v->priority)) {
            ++n;
        }
    }
    return n;
}

// ---- voices and hardware output (0x0200a500..0x0200afe0) ---------------------
void Driver::Impl::voicesInit() { // 0x0200a74c
    for (unsigned i = 0; i < 16; ++i) {
        Voice& v = voices[i];
        v.flags = 0;
        v.dirty = 0;
        v.unknown146 = 1;
        v.maskGroup = 0;
        v.index = std::uint8_t(i);
        v.hardware = std::uint16_t(i);
        v.owner = 0;
        v.priority = 0;
        v.key = 0;
        v.sample = 0;
        v.keyGroup = 0;
        v.pitch = 0;
        v.velocityVolume = 0;
        v.pan = 0x40;
        envelopeClear(v.envelope);
        lfoClear(v.lfo);
        v.next = nullptr;
        v.channel = nullptr;
    }
    hardwareReset();
}

void Driver::Impl::hardwareReset() { // 0x0200a80c
    startMask = stopMask = releasedMask = 0;
    alarmStart = alarmStop = 0;
    lastStarted = 0;
    hw.startTimers(0, 0, 0, 0);
    stopTimers(0xffff, 0xffff, 0xffff, 0);
}

void Driver::Impl::envelopes() { // 0x0200a870
    std::uint16_t count = 0;
    for (auto& v : voices) {
        std::int32_t level = envelopeStep(tables, v.envelope, tickMicros);
        if (v.envelope.active == 1) {
            v.dirty |= lfoUpdate(v.lfo);
            if (v.envelope.state == 8) {
                v.envelope.state = 2;
                v.dirty |= 2;
            }
            if (std::int32_t(std::uint8_t(v.priority)) != level) {
                v.dirty |= 0x20;
            }
            if (level > 0) {
                ++count;
            }
        }
        v.priority = (v.priority & ~0xffu) | std::uint8_t(level);
    }
    if (peakHold < 0) {
        peakVoices = 0;
    }
    activeVoices = count;
    if (peakVoices < count) {
        peakHold = 100;
        peakVoices = count;
    }
    --peakHold;
}

void Driver::Impl::update() { // 0x0200a500
    for (ChannelGroup* g = groups; g; g = g->next) {
        for (auto& c : g->channels) {
            const std::uint16_t flags = c.dirty | g->dirty;
            if (flags) {
                if (flags & 0x10) {
                    c.pitch = std::uint16_t(g->pitch + c.detune + s16(c.sweep >> 16));
                }
                if (flags & 0x20) {
                    std::int32_t x = std::int32_t(g->combined) *
                                     ((c.volumeValue >> 16) * std::int32_t(c.expression));
                    c.volume = std::uint16_t(div16129(x));
                }
                if (flags & 0x40) {
                    c.pan = std::uint16_t(std::int32_t(g->pan) - 0x40 + (c.panValue >> 16));
                }
            }
            for (Voice* v = c.voices; v; v = v->next) {
                const std::uint16_t f = v->dirty | flags;
                if (f) {
                    if (f & 0x10) {
                        std::uint32_t pitch =
                            std::uint16_t(c.pitch + v->pitch + v->bend + v->lfo.out[1]);
                        std::int32_t frequency = std::int32_t(pitchToFrequency(tables, pitch));
                        if (v->format() < 3) {
                            v->timer = std::uint32_t(sdiv(0x00ffb0ff, frequency));
                        } else {
                            v->timer = std::uint32_t(sdiv(
                                0x00ffb0ff,
                                (frequency + std::int32_t(std::uint32_t(frequency >> 3) >> 28)) >>
                                    4));
                        }
                    }
                    if (f & 0x20) {
                        std::int32_t e = std::int32_t(v->velocityVolume) + (v->lfo.out[2] >> 6);
                        e = std::clamp(e, 0, 0x7f);
                        std::int32_t x =
                            std::int32_t(c.volume) * e * std::int32_t(std::uint8_t(v->priority));
                        std::int32_t y = magic(x, std::int32_t(0x828cbfbf), 12);
                        std::int32_t z = y * y;
                        v->volume =
                            std::uint16_t((z + std::int32_t(std::uint32_t(z >> 8) >> 23)) >> 9);
                    }
                    if (f & 0x40) {
                        std::int32_t p = std::int32_t(c.pan) + (std::int32_t(v->pan) - 0x40) +
                                         (v->lfo.out[3] >> 6);
                        if (p < 0) {
                            p = 0;
                        }
                        if (p > 0x7f) {
                            p = 0x7f;
                        }
                        v->panOut = tables.panCurve[p];
                    }
                }
                v->dirty = f;
            }
            c.dirty = 0;
        }
        g->dirty = 0;
    }
}

void Driver::Impl::setupDone(Voice& v) { // 0x0200ace8
    v.dirty = 0xff0;
    v.priority |= 0x1ff;
    const std::uint32_t bit = 1u << v.index;
    stopMask &= ~bit;
    releasedMask &= ~bit;
    startMask |= bit;
}

void Driver::Impl::stopped(Voice& v) { // 0x0200ad54
    v.priority &= ~0xff00u;
    const std::uint32_t bit = 1u << v.index;
    startMask &= ~bit;
    stopMask |= bit;
}

void Driver::Impl::output() { // 0x0200adcc
    for (auto& v : voices) {
        std::uint16_t d = v.dirty;
        if (!d) {
            continue;
        }
        if (d & 1) {
            const unsigned repeat = v.loops() ? 1 : 2;
            switch (v.format()) {
            case 0:
            case 1:
            case 2:
                hw.setupPcm(v.hardware, v.format(), v.data, repeat, v.loopStart(), v.loopLength(),
                            v.volume, 0, v.timer, v.panOut);
                break;
            case 3:
                hw.setupPsg(v.hardware, v.duty(), v.volume, 0, v.timer, v.panOut);
                break;
            case 4:
                hw.setupNoise(v.hardware, v.volume, 0, v.timer, v.panOut);
                break;
            default:
                break;
            }
            setupDone(v);
        } else {
            if (d & 2) {
                stopped(v);
                d = 0;
            }
            if (d & 0x10) {
                hw.setTimer(1u << (v.hardware & 0xff), v.timer);
            }
            if (d & 0x20) {
                hw.setVolume(1u << (v.hardware & 0xff), v.volume, 0);
            }
            if (d & 0x40) {
                hw.setPan(1u << (v.hardware & 0xff), v.panOut);
            }
        }
        v.dirty = 0;
    }
    hw.startTimers(startMask, captureStart, alarmStart, 0);
    stopTimers(stopMask | releasedMask, captureStop, alarmStop, 0);
    lastStarted = std::uint16_t(startMask);
    captureStart = 0;
    stopMask = 0;
    startMask = 0;
    captureStop = 0;
    alarmStop = 0;
    alarmStart = 0;
}

void Driver::Impl::status() { // 0x0200a95c
    std::uint32_t bits = std::uint16_t(lastStarted | hw.channelStatus());
    for (auto& v : voices) {
        if (v.envelope.active == 1) {
            if (!(bits & 1)) {
                envelopeReset(v.envelope);
                v.priority &= 0xffff0000u;
            }
        } else if ((bits & 1) == 1) {
            stopped(v);
        }
        bits = (bits << 15 & 0xffffffffu) >> 16;
    }
}

// ---- players (0x02002db8..0x02004848) ----------------------------------------
Player* Driver::Impl::findSequence(std::uint32_t id) { // 0x02003b94
    Player* p = players;
    while (p && p->id != id) {
        p = p->next;
    }
    return p;
}

Player* Driver::Impl::allocatePlayer(int trackCount, int channels) { // 0x02003be0
    std::uint32_t at = heap.allocate(std::uint32_t(trackCount) * 0x5c + 0xc4);
    if (!at) {
        error(-0x84);
        return nullptr;
    }
    auto p = std::make_unique<Player>();
    p->heap = at;
    p->flags = 0x40; // bits 0,1,2,3,5 cleared, bit 6 set (fresh heap memory assumed zero)
    p->marker = 0;
    p->activeTracks = 0;
    p->age = 0;
    p->id = 0;
    p->user = 0;
    p->ppq8 = 6;
    p->maxTracks = std::uint8_t(trackCount);
    p->trackCount = std::uint8_t(trackCount);
    p->channelCount = std::uint8_t(channels);
    p->b6 = 0;
    p->priority = 0;
    p->data = nullptr;
    p->entry = nullptr;
    p->tracks.resize(std::size_t(std::max(trackCount, 0)));
    p->group = allocateGroup(channels);
    if (!p->group) {
        heap.release(at);
        error(-0x86);
        return nullptr;
    }
    p->tempo = 0x78;
    p->accumulator = 0;
    p->increment = std::int32_t(p->ppq8) * 8 * tickMicros;
    p->beat = 500000;
    p->callback = nullptr;
    p->next = nullptr;
    resetTracks(*p);
    Player* result = p.get();
    ownedPlayers.push_back(std::move(p));
    return result;
}

void Driver::Impl::resetTracks(Player& p) { // 0x02003d2c
    for (int i = 0; i < p.trackCount; ++i) {
        Track& t = p.tracks[std::size_t(i)];
        t.active = 0;
        t.id = std::uint16_t(i);
        t.channel = std::uint8_t(i);
        t.start = t.cursor = t.loop = nullptr;
        t.state = nullptr;
    }
}

int Driver::Impl::parseEntry(Player& p, const std::uint8_t* e) { // 0x02003a84
    if (e[6] > p.maxTracks) {
        return -1;
    }
    p.ppq8 = std::uint8_t(le16(e + 2) >> 3);
    p.b6 = e[4];
    p.trackCount = e[6];
    p.channelCount = e[7];
    p.priority = e[0x1b];
    p.volumeGroup = e[0x19];
    p.limitGroup = e[0x1a];
    p.flags = std::uint8_t((p.flags & ~0x10u) | ((e[0x18] & 0x80) ? 0x10 : 0));
    std::size_t k = 0;
    for (const std::uint8_t* c = e + 0x30; chunkTag(c) != tag("eoc "); c = nextChunk(c)) {
        std::uint32_t t = chunkTag(c);
        if (t == tag("bnkl")) {
            continue;
        }
        if (t == tag("trk ")) {
            if (k >= p.tracks.size()) {
                throw std::runtime_error("more track chunks than allocated tracks");
            }
            Track& tr = p.tracks[k++];
            tr.start = c + 0x14;
            tr.id = c[0x10];
            tr.channel = c[0x11];
        }
    }
    p.entry = e;
    return 0;
}

int Driver::Impl::resetPlayer(Player& p) { // 0x020037c4
    prepareGroup(*p.group, 0, p.id);
    setGroupVolumeIndex(*p.group, p.volumeGroup, 0);
    p.flags = std::uint8_t((p.flags & ~2u) | 0x40);
    p.activeTracks = 0;
    p.age = owner.counter_;
    p.ticks = 0;
    p.tempo = 0x78;
    p.accumulator = 0;
    p.increment = std::int32_t(p.ppq8) * 8 * tickMicros;
    p.beat = 500000;
    p.fading = 0;
    p.w88 = 0;
    p.tempoValue = 0x1000000;
    p.tempoCount = 0;
    p.pitchValue = 0;
    p.pitchCount = 0;
    p.volumeValue = 0x7f0000;
    p.volumeCount = 0;
    p.panValue = 0x400000;
    p.panCount = 0;
    p.volume = 0x7f;
    for (int i = 0; i < p.trackCount; ++i) {
        Track& t = p.tracks[std::size_t(i)];
        if (t.start) {
            t.active = 1;
            t.cursor = t.start;
            t.wait = 0;
            t.lastDuration = 0;
            t.gate = 0x7f;
            t.octave = 4;
            t.depth = 0;
            t.loop = nullptr;
            if (t.channel >= p.group->channels.size()) {
                throw std::runtime_error("track channel beyond the player's channel group");
            }
            t.state = &p.group->channels[t.channel];
        }
        ++p.activeTracks;
    }
    p.flags |= 1;
    return 0;
}

namespace {
// Shared fade setup pattern of 0x02003268/0x02003354/0x02003520/0x02003924.
std::uint16_t fadeTo(std::int32_t& value, std::int32_t& step, std::int32_t& target,
                     std::int32_t newTarget, std::uint16_t count) {
    target = newTarget;
    if (count == 0) {
        value = newTarget;
        return 0;
    }
    std::int32_t delta = newTarget - value;
    if (delta == 0) {
        return 0;
    }
    step = sdiv(delta, count);
    return count;
}

std::uint32_t beatMicros(std::int32_t tempoValue, std::uint8_t tempo, bool halfword) {
    std::uint32_t product =
        halfword ? std::uint32_t(std::int32_t(std::int16_t(tempoValue >> 16)) * std::int16_t(tempo))
                 : std::uint32_t((tempoValue >> 16) * std::int32_t(tempo));
    std::uint32_t denominator = product >> 8;
    if (!denominator) {
        denominator = 1;
    }
    return udiv(60000000, denominator);
}
} // namespace

void Driver::Impl::applyParameters(Player& p, PlayParameters& q) { // 0x02003520
    const std::uint32_t tick = std::uint32_t(std::int32_t(tickMicros));
    if (q.tempoValid) {
        std::int32_t scale = q.tempo ? q.tempo : 0x100;
        std::uint16_t n = std::uint16_t(udiv(std::uint32_t(q.tempoMs) * 1000, tick));
        p.tempoCount = n = fadeTo(p.tempoValue, p.tempoStep, p.tempoTarget,
                                  std::int32_t(std::uint32_t(scale) << 16), n);
        if (n == 0) {
            p.beat = std::int32_t(beatMicros(p.tempoValue, p.tempo, true));
        } else {
            p.fading |= 1;
        }
    }
    if (q.pitchValid) {
        std::uint16_t n = std::uint16_t(udiv(std::uint32_t(q.pitchMs) * 1000, tick));
        p.pitchCount = n = fadeTo(p.pitchValue, p.pitchStep, p.pitchTarget,
                                  std::int32_t(std::uint32_t(std::int32_t(q.pitch)) << 16), n);
        if (n) {
            p.fading |= 2;
        } else {
            setGroupPitch(*p.group, s16(p.pitchValue >> 16), 1);
        }
    }
    if (q.volumeValid) {
        p.volume = std::uint8_t(q.volume);
        if (p.flags & 0x20) {
            q.volume = 0;
            q.volumeMs = 0;
        }
        std::uint16_t n = std::uint16_t(udiv(std::uint32_t(q.volumeMs) * 1000, tick));
        p.volumeCount = n = fadeTo(p.volumeValue, p.volumeStep, p.volumeTarget,
                                   std::int32_t(std::uint32_t(std::int32_t(q.volume)) << 16), n);
        if (n) {
            p.fading |= 4;
        } else {
            setGroupVolume(*p.group, s8(p.volumeValue >> 16), 1);
        }
    }
    if (q.panValid) {
        std::uint16_t n = std::uint16_t(udiv(std::uint32_t(q.panMs) * 1000, tick));
        p.panCount = n = fadeTo(p.panValue, p.panStep, p.panTarget,
                                std::int32_t(std::uint32_t(std::int32_t(q.pan)) << 16), n);
        if (n) {
            p.fading |= 0x10;
        } else {
            setGroupPan(*p.group, std::uint8_t(s8(p.panValue >> 16)), 1);
        }
    }
    if (p.startDelay < 0) {
        p.startDelay = 0;
    }
}

int Driver::Impl::start(Player& p, std::uint16_t user, std::uint8_t limitGroup,
                        PlayParameters& q) { // 0x02003434
    applyParameters(p, q);
    p.flags &= std::uint8_t(~0xcu);
    p.user = user;
    p.limitGroup = limitGroup;
    p.startDelay = q.startDelayMs < 0 ? -1 : s16(sdiv(q.startDelayMs * 1000, tickMicros));
    p.flags |= 2;
    return 0;
}

int Driver::Impl::stop(Player& p, int fadeMs) { // 0x02003924
    if (!(p.flags & 2)) {
        return 0;
    }
    if (fadeMs) {
        p.flags |= 8;
        p.volume = 0;
        std::uint16_t n = std::uint16_t(
            udiv(std::uint32_t(fadeMs) * 1000, std::uint32_t(std::int32_t(tickMicros))));
        p.volumeCount = n = fadeTo(p.volumeValue, p.volumeStep, p.volumeTarget, 0, n);
        if (n == 0) {
            setGroupVolume(*p.group, s8(p.volumeValue >> 16), 1);
        } else {
            p.fading |= 4;
        }
    } else {
        p.flags &= std::uint8_t(~2u);
        releaseAll(*p.group);
    }
    return 0;
}

const std::uint8_t* Driver::Impl::findSong(const std::uint8_t* data, int number) { // 0x02003a18
    for (const std::uint8_t* c = data + 0x40;; c = nextChunk(c)) {
        std::uint32_t t = chunkTag(c);
        if (t == tag("eod ")) {
            return nullptr;
        }
        if (t == tag("song") && std::int16_t(le16(c + 0x10)) == number) {
            return c + 0x10;
        }
    }
}

void Driver::Impl::freePlayer(Player* p) {
    release(ownedPlayers, p);
}

int Driver::loadSequence(const std::uint8_t* data, std::size_t,
                         PlayerCallback callback) { // 0x02002cfc -> 0x02002db8, 0x02002f78
    auto& d = *impl_;
    if (!data) {
        d.error(-0x18);
        return -0x18;
    }
    int v = checkHeader(d, data, tag("smdl"), 0x415);
    if (v < 0) {
        return v;
    }
    std::uint32_t id = std::uint32_t(v) + 0x10000;
    if (d.findSequence(id)) {
        d.error(-0x49);
        return -0x49;
    }
    const std::uint8_t* entry = d.findSong(data, 1);
    if (!entry) {
        d.error(-0x43);
        return -0x43;
    }
    Player* p = d.allocatePlayer(entry[6], entry[7]);
    if (!p) {
        return -0x80;
    }
    p->id = id;
    p->data = data;
    d.parseEntry(*p, entry);
    Player** link = &d.players;
    while (*link) {
        link = &(*link)->next;
    }
    *link = p;
    p->callback = std::move(callback);
    return std::int32_t(id);
}

void Driver::unloadSequence(int id) { // 0x02002d34 -> 0x02002ec8
    auto& d = *impl_;
    if (id < 0) {
        return;
    }
    Player* p = d.findSequence(std::uint32_t(id));
    if (!p) {
        d.error(-0x42);
        return;
    }
    if (p->flags & 2) {
        stopSequence(id, 0);
    }
    Player** link = &d.players;
    while (*link && *link != p) {
        link = &(*link)->next;
    }
    if (*link) {
        *link = p->next;
    }
    d.freeGroup(p->group);
    d.freePlayer(p);
}

int Driver::sequencePlaying(int id) { // 0x02002fb8
    Player* p = impl_->findSequence(std::uint32_t(id));
    if (!p) {
        impl_->error(-0x42);
        return -0x42;
    }
    return (p->flags >> 1) & 1;
}

int Driver::playSequence(int id, std::uint8_t volume, std::uint16_t fadeMs) { // 0x02003000
    auto& d = *impl_;
    Player* p = d.findSequence(std::uint32_t(id));
    if (!p) {
        d.error(-0x42);
        return -0x42;
    }
    if (p->flags & 2) {
        stopSequence(id, 0);
    }
    int r = d.resetPlayer(*p);
    if (r < 0) {
        return r;
    }
    d.resetChannels(*p->group, -1, p->id);
    PlayParameters q;
    q.startDelayMs = 0;
    q.tempoValid = 0;
    q.pitchValid = 0;
    q.volumeValid = 2;
    q.panValid = 0;
    q.volume = std::int8_t(volume);
    q.volumeMs = fadeMs;
    p->volumeValue = 0;
    p->volumeCount = 0;
    const bool muted = d.paused && (p->flags & 0x40);
    p->flags = std::uint8_t((p->flags & ~0x20u) | (muted ? 0x20 : 0));
    r = d.start(*p, 0, 0, q);
    return r < 0 ? r : 0;
}

int Driver::resumeSequence(int id, std::uint8_t volume, std::uint16_t fadeMs) { // 0x02003128
    auto& d = *impl_;
    Player* p = d.findSequence(std::uint32_t(id));
    if (!p) {
        d.error(-0x42);
        return -0x42;
    }
    PlayParameters q;
    q.pitchValid = 0;
    q.volume = std::int8_t(volume);
    q.volumeMs = fadeMs;
    q.panValid = 0;
    q.tempoValid = 0;
    q.volumeValid = 2;
    if (!(p->flags & 2)) {
        if (!(p->flags & 1)) {
            int r = d.resetPlayer(*p);
            if (r < 0) {
                return r;
            }
        }
        p->volumeValue = 0;
        p->volumeCount = 0;
    }
    int r = d.start(*p, 0, 0, q); // q.startDelayMs is uninitialised stack in the original
    return r < 0 ? r : 0;
}

int Driver::stopSequence(int id, int fadeMs) { // 0x020031dc
    Player* p = impl_->findSequence(std::uint32_t(id));
    if (!p) {
        impl_->error(-0x42);
        return -0x42;
    }
    impl_->stop(*p, fadeMs);
    return 0;
}

void Driver::stopAllSequences() { // 0x0200321c
    for (Player* p = impl_->players; p; p = p->next) {
        if (p->flags & 2) {
            impl_->stop(*p, 0);
        }
    }
}

int Driver::sequenceTempo(int id, int scale, int ms) { // 0x02003268
    auto& d = *impl_;
    Player* p = d.findSequence(std::uint32_t(id));
    if (!p) {
        d.error(-0x42);
        return -0x42;
    }
    std::uint32_t previous = std::uint32_t(p->tempoValue >> 16) & 0xff;
    std::int32_t value = std::int16_t(scale);
    if (value == 0) {
        value = 0x100;
    }
    std::uint16_t n =
        std::uint16_t(udiv(std::uint32_t(ms * 1000), std::uint32_t(std::int32_t(d.tickMicros))));
    p->tempoCount = n = fadeTo(p->tempoValue, p->tempoStep, p->tempoTarget,
                               std::int32_t(std::uint32_t(value) << 16), n);
    if (n == 0) {
        p->beat = std::int32_t(beatMicros(p->tempoValue, p->tempo, true));
    } else {
        p->fading |= 1;
    }
    return std::int32_t(previous);
}

int Driver::sequenceVolume(int id, int volume, int ms) { // 0x02003354
    auto& d = *impl_;
    Player* p = d.findSequence(std::uint32_t(id));
    if (!p) {
        d.error(-0x42);
        return -0x42;
    }
    p->volume = std::uint8_t(volume);
    if (p->flags & 0x20) {
        return 0; // original returns an unassigned register here
    }
    std::uint32_t previous = std::uint32_t(p->volumeValue >> 16) & 0xff;
    std::uint16_t n =
        std::uint16_t(udiv(std::uint32_t(ms * 1000), std::uint32_t(std::int32_t(d.tickMicros))));
    p->volumeCount = n = fadeTo(p->volumeValue, p->volumeStep, p->volumeTarget,
                                std::int32_t(std::uint32_t(volume) << 16), n);
    if (n) {
        p->fading |= 4;
    } else {
        d.setGroupVolume(*p->group, s8(p->volumeValue >> 16), 1);
    }
    return std::int32_t(previous);
}

// ---- effects ------------------------------------------------------------------
EffectBank* Driver::Impl::findEffectBank(std::uint32_t id) { // 0x02004698
    EffectBank* b = effectBanks;
    while (b && b->id != id) {
        b = b->next;
    }
    return b;
}

const std::uint8_t* Driver::Impl::effectEntry(const std::uint8_t* data, int index,
                                              const std::uint8_t* table) { // 0x020043a8
    if (index < 0) {
        return nullptr;
    }
    if (std::int16_t(le16(data + 0x30)) <= index) {
        return nullptr;
    }
    const std::uint8_t* base = table;
    const std::uint8_t* offsets = table;
    if (data[0x35] == 0) {
        base = data;
        offsets = data + 0x40;
    }
    std::uint16_t offset = le16(offsets + index * 2);
    return offset ? base + offset : nullptr;
}

int Driver::Impl::limitById(std::uint32_t id, int limit, int steal) { // 0x020043f0
    if (!limit) {
        return 0;
    }
    if (steal == 1) {
        int count;
        do {
            count = 0;
            std::uint32_t oldest = 0x7fffffff;
            Player* victim = nullptr;
            for (Player* p = effects; p; p = p->next) {
                if ((p->flags & 2) && p->id == id) {
                    ++count;
                    if (oldest > p->age) {
                        oldest = p->age;
                        victim = p;
                    }
                }
            }
            if (count >= limit && victim) {
                stop(*victim, 0);
            }
        } while (count > limit);
        return 0;
    }
    int count = 0;
    for (Player* p = effects; p; p = p->next) {
        if ((p->flags & 2) && p->id == id) {
            ++count;
        }
    }
    return count < limit ? 0 : count;
}

int Driver::Impl::limitByGroup(std::uint32_t group, int limit, int steal) { // 0x02004504
    if (!limit) {
        return 0;
    }
    if (steal == 1) {
        int count;
        do {
            count = 0;
            std::uint32_t oldest = 0x7fffffff;
            Player* victim = nullptr;
            for (Player* p = effects; p; p = p->next) {
                if ((p->flags & 2) && p->limitGroup == group) {
                    ++count;
                    if (oldest > p->age) {
                        oldest = p->age;
                        victim = p;
                    }
                }
            }
            if (count >= limit && victim) {
                stop(*victim, 0);
            }
        } while (count > limit);
        return 0;
    }
    int count = 0;
    for (Player* p = effects; p; p = p->next) {
        if ((p->flags & 2) && p->limitGroup == group) {
            ++count;
        }
    }
    return count < limit ? 0 : count;
}

Player* Driver::Impl::freeEffectPlayer(std::uint8_t priority) { // 0x02004618
    Player* candidate = nullptr;
    std::uint32_t oldest = 0xffffffffu;
    for (Player* p = effects; p; p = p->next) {
        if (!(p->flags & 2)) {
            return p;
        }
        if (p->priority >= priority && p->age < oldest) {
            oldest = p->age;
            candidate = p;
        }
    }
    return candidate;
}

int Driver::Impl::createEffectPlayers(int count, int trackCount) { // 0x02003f08
    for (Player* p = effects; p;) {
        Player* next = p->next;
        stop(*p, 0);
        freePlayer(p); // only the player block: its channel group stays allocated and listed
        p = next;
    }
    effects = nullptr;
    effectPlayers = std::uint8_t(count);
    effectTracks = std::uint8_t(trackCount);
    Player** link = &effects;
    for (int i = 0; i < count; ++i) {
        Player* p = allocatePlayer(trackCount, trackCount);
        if (!p) {
            return -0x84;
        }
        *link = p;
        link = &p->next;
    }
    return 0;
}

void Driver::Impl::stopEffectsOf(std::uint32_t handle) { // 0x020047d4
    for (Player* p = effects; p; p = p->next) {
        if ((p->flags & 2) && (p->id & 0xffff) == handle) {
            stop(*p, 0);
        }
    }
}

int Driver::loadEffects(const std::uint8_t* data, std::size_t) { // 0x02003e4c -> 0x02003fac
    auto& d = *impl_;
    if (!data) {
        d.error(-0x18);
        return -0x18;
    }
    int v = checkHeader(d, data, tag("sedl"), 0x415);
    if (v < 0) {
        return v;
    }
    if (d.findEffectBank(std::uint32_t(v) & 0xffff)) {
        d.error(-0x4a);
        return -0x4a;
    }
    std::uint32_t at = d.heap.allocate(0x1c);
    if (!at) {
        d.error(-0x80);
        return -0x80;
    }
    auto b = std::make_unique<EffectBank>();
    b->heap = at;
    b->data = data;
    b->count = std::int16_t(le16(data + 0x30));
    b->id = std::uint16_t(v);
    b->flag = 1;
    if (data[0x35]) {
        for (const std::uint8_t* c = data + 0x40;; c = nextChunk(c)) {
            std::uint32_t t = chunkTag(c);
            if (t == tag("eod ")) {
                break;
            }
            if (t == tag("bnkl")) {
                b->bankLinks = c + 0x10;
            } else if (t == tag("mcrl")) {
                b->macros = c + 0x10;
            } else if (t == tag("seq ")) {
                b->sequences = c + 0x10;
            }
        }
    }
    EffectBank** link = &d.effectBanks;
    while (*link) {
        link = &(*link)->next;
    }
    *link = b.get();
    int result = b->id;
    d.ownedEffects.push_back(std::move(b));
    return result;
}

void Driver::unloadEffects(int handle) { // 0x02003e68 -> 0x02004150
    auto& d = *impl_;
    if (handle < 0) {
        return;
    }
    std::uint32_t id = std::uint32_t(handle) & 0xffff;
    d.stopEffectsOf(id);
    EffectBank** link = &d.effectBanks;
    while (*link && (*link)->id != id) {
        link = &(*link)->next;
    }
    if (!*link) {
        d.error(-0x44);
        return;
    }
    EffectBank* b = *link;
    *link = b->next;
    d.release(d.ownedEffects, b);
}

int Driver::playEffect(std::uint32_t id, std::uint16_t user,
                       const PlayParameters& parameters) { // 0x02004214
    auto& d = *impl_;
    EffectBank* bank = d.findEffectBank(id & 0xffff);
    if (!bank) {
        d.error(-0x44);
        return -0x44;
    }
    const std::uint8_t* entry = d.effectEntry(bank->data, std::int16_t(id >> 16), bank->sequences);
    if (!entry) {
        d.warn(-0x45);
        return -0x45;
    }
    const int steal = (entry[0x18] & 1) == 0 ? 1 : 0;
    const std::uint8_t group = entry[0x1a];
    int limited = group == 0 ? d.limitById(id, entry[0x1c], steal)
                             : d.limitByGroup(group, entry[0x1c], steal);
    if (std::uint8_t(limited)) {
        return 0;
    }
    Player* p = d.freeEffectPlayer(entry[0x1b]);
    if (!p) {
        return 0;
    }
    if (p->flags & 2) {
        d.stop(*p, 0);
    }
    p->id = id;
    p->data = bank->data;
    d.resetTracks(*p);
    d.parseEntry(*p, entry);
    d.resetPlayer(*p);
    p->volumeValue = 0;
    p->volumeCount = 0;
    p->flags = std::uint8_t((p->flags & ~0x40u) | ((bank->flag & 1) << 6));
    const bool muted = d.paused && bank->flag;
    p->flags = std::uint8_t((p->flags & ~0x20u) | (muted ? 0x20 : 0));
    PlayParameters q = parameters;
    int r = d.start(*p, user, group, q);
    return r < 0 ? r : 0;
}

void Driver::stopEffect(std::uint32_t id, std::uint16_t user, int fadeMs) { // 0x020046e4
    for (Player* p = impl_->effects; p; p = p->next) {
        if ((p->flags & 2) && p->id == id && (user == 0 || p->user == user)) {
            impl_->stop(*p, fadeMs);
        }
    }
}

int Driver::effectQuery(std::uint32_t id, std::uint32_t argument) { // 0x020041b4
    for (Player* p = impl_->effects; p; p = p->next) {
        if ((p->flags & 2) && p->id == id && (argument == 0 || p->user == argument)) {
            return 1;
        }
    }
    return 0;
}

int Driver::configureEffectPlayers(int count, int tracks) {
    return impl_->createEffectPlayers(count, tracks);
}

void Driver::stopAllEffects() { // 0x02004768
    for (Player* p = impl_->effects; p; p = p->next) {
        if (p->flags & 2) {
            impl_->stop(*p, 0);
        }
    }
}

PlayParameters buildParameters(const RequestBlock& r, std::int32_t delay) { // 0x02002094
    auto h = [&](int byteOffset) {
        return r.half[std::size_t(byteOffset / 2)];
    };
    auto word = [&](int byteOffset) {
        return std::bit_cast<float>(
            std::uint32_t(h(byteOffset) | std::uint32_t(h(byteOffset + 2)) << 16));
    };
    auto volumeFromFloat = [](float x) {
        const float v = 127.0f * x;
        return !(v > -2147483648.0f)  ? INT32_MIN
               : !(v < 2147483648.0f) ? INT32_MAX
                                      : std::int32_t(v);
    };
    PlayParameters q;
    q.startDelayMs = delay;
    q.pitchValid = std::uint8_t(h(4));
    q.pitch = std::int16_t(h(0));
    q.pitchMs = h(2);
    q.volumeValid = std::uint8_t(h(0x12));
    switch (h(0x12)) {
    case 2:
        q.volume = std::int8_t(std::int16_t(h(8)));
        q.volumeAux = 0;
        break;
    case 4:
        q.volume = std::int8_t(std::int16_t(h(8)));
        q.volumeAux = std::int8_t(std::int16_t(h(0xa)));
        break;
    // Modes 3/5: floats scaled by 127.0f and truncated (0x0203afd4, 0x0203aed0).
    case 3:
        q.volume = std::int8_t(volumeFromFloat(word(8)));
        q.volumeAux = 0;
        break;
    case 5:
        q.volume = std::int8_t(volumeFromFloat(word(8)));
        q.volumeAux = std::int8_t(volumeFromFloat(word(0xc)));
        break;
    default:
        throw std::runtime_error(
            "volume request mode without assigned values (0x02002094 uses stale registers)");
    }
    q.volumeMs = h(0x10);
    q.panValid = std::uint8_t(h(0x26));
    switch (h(0x26)) {
    case 2:
        q.pan = std::int8_t(std::int16_t(h(0x14)));
        break;
    // Mode 3: double (1 + x) / 2 * 127, truncated (0x0203ae4c, 0x020393fc,
    // 0x0203bb4c, 0x020399c4, 0x02039864).
    case 3:
        q.pan = std::int8_t(std::int32_t(127.0 * ((1.0 + double(word(0x14))) / 2.0)));
        break;
    // Modes 5/8 copy four words to +0x18..+0x24, which 0x02003520 never reads,
    // and leave the pan byte it does read unassigned (caller stack contents).
    case 5:
    case 8:
        throw std::runtime_error(
            "pan request modes 5/8 leave the pan byte unassigned (0x02002094)");
    default:
        break;
    }
    q.panMs = h(0x24);
    q.tempoValid = std::uint8_t(h(0x2e));
    q.tempo = std::int16_t(h(0x28));
    q.tempoMs = h(0x2c);
    return q;
}

// ---- sequencer (0x020081e4..0x02009488) --------------------------------------
void Driver::Impl::countDurations(Player& p) { // 0x020081e4
    for (auto& c : p.group->channels) {
        for (Voice* v = c.voices; v; v = v->next) {
            if (!(c.flags & 2) && v->duration > 0 && --v->duration == 0) {
                release(c, *v);
            }
        }
    }
}

void Driver::Impl::fades(Player& p) { // 0x02008640
    if (p.tempoCount) {
        --p.tempoCount;
        p.tempoValue = p.tempoCount == 0 ? p.tempoTarget : p.tempoValue + p.tempoStep;
        p.beat = std::int32_t(beatMicros(p.tempoValue, p.tempo, false));
        if (p.tempoCount == 0) {
            p.fading &= std::uint8_t(~1u);
        }
    }
    if (p.pitchCount) {
        --p.pitchCount;
        p.pitchValue = p.pitchCount == 0 ? p.pitchTarget : p.pitchValue + p.pitchStep;
        setGroupPitch(*p.group, s16(p.pitchValue >> 16), 1);
        if (p.pitchCount == 0) {
            p.fading &= std::uint8_t(~2u);
        }
    }
    if (p.volumeCount) {
        --p.volumeCount;
        p.volumeValue = p.volumeCount == 0 ? p.volumeTarget : p.volumeValue + p.volumeStep;
        setGroupVolume(*p.group, s8(p.volumeValue >> 16), 1);
        if (p.volumeCount == 0) {
            p.fading &= std::uint8_t(~4u);
        }
    }
    if (p.panCount) {
        --p.panCount;
        p.panValue = p.panCount == 0 ? p.panTarget : p.panValue + p.panStep;
        setGroupPan(*p.group, std::uint8_t(p.panValue >> 16), 1);
        if (p.panCount == 0) {
            p.fading &= std::uint8_t(~0x10u);
        }
    }
}

void Driver::Impl::tickPlayers(Player* p) { // 0x020083f8
    for (; p; p = p->next) {
        if (!(p->flags & 2)) {
            continue;
        }
        if (p->startDelay != 0) {
            if (p->startDelay > 0) {
                --p->startDelay;
            }
            continue;
        }
        if ((p->flags & 8) && p->volumeValue == 0) {
            p->flags &= std::uint8_t(~0xau);
            releaseAll(*p->group);
            if (p->callback) {
                p->callback(p->id, 1, 0);
            }
        }
        if (p->fading) {
            fades(*p);
        }
        p->accumulator += p->increment;
        if (p->accumulator > p->beat) {
            do {
                countDurations(*p);
                for (auto& c : p->group->channels) {
                    if (c.sweepCount) {
                        --c.sweepCount;
                        c.sweep = c.sweepCount == 0 ? c.sweepTarget : c.sweep + c.sweepStep;
                        c.dirty |= 0x10;
                    }
                    if (c.volumeCount) {
                        --c.volumeCount;
                        c.volumeValue =
                            c.volumeCount == 0 ? c.volumeTarget : c.volumeValue + c.volumeStep;
                        c.dirty |= 0x20;
                    }
                    if (c.panCount) {
                        --c.panCount;
                        c.panValue = c.panCount == 0 ? c.panTarget : c.panValue + c.panStep;
                        c.dirty |= 0x40;
                    }
                }
                p->activeTracks = 0;
                for (int i = 0; i < p->trackCount; ++i) {
                    Track& t = p->tracks[std::size_t(i)];
                    if (t.active == 1) {
                        step(*p, t);
                        if (t.active == 1) {
                            ++p->activeTracks;
                        }
                    }
                }
                ++p->ticks;
                p->accumulator -= p->beat;
            } while (p->accumulator > p->beat);
        }
        if (p->activeTracks == 0) {
            p->flags &= std::uint8_t(~2u);
        }
    }
}

void Driver::Impl::step(Player& p, Track& t) { // 0x02008254
    ChannelState& c = *t.state;
    const std::uint8_t* cursor = t.cursor;
    for (std::int32_t wait = t.wait; wait == 0; wait = t.wait) {
        if (traceStep) {
            traceStep(cursor, *cursor);
        }
        const std::uint8_t op = *cursor++;
        if (op < 0x80) {
            const std::uint8_t b = *cursor;
            const std::int8_t octave =
                s8(std::int32_t(std::uint8_t(t.octave)) + (std::int32_t((b >> 4) & 3) - 2));
            Note note;
            note.key = std::uint8_t(std::int32_t(octave) * 12 + (b & 0xf));
            t.octave = octave;
            const unsigned bytes = b >> 6;
            const std::uint8_t* q = cursor + 1;
            std::int32_t duration;
            if (bytes == 0) {
                duration = t.lastDuration;
            } else {
                duration = 0;
                for (unsigned n = bytes; n; --n) {
                    duration = std::int32_t(*q++) + duration * 256;
                }
                t.lastDuration = duration;
            }
            std::uint32_t x = std::uint32_t(duration) * t.gate;
            std::uint32_t hi = mulhu(x, 0x02040811);
            note.duration = std::int32_t((hi + ((x - hi) >> 1)) >> 6);
            note.flag = 0;
            note.velocity = op;
            if (t.muted == 0) {
                if (c.flags & 2) {
                    legato(c, note, t.lastKey);
                } else {
                    c.sweep = 0;
                    noteOn(c, note);
                }
            }
            t.lastKey = note.key;
            c.flags &= std::uint8_t(~2u);
            cursor = q;
        } else if (op < 0x90) {
            t.wait = t.lastWait = waitTable[op - 0x80];
        } else {
            cursor = event(op, cursor, p, t, c);
        }
        if (t.active == 0) {
            break;
        }
    }
    --t.wait;
    t.cursor = cursor;
}

namespace {
std::int32_t big16(const std::uint8_t* q) {
    return std::int16_t(std::uint16_t(q[0] << 8 | q[1]));
}

void lfoShort(LfoParameters& l, std::uint8_t kind, std::uint8_t destination,
              const std::uint8_t* q) { // 0xdc/0xe4/0xec
    l.kind = kind;
    l.destination = destination;
    l.waveform = q[4];
    l.depth = std::int16_t(le16(q));
    l.rateMs = le16(q + 2);
    l.delayMs = 0;
    l.fadeMs = 0;
    l.b14 = 0;
}
} // namespace

const std::uint8_t* Driver::Impl::event(std::uint8_t op, const std::uint8_t* q, Player& p, Track& t,
                                        ChannelState& c) {
    const std::uint32_t tick = std::uint32_t(std::int32_t(tickMicros));
    switch (op) {
    case 0x90:
        t.wait = t.lastWait;
        return q; // 0x020087f0
    case 0x91:
        t.lastWait += std::int8_t(q[0]);
        t.wait = t.lastWait;
        return q + 1; // 0x020087fc
    case 0x92:
        t.lastWait = t.wait = q[0];
        return q + 1; // 0x02008814
    case 0x93:
        t.lastWait = t.wait = le16(q);
        return q + 2; // 0x02008824
    case 0x94:
        t.lastWait = t.wait = std::int32_t(q[0] | q[1] << 8 | q[2] << 16);
        return q + 3; // 0x02008840
    case 0x95:
        if (countSounding(c) > 0) {
            t.wait = q[0];
            return q - 1;
        }
        return q + 1; // 0x02008864
    case 0x98:        // 0x02008890
        if (t.loop) {
            return t.loop;
        }
        t.active = 0;
        stopChannel(c);
        return q - 1;
    case 0x99:
        t.loop = q;
        p.flags |= 0x10;
        return q; // 0x020088c4
    case 0x9c: {  // 0x020088d8
        if (t.depth >= 3) {
            throw std::runtime_error("sequence repeat nesting beyond the track's three frames");
        }
        auto& f = t.stack[t.depth];
        f.start = q + 1;
        f.end = nullptr;
        f.count = q[0];
        f.octave = t.octave;
        ++t.depth;
        return q + 1;
    }
    case 0x9d: { // 0x02008918
        auto& f = t.stack[t.depth - 1];
        if (std::uint8_t(f.count - 1) == 0) {
            --t.depth;
            return q;
        }
        f.end = q;
        --f.count;
        t.octave = f.octave;
        return f.start;
    }
    case 0x9e: { // 0x02008954
        auto& f = t.stack[t.depth - 1];
        if (std::uint8_t(f.count - 1) == 0) {
            --t.depth;
            return f.end;
        }
        return q;
    }
    case 0xa0:
        t.octave = std::int8_t(q[0]);
        return q + 1; // 0x0200897c
    case 0xa1:
        t.octave = s8(std::uint8_t(t.octave) + std::int8_t(q[0]));
        return q + 1; // 0x02008988
    case 0xa4:
    case 0xa5:
        p.beat = std::int32_t(beatMicros(p.tempoValue, q[0], false));
        p.tempo = q[0];
        return q + 1; // 0x0200899c / 0x020089dc
    case 0xa8:
        selectBank(c, std::uint16_t(q[1] + (q[0] << 8)));
        return q + 2; // 0x02008a1c
    case 0xa9:
        selectBank(c, std::uint16_t((c.bank & 0xff) + (q[0] << 8)));
        return q + 1; // 0x02008a48
    case 0xaa:
        selectBank(c, std::uint16_t((c.bank & 0xff00) + q[0]));
        return q + 1; // 0x02008a78
    case 0xab:
        return q + 1; // 0x02008aa8
    case 0xac: {      // 0x02008ab0
        std::int32_t r = selectProgram(c, q[0]);
        if (r < 0 && p.callback) {
            p.callback(p.id, -0xcc, std::int32_t(std::uint32_t(c.bank) << 16) - r);
        }
        return q + 1;
    }
    case 0xaf: { // 0x02008af8
        std::uint16_t n = std::uint16_t(sdiv(std::int32_t((q[1] << 8) * 1000 + q[0]), tickMicros));
        p.volumeCount = fadeTo(p.volumeValue, p.volumeStep, p.volumeTarget,
                               std::int32_t(std::uint32_t(q[2]) << 16), n);
        return q + 3;
    }
    case 0xb0:
        overrideReset(c.envelopeOverride);
        return q; // 0x02008b6c
    case 0xb1:
        c.envelopeOverride[8] = q[0];
        overrideValidate(c.envelopeOverride);
        return q + 1; // 0x02008b84
    case 0xb2:
        c.envelopeOverride[9] = q[0];
        overrideValidate(c.envelopeOverride);
        return q + 1; // 0x02008ba4
    case 0xb3:
        c.envelopeOverride[12] = q[0];
        overrideValidate(c.envelopeOverride);
        return q + 1; // 0x02008bc4
    case 0xb4:        // 0x02008be4
        if (q[0] != 0xff) {
            c.envelopeOverride[10] = q[0];
        }
        if (q[1] != 0xff) {
            c.envelopeOverride[11] = q[1];
        }
        overrideValidate(c.envelopeOverride);
        return q + 2;
    case 0xb5:
        c.envelopeOverride[13] = q[0];
        overrideValidate(c.envelopeOverride);
        return q + 1; // 0x02008c14
    case 0xb6:
        c.envelopeOverride[14] = q[0];
        overrideValidate(c.envelopeOverride);
        return q + 1; // 0x02008c34
    case 0xbc:
        t.gate = q[0];
        return q + 1; // 0x02008c54
    case 0xbe:
        setLfoScale(c, std::int8_t(q[0]));
        return q + 1; // 0x02008c60
    case 0xbf:        // 0x02008c7c
        if (q[0] >= 0x40) {
            t.state->flags |= 1;
        } else {
            t.state->flags &= std::uint8_t(~1u);
            releaseHeld(*t.state);
        }
        return q + 1;
    case 0xc0:
        c.flags |= 2;
        return q; // 0x02008cb4
    case 0xc3:
        c.velocityScale = q[0];
        return q + 1; // 0x02008cc4
    case 0xcb:
        return q + 2; // 0x02008cd0
    case 0xd0:
        c.detune = s16(std::int32_t(std::int8_t(q[0])) << 8);
        c.dirty |= 0x10;
        return q + 1; // 0x02008cd8
    case 0xd1:
        c.detune = s16(c.detune + (std::int32_t(std::int8_t(q[0])) << 8));
        c.dirty |= 0x10;
        return q + 1; // 0x02008cf4
    case 0xd2:
        c.detune = s16(c.detune + (std::int32_t(std::int8_t(q[0])) << 2));
        c.dirty |= 0x10;
        return q + 1; // 0x02008d14
    case 0xd3:
        c.detune = s16(c.detune + std::int16_t(le16(q)));
        c.dirty |= 0x10;
        return q + 2; // 0x02008d34
    case 0xd4: {      // 0x02008d64
        if (!(c.flags & 2)) {
            c.sweep = 0;
        }
        std::uint16_t n = le16(q);
        std::int32_t target =
            std::int32_t(std::uint32_t(std::uint16_t((c.sweep >> 16) + (q[2] << 8))) << 16);
        c.sweepCount = fadeTo(c.sweep, c.sweepStep, c.sweepTarget, target, n);
        c.dirty |= 0x10;
        return q + 3;
    }
    case 0xd5: { // 0x02008df8
        std::uint8_t lo = q[0], hi = q[1];
        if (lo > hi) {
            std::swap(lo, hi);
        }
        c.keyLow = lo;
        c.keyHigh = hi;
        return q + 2;
    }
    case 0xd6:
        c.randomPitch = std::int16_t(le16(q));
        return q + 2; // 0x02008e20
    case 0xd7:
        setBend(c, s16(big16(q)));
        return q + 2; // 0x02008e38
    case 0xd8:
        c.fine = s16(big16(q));
        return q + 2; // 0x02008e64
    case 0xdb:
        c.bendRange = q[0];
        return q + 1; // 0x02008e7c
    case 0xdc:
        lfoShort(c.lfo[0], 1, 1, q);
        return q + 5; // 0x02008e88
    case 0xdd:
        c.lfo[0].delayMs = le16(q);
        c.lfo[0].fadeMs = le16(q + 2);
        return q + 4; // 0x02008ee0
    case 0xdf: {
        std::uint8_t v = q[0] == 2 ? 1 : q[0];
        c.lfo[0].kind = v;
        c.lfo[0].destination = v ? 1 : 0;
        return q + 1;
    } // 0x02008f08
    case 0xe0:
        c.volumeTarget = c.volumeValue = std::int32_t(std::uint32_t(q[0]) << 16);
        c.volumeCount = 0;
        c.dirty |= 0x20;
        return q + 1; // 0x02008f30
    case 0xe1: {      // 0x02008f58
        std::int32_t v =
            std::clamp(std::int32_t(std::int8_t(q[0])) + (c.volumeValue >> 16), 0, 0x7f);
        c.volumeTarget = c.volumeValue = v << 16;
        c.volumeCount = 0;
        c.dirty |= 0x20;
        return q + 1;
    }
    case 0xe2: // 0x02008fa0
        c.volumeCount =
            fadeTo(c.volumeValue, c.volumeStep, c.volumeTarget,
                   std::int32_t(std::uint32_t(std::int32_t(std::int8_t(q[2]))) << 16), le16(q));
        c.dirty |= 0x20;
        return q + 3;
    case 0xe3:
        c.expression = q[0];
        c.dirty |= 0x20;
        return q + 1; // 0x02009008
    case 0xe4:
        lfoShort(c.lfo[1], 1, 2, q);
        return q + 5; // 0x02009020
    case 0xe5:
        c.lfo[1].delayMs = le16(q);
        c.lfo[1].fadeMs = le16(q + 2);
        return q + 4; // 0x0200907c
    case 0xe7: {
        std::uint8_t v = q[0] == 2 ? 1 : q[0];
        c.lfo[1].kind = v;
        c.lfo[1].destination = v ? 2 : 0;
        return q + 1;
    } // 0x020090a4
    case 0xe8: { // 0x020090cc
        std::uint32_t v = q[0] > 0x7f ? 0x7f : q[0];
        c.panTarget = c.panValue = std::int32_t(v << 16);
        c.panCount = 0;
        c.dirty |= 0x40;
        return q + 1;
    }
    case 0xe9: { // 0x02009100
        std::int32_t v = std::clamp(std::int32_t(std::int8_t(q[0])) + (c.panValue >> 16), 0, 0x7f);
        c.panTarget = c.panValue = v << 16;
        c.panCount = 0;
        c.dirty |= 0x40;
        return q + 1;
    }
    case 0xea: // 0x02009148
        c.panCount =
            fadeTo(c.panValue, c.panStep, c.panTarget,
                   std::int32_t(std::uint32_t(std::int32_t(std::int8_t(q[2]))) << 16), le16(q));
        c.dirty |= 0x40;
        return q + 3;
    case 0xec:
        lfoShort(c.lfo[2], 1, 3, q);
        return q + 5; // 0x020091b0
    case 0xed:
        c.lfo[2].delayMs = le16(q);
        c.lfo[2].fadeMs = le16(q + 2);
        return q + 4; // 0x0200920c
    case 0xef: {
        std::uint8_t v = q[0] == 2 ? 1 : q[0];
        c.lfo[2].kind = v;
        c.lfo[2].destination = v ? 3 : 0;
        return q + 1;
    } // 0x02009234
    case 0xf0: { // 0x0200925c
        auto& l = c.lfo[c.lfoSlot & 3];
        if (c.lfoSlot > 3) {
            throw std::runtime_error("LFO slot beyond the channel's four parameter blocks");
        }
        l.waveform = q[4];
        l.depth = std::int16_t(le16(q));
        l.rateMs = le16(q + 2);
        l.delayMs = 0;
        l.fadeMs = 0;
        l.b14 = 0;
        return q + 5;
    }
    case 0xf1: { // 0x020092b4
        auto& l = c.lfo[c.lfoSlot & 3];
        l.delayMs = le16(q);
        l.fadeMs = le16(q + 2);
        return q + 4;
    }
    case 0xf2: { // 0x020092e8
        if (c.lfoSlot > 3) {
            throw std::runtime_error("LFO slot beyond the channel's four parameter blocks");
        }
        auto& l = c.lfo[c.lfoSlot];
        const std::uint8_t v = q[1];
        switch (q[0]) {
        case 1:
            c.lfoSlot = v;
            break;
        case 2:
            l.kind = v;
            break;
        case 3:
            l.destination = v;
            break;
        case 4:
            l.waveform = v;
            break;
        case 5: {
            static constexpr std::int32_t factor[5] = {20, 10, -20, 20, 10};
            l.depth = std::int32_t(v) * (l.destination < 5 ? factor[l.destination] : 20);
            break;
        }
        case 6:
            l.rateMs = std::uint16_t(v * 5);
            break;
        case 7:
            l.delayMs = std::uint16_t(v * 20);
            break;
        case 8:
            l.delayMs = std::uint16_t((l.delayMs & 0xff00) | v);
            break;
        case 9:
            l.delayMs = std::uint16_t((l.delayMs & 0xff) | v << 8);
            break;
        case 10:
            l.fadeMs = std::uint16_t(v * 20);
            break;
        default:
            break;
        }
        return q + 2;
    }
    case 0xf3: { // 0x02009424
        c.lfoSlot = q[0];
        if (c.lfoSlot > 3) {
            throw std::runtime_error("LFO slot beyond the channel's four parameter blocks");
        }
        c.lfo[c.lfoSlot].kind = q[1] == 2 ? 1 : q[1];
        c.lfo[c.lfoSlot].destination = q[2];
        return q + 3;
    }
    case 0xf6:
        p.marker = q[0];
        if (p.callback) {
            p.callback(p.id, 8, q[0]);
        }
        return q + 1; // 0x02009458
    case 0xf8:
        return q + 2; // 0x02009488
    default:
        t.active = 0;
        return q; // 0x020087e4
    }
    (void)tick;
}

// ---- driver entry points ---------------------------------------------------
Driver::Driver(SoundCommands& hardware, const SoundTables& tables)
    : impl_(std::make_unique<Impl>(*this, hardware, tables)) {}

SoundTables SoundTables::fromArm9(std::span<const std::uint8_t> program) {
    return read(program, 0x02000000, 0x02043ee0, 0x02043ba8, 0x02043aa8, 0x02043e60);
}

SoundTables SoundTables::read(std::span<const std::uint8_t> program, std::uint32_t load,
                              std::uint32_t pitchAt, std::uint32_t msAt, std::uint32_t unitsAt,
                              std::uint32_t panAt) {
    auto at = [&](std::uint32_t address, std::size_t bytes) {
        if (address < load || address - load + bytes > program.size()) {
            throw std::runtime_error("sound tables outside the program");
        }
        return program.data() + (address - load);
    };
    SoundTables t;
    const std::uint8_t* pitch = at(pitchAt, t.pitch.size() * 2);
    for (std::size_t i = 0; i < t.pitch.size(); ++i) {
        t.pitch[i] = le16(pitch + 2 * i);
    }
    const std::uint8_t* ms = at(msAt, t.envelopeMs.size() * 4);
    const std::uint8_t* units = at(unitsAt, t.envelopeUnits.size() * 2);
    for (std::size_t i = 0; i < 128; ++i) {
        t.envelopeMs[i] = std::int32_t(le32(ms + 4 * i));
        t.envelopeUnits[i] = le16(units + 2 * i);
    }
    std::copy_n(at(panAt, t.panCurve.size()), t.panCurve.size(), t.panCurve.begin());
    return t;
}

Driver::~Driver() = default;

int Driver::initialize(std::uint32_t heapBase, std::uint32_t heapSize,
                       std::uint8_t mode) { // 0x02000dac
    auto& d = *impl_;
    if (d.initialized == 1) {
        d.error(-0xc0);
        return -0xc0;
    }
    error_ = 0;
    d.warning = 0;
    d.b30 = 1;
    d.resetGuard = 1;
    d.pausedTicks = 0;
    d.tickMicros = mode ? 0x2710 : 0x414b;
    d.random = 0xbc614e;
    d.groups = nullptr;
    d.mainBanks = nullptr;
    d.programBanks = nullptr;
    d.players = nullptr;
    d.effects = nullptr;
    d.effectBanks = nullptr; // 0x020010ec
    d.streamTimeout = 1;
    d.velocityMode = 2;
    d.mode = mode;
    for (unsigned i = 0; i < 128; ++i) {
        d.controllerDefault[i] = std::uint8_t(i);
    }
    d.controllerDefault[7] = 100;
    d.controllerDefault[11] = 0x7f;
    d.controllerDefault[10] = 0x40;
    d.streamsReady = 0;
    d.streamCounter = 1;
    d.monoTwoVoices = 0; // 0x02004940
    d.heap.initialize(heapBase, heapSize);
    d.voicesInit();
    // 0x02009490: external MIDI input pointer cleared (never set in this ROM).
    d.groupVolume.fill(0);
    for (unsigned i = 1; i < 16; ++i) {
        d.groupVolume[i] = 0x7f;
    }
    d.b32 = 0;
    d.createEffectPlayers(4, 4); // 0x02003df8(4, 4)
    // 0x02001410: stream request queue/thread. 0x02007f68 creates the sound
    // thread (host scheduler) and starts its alarm through 0x02008038.
    d.soundThreadStart();
    d.initialized = 1;
    d.paused = 0;
    return 0;
}

void Driver::Impl::soundThreadStart() {              // 0x02008038
    hw.setupAlarm(1, 0x1474, 0x1474, ++alarmIds[1]); // 0x02021188 (callback 0x02008114)
    hw.startTimers(0, 0, 2, 0);                      // 0x020210bc; 0x02021718/0x02021548 flush
}

void Driver::Impl::soundThreadStop() { // 0x02008090
    stopTimers(0xffff, 0, 2, 0);
    hw.setupAlarm(1, 0, 0, ++alarmIds[1]);
    lastStarted = 0;
    stopMask = 0;
    startMask = 0;
    alarmStop = 0;
    alarmStart = 0;
}

int Driver::pause(int ms) { // 0x02000fcc
    auto& d = *impl_;
    if (d.paused == 1) {
        return 0;
    }
    d.soundThreadStop();
    for (Player* list : {d.players, d.effects}) { // 0x020034ac: playing players with bit 6
        for (Player* p = list; p; p = p->next) {
            if ((p->flags & 2) && (p->flags & 0x40)) {
                p->flags |= 0x20;
            }
        }
    }
    if (Impl::Stream* s = d.streams;
        s && s->fileOpen == 1 && s->playing == 1) { // 0x020051d8 (first stream)
        s->playing = 0;
        s->stopNow = 0;
        s->paused = 1;
        d.streamHardwareStop(*s);
    }
    // 0x0200155c (a fade over ms x 0xfa) is an empty stub in this ROM.
    d.hardwareReset();                                        // 0x0200a80c
    const std::int32_t ticks = sdiv(ms * 1000, d.tickMicros); // 0x0203b744
    d.pausedTicks = ticks ? ticks : 1;
    d.paused = 1;
    d.streamTimeout = 2;
    return 0;
}

int Driver::resume(int) { // 0x02001078
    auto& d = *impl_;
    if (d.paused == 0) {
        return 0;
    }
    d.paused = 0;
    d.streamTimeout = 1;
    for (Player* list : {d.players, d.effects}) { // 0x020034ec
        for (Player* p = list; p; p = p->next) {
            if (p->flags & 0x20) {
                p->flags &= std::uint8_t(~0x20);
            }
        }
    }
    if (Impl::Stream* s = d.streams; s && s->fileOpen == 1 && s->paused == 1) { // 0x02005224
        s->playing = 1;
        s->stopNow = 0;
        s->paused = 0;
        d.streamHardwareStart(*s);
    }
    // 0x02001560 is an empty stub.
    d.soundThreadStart();
    return 0;
}

void Driver::debugPoke(std::uint32_t address, std::uint32_t value) {
    auto& d = *impl_;
    switch (address) {
    case 0x020464c4:
        d.startMask = value;
        break;
    case 0x020464c8:
        d.stopMask = value;
        break;
    case 0x020464cc:
        d.releasedMask = value;
        break;
    case 0x02052448:
        d.alarmIds[0] = std::uint8_t(value);
        break; // SDK alarm 0 setup counter (history)
    default:
        throw std::runtime_error("debugPoke: unsupported address");
    }
}

void Driver::tick() { // 0x02008130 loop body
    auto& d = *impl_;
    RandomWaves::driver = &d;
    ++counter_;
    d.tickPlayers(d.players);
    d.tickPlayers(d.effects);
    // 0x020096a4: external MIDI input; its source pointer is always null here.
    d.streamUpdate(); // 0x02005cd8: SAD stream fades and stops
    d.envelopes();
    d.update();
    d.output();
    d.status();
}

// ---- SAD streams (0x02004a30 .. 0x02006bdc) ---------------------------------
// The overlay-2 controller (0x0207e41c..0x0207ee3c) drives these. Ring buffers,
// work buffer, pool objects and the thread stack come from the driver heap in
// the original order (0x0200491c: 32-byte aligned). Decoding reuses the
// recovered SAD decoder (src/sad_stream.*, 02007cac/02007ab0), whose refill
// step is exactly one half (+0x1e4 = 512 source bytes for this ROM's 0xb2/0xb4).
int Driver::streamInit(const StreamConfig& c) { // 0x02004f8c
    auto& d = *impl_;
    if (d.streamsReady == 1) {
        return 0;
    }
    d.streamConfig = c;
    std::uint8_t voices = c.voices ? c.voices : std::uint8_t(0x10 - c.firstVoice);
    d.streamConfig.voices = voices;
    d.streamConfig.bufferBytes = c.bufferBytes ? c.bufferBytes : 0x2000;
    d.streams = nullptr;
    d.streamWork = d.heap.allocate(d.streamConfig.bufferBytes / 2, 0x20);
    if (!d.streamWork) {
        d.error(-0x80);
        return -0x80;
    }
    for (int i = 0; i < c.streams; ++i) { // 0x02004e6c
        auto s = std::make_unique<Impl::Stream>();
        s->heap = d.heap.allocate(0x280, 0);
        if (!s->heap) {
            return -0x80;
        }
        s->id = 0;
        s->fileOpen = 0;
        Impl::Stream** link = &d.streams;
        while (*link) {
            link = &(*link)->next;
        }
        *link = s.get();
        d.ownedStreams.push_back(std::move(s));
    }
    // 0x02006fac fills a decoder index table (static data natively); 0x02006054 starts the thread.
    d.streamStack = d.heap.allocate(0x1000, 0x20);
    if (!d.streamStack) {
        return -0x80;
    }
    d.streamsReady = 1;
    return 0;
}

void Driver::streamMonoOnTwoVoices(bool enable) {
    impl_->monoTwoVoices = enable ? 1 : 0;
} // 0x02004a48: state +0x0e

Driver::Impl::Stream* Driver::Impl::findStream(std::uint32_t id, bool report) { // 0x02005fd8
    if (id == 0) {
        return nullptr;
    }
    Stream* s = streams;
    while (s && s->id != id) {
        s = s->next;
    }
    if (!s && report) {
        error(-0x40);
    }
    return s;
}

int Driver::streamCreate(std::unique_ptr<StreamFile> file, int mode) { // 0x020052b4
    auto& d = *impl_;
    if (!d.streamsReady) {
        return -0x5a;
    }
    Impl::Stream* s = d.streams; // 0x020048d0: first free pool object
    while (s && s->id) {
        s = s->next;
    }
    if (!s) {
        d.error(-0xb5);
        return -0xb5;
    }
    // 0x02005478: reset the object, assign the next handle, event 1.
    Impl::Stream* keepNext = s->next;
    std::uint32_t keepHeap = s->heap;
    *s = Impl::Stream{};
    s->next = keepNext;
    s->heap = keepHeap;
    s->id = d.streamCounter << 16;
    if (++d.streamCounter >= 0xffff) {
        d.streamCounter = 1;
    }
    s->curve = std::uint8_t(d.groupVolume[1]);
    s->fileOpen = 0;
    if (!file) {
        // 0x02006b28 FS_OpenFile failed. The object goes back to the pool
        // (hypothesis: the ROM's failure path was not traced; without it
        // every missing file would use up one of the few stream objects).
        s->id = 0;
        d.error(-0xb3);
        return -0xb3;
    }
    s->file = std::move(file);
    s->fileOpen = 1;
    // 0x02006b9c reads 0x100 header bytes into the work buffer; 020064f4 parses them.
    std::array<std::uint8_t, 0x100> header{};
    if (!s->file->read(0, header.data(), 0x100)) {
        d.error(-0xb3);
        return -0xb3;
    }
    const std::uint8_t format = header[0x33];
    s->channels = header[0x32];
    s->voiceCount = header[0x32];
    s->curve = std::uint8_t(d.groupVolume[header[0x62] & 15]);
    std::uint32_t blockOut; // +0x1e0: output bytes per 16-byte block
    if (format == 0xb2) {
        blockOut = 0x78;
    } else if (format == 0xb4) {
        blockOut = 0x3c;
    } else {
        throw std::runtime_error("SAD stream format outside this ROM's 0xb2/0xb4");
    }
    const std::uint32_t unit = blockOut * 0x20;
    s->ringBytes = unit * udiv(d.streamConfig.bufferBytes, unit);
    s->volume = header[0x60] ? header[0x60] : 0x7f;
    const std::uint8_t loopFlag = header[0x31];
    s->loops = s->loopSetting == 0 ? 0 : s->loopSetting == 1 ? 1 : std::uint8_t(loopFlag != 0);
    // 0x02005e28: reserve voices firstVoice.. (marker +0x14a = 0x10).
    if (!s->voicesReserved) {
        if (s->voiceCount == 1 && d.monoTwoVoices == 1) {
            s->voiceCount = 2;
        }
        unsigned taken = 0;
        for (unsigned i = d.streamConfig.firstVoice;
             i < unsigned(d.streamConfig.firstVoice + d.streamConfig.voices) && i < 16; ++i) {
            Voice& v = d.voices[i];
            if (std::uint8_t(v.priority >> 16) != 0x10) {
                d.freeVoice(v);
                v.envelope.valid = 0;
                v.envelope.state = 1;
                v.envelope.level = 0x3fffffff;
                v.envelope.remaining = 0;
                v.envelope.active = 1; // 0x0200b2dc
                lfoClear(v.lfo);       // 0x0200b45c
                v.dirty = 0;
                v.priority = (v.priority & 0xff00ffffu) | 0x100000u;
                s->voices[taken++] = &v;
                if (taken >= s->voiceCount) {
                    break;
                }
            }
        }
        s->voicesReserved = 1;
    }
    // 0x02005f28: ring allocation, per-channel halves.
    s->channelBytes = std::uint32_t(sdiv(std::int32_t(s->ringBytes), s->channels));
    s->halfBytes = s->channelBytes / 2;
    s->ringHeap = d.heap.allocate(s->ringBytes, 0x20);
    if (!s->ringHeap) {
        d.error(-0x81);
        return -0x81;
    }
    s->ring.assign(s->ringBytes, 0);
    // 0x020069a4 (decoder states from +0x80) and the native decoder open.
    // The recovered decoder owns the whole file and performs the +0x1e4-byte
    // reads of 0x02006b9c itself: half the ring in 16-byte blocks of
    // +0x1e0 output bytes (0x020066a8..0x020066c0), so one refill fills a
    // half exactly (512 bytes for 0xb2, 1024 for 0xb4 with a 0x1e00 ring).
    std::vector<std::uint8_t> bytes(s->file->size());
    if (!s->file->read(0, bytes.data(), std::uint32_t(bytes.size()))) {
        d.error(-0xb3);
        return -0xb3;
    }
    s->decoder.open(bytes, udiv((s->ringBytes / 2) << 4, blockOut));
    s->loaded = 1;
    if (mode == 1) { // pre-fill both halves before start
        for (unsigned h = 0; h < 2; ++h) {
            s->half = std::uint8_t(h);
            d.streamRefill(*s);
        }
        s->clearRing = 0;
    }
    return std::int32_t(s->id);
}

void Driver::Impl::streamRefill(Stream& s) { // 0x02006bdc for the half (s.half)
    const unsigned h = s.half;
    auto pcm = s.decoder.refill();
    const unsigned channels = s.channels;
    const std::size_t frames = channels ? pcm.size() / channels : 0;
    for (unsigned c = 0; c < channels; ++c) {
        std::uint8_t* out = s.channel(c, h);
        std::size_t i = 0;
        for (; i < frames && (i + 1) * 2 <= s.halfBytes; ++i) {
            const std::uint16_t v = std::uint16_t(pcm[i * channels + c]);
            out[i * 2] = std::uint8_t(v);
            out[i * 2 + 1] = std::uint8_t(v >> 8);
        }
        std::fill(out + i * 2, out + s.halfBytes, 0);
    }
    if (s.meter == 1) { // 0x02006e9c -> 0x02006ef0 over each channel's new half
        for (unsigned c = 0; c < channels && c < 2; ++c) {
            const std::uint8_t* in = s.channel(c, h);
            std::int32_t low = 0x7fff, high = -0x8000;
            for (std::uint32_t i = 0; i < s.halfBytes / 2; ++i) {
                const std::int16_t v = std::int16_t(in[i * 2] | in[i * 2 + 1] << 8);
                low = std::min<std::int32_t>(low, v);
                high = std::max<std::int32_t>(high, v);
            }
            s.levels[c * 2] = std::int16_t(low);
            s.levels[c * 2 + 1] = std::int16_t(high);
        }
    }
    if (!s.decoder.playing && s.playing) { // second empty refill: end of data
        s.playing = 0;
        s.stopAfterFade = 0;
        streamHardwareStop(s);
        streamEvent(s, 8);
    }
}

void Driver::Impl::streamVoiceVolume(Stream& s, bool allTwo) {
    std::int32_t x = std::int32_t(s.curve) * std::int32_t(s.volume) * (s.level >> 16);
    std::int32_t y = div16129(x);
    const std::uint16_t value = std::uint16_t(div127(y * y));
    const unsigned n = allTwo ? 2u : s.voiceCount;
    for (unsigned i = 0; i < n; ++i) {
        if (Voice* v = s.voices[i]) {
            v->volume = value;
            v->dirty |= 0x20;
        }
    }
}

void Driver::Impl::streamHardwareStart(Stream& s) { // 0x02006258
    if (s.running == 1) {
        return;
    }
    if (s.clearRing == 1) {
        std::fill(s.ring.begin(), s.ring.end(), 0);
        s.clearRing = 0;
    }
    s.half = 0;
    const std::uint32_t timer = udiv(0x00ffb0ff, s.rate);
    const std::uint32_t period = (timer * s.halfBytes) >> 6;
    hw.setupAlarm(0, period, period, ++alarmIds[0]);
    std::int32_t x = std::int32_t(s.curve) * std::int32_t(s.volume) * (s.level >> 16);
    std::int32_t y = div16129(x);
    const std::uint16_t volume = std::uint16_t(div127(y * y));
    for (unsigned i = 0; i < s.voiceCount; ++i) {
        Voice& v = *s.voices[i];
        v.flags = 0x1000;
        // +0x00c = 1 and +0x01f below: no native consumer reads +0x00c.
        v.wave[3] = 1;                                   // +0x01f format PCM16
        v.data = s.channel(s.channels == 1 ? 0 : i, 0);  // +0x030
        std::memset(v.wave + 0x18, 0, 4);                // +0x034 loop start 0
        const std::uint32_t words = s.channelBytes >> 2; // +0x038 whole ring per channel
        v.wave[0x1c] = std::uint8_t(words);
        v.wave[0x1d] = std::uint8_t(words >> 8);
        v.wave[0x1e] = std::uint8_t(words >> 16);
        v.wave[0x1f] = std::uint8_t(words >> 24);
        v.wave[5] = 1;   // +0x021 loops
        v.timer = timer; // +0x13c
        v.volume = volume;
        v.panOut = s.voiceCount == 1 ? 0x40 : (i & 1) ? 0x7f : 0;
        v.envelope.valid = 0;
        v.envelope.state = 1;
        v.envelope.level = 0x3fffffff;
        v.envelope.remaining = 0;
        v.envelope.active = 1;
        lfoClear(v.lfo);
        v.dirty = 1;
    }
    alarmStart = 1;
    s.running = 1;
}

void Driver::Impl::streamHardwareStop(Stream& s) { // 0x0200647c
    if (!s.running) {
        return;
    }
    s.running = 0;
    for (unsigned i = 0; i < s.voiceCount; ++i) {
        s.voices[i]->dirty = 2;
    }
    alarmStop = 1;
}

int Driver::streamStart(std::uint32_t id, std::uint8_t volume, int fadeMs,
                        std::uint16_t user) { // 0x02005814
    auto& d = *impl_;
    if (!d.streamsReady) {
        return -0x5a;
    }
    Impl::Stream* s = d.findStream(id, true);
    if (!s) {
        return -0x40;
    }
    if (s->playing == 1) {
        s->stopAfterFade = 0;
        return streamVolume(id, volume, fadeMs);
    }
    s->user = user;
    s->stopAfterFade = 0;
    s->stopNow = 0;
    s->targetVolume = volume;
    std::int16_t count =
        std::int16_t(udiv(std::uint32_t(fadeMs * 1000), std::uint32_t(std::int32_t(d.tickMicros))));
    const std::int32_t target = std::int32_t(std::uint32_t(volume) << 16);
    s->target = target;
    if (count == 0) {
        s->level = target;
    } else {
        const std::int32_t diff = target - s->level;
        if (diff == 0) {
            count = 0;
        } else {
            s->step = sdiv(diff, count);
        }
    }
    s->count = count;
    if (s->loaded == 1) {
        d.streamHardwareStart(*s);
    }
    s->playing = 1;
    return 0;
}

int Driver::streamVolume(std::uint32_t id, std::uint8_t volume, int fadeMs) { // 0x02005a14
    auto& d = *impl_;
    Impl::Stream* s = d.findStream(id, true);
    if (!s) {
        return -0x40;
    }
    s->targetVolume = volume;
    std::int16_t count =
        std::int16_t(udiv(std::uint32_t(fadeMs * 1000), std::uint32_t(std::int32_t(d.tickMicros))));
    const std::int32_t target = std::int32_t(std::uint32_t(volume) << 16);
    s->target = target;
    if (count == 0) {
        s->level = target;
    } else {
        const std::int32_t diff = target - s->level;
        if (diff == 0) {
            count = 0;
        } else {
            s->step = sdiv(diff, count);
        }
    }
    s->count = count;
    if (count == 0) {
        d.streamVoiceVolume(*s, true);
    }
    return 0;
}

int Driver::streamStop(std::uint32_t id, int fadeMs) { // 0x02005970
    auto& d = *impl_;
    if (!d.streamsReady) {
        return -0x5a;
    }
    Impl::Stream* s = d.findStream(id, true);
    if (!s) {
        return -0x40;
    }
    s->targetVolume = 0;
    std::int16_t count =
        std::int16_t(udiv(std::uint32_t(fadeMs * 1000), std::uint32_t(std::int32_t(d.tickMicros))));
    s->target = 0;
    if (count == 0) {
        s->level = 0;
    } else if (s->level == 0) {
        count = 0;
    } else {
        s->step = sdiv(-s->level, count);
    }
    s->count = count;
    if (count == 0) {
        s->stopNow = 1;
    } else {
        s->stopAfterFade = 1;
    }
    return -1;
}

int Driver::streamPlaying(std::uint32_t id) { // 0x020057e0
    auto& d = *impl_;
    if (!d.streamsReady) {
        return -0x5a;
    }
    Impl::Stream* s = d.findStream(id, true);
    return s ? s->playing : -0x40;
}

int Driver::streamQuery(std::uint32_t id) { // 0x02005270
    auto& d = *impl_;
    if (!d.streamsReady) {
        return -0x5a;
    }
    Impl::Stream* s = d.findStream(id, false);
    if (!s) {
        return -0xb1;
    }
    return s->fileOpen ? std::int32_t(s->id) : 0;
}

int Driver::streamSetMeter(std::uint32_t id, std::uint8_t enable) { // 0x02004bd8
    Impl::Stream* s = impl_->findStream(id, false);
    if (!s) {
        return -0x40;
    }
    const int previous = s->meter;
    s->meter = enable;
    return previous;
}

int Driver::streamLevels(std::uint32_t id, std::array<std::int16_t, 4>& levels) { // 0x02004c04
    Impl::Stream* s = impl_->findStream(id, false);
    if (!s) {
        return -0x40;
    }
    for (unsigned c = 0; c < s->channels && c < 2; ++c) {
        levels[c * 2] = s->meter == 1 ? s->levels[c * 2] : 0;
        levels[c * 2 + 1] = s->meter == 1 ? s->levels[c * 2 + 1] : 0;
    }
    return s->channels;
}

std::int32_t Driver::streamPosition(std::uint32_t id) { // 0x02004b60 (+0x08 of its record)
    Impl::Stream* s = impl_->findStream(id, false);
    if (!s) {
        return -0x40;
    }
    return std::int32_t(s->decoder.bytesRead - s->decoder.header().dataBegin);
}

namespace {
// 0x02004d80 / 0x02004e2c: rate of the stream's +0x1c low nibble (0xb2 -> 2, 0xb4 -> 4).
std::int64_t streamRate(std::uint8_t format) {
    return (format & 0xf) == 2 ? 0x3fec : (format & 0xf) == 4 ? 0x7fd8 : 0xbb80;
}

constexpr std::int32_t blockUnits =
    0x1e; // +0x1dc for both formats in this ROM (0x02006634 / 0x02006650)
} // namespace

std::int32_t Driver::streamBytesToMs(std::uint32_t id, std::int32_t bytes) { // 0x02004d90
    Impl::Stream* s = impl_->findStream(id, true);
    if (!s) {
        return -0x40;
    }
    if (bytes < 0) {
        bytes = 0;
    }
    const std::int32_t perChannel = sdiv(bytes, s->channels);
    const std::uint32_t units = std::uint32_t((perChannel / 16) * blockUnits);
    return std::int32_t(std::int64_t(std::uint64_t(units) * 1000) /
                        streamRate(s->decoder.header().format));
}

std::int32_t Driver::streamMsToBytes(std::uint32_t id, std::int32_t ms) { // 0x02004cf8
    Impl::Stream* s = impl_->findStream(id, true);
    if (!s) {
        return -0x40;
    }
    if (ms < 0) {
        ms = 0;
    }
    const std::int32_t units =
        std::int32_t(std::int64_t(ms) * streamRate(s->decoder.header().format) / 1000);
    return std::int32_t(std::uint32_t(sdiv(units << 4, blockUnits) * s->channels) & 0x7fffff00u);
}

int Driver::streamSeek(std::uint32_t id, std::int32_t bytes) { // 0x02005b34
    auto& d = *impl_;
    Impl::Stream* s = d.findStream(id, true);
    if (!s) {
        return -0x40;
    }
    if (bytes & 0xff) {
        d.warn(-0x18);
        bytes &= ~0xff;
    } // 0x02001140: not block aligned
    const SadHeader& h = s->decoder.header();
    // +0x22 (header byte 0x65 != 0) gates non-zero seeks with -0xb6; readSadHeader
    // accepts only files with 0x65 == 1, as every stream in this ROM has.
    std::fill(s->ring.begin(), s->ring.end(), 0);
    if (bytes < 0) {
        bytes = 0;
    } else {
        bytes = std::min<std::int32_t>(bytes, std::int32_t(h.dataEnd - h.dataBegin));
    }
    s->decoder.cursor = s->decoder.bytesRead = h.dataBegin + std::uint32_t(bytes); // +0x40 / +0x44
    if ((h.format & 0xf0) == 0x70) {
        throw std::runtime_error("SAD seek for format family 0x7x is outside this ROM");
    }
    for (unsigned c = 0; c < s->channels && c < 2; ++c) {
        s->decoder.channels[c].history = 0; // +0x208 + c*0x18 only
    }
    return 0;
}

int Driver::streamSetCallback(std::uint32_t id, StreamCallback callback) { // 0x02005788
    Impl::Stream* s = impl_->findStream(id, true);
    if (!s) {
        return -0x40;
    }
    s->callback = std::move(callback);
    return 0;
}

void Driver::Impl::streamDestroy(Stream& s) { // 0x02005648
    const std::uint32_t id = s.id;
    s.fileOpen = 0;
    s.playing = 0;
    s.stopNow = 0;
    for (auto* v : s.voices) {
        if (v) {
            freeVoice(*v);
        }
    }
    if (s.ringHeap) {
        heap.release(s.ringHeap);
        s.ringHeap = 0;
        s.ring.clear();
    }
    StreamCallback callback = std::move(s.callback);
    s.id = 0;
    if (callback) {
        callback(id, 2);
    }
}

int Driver::streamClose(std::uint32_t id) { // 0x020056f0
    auto& d = *impl_;
    if (!d.streamsReady) {
        return -0x5a;
    }
    Impl::Stream* s = d.findStream(id, true);
    if (!s) {
        return -0x40;
    }
    if (s->closing) {
        return 0;
    }
    if (s->playing) {
        streamStop(id, 0);
        // The original spins until the sound thread's next 0x02005cd8 clears
        // +9; that pass is performed here for this stream.
        s->playing = 0;
        s->stopNow = 0;
        s->stopAfterFade = 0;
        d.streamHardwareStop(*s);
        d.streamEvent(*s, 9);
    }
    s->closing = 1;
    s->closed = 1;
    s->file.reset();
    s->fileOpen = 0; // 0x02006b70
    d.streamDestroy(*s);
    s->closing = 0;
    return 0;
}

void Driver::Impl::streamUpdate() { // 0x02005cd8
    for (Stream* s = streams; s; s = s->next) {
        if (s->playing && s->loaded && s->count) {
            --s->count;
            s->level = s->count == 0 ? s->target : s->level + s->step;
            streamVoiceVolume(*s, false);
            if (s->count == 0 && s->stopAfterFade == 1) {
                s->stopNow = 1;
            }
        }
        if (s->stopNow == 1) {
            s->playing = 0;
            s->stopNow = 0;
            s->stopAfterFade = 0;
            streamHardwareStop(*s);
            streamEvent(*s, 9);
        }
    }
}

void Driver::streamAlarm() { // 0x020060e4 loop body: one alarm-0 message
    auto& d = *impl_;
    for (Impl::Stream* s = d.streams; s; s = s->next) {
        if (s->running != 1) {
            continue;
        }
        const std::uint8_t h = s->half;
        s->half = std::uint8_t(h ^ 1);
        Impl::Stream& stream = *s;
        stream.half = h;
        d.streamRefill(stream);
        stream.half = std::uint8_t(h ^ 1);
    }
}

std::span<const std::uint8_t> Driver::streamRing(std::uint32_t id, unsigned channel) const {
    for (auto& s : impl_->ownedStreams) {
        if (s->id == id && !s->ring.empty() && channel < s->channels) {
            return {s->ring.data() + channel * s->channelBytes, s->channelBytes};
        }
    }
    return {};
}

bool Driver::streamLocate(const std::uint8_t* p, unsigned& channel, std::uint32_t& offset) const {
    for (auto& s : impl_->ownedStreams) {
        if (!s->ring.empty() && p >= s->ring.data() && p < s->ring.data() + s->ring.size()) {
            const auto at = std::uint32_t(p - s->ring.data());
            channel = s->channelBytes ? at / s->channelBytes : 0;
            offset = at - channel * s->channelBytes;
            return true;
        }
    }
    return false;
}

const std::uint8_t* Driver::streamMemoryLimit(const std::uint8_t* p) const {
    for (auto& s : impl_->ownedStreams) {
        if (!s->ring.empty() && p >= s->ring.data() && p < s->ring.data() + s->ring.size()) {
            return s->ring.data() + s->ring.size();
        }
    }
    return nullptr;
}

} // namespace picarat::dse
