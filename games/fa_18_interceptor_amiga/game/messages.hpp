#pragma once
#include "amiga/hunk.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <cstdint>
#include <vector>

// The cockpit's message line (game.md "Cockpit messages"): one of the 54
// message records ($1321C8: an attribute byte, 26 characters, a beep byte)
// chosen each frame ($103D4C) - a message the game posted ($119C68), else
// the one for the first warning that is on (the stall's here). A code's low
// byte is the record; its high byte says how it shows: bit 15 blinks
// between the record and the next one (both bits 15 and 14: and blank),
// bit 12 at twice the rate, bit 14 alone shows it for a time. The record's
// attribute gives the time (low 6 bits, counted in blinks; 0: for ever)
// and the colour; its beep byte makes the warning beep sound while it
// blinks.
namespace interceptor::game {

class Messages {
public:
    explicit Messages(const amiga::LoadedImage& program);

    // $119C68: the game posts a message.
    void post(std::uint16_t code);
    // $103C94, at the start of each frame: the blink clock.
    void clock();
    // $103D4C, after the flight: the message for this frame. Returns the
    // warning beep's bits for the sound update (sounds.hpp `warning`).
    std::uint32_t update(bool stall_warning);

    std::uint16_t code() const { return code_; }
    // The record on the line now, and its colour (palette index).
    int shown() const { return shown_ & 0xFF; }
    int colour() const;

    // $10D8D8 (a new start): no message.
    void clear() {
        code_ = shown_ = 0;
        posted_ = false;
    }

    // Research: the state as a recording of the original has it.
    struct State {
        std::uint16_t code, chosen, shown;
        bool posted, kept, started, lit;
        std::int8_t count;
        std::uint8_t attribute, duration, beeps, clock;
    };
    void set_state(const State& s) {
        code_ = s.code; chosen_ = s.chosen; shown_ = s.shown;
        posted_ = s.posted; kept_ = s.kept; started_ = s.started; lit_ = s.lit;
        count_ = s.count; attribute_ = s.attribute; duration_ = s.duration; beeps_ = s.beeps; clock_ = s.clock;
    }

private:
    struct Record {
        std::uint8_t attribute{}, beeps{};
    };
    std::vector<Record> records_;
    std::uint16_t code_ = 0;    // $13ADD0: the message
    std::uint16_t chosen_ = 0;  // $13ADD2: its record, when it was chosen
    std::uint16_t shown_ = 0;   // $13ADCE: what the line shows
    bool posted_ = false;       // $13ABBC bit 0: the game posted it
    bool kept_ = false;         // $13ABBC bit 2
    bool started_ = false;      // $13ABBE bit 13: its count is running
    bool lit_ = false;          // $13ABBE bit 5: the blink's first half shows
    std::int8_t count_ = 0;     // $13AB82
    std::uint8_t attribute_ = 0, duration_ = 0, beeps_ = 0; // $13AB52, $13AB83, $13AB53
    std::uint8_t clock_ = 0;    // $13AB80: 8 .. 1
    std::uint8_t ticks_ = 0;    // $13AB7E: this frame's blink ticks (bit 1 every 4th, bit 2 every 2nd)
};

class Sounds;

// $11A46C: the ground's warnings for the player, from the frame's start
// (after the input): sinking towards the ground nose down, or low and
// banked - CRASH IMMINENT; sinking in a spin - EJECT. Each posted (with its
// beep) once until the line shows something else.
void ground_warnings(const Aircraft& aircraft, Messages& messages, Sounds& sounds);

} // namespace interceptor::game
