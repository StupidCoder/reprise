#include <algorithm>
#include "stunt_car_racer_amiga/race/race.hpp"
#include "stunt_car_racer_amiga/data/cockpit.hpp"
#include "stunt_car_racer_amiga/race/physics.hpp"
#include "stunt_car_racer_amiga/race/placement.hpp"

namespace stuntcar::race {
namespace {

constexpr std::uint32_t car_table = 0x1FE6C;
constexpr int car_entry = 11;
// Which shape kinds a car may start on (bit 7 set: not; the curves), in the track bank.
constexpr std::uint32_t start_table = 0x1F0C2;
// The spawn: cell centres, the crane's starting height, and how far the
// car is set along its heading ($A0/256 of a matrix term << 6).
constexpr int cell_centre = 0x40;
constexpr std::int32_t hanging_height = 0x04000000, start_height = 0x00100000;
constexpr std::uint8_t spawn_shift = 0xA0;
constexpr std::int32_t spring_start = 0x1000;

// The clock: 0.13 s a frame; a race is four crossings of the line (three
// laps); the times start at 9 minutes so that any lap beats them.
constexpr std::uint8_t frame_time = 0x13, start_minutes = 9;
constexpr std::uint8_t random_seed = 0x3B;
constexpr std::uint8_t laps_to_finish = 4;
// A finished lap's time stays shown for $1B frames before the best lap.
constexpr std::uint8_t lap_time_hold = 0x1B;
// Frames the car lies on the ground after going over before the crane.
constexpr std::uint8_t crash_wait = 0x10;
// Slow-clock frames from the finish to the end of the race.
constexpr std::uint8_t finish_countdown = 0x2C;
// Messages by their place in the message table.
constexpr std::uint8_t wrecked_message = 0x00, race_won = 0x08, race_lost = 0x1C, laps_over = 0x54, press_fire = 0x3C;
// Damage: an impact of $1400 knocks a hole in the roll bar, and none other
// for $45 frames; the crack runs from screen column $28 along the roof (at
// most $F0 pixels); a wreck ends the race in $3C slow frames.
constexpr std::uint16_t hole_impact = 0x1400;
constexpr std::uint8_t hole_wait = 0x45, crack_end = 0xF0, wreck_countdown = 0x3C, wrecked_wheels = 0x92;
constexpr int crack_left = 0x28;

std::uint8_t add_bcd(std::uint8_t a, std::uint8_t b, bool& carry) {
    int low = (a & 0xF) + (b & 0xF) + (carry ? 1 : 0);
    int high = (a >> 4) + (b >> 4);
    if (low > 9) low -= 10, ++high;
    carry = high > 9;
    if (carry) high -= 10;
    return std::uint8_t(high << 4 | low);
}

// Adds hundredths (BCD) to a time: seconds wrap at 60 into the minutes,
// which stop at 9.
void add_time(Time& t, std::uint8_t hundredths) {
    bool carry = false;
    t.hundredths = add_bcd(t.hundredths, hundredths, carry);
    if (!carry) return;
    t.seconds = add_bcd(t.seconds, 0, carry);
    if (t.seconds < 0x60) return;
    t.seconds = 0;
    carry = false;
    const auto minutes = add_bcd(t.minutes, 1, carry);
    if (minutes < 0x0A) t.minutes = minutes;
}

// The spawn clears the body and its motion, the spring travels and the
// placement in the turned frame; everything else (springs' road contact
// and damage, the flags and clocks) carries over.
void clear_body(Car& c) {
    c.turned_x = c.turned_z = 0;
    c.height = 0;
    c.x = c.y = c.z = 0;
    c.pitch = c.yaw = c.roll = 0;
    c.vx = c.vy = c.vz = 0;
    c.spin_pitch = c.spin_yaw = c.spin_roll = 0;
    c.force = {};
    c.torque_pitch = c.torque_yaw = c.torque_roll = 0;
    c.contact_offsets = {};
    c.gravity = {};
    for (auto& s : c.springs) s.travel = s.previous_travel = s.force = 0;
    c.pitch_lift = c.roll_lift = 0;
    c.drive = 0;
    c.body_velocity = {};
    c.body_force = {};
    c.net_lift = 0;
    c.rate_pitch = c.rate_yaw = c.rate_roll = 0;
    c.loads = {};
    c.previous_forward_load = 0;
    c.impulse = {};
    c.heading = 0;
    c.speed = 0;
}

bool faster(const Time& a, const Time& b) {
    if (a.minutes != b.minutes) return a.minutes < b.minutes;
    if (a.seconds != b.seconds) return a.seconds < b.seconds;
    return a.hundredths < b.hundredths;
}

} // namespace

int decimal(std::uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0xF); }

CarSpec car_spec(const data::Program& program, bool super_league) {
    const auto at = car_table + std::uint32_t(super_league ? car_entry : 0);
    return {program.u8(at + 2), program.u8(at + 3), program.u8(at + 6), program.u8(at + 9)};
}

Race::Race(const data::Program& program, const data::Circuits& circuits, int track, bool super_league,
           std::optional<Driver> opponent, Random random, int roll_bar_marks)
    : track_(circuits.tracks.at(std::size_t(track))), tables_(load_tables(program)),
      contact_(circuits, track_) {
    state_.random = random;
    state_.intact_marks = std::uint8_t(roll_bar_marks);
    // Loading the track rolls the opponent's skill; the race set-up then
    // seeds one byte of the generator.
    if (opponent) {
        driver_.emplace(program, track_, contact_, *opponent, super_league);
        driver_->prepare(track, state_.random);
    }
    state_.random.bytes[3] = random_seed;
    for (std::size_t k = 0; k < start_allowed_.size(); ++k) start_allowed_[k] = program.u8(start_table + std::uint32_t(k));
    const auto spec = car_spec(program, super_league);
    car_.accelerate_low = spec.accelerate_low;
    car_.accelerate_high = spec.accelerate_high;
    car_.boost_reload = spec.boost_reload;
    car_.damage_tolerance = spec.damage_tolerance;
    car_.damage_window = track_.trailer[super_league ? 1 : 0];
    // The boosts the track allows (trailer byte 2, 3 for the super league's
    // car), counted in BCD.
    const int boosts = track_.trailer[super_league ? 3 : 2];
    car_.boost_left = car_.boost_allowance = std::uint8_t((boosts / 10) << 4 | (boosts % 10));
    car_.track_number = std::uint8_t(track);
    const int count = int(track_.segments.size());
    lap_segment_ = (track_.start_line_segment + 1) % count;
    halfway_segment_ = track_.halfway_segment;
    state_.spawn_segment = track_.car_start_segment;
    state_.laps_to_finish = laps_to_finish;
    for (auto& c : state_.cars) c.lap_time.minutes = start_minutes;
    state_.best_lap.minutes = start_minutes;
    state_.league = car_.league = bool(driver_);
    if (driver_) driver_->start(state_.opponent, state_.random);
    // Which of the roof's pixels are painted (any colour but 0): the crack
    // only shows on them.
    const auto art = data::load_cockpit_art(program);
    for (std::size_t y = 0; y < roof_.size(); ++y)
        for (std::size_t x = 0; x < roof_[y].size(); ++x) roof_[y][x] = art.picture[y * std::size_t(art.width) + x] != 0;
}

void Race::spawn(int segment) {
    const int count = int(track_.segments.size());
    // Back up to a piece the car may start on.
    for (;;) {
        const auto& s = track_.segments[std::size_t(segment)];
        bool barred = std::int8_t(start_allowed_[std::size_t(s.shape)]) < 0;
        for (const auto listed : track_.segment_list) barred = barred || listed == segment;
        if (!barred) break;
        segment = (segment + count - 1) % count;
    }
    state_.cars[0].segment = segment;
    const auto& piece = track_.segments[std::size_t(segment)];
    const bool relaunch = car_.launched;
    clear_body(car_);
    car_.segment = segment;
    car_.recovery = 0xF0;
    car_.x = std::int32_t((piece.cell_x * 128 + cell_centre) << 16);
    car_.z = std::int32_t((piece.cell_z * 128 + cell_centre) << 16);
    car_.y = hanging_height;
    const int turn = (piece.shape == 4 || piece.shape == 10) ? 0x20 : 0; // diagonals: 45 degrees more
    car_.yaw = std::int16_t(std::uint8_t(((piece.heading << 6) ^ (piece.reversed ? 0x80 : 0)) + turn) << 8);
    place(car_, tables_, contact_);
    physics();
    car_.y = start_height;
    const auto rear_surface = car_.springs[2].surface;
    if (relaunch) {
        car_.y = (rear_surface << 9) + 0x180000;
        car_.recovery = 0xE6;
    }
    car_.ride_height = std::int16_t(rear_surface >> 2);
    // Set the car a little along its heading (back when facing back).
    for (int axis = 0; axis < 2; ++axis) {
        auto d = car_.matrix[std::size_t(axis == 0 ? 2 : 3)];
        if (axis == 0) d = std::int16_t(-d);
        int factor = spawn_shift;
        if (car_.facing_back) factor = -factor;
        factor = std::int16_t(factor << 7);
        const auto shift = std::int32_t(std::int16_t(((std::int32_t(d) * factor) << 1) >> 16)) << 6;
        (axis == 0 ? car_.z : car_.x) += shift;
    }
    for (auto& s : car_.springs) s.surface = spring_start, s.contact = spring_start;
    car_.springs[0].previous_travel = car_.springs[1].previous_travel = car_.springs[2].previous_travel = 0;
    place(car_, tables_, contact_);
}

void Race::physics() {
    step_physics(car_, tables_, contact_, state_.random);
    if (driver_) OpponentDriver::take_knock(state_.opponent, car_);
    car_.knock_taken = {};
}

void Race::place_car() {
    place(car_, tables_, contact_);
    if (car_.crash_started) {
        // Over the edge: the crane comes after a while; the dust settles.
        car_.crash_started = false;
        state_.crash_wait = crash_wait;
        state_.smoke.clear();
    }
    const bool on_grid = !(car_.crash_state & 0x40);
    if (on_grid) {
        state_.cars[0].segment = car_.segment;
        state_.last_segment = car_.segment;
    }
    // The opponent's frame (off the grid it only rides and is placed).
    if (driver_) {
        auto& o = state_.opponent;
        driver_->drive(o, car_, state_.random);
        if (on_grid) {
            driver_->measure(o, car_);
            driver_->steer(o, car_);
        }
        driver_->place(o);
        state_.cars[1].segment = o.segment;
    }
    // The drawing pass: dust and smoke after the view; the wheels.
    state_.smoke.step(car_, state_.random, state_.frame_clock);
    front_wheels();
}

// The front wheels' rows ($5E778): each hangs by its spring's travel
// (-$100..$700 taken, an eighth of it read as a quarter-wave angle, the
// top five bits of the wave lifting the wheel), from the rest row; a wheel
// over the road's edge (the rumble bits, one per wheel; the frame's end
// shifts them on) jumps to its lowest row. Rows $97-$B9 less $32.
void Race::front_wheels() {
    for (std::size_t w = 0; w < 2; ++w) { // left (spring 0), right (spring 1)
        const int travel = std::clamp(car_.springs[w].travel + 0x100, 0, 0x7FF);
        const auto angle = std::uint8_t(~(travel >> 3));
        const int lift = tables_.quarter_wave[angle] >> 11;
        auto row = std::uint8_t(state_.wheel_base - 1 - lift);
        const bool rumble = car_.edge_side & (0x80 >> w);
        if (rumble || row >= 0xBA) row = 0xB9;
        if (row < 0x97) row = 0x97;
        state_.wheel_rows[w] = row - 0x32;
    }
}

void WheelSpin::vertical_blank(const Car& car) {
    const int sum = phase + (std::uint16_t(car.engine_note) >> 8);
    phase = std::uint8_t(sum);
    if (sum < 0x100) return;
    frame = car.body_velocity[2] < 0 ? (frame + 2) % 3 : (frame + 1) % 3;
}

void Race::step(const Controls& controls) {
    auto& random = state_.random;
    switch (state_.phase) {
    case Phase::spawn:
        // Lowered by the crane: placed, the clock runs, no physics yet.
        spawn(state_.spawn_segment);
        state_.smoke.clear();
        place_car();
        clock();
        --state_.frame_clock;
        after_frame();
        state_.phase = Phase::settle;
        return;
    case Phase::settle:
        physics();
        place_car();
        clock();
        --state_.frame_clock;
        after_frame();
        car_.armed = true;
        random.next(); // the engine sound starts
        --state_.frame_clock;
        state_.phase = Phase::drive;
        return;
    case Phase::drive:
        break;
    case Phase::over:
        return;
    }
    random.next(); // the joystick routine stirs the generator
    read_controls(car_, controls);
    physics();
    random.next(); // and the engine sound
    place_car();
    damage();
    clock();
    // The dashboard's distance, every fourth frame ($5E12A): 5/16 of the
    // gap, signed by who is ahead.
    if (!(state_.frame_clock & 3)) {
        const auto d = state_.opponent.distance;
        const int shown = (d + (d >> 2)) >> 2;
        state_.distance_shown = std::int8_t(car_.opponent_flags) < 0 ? -shown : shown;
    }
    // The race ends once the countdown has run out, on a frame drawn into
    // the second buffer, with no lap time on show and the car down or held.
    if (state_.race_over && state_.odd_frame && !state_.lap_time_hold && (car_.recovery || car_.on_ground)) {
        if (!state_.finished) state_.right_lamp = false;
        if (state_.hole_timer == hole_wait) state_.cracking = false;
        show_message(press_fire, 4);
        state_.phase = Phase::over;
        after_frame();
        return;
    }
    after_frame();
    // Over the edge: once the car has lain on the ground for a while, the
    // crane lifts it back onto the segment where it left the road. A car
    // below the road keeps the narrow angle limits.
    if (!car_.recovery && std::int8_t(car_.crash_state) < 0 && car_.on_ground) {
        const auto level = std::int8_t(std::uint16_t(car_.height) >> 8);
        if (level < 2) car_.limit_select = car_.crash_state;
        if (std::int8_t(--state_.crash_wait) < 0) {
            state_.crash_wait = 0;
            if (!state_.finish_countdown) {
                state_.spawn_segment = state_.last_segment;
                state_.phase = Phase::spawn;
                return;
            }
        }
    }
    --state_.frame_clock;
}

// Knocks and damage ($5DFB4), after the frame's physics.
void Race::damage() {
    if (car_.damaged) {
        const auto& s = car_.springs;
        const int pair = (s[0].damage + s[1].damage) >> 1;
        state_.crack.target = std::uint8_t((pair + s[2].damage) >> 1);
        grow_crack();
    }
    if (state_.hole_timer) {
        // The frame after a hole is knocked, its crack becomes a hole.
        if (state_.hole_timer-- == hole_wait) state_.cracking = false;
        else if (!car_.damaged) return;
    } else {
        if (!car_.damaged) return;
        if (std::uint16_t(car_.peak_impact) >= hole_impact && state_.intact_marks) {
            --state_.intact_marks;
            state_.cracking = true;
            state_.hole_timer = hole_wait;
        }
    }
    // (The knock's sound, louder with the impact, is not played yet.)
    car_.damaged = false;
}

// The crack grows a pixel at a time towards the springs' damage, wandering
// up and down at random every second pixel. Where the roof has no paint
// it cannot be drawn and the damage grows instead; past the roof's end the
// car is wrecked.
void Race::grow_crack() {
    auto& crack = state_.crack;
    while (crack.target > crack.length) {
        ++crack.length;
        int row = crack.row;
        if (!(crack.length & 1)) {
            auto r = state_.random.next();
            if (r & 1) {
                r >>= 1;
                const bool up = r & 1;
                r >>= 1;
                if (up) {
                    if (row < 5) ++row;
                    else if (!(r & 1)) {
                        if (row >= 3) --row;
                        else if (!((r >> 1) & 1)) ++row;
                    }
                } else if (row >= 3) {
                    --row;
                } else if (!(r & 1)) {
                    ++row;
                }
            }
        }
        crack.colour = row > crack.row ? 0x0C : 0x0B;
        crack.row = std::uint8_t(row);
        if (crack.length >= crack_end) {
            --crack.length;
            wreck();
            return;
        }
        const int x = crack_left + crack.length;
        if (roof_[std::size_t(row)].test(std::size_t(x))) {
            crack.pixels.push_back({x, row, 0});
            crack.pixels.push_back({x, row - 1, 0});
            crack.pixels.push_back({x, row - 2, crack.colour});
            continue;
        }
        if (!++crack.target) {
            wreck();
            return;
        }
        for (auto& spring : car_.springs) spring.damage = crack.target;
    }
}

// Wrecked ($5E848): the car sags and stalls, and the race ends.
void Race::wreck() {
    if (state_.finish_countdown) return;
    state_.wrecked = true;
    car_.stalled = true;
    car_.rest_length = (car_.rest_length & ~0xFF00) | 0x0200;
    state_.wheel_base = wrecked_wheels;
    state_.finish_countdown = wreck_countdown;
    show_message(wrecked_message, 2);
}

// The display buffers swap.
void Race::after_frame() { state_.odd_frame = !state_.odd_frame; }

// The race clock ($5DB34): lap times, line crossings, the finish.
void Race::clock() {
    end_frame(car_);
    if (state_.lap_time_hold && !--state_.lap_time_hold) show_best_lap();
    if (state_.league) {
        add_time(state_.cars[1].lap_time, frame_time);
        count_laps(1);
    }
    add_time(state_.cars[0].lap_time, frame_time);
    count_laps(0);
    check_finish();
    // Below the road, the countdown runs twice as fast.
    if (std::int8_t(car_.limit_select) < 0) count_down_finish();
    count_down_finish();
}

void Race::count_laps(int car) {
    auto& c = state_.cars[std::size_t(car)];
    if (car == 0 && (car_.crash_state & 0x40)) return;
    if (c.on_lap) {
        if (c.segment == halfway_segment_) c.on_lap = false;
        return;
    }
    if (c.segment != lap_segment_) return;
    c.on_lap = true;
    ++c.laps;
    if (c.laps != 1) {
        // The part of the frame before the line: the time to cover the rest
        // of the step the car is in (13 hundredths for a whole step).
        const auto progress = car == 0 ? car_.progress : std::int16_t(state_.opponent.progress);
        if (!(std::uint16_t(progress) >> 8)) {
            int part = (std::uint8_t(~progress) * 0x0D) >> 8;
            if (part >= 0x0A) part += 6;
            add_time(c.lap_time, std::uint8_t(part));
        }
        if (car == 0) {
            state_.lap_time_hold = lap_time_hold;
            state_.last_lap = c.lap_time;
        }
        if (faster(c.lap_time, state_.best_lap)) {
            state_.best_lap = c.lap_time;
            state_.best_by_opponent = car == 1;
            if (state_.best_by_opponent) {
                state_.lap_time_hold = 0;
                show_best_lap();
            }
        }
    }
    c.lap_time = {};
}

// Who is ahead: laps first, then how far round the lap.
void Race::check_finish() {
    for (int car = 1; car >= 0; --car) {
        if (state_.finished || state_.cars[std::size_t(car)].laps != state_.laps_to_finish) continue;
        state_.finished = state_.laps_to_finish;
        if (!state_.finish_countdown) state_.finish_countdown = finish_countdown;
        const int count = int(track_.segments.size());
        const auto round = [&](int segment) { return (segment - lap_segment_ + count) % count; };
        int ahead = state_.cars[1].laps - state_.cars[0].laps;
        if (!ahead) ahead = round(state_.cars[1].segment) - round(state_.cars[0].segment);
        if (!ahead) ahead = state_.opponent.gap;
        const bool won = ahead < 0;
        state_.flag_lamp = won;
        show_message(won ? (state_.league ? race_won : laps_over) : race_lost, 4);
    }
}

// The best lap goes up on the dashboard; the lamp right of the wheel says
// whether it is the player's.
void Race::show_best_lap() {
    state_.best_shown = true;
    state_.right_lamp = !state_.best_by_opponent;
}

void Race::count_down_finish() {
    auto& n = state_.finish_countdown;
    if (!n) return;
    if (std::int8_t(n) < 0) {
        state_.race_over = true;
        return;
    }
    state_.flashing = (n >> 2) & 1;
    if (!car_.recovery && !car_.on_ground && n < 6) return;
    if (std::int8_t(car_.tick) < 0) return;
    if (--n) return;
    state_.race_over = true;
    n = 0x80;
}

void Race::show_message(std::uint8_t start, std::uint8_t groups) {
    car_.launch_flag = true;
    car_.launch_message = start;
    car_.launch_message_length = groups;
}

} // namespace stuntcar::race
