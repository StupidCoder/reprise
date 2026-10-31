#pragma once
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/sound_driver.hpp"
#include <cstdint>
#include <vector>

// What the game plays and when (game.md "The game's sounds"), for the
// cockpit view of the player's aircraft:
// - the engine: textcpt as two looping channels a period apart (textegn
//   with the afterburner), its pitch and volume from the throttle, moved
//   every fourth frame by a glide of 32 ticks ($104AA0);
// - the wind (textwnd) when the throttle is at 0, restarted every fourth
//   frame at a random pitch;
// - the gear (textger) for about two seconds after the gear key, the tyres
//   (texttre) at a touch-down, the crash's noise;
// - the beeps of the warnings on channel 3 (a square wave and a script).
namespace interceptor::game {

// The engine's sound events ($13ABA0), from the flight and the game.
enum class EngineSound : std::int8_t {
    none = 0,
    update = -1,         // now rather than on the fourth frame ($FF)
    start = -2,          // the engine (re)starts: the textcpt pair ($FE)
    afterburner_on = -3, // $FD
    afterburner_off = -4,// $FC
    missile = -5,        // $FB: a missile leaves ($1176F0, the only setter)
    boom = -6,           // $FA: past the speed of sound
};

// The warning beeps ($13AE44 bits), checked in this order.
namespace warning {
constexpr std::uint32_t spin = 0x40, crash = 0x80, bit_01 = 0x01, message = 0x04, bit_100 = 0x100,
                        bit_10 = 0x10, bit_02 = 0x02, stall = 0x20, bit_800 = 0x800, message_off = 0x08;
}

class Sounds {
public:
    explicit Sounds(SoundDriver& driver);

    // One frame's sound update ($104AA0), after the flight and messages.
    void update(const Aircraft& aircraft);
    // A real second passed (`$119876`): the gear sound's and the beeps'
    // second counters.
    void second();

    void event(EngineSound e) { event_ = e; }
    void restart_after(int frames) { restart_in_ = frames; }
    void warn(std::uint32_t bits) { warnings_ |= bits; }
    void clear_warnings() { warnings_ = 0; } // ($10D8BC, a new start)
    // The sound update runs ($13AA85: off while the crash's end screen
    // waits).
    void set_active(bool on) { active_ = on; }
    // $13AB79 above 5: the pilot blacking out hears the engine at half volume.
    void set_blackout(bool on) { blackout_ = on; }
    // The outside view ($13AA75, $13ABA2): the engine heard from outside.
    void set_view(bool outside, int position, int distance) {
        outside_ = outside;
        view_position_ = position;
        view_distance_ = distance;
    }
    // The gear key: its sound starts at the next update ($10E98E).
    void gear_moved();

    // The game's routines that start sounds.
    void stop_all();                               // $1015F6
    void noise(int volume, int ticks);             // $10A0DC: the crash
    void tyres(int volume);                        // $10A18C
    // $107480: a round of the gun - slot 10's sound ($10A2F0: its script's
    // two values, 22 in the cockpit, 30 outside) when the game has it
    // (`$13AE48` bit 5), else the noise as for flares and chaff.
    void gun(bool outside);
    void beep(int period1, int volume1, int ticks1, int period2, int volume2, int ticks2, int repeats,
              int groups, int pause);              // $10A042
    void click();                                  // $127D66: a typed character
    void refuse();                                 // $127D82: a key the game refuses
    void warn_beep(int groups);                    // $127D6A: a warning's own beep
    void menu_key();                               // $127D8A: a menu choice
    // $109CE6: the title tune from its start (all channels stopped first);
    // $10201E: the tune stopped (channels 0 and 1).
    void start_tune();
    void stop_tune();
    bool has_tune() const { return driver_.has_tune(); }
    // $13A958: the volume the tune glides to (0-63).
    void set_tune_target(int volume) { driver_.set_master_target(volume); }
    // $10201E: the sound under the free-flight start map: the wind at
    // `volume`, or the outside engine at period $300 without the wind.
    void ambience(int volume);

    // Research: the counters as a recording of the original has them
    // ($13AAB1, $13AA87, $13AB75, $13AB7A).
    void set_counters(int frame, int restart_in, int gear_seconds, int beep_lock) {
        frame_ = std::uint8_t(frame);
        restart_in_ = restart_in;
        gear_seconds_ = std::uint8_t(gear_seconds);
        beep_lock_ = std::int8_t(beep_lock);
    }

    // Research: the calls of the original's sound routines this would
    // make, as `flightcap.py --calls` logs them (routine, arguments).
    std::vector<std::vector<std::int32_t>>* log{};

private:
    void engine_cockpit(int period, int volume);   // $109DB2
    void engine_outside(int period, int volume);   // $109E46
    void engine_glide(int period, int volume, int ticks); // $109EBE
    void glide(int period, int volume, int ticks); // $109EFA
    void wind(int volume);                         // $109F9A
    void gear(int volume);                         // $10A1E6
    void stop_gear();                              // $10A24C
    void thump(int volume);                        // $10A258
    int engine_volume(const Aircraft&, int throttle) const; // $10530E
    int wind_volume(const Aircraft&) const;        // $105502
    void record(std::vector<std::int32_t> call) {
        if (log) log->push_back(std::move(call));
    }

    SoundDriver& driver_;
    bool cockpit_engine_, outside_engine_, tyres_, wind_, gear_, noise_, thump_, gun_;
    EngineSound event_ = EngineSound::none;
    int restart_in_ = 0;          // $13AA87: frames to the engine's restart
    std::uint8_t frame_ = 0;      // $13AAB1
    std::uint8_t gear_seconds_ = 0; // $13AB75: bit 7 starts the sound, 1 stops it
    std::int8_t beep_lock_ = 0;   // $13AB7A: seconds without the warnings' beeps
    std::uint32_t warnings_ = 0;  // $13AE44
    bool blackout_ = false;
    bool outside_ = false;
    int view_position_ = 0, view_distance_ = -1;
    bool active_ = true;
public:
    template <class A> void persist(A& ar) { ar(cockpit_engine_, outside_engine_, tyres_, wind_, gear_, noise_, thump_, gun_, event_, restart_in_, frame_,
           gear_seconds_, beep_lock_, warnings_, blackout_, outside_, view_position_, view_distance_, active_); }
};

} // namespace interceptor::game
