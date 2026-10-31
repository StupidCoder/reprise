#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/race/car.hpp"
#include "stunt_car_racer_amiga/race/contact.hpp"
#include "stunt_car_racer_amiga/race/opponent.hpp"
#include "stunt_car_racer_amiga/race/random.hpp"
#include "stunt_car_racer_amiga/race/smoke.hpp"
#include "stunt_car_racer_amiga/race/tables.hpp"
#include <array>
#include <bitset>
#include <optional>
#include <vector>

// A race in progress (game.md, "The race"): the player's car lowered onto the
// circuit by the crane, then driven frame by frame as the game's race loop
// does it: controls, physics, placement, clock, laps; over the edge, the
// crane lifts the car back where it left the road.
namespace stuntcar::race {

// The figures of the player's car (a table of two cars at $1FE6C: the
// standard one and the super league's).
struct CarSpec {
    std::uint8_t accelerate_low{}, accelerate_high{};
    std::uint8_t boost_reload{};
    std::uint8_t damage_tolerance{};
};
CarSpec car_spec(const data::Program&, bool super_league);

// A time as the game keeps it: binary-coded decimal minutes, seconds and
// hundredths (the clock adds 0.13 s a frame).
struct Time {
    std::uint8_t minutes{}, seconds{}, hundredths{};
    bool operator==(const Time&) const = default;
};
int decimal(std::uint8_t bcd);

// One car's laps: the start line is crossed when the car reaches the segment
// after it, and only counts again once the car has been round to the
// track's halfway segment.
struct Lapping {
    int segment{};
    bool on_lap{};         // crossed the line, the halfway segment still ahead
    std::uint8_t laps{};   // crossings; the first starts lap 1
    Time lap_time{};
};

// What happens in the frame the race loop runs next.
enum class Phase : std::uint8_t {
    spawn,  // the crane lowers the car (at the start, or where it went over)
    settle, // one physics frame before the controls are read
    drive,
    over,   // the race has ended: PRESS FIRE
};

// Damage (game.md, "Damage"): hard knocks (an impact of $1400 or more)
// knock holes in the roll bar's ten marks; the springs' damage draws a crack
// along the roof from the left, and a crack that reaches the right end
// wrecks the car.
struct Crack {
    std::uint8_t length{};       // pixels drawn, from column $28
    std::uint8_t target{};       // the springs' average damage
    std::uint8_t row = 4;        // 2-5, wandering at random
    std::uint8_t colour = 0x0B;  // of the upper pixel: $C where the crack rose
    struct Pixel {
        int x{}, y{};
        std::uint8_t colour{};
    };
    std::vector<Pixel> pixels;   // stay on the screen
};

struct RaceState {
    Phase phase = Phase::spawn;
    int spawn_segment{};
    std::array<Lapping, 2> cars{}; // the player, the opponent
    Time best_lap{};
    bool best_by_opponent{};
    Time last_lap{};                   // the player's lap just finished
    bool best_shown{};                 // the best lap is on the dashboard
    std::uint8_t laps_to_finish{};
    std::uint8_t lap_time_hold{};      // frames a finished lap's time stays shown
    std::uint8_t finished{};           // the laps of the car that finished first
    std::uint8_t finish_countdown{};   // slow-clock frames until the race is over
    bool race_over{};
    bool odd_frame{};                  // the display buffer the frame draws into
    std::uint8_t crash_wait{};         // frames on the ground after going over
    int last_segment{};                // the segment the crane returns the car to
    bool flag_lamp{};                  // the player won
    bool right_lamp{};                 // the player holds the best lap
    bool flashing{};                   // the message flashes white as the race ends
    std::uint8_t frame_clock{};        // counts down once a frame
    std::uint8_t intact_marks = 10;
    std::uint8_t hole_timer{};         // frames after a hole before another
    bool cracking{};                   // the newest hole is still a crack
    Crack crack;
    bool wrecked{};
    std::uint8_t wheel_base = 0xBA;    // the front wheels' rest row; lower when wrecked
    std::array<int, 2> wheel_rows{0x7A, 0x7A}; // the front wheels' top rows on screen (left, right)
    Random random;
    Smoke smoke;
    bool league{};                     // against an opponent
    int distance_shown{};              // the dashboard's gap to the opponent (negative: behind)
    Opponent opponent;
};

// The front wheels turning ($651B8, each 50 Hz vertical blank, apart from
// the race's own frames): the engine note's high byte is added to a byte;
// each time it carries, the wheel picture (one of three) moves on, or
// back while the car rolls backwards ($5E744).
struct WheelSpin {
    std::uint8_t phase{};
    int frame = 1;
    void vertical_blank(const Car&);
};

class Race {
  public:
    // A practice, or a league race against `opponent`.
    Race(const data::Program&, const data::Circuits&, int track, bool super_league = false,
         std::optional<Driver> opponent = std::nullopt);
    Race(const Race&) = delete;
    Race& operator=(const Race&) = delete;

    // One frame of the race loop. The first lowers the car from the crane.
    void step(const Controls&);

    const Car& player() const { return car_; }
    const RaceState& state() const { return state_; }
    const data::Track& track() const { return track_; }
    const Tables& tables() const { return tables_; }
    const OpponentDriver* opponent_driver() const { return driver_ ? &*driver_ : nullptr; }

    // Research tools load a captured frame into the race.
    Car& player_for_tools() { return car_; }
    RaceState& state_for_tools() { return state_; }
    OpponentDriver* opponent_driver_for_tools() { return driver_ ? &*driver_ : nullptr; }

  private:
    void spawn(int segment);
    void clock();
    void count_laps(int car);
    void check_finish();
    void count_down_finish();
    void show_best_lap();
    void show_message(std::uint8_t start, std::uint8_t groups);
    void after_frame();
    void damage();
    void grow_crack();
    void wreck();
    void place_car();
    void front_wheels();
    void physics();

    const data::Track& track_;
    Tables tables_;
    TrackContact contact_;
    Car car_;
    RaceState state_;
    std::array<std::uint8_t, 16> start_allowed_{}; // by shape kind
    std::array<std::bitset<320>, 8> roof_{};        // the cockpit picture's top rows: drawn pixels
    int lap_segment_{}, halfway_segment_{};
    std::optional<OpponentDriver> driver_;
};

} // namespace stuntcar::race
