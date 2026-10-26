#pragma once
#include "marble_madness_amiga/sound/effects.hpp"
#include <array>
#include <cstdint>

// The music player ($11F5E8-$1200C0, its task $11FABA): eight voices read
// byte streams from the course's sound file and play their notes through
// the effect engine, all with one instrument (an IFF 8SVX voice: a wave
// stored in several octaves).
//
// A score (8 bytes per voice: pattern list, pan, priority) gives each
// voice a list of patterns (6 bytes: repeats, address; 0 repeats ends the
// list). A pattern's bytes are notes (0-$7E, $7F a rest) lasting the
// current duration, or controls ($80 and over): a low nibble of 0 ends the
// pattern, otherwise the byte becomes the voice's duration code (low
// nibble, an index into a table of 16.16 lengths) and flags (bit 6: notes
// end a tempo step early and the next one restarts the sound). Voice 0
// counts the score's repeats. Time runs in vertical blanks, a tempo step
// each.
namespace marble_madness::sound {

class Music {
public:
    Music(const Memory&, Effects&);

    // $122526 + $11FE90: sets the instrument and tempo from a music entry
    // (score, instrument record, tempo word) and plays the score 99 times.
    void start(std::uint32_t entry);
    void stop();  // $11FB52
    // Each vertical blank: the music task ($11FABA) wakes and makes up the
    // blanks since it last played, five at most ($11FA46 each). Its count
    // stands still while the music is stopped, so a new score starts with
    // five steps at once.
    void vertical_blank();
    bool playing() const { return running_; }

private:
    struct Voice {
        std::int32_t time{};          // $120192: 16.16 until the next event
        std::int16_t event{};         // $1201C2: byte in the pattern
        std::int16_t pattern{};       // $1201B2: entry in the pattern list
        std::int16_t repeats{};       // $1201D2: plays of the pattern left
        std::uint8_t flags{0xC1};     // $12018A: duration code and flags
        Handle sound;                 // $12016A
        std::int8_t last_note{};      // $1200F0
        bool restart{};               // $1200F1
        std::uint8_t volume{};        // $120162
        std::uint32_t volume_envelope{}, pitch_envelope{}; // $120652, $120672
        // The instrument for this voice ($11FF42): octaves it can play, the
        // one-shot + repeat and repeat lengths at the highest one, and
        // where its samples start.
        std::uint8_t top_octave{}, bottom_octave{};
        std::uint16_t total_words{}, repeat_words{};
        std::uint32_t base{};
    };

    void set_instrument(int voice, std::uint32_t samples, std::uint32_t header, std::uint32_t volume_envelope,
                        std::uint32_t pitch_envelope);                    // $11FF42
    void set_tempo(std::uint16_t tempo);                                 // $11FB72
    void play(std::uint32_t score, std::int16_t repeats);                // $11FD96
    void next_events(int voice, std::uint32_t record);                   // $11F922
    void pattern_end(int voice, std::uint32_t record);                   // $11F816
    void note(int voice, int note, std::uint32_t record);                // $11F64A
    void release(int voice);                                             // $11F5E8
    void tick();                                                         // $11FA46
    std::uint32_t octave_shift(std::uint32_t header) const;              // $11FF02

    const Memory* memory_;
    Effects* effects_;
    std::array<Voice, 8> voices_{};
    bool running_{};                 // $1200C6 clear
    bool finished_{true};            // $1200C2
    std::uint32_t score_{};          // $1201E2
    std::int16_t score_repeats_{};   // $1201E6
    std::int8_t transpose_{5};       // $1201ED
    std::uint32_t step_{};           // $120238: 16.16 per tick
    std::uint32_t blanks_{5};        // $1200BC (the task has waited for a while)
    std::uint32_t played_to_{};      // the task's count at its last steps
};

} // namespace marble_madness::sound
