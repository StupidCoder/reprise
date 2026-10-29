#pragma once
#include "sonic1_gg/data/level_data.hpp"
#include <array>

namespace sonic1_gg {
struct StateAccess;

// Music bytecode is data, interpreted by this reconstructed sequencer. It is
// not Z80 machine code. Addresses below identify bytes in ROM bank 3.
struct SoundData {
    Bytes sequences; // CPU 4716..7FBC: song/effect tables and musical scores.
    Bytes notes;     // 44D5..44F2: fifteen base periods.
    Bytes drums;     // 43CE..43DD: two noise-envelope presets.
    unsigned byte(unsigned address) const;

    unsigned word(unsigned address) const { return byte(address) | (byte(address + 1) << 8); }
};

SoundData extract_sound(const Rom& rom);
void save_sound(const SoundData&, const std::filesystem::path&);
SoundData load_sound(const std::filesystem::path&);

// Music numbers of the sound driver. Only the ones the game logic starts by
// name are listed; levels store their own music number in the level header.
enum class Music : unsigned {
    GreenHill = 0,
    ScrapBrain = 4, // Also Sky Base 1/3 and the escape after the final boss.
    Title = 6,
    Map = 7,
    Invincibility = 8,
    ActClear = 9,
    Death = 10,
    Boss = 11,
    FinalBoss = 13,
    Credits = 14,
    Ending = 19,
    Emerald = 20,
};

// Sound effect numbers, named after where the game uses them.
enum class Effect : unsigned {
    Jump = 0,
    Break = 1, // Enemy destroyed, boss hit, cannon fire.
    Ring = 2,
    Skid = 3,
    Spring = 4,
    Launch = 5,       // Ramp launch, waterfall.
    Roll = 6,         // Also the teleporter.
    SpecialStage = 7, // Goal sign's special stage face, special stage bumpers.
    ExtraLife = 9,
    Projectile = 10,
    SignLand = 12,
    SignReward = 14,
    RingLoss = 17,
    WaterEnter = 18,
    Electric = 19, // Electric floor and arcs switching on.
    FlameJet = 23,
    Bolt = 24,  // Electric bolts and spears setting off.
    Door = 25,  // Doors and the Scrap Brain lift.
    Chime = 26, // Pressure switch; Labyrinth's air warning.
    MineBurst = 27,
    LauncherFire = 28,
    SpikeChain = 29,
    PlankFall = 32,
    BossDefeated = 33, // Final boss.
    AirBubble = 34,
};

struct SoundEvent {
    enum class Kind { Music, Effect, Tempo, Stop, Fade } kind;
    unsigned value{};
};

// Four music voices plus one effect voice. The latter can borrow one chip
// channel while the music's note timers continue in the background (423A).
class SoundDriver {
    friend struct StateAccess;

public:
    explicit SoundDriver(const SoundData& data) : data_(data) {}

    void music(unsigned id);
    bool playing() const; // Music voices can still produce notes (excludes terminal rest loops).
    void effect(unsigned id);
    void stop();
    void speed_shoes(bool fast); // 4B80/4BD2 change the 16-bit tempo.
    void fade(unsigned amount);  // 4224
    void tick();                 // 4000, once per video interrupt.
    void event(SoundEvent);
    Bytes take_writes(); // Ordered writes to the Game Gear PSG.
private:
    struct Voice {
        std::uint8_t tone_latch{}, volume_latch{};
        std::uint16_t remaining{}, volume{}, period{}, detune{}, vibrato_offset{};
        std::uint8_t envelope{}, stage{};
        std::array<std::uint8_t, 6> shape{}; // attack, decay, sustain, decay, sustain, release
        std::uint8_t vibrato_delay{}, vibrato_rate{}, vibrato_steps{};
        std::uint16_t vibrato_delta{};
        std::uint8_t delay_left{}, rate_left{}, steps_left{};
        std::uint16_t delta{};
        std::uint8_t gate{}, octave{}, default_length{}, noise{};
        std::uint16_t unit{}, song_base{}, position{}, repeat_position{};
        std::uint8_t flags{}, index{}, target_volume{};
        unsigned loop_depth{};
        bool loop_initialized{};
        std::array<std::uint8_t, 16> loops{};

        bool active() const { return flags & 2; }
    };

    const SoundData& data_;
    std::array<Voice, 5> voices_{};
    std::array<unsigned, 4> output_{};
    bool output_initialized_{};
    std::uint8_t flags_{}, effect_channel_{}, priority_{}, last_noise_{};
    std::uint16_t unit_{}, tempo_{}, normal_tempo_{}, effect_tempo_{}, fade_left_{}, fade_step_{};
    Bytes writes_;
    void sequence(Voice&, unsigned tempo);
    void synthesize(Voice&);
    void start_note(Voice&);

    void write(unsigned value) { writes_.push_back(value); }
};
} // namespace sonic1_gg
