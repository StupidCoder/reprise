#pragma once
#include "amiga/hunk.hpp"
#include "amiga/paula.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The sound-effect engine (the original's SfxTask, $121214 and $1206F0-
// $122168): a small synthesiser over the four channels.
//
// A sound is a chain of segments, each a wave (a block of samples with a
// period and a volume) played a number of times; two envelopes shape its
// volume and its pitch, a duration ends it. Up to four sounds play at
// once ("slots"); each takes one or two channels, chosen by its side of
// the screen, and takes channels from sounds of lower priority. Every
// tick (each vertical blank on PAL) the envelopes step and changed periods
// and volumes go to the channels.
//
// The sounds' data (segments, waves, envelopes) are read from the course's
// sound file (or the program); the music builds its chains itself.
namespace marble_madness::sound {

// Reads the 68000 addresses of the program and the loaded sound file.
class Memory {
public:
    Memory(const amiga::LoadedImage& program, const amiga::LoadedImage& bank) : program_(&program), bank_(&bank) {}
    const amiga::LoadedImage& at(std::uint32_t address) const {
        return bank_->contains(address) ? *bank_ : *program_;
    }
    std::uint8_t u8(std::uint32_t a) const { return at(a).u8(a); }
    std::int8_t s8(std::uint32_t a) const { return at(a).s8(a); }
    std::uint16_t u16(std::uint32_t a) const { return at(a).u16(a); }
    std::int16_t s16(std::uint32_t a) const { return at(a).s16(a); }
    std::uint32_t u32(std::uint32_t a) const { return at(a).u32(a); }
    std::int32_t s32(std::uint32_t a) const { return at(a).s32(a); }
    const amiga::LoadedImage& program() const { return *program_; }

private:
    const amiga::LoadedImage* program_;
    const amiga::LoadedImage* bank_;
};

// A block of samples (12 bytes in the original: length in words, period,
// volume, address).
struct Wave {
    std::uint32_t words{};
    std::uint16_t period{}, volume{};
    std::uint32_t data{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(words, period, volume, data); }
};

// A segment (14 bytes: next, count, skip, wave): the wave played `count`
// times (-2: until the sound ends); `skip` non-zero passes the segment by.
struct Segment {
    int next{-1}; // index in the chain, -1: the end
    std::int16_t count{};
    std::uint32_t skip{};
    Wave wave{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(next, count, skip, wave); }
};
using Chain = std::vector<Segment>;

// A started sound, to stop it later (slot and generation; the original
// packs them into a long).
struct Handle {
    int slot{-1};
    std::uint16_t generation{};
    bool valid() const { return slot >= 0; }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(slot, generation); }
};

class Effects {
public:
    explicit Effects(const Memory&);

    // Reads a chain of segments that starts at an address (the segments'
    // next pointers may lead back into the chain).
    Chain read_chain(std::uint32_t first) const;

    // $12156E: reserves channels for the side (pan -2: the right pair, -1:
    // the left pair, 0-$13F: a left and a right channel panned to that
    // screen x) and starts the sound. Envelopes: addresses of (rate,
    // target) lists, 0 for the steady default ($121938).
    Handle play(const Chain&, std::uint32_t volume_envelope, std::uint32_t pitch_envelope, std::uint16_t duration,
                std::int8_t priority, std::int16_t pan);
    void stop(Handle);                              // $1216A6
    void set_pan(Handle, std::int16_t pan);         // $121664
    void tick();                                    // $121F30, each vertical blank
    void stop_all();                                // $121128

    amiga::Paula& paula() { return paula_; }
    // A channel has played a write (audio.device replied it).
    void write_done(int channel, int tag);

private:
    struct Envelope { // $1217F8 / $121888: a value stepped along a list
        std::int32_t value{};
        std::uint32_t list{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(value, list); }
    };
    struct Channel {
        std::uint8_t owner{0xFF}; // $1217B8: $FF free, 4 reserved, else the slot
        bool ended{true};         // $1217B4: the chain has run out
        Chain chain;
        int segment{-1};
        Segment state;            // $121C78: the playing segment's copy
        std::uint16_t base_period{}, base_volume{}; // $12178C, $121794
        std::uint16_t pan_scale{0x100};             // $12179C
        std::uint16_t last_period{}, last_volume{}; // $1217AC, $1217A4
        Envelope volume, pitch;                     // $121918, $121928

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(owner, ended, chain, segment, state, base_period, base_volume, pan_scale, last_period,
               last_volume, volume, pitch);
        }
    };
    struct Slot {
        bool free{true};          // $1217EC
        bool active{};            // $1217F0
        std::uint16_t generation{1}; // $1217CC
        std::uint16_t mask{};     // $1217E4: its channels
        std::int8_t priority{};   // $1217F4
        std::int16_t pan{};       // $1217D4
        std::uint16_t duration{}; // $1217DC
        bool positional{};        // $121944

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(free, active, generation, mask, priority, pan, duration, positional);
        }
    };

    std::uint16_t reserve(std::int8_t priority, std::int16_t pan); // $120BAE / $120A4E
    void stop_slot(int slot);                                      // $120936
    void pan_channels(int slot);                                   // $120CBA
    void next_write(int channel, int tag, bool first);             // $120D52
    void assign(Envelope&, std::uint32_t list);                    // $120EE2
    void step(Envelope&);                                          // $122168
    std::uint16_t period(const Channel&) const;                    // $121EF8
    std::uint16_t volume(const Channel&) const;                    // $122040
    bool alive(Handle h) const {
        return h.valid() && h.slot < 4 && slots_[std::size_t(h.slot)].generation == h.generation;
    }

    const Memory* memory_;
    amiga::Paula paula_;
    std::array<Channel, 4> channels_{};
    std::array<Slot, 4> slots_{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(channels_, slots_);
        paula_.persist_samples(ar, [this](std::uint32_t address) { return &memory_->at(address); });
    }
};

} // namespace marble_madness::sound
