#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/race/car.hpp"
#include "stunt_car_racer_amiga/race/contact.hpp"
#include "stunt_car_racer_amiga/race/random.hpp"
#include <array>
#include <vector>

// The computer's car in league races (game.md, "The opponent"). It does not
// run the player's physics: it rides along the track, a speed and a
// fraction of a step at a time, with a lane across the road, and only its
// three wheels bounce on a simple suspension over the road under them. Its
// speed follows a target per segment (set when the track is loaded, from
// the track's own figures and a skill rolled for the race); it changes lane
// to block or pass the player, hops now and then, and knocks the player's
// car when they touch.
namespace stuntcar::race {

// Who drives: the opponent's number in the league's table of drivers, which
// sets how hard it pushes over bumps and how it blocks ($1ED8A).
struct Driver {
    std::uint8_t number{};
    std::uint8_t habits{}; // bit 1 follows the player's lane up close, 2 hops at
                           // random, 3 keeps its own lane, 5 passes from further back
};

struct Opponent {
    // Along the track: the segment and the step within it (high byte) with
    // its fraction, in driving direction; the fraction's carry; the speed
    // (high byte: steps per frame × 256 / the piece's along gain).
    int segment{};
    std::uint16_t progress{};
    std::uint8_t progress_carry{};
    std::uint16_t speed{};
    std::uint16_t acceleration{};     // this frame's push (the car table's)
    std::uint8_t target_speed{};
    bool braking{};
    // Across the road: 0 at the left edge, 256 at the right; where it is
    // heading; how far the player's lane is and on which side.
    std::uint16_t lane = 0x4C;
    std::uint8_t lane_target{};
    std::uint8_t lane_gap{}, lane_side{};
    // Bumps: a timer while it bucks over a rough segment, the phase in the
    // wobble table, the wobble added to the rear wheels and to the lane.
    std::uint8_t bump_timer{}, bump_phase{};
    std::array<std::uint8_t, 2> wobble{};
    std::uint8_t lane_wobble{};
    std::uint8_t bump_side{};
    // The suspension: body heights over the left rear, right rear and front
    // wheels (and a fourth set at the start), the road under each, the
    // spring compressions and their last values, the wheels' vertical
    // speeds and forces; on the ground while any spring is compressed.
    std::array<std::int16_t, 4> body{};
    std::array<std::int16_t, 3> road{};
    std::array<std::int16_t, 3> compression{}, previous{}, wheel_speed{}, force{};
    std::uint8_t on_ground{};
    std::int16_t deepest{};           // the largest road - body this frame
    std::uint8_t half_track{};        // its wheels either side of the lane
    // The rear axle's place (the car is drawn from it) and the corner
    // heights the drawing uses (rear left, rear right, front left, front right).
    int rear_segment{};
    std::uint16_t rear_progress{};
    std::array<std::int16_t, 4> lift{};
    bool rough{};                     // the road under it steps by $14 or more
    // Touching the player: frames the vertical knock may still repeat, and
    // whether a knock has been dealt since they last separated.
    std::uint8_t touch_frames{};
    bool knocking{};
    // Between the cars along the track, in 1/32 steps: signed (opponent
    // ahead positive) and the shorter way round.
    std::int16_t gap{};
    std::uint16_t distance{};
};

// The race's opponent and what it is given at the start.
class OpponentDriver {
  public:
    OpponentDriver(const data::Program&, const data::Track&, const TrackContact&, Driver, bool super_league);

    // Track load ($63A8E, $5B1E0): the skill (the fastest it will go, and
    // its speed on curves), rolled from the generator, and the target speed
    // of every segment.
    void prepare(int track_number, Random&);
    // The race start ($5D3C6, $63A58): on the start segment, four steps on,
    // its body a random height above the road.
    void start(Opponent&, Random&) const;

    // A frame's work after the player's placement ($63996, $6076C, $636C0,
    // $5A186): ride on, measure the gap, steer (and knock), place.
    void drive(Opponent&, Car& player, Random&) const;
    void measure(Opponent&, Car& player) const;
    void steer(Opponent&, Car& player) const;
    void place(Opponent&) const;

    // The knock's share for the opponent, taken from the player's frame.
    static void take_knock(Opponent&, const Car& player);

    std::uint8_t top_speed() const { return top_speed_; }
    std::uint8_t curve_speed() const { return curve_speed_; }
    const std::vector<std::uint8_t>& segment_speeds() const { return segment_speeds_; }
    // Research tools set what the original rolled.
    void set_skill(std::uint8_t top, std::uint8_t curve, std::vector<std::uint8_t> speeds) {
        top_speed_ = top, curve_speed_ = curve, segment_speeds_ = std::move(speeds);
    }

    int steps(int segment) const;                // cross-sections less one
    int distance_to(int segment) const { return start_distance_[std::size_t(segment)]; }

  private:
    void suspension(Opponent&, Random&) const;
    void bumps(Opponent&, const Car& player, Random&) const;
    void push(Opponent&) const;
    void aim_speed(Opponent&) const;
    void change_speed(Opponent&, const Car& player) const;
    void touch(Opponent&, Car& player) const;
    bool curve(int segment) const;

    const data::Track& track_;
    const TrackContact& contact_;
    Driver driver_;
    bool super_league_{};
    std::array<std::uint8_t, 0x40> skills_{};   // $1FE2C: masks and bases by track
    std::array<std::uint8_t, 16> start_allowed_{};
    std::array<std::uint8_t, 0x1000> half_tracks_{}; // $1EF4A by body roll
    std::array<std::uint8_t, 16> wobbles_{};     // $5BE34
    std::uint16_t push_{};                        // the car table's opponent push
    std::uint8_t top_speed_{}, curve_speed_{};
    std::vector<std::uint8_t> segment_speeds_;   // bit 7: rough (no bumps; double push)
    std::vector<int> start_distance_;            // 32 × the steps before each segment
    int length_{};
};

} // namespace stuntcar::race
