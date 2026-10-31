// Stunt Car Racer research tool: lockstep check of the car physics against
// frames captured from the original (.analysis harness/physcap.py): each
// frame's state before the physics tick is loaded into a race::Car, one
// physics frame is run, and every field is compared with the state after.
//
//   stunt_car_racer_physcheck <content> <capture.bin> <track> [physics|place] [first] [count]
//       place: the capture was taken around the placement (PLACE=1)
//       drive: start from the first frame and run the race loop on its own
//              (controls from the capture, physics, placement, clock),
//              comparing every frame with the original's
//   stunt_car_racer_physcheck <content> <race.bin> <track> race [first] [count]
//   stunt_car_racer_physcheck <content> <samples.bin> <track> samples <inputs>
//       a sampled whole race (harness/racesample.py) with its input list
//       a capture of the whole race loop (harness/racecap.py, from the
//       race's first frame): race::Race runs on its own with the captured
//       controls and every frame start is compared, car and race state
//
// The game-variable addresses below are research knowledge (game.md, "Car
// physics"); the game code itself never sees them.

#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/race/contact.hpp"
#include "stunt_car_racer_amiga/race/physics.hpp"
#include "stunt_car_racer_amiga/race/placement.hpp"
#include "stunt_car_racer_amiga/race/race.hpp"
#include "platform/files.hpp"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Window {
    std::uint32_t address, length;
};
const Window windows[] = {{0x1B800, 0x1400}, {0x5C6B8, 0x10}, {0x61AD4, 0x8}, {0x6125A, 0x8}, {0x620B6, 0x2},
                          {0x63CE2, 0x2},    {0x63EDE, 0x4},  {0x64AEC, 0x4}, {0x57C3C, 0x2}};

const Window race_windows[] = {{0x1B800, 0x1400}, {0x5C6B8, 0x10}, {0x61AD4, 0x8}, {0x6125A, 0x8}, {0x620B6, 0x2},
                               {0x63CE2, 0x2},    {0x63EDE, 0x4},  {0x64AEC, 0x4}, {0x57C3C, 0x2}, {0x5E65A, 0x2},
                               {0x625A2, 0x6},    {0x662AA, 0x2},  {0x63CE0, 0x2}};
// Captures taken before the opponent's skill was recorded lack the last window.
bool old_race_capture = false;

struct Ram {
    const std::uint8_t* bytes;
    bool race = false;
    std::uint8_t u8(std::uint32_t a) const {
        std::size_t at = 0;
        const Window* first = race ? std::begin(race_windows) : std::begin(windows);
        const Window* last = race ? std::end(race_windows) - (old_race_capture ? 1 : 0) : std::end(windows);
        for (const auto* w = first; w != last; ++w) {
            if (a >= w->address && a < w->address + w->length) return bytes[at + a - w->address];
            at += w->length;
        }
        throw std::runtime_error("address outside the capture: " + std::to_string(a));
    }
    std::int16_t w(std::uint32_t a) const { return std::int16_t(u8(a) << 8 | u8(a + 1)); }
    std::int32_t l(std::uint32_t a) const { return std::int32_t(std::uint32_t(std::uint16_t(w(a))) << 16 | std::uint16_t(w(a + 2))); }
};

using stuntcar::race::Car;

// Each field: name, read from RAM into the car, and the car's value as an int.
struct Field {
    std::string name;
    std::function<void(Car&, const Ram&)> load;
    std::function<long(const Car&)> value;
    std::function<long(const Ram&)> ram;
};

std::vector<Field> fields() {
    std::vector<Field> f;
    const auto word = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({n, [=](Car& c, const Ram& r) { c.*member = r.w(a); }, [=](const Car& c) { return long(c.*member); },
                     [=](const Ram& r) { return long(r.w(a)); }});
    };
    const auto lng = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({n, [=](Car& c, const Ram& r) { c.*member = r.l(a); }, [=](const Car& c) { return long(c.*member); },
                     [=](const Ram& r) { return long(r.l(a)); }});
    };
    const auto byte = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({n, [=](Car& c, const Ram& r) { c.*member = r.u8(a); }, [=](const Car& c) { return long(c.*member); },
                     [=](const Ram& r) { return long(r.u8(a)); }});
    };
    const auto flag = [&](std::string n, std::uint32_t a, auto member, std::uint8_t mask) {
        f.push_back({n, [=](Car& c, const Ram& r) { c.*member = (r.u8(a) & mask) != 0; },
                     [=](const Car& c) { return long(c.*member); }, [=](const Ram& r) { return long((r.u8(a) & mask) != 0); }});
    };
    const auto custom = [&](std::string n, std::function<void(Car&, const Ram&)> load, std::function<long(const Car&)> value,
                            std::function<long(const Ram&)> ram) { f.push_back({n, load, value, ram}); };
    const auto array_word = [&](std::string n, std::uint32_t a, auto member, int count, int stride) {
        for (int i = 0; i < count; ++i)
            custom(n + std::to_string(i), [=](Car& c, const Ram& r) { (c.*member)[std::size_t(i)] = r.w(a + std::uint32_t(i * stride)); },
                   [=](const Car& c) { return long((c.*member)[std::size_t(i)]); },
                   [=](const Ram& r) { return long(r.w(a + std::uint32_t(i * stride))); });
    };
    lng("x", 0x1BCD8, &Car::x); lng("y", 0x1BCDC, &Car::y); lng("z", 0x1BCE0, &Car::z);
    word("pitch", 0x1BCE4, &Car::pitch); word("yaw", 0x1BCE6, &Car::yaw); word("roll", 0x1BCE8, &Car::roll);
    word("vx", 0x1BCEA, &Car::vx); word("vy", 0x1BCEC, &Car::vy); word("vz", 0x1BCEE, &Car::vz);
    word("spin_pitch", 0x1BCF0, &Car::spin_pitch); word("spin_yaw", 0x1BCF2, &Car::spin_yaw); word("spin_roll", 0x1BCF4, &Car::spin_roll);
    array_word("force", 0x1BCF6, &Car::force, 3, 2);
    word("torque_pitch", 0x1BCFC, &Car::torque_pitch); word("torque_yaw", 0x1BCFE, &Car::torque_yaw); word("torque_roll", 0x1BD00, &Car::torque_roll);
    array_word("contact_offsets", 0x1BD02, &Car::contact_offsets, 6, 2);
    array_word("gravity", 0x1BD0E, &Car::gravity, 3, 2);
    for (int i = 0; i < 3; ++i) {
        const auto s = std::size_t(i);
        const auto o = std::uint32_t(i);
        custom("spring" + std::to_string(i) + ".surface", [=](Car& c, const Ram& r) { c.springs[s].surface = r.l(0x1BCA4 + o * 4); },
               [=](const Car& c) { return long(c.springs[s].surface); }, [=](const Ram& r) { return long(r.l(0x1BCA4 + o * 4)); });
        custom("spring" + std::to_string(i) + ".contact", [=](Car& c, const Ram& r) { c.springs[s].contact = r.l(0x1BC94 + o * 4); },
               [=](const Car& c) { return long(c.springs[s].contact); }, [=](const Ram& r) { return long(r.l(0x1BC94 + o * 4)); });
        custom("spring" + std::to_string(i) + ".compression", [=](Car& c, const Ram& r) { c.springs[s].compression = r.l(0x1BCB0 + o * 4); },
               [=](const Car& c) { return long(c.springs[s].compression); }, [=](const Ram& r) { return long(r.l(0x1BCB0 + o * 4)); });
        custom("spring" + std::to_string(i) + ".travel", [=](Car& c, const Ram& r) { c.springs[s].travel = r.w(0x1BD14 + o * 2); },
               [=](const Car& c) { return long(c.springs[s].travel); }, [=](const Ram& r) { return long(r.w(0x1BD14 + o * 2)); });
        custom("spring" + std::to_string(i) + ".previous", [=](Car& c, const Ram& r) { c.springs[s].previous_travel = r.w(0x1BD1A + o * 2); },
               [=](const Car& c) { return long(c.springs[s].previous_travel); }, [=](const Ram& r) { return long(r.w(0x1BD1A + o * 2)); });
        custom("spring" + std::to_string(i) + ".force", [=](Car& c, const Ram& r) { c.springs[s].force = r.w(0x1BD20 + o * 2); },
               [=](const Car& c) { return long(c.springs[s].force); }, [=](const Ram& r) { return long(r.w(0x1BD20 + o * 2)); });
        custom("spring" + std::to_string(i) + ".damage", [=](Car& c, const Ram& r) { c.springs[s].damage = r.u8(0x1BB4F + o); },
               [=](const Car& c) { return long(c.springs[s].damage); }, [=](const Ram& r) { return long(r.u8(0x1BB4F + o)); });
    }
    word("pitch_lift", 0x1BD26, &Car::pitch_lift); word("roll_lift", 0x1BD28, &Car::roll_lift); word("drive", 0x1BD2A, &Car::drive);
    array_word("body_velocity", 0x1BD2C, &Car::body_velocity, 3, 2);
    array_word("body_force", 0x1BD32, &Car::body_force, 3, 2);
    word("net_lift", 0x1BD38, &Car::net_lift);
    word("rate_pitch", 0x1BD3A, &Car::rate_pitch); word("rate_yaw", 0x1BD3C, &Car::rate_yaw); word("rate_roll", 0x1BD3E, &Car::rate_roll);
    array_word("loads", 0x1BD40, &Car::loads, 3, 2);
    word("previous_forward_load", 0x1BD46, &Car::previous_forward_load);
    array_word("impulse", 0x1BD54, &Car::impulse, 3, 2);
    word("heading", 0x1BD5A, &Car::heading); word("speed", 0x1BD5C, &Car::speed);
    array_word("matrix", 0x1C230, &Car::matrix, 36, 2);
    lng("rest_length", 0x1BCA0, &Car::rest_length);
    byte("impact_frames", 0x1BB56, &Car::impact_frames); byte("damage_tolerance", 0x1BB01, &Car::damage_tolerance);
    byte("damage_window", 0x63CE2, &Car::damage_window); byte("bottomed", 0x1BB7D, &Car::bottomed);
    flag("damaged", 0x1BB54, &Car::damaged, 0x80); word("peak_impact", 0x1BC3A, &Car::peak_impact);
    byte("on_ground", 0x1BB7E, &Car::on_ground); flag("sliding", 0x1BBC1, &Car::sliding, 0x80);
    flag("impulse_pending", 0x1BB46, &Car::impulse_pending, 0xFF);
    byte("knock_timer", 0x63EE0, &Car::knock_timer);
    custom("steer", [](Car& c, const Ram& r) { c.steer = std::int8_t(r.u8(0x1BBC6)); }, [](const Car& c) { return long(std::uint8_t(c.steer)); },
           [](const Ram& r) { return long(r.u8(0x1BBC6)); });
    flag("fire_up", 0x1BB70, &Car::fire_up, 0xFF); flag("latched", 0x1BBA8, &Car::accelerator_latched, 0x80);
    byte("boosting_flag", 0x1BB62, &Car::boost_timer); // placeholder, overwritten below
    f.pop_back();
    flag("boosting", 0x1BB62, &Car::boosting, 0x80);
    byte("accelerate_low", 0x1BAFA, &Car::accelerate_low); byte("accelerate_high", 0x1BAFB, &Car::accelerate_high);
    byte("boost_reload", 0x1BAFE, &Car::boost_reload); byte("boost_timer", 0x1BB3D, &Car::boost_timer); byte("boost_left", 0x1CA20, &Car::boost_left);
    byte("boost_allowance", 0x1BB95, &Car::boost_allowance);
    flag("stalled", 0x1BCA2, &Car::stalled, 0xFF); word("engine_note", 0x1BC62, &Car::engine_note);
    word("view_pitch", 0x1BC42, &Car::view_pitch); flag("pitch_at_limit", 0x1BBAB, &Car::pitch_at_limit, 0x80);
    byte("tick_phase", 0x1BBCF, &Car::tick_phase); byte("frame_count", 0x1BBC9, &Car::frame_count);
    byte("recovery", 0x1BBDF, &Car::recovery); flag("armed", 0x1BB72, &Car::armed, 0xFF); byte("tick", 0x1BBCD, &Car::tick);
    flag("facing_back", 0x1BBE1, &Car::facing_back, 0x80); word("settle_roll", 0x1BC00, &Car::settle_roll);
    word("across_centre", 0x1BC5C, &Car::across_centre); word("height", 0x1BCD0, &Car::height); word("ride_height", 0x1BC60, &Car::ride_height);
    flag("launched", 0x1BBC4, &Car::launched, 0x80); flag("launch_flag", 0x1BB8E, &Car::launch_flag, 0x80);
    byte("crash_state", 0x1BB9C, &Car::crash_state); byte("limit_select", 0x1BB75, &Car::limit_select);
    byte("thud_holdoff", 0x620B6, &Car::thud_holdoff); byte("drag_flags", 0x1BBC7, &Car::drag_flags);
    byte("opponent_flags", 0x1BBB8, &Car::opponent_flags);
    custom("segment", [](Car& c, const Ram& r) { c.segment = r.u8(0x1BB1C); }, [](const Car& c) { return long(c.segment); },
           [](const Ram& r) { return long(r.u8(0x1BB1C)); });
    word("across", 0x1BC5E, &Car::across); word("along", 0x1BB10, &Car::along); byte("step_index", 0x1BB0A, &Car::step_index);
    byte("track_number", 0x1CA33, &Car::track_number);
    byte("off_track", 0x1BB9A, &Car::off_track); byte("edge_side", 0x1BBDA, &Car::edge_side); byte("rear_along", 0x1BBA1, &Car::rear_along);
    word("heading_error", 0x1BC2A, &Car::heading_error); word("heading_error_scaled", 0x1BC3C, &Car::heading_error_scaled);
    byte("curve_side", 0x1BB5D, &Car::curve_side);
    // Placement.
    custom("quadrant", [](Car& c, const Ram& r) { c.quadrant = std::uint16_t(r.w(0x1BC30)); },
           [](const Car& c) { return long(c.quadrant); }, [](const Ram& r) { return long(std::uint16_t(r.w(0x1BC30))); });
    byte("cell_x", 0x1BB04, &Car::cell_x); byte("cell_z", 0x1BB06, &Car::cell_z);
    byte("fine_x", 0x1BB07, &Car::fine_x); byte("fine_z", 0x1BB09, &Car::fine_z);
    lng("turned_x", 0x1BCCC, &Car::turned_x); lng("turned_z", 0x1BCD4, &Car::turned_z);
    word("eye", 0x1BBFA, &Car::eye); word("view_yaw", 0x1BC2E, &Car::view_yaw);
    custom("corner_x", [](Car& c, const Ram& r) { c.corner_x = std::int16_t(r.u8(0x1BB2E) << 8 | r.u8(0x1BB23)); },
           [](const Car& c) { return long(c.corner_x); }, [](const Ram& r) { return long(std::int16_t(r.u8(0x1BB2E) << 8 | r.u8(0x1BB23))); });
    custom("corner_z", [](Car& c, const Ram& r) { c.corner_z = std::int16_t(r.u8(0x1BB32) << 8 | r.u8(0x1BB27)); },
           [](const Car& c) { return long(c.corner_z); }, [](const Ram& r) { return long(std::int16_t(r.u8(0x1BB32) << 8 | r.u8(0x1BB27))); });
    word("progress", 0x1BB0A, &Car::progress); flag("near_end", 0x1BB0E, &Car::near_end, 0x80);
    byte("off_road_side", 0x1BBD1, &Car::off_road_side);
    return f;
}

// The race loop's own state (game.md, "The race").
using stuntcar::race::RaceState;
struct StateField {
    std::string name;
    std::function<long(const RaceState&, const Car&)> value;
    std::function<long(const Ram&)> ram;
    std::function<void(RaceState&, Car&, long)> load;
};

std::vector<StateField> state_fields() {
    std::vector<StateField> f;
    using stuntcar::race::Time;
    const auto time = [&](std::string n, int slot, auto get) {
        const auto o = std::uint32_t(slot);
        const auto part = [&](std::string p, std::uint32_t base, std::uint8_t Time::*member) {
            f.push_back({n + "." + p, [=](const RaceState& s, const Car&) { return long(get(const_cast<RaceState&>(s)).*member); },
                         [=](const Ram& r) { return long(r.u8(base + o)); },
                         [=](RaceState& s, Car&, long v) { get(s).*member = std::uint8_t(v); }});
        };
        part("minutes", 0x1C908, &Time::minutes);
        part("seconds", 0x1C920, &Time::seconds);
        part("hundredths", 0x1C938, &Time::hundredths);
    };
    for (int car = 0; car < 2; ++car) {
        const auto o = std::uint32_t(car);
        const auto c = std::size_t(car);
        const auto n = "car" + std::to_string(car);
        f.push_back({n + ".segment", [=](const RaceState& s, const Car&) { return long(s.cars[c].segment); }, [=](const Ram& r) { return long(r.u8(0x1BB1C + o)); },
                     [=](RaceState& s, Car&, long v) { s.cars[c].segment = int(v); }});
        f.push_back({n + ".on_lap", [=](const RaceState& s, const Car&) { return long(s.cars[c].on_lap); }, [=](const Ram& r) { return long(r.u8(0x1BB1E + o) >> 7); },
                     [=](RaceState& s, Car&, long v) { s.cars[c].on_lap = v; }});
        f.push_back({n + ".laps", [=](const RaceState& s, const Car&) { return long(s.cars[c].laps); }, [=](const Ram& r) { return long(r.u8(0x1BB20 + o)); },
                     [=](RaceState& s, Car&, long v) { s.cars[c].laps = std::uint8_t(v); }});
        time(n + ".lap_time", car, [=](RaceState& s) -> Time& { return s.cars[c].lap_time; });
    }
    time("best_lap", 2, [](RaceState& s) -> Time& { return s.best_lap; });
    const auto byte = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({n, [=](const RaceState& s, const Car&) { return long(s.*member); }, [=](const Ram& r) { return long(r.u8(a)); },
                     [=](RaceState& s, Car&, long v) { s.*member = decltype(s.*member)(v); }});
    };
    const auto flag = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({n, [=](const RaceState& s, const Car&) { return long(s.*member); }, [=](const Ram& r) { return long(r.u8(a) >> 7); },
                     [=](RaceState& s, Car&, long v) { s.*member = v != 0; }});
    };
    byte("laps_to_finish", 0x1BB99, &RaceState::laps_to_finish);
    byte("lap_time_hold", 0x1BBCC, &RaceState::lap_time_hold);
    byte("finished", 0x1BBAF, &RaceState::finished);
    byte("finish_countdown", 0x1BB6C, &RaceState::finish_countdown);
    flag("race_over", 0x1BB6F, &RaceState::race_over);
    flag("odd_frame", 0x1BBC8, &RaceState::odd_frame);
    byte("crash_wait", 0x1BB41, &RaceState::crash_wait);
    f.push_back({"last_segment", [](const RaceState& s, const Car&) { return long(s.last_segment); }, [](const Ram& r) { return long(r.u8(0x1BB9B)); },
                 [](RaceState& s, Car&, long v) { s.last_segment = int(v); }});
    byte("frame_clock", 0x1BBAC, &RaceState::frame_clock);
    byte("intact_marks", 0x1C9CF, &RaceState::intact_marks);
    byte("hole_timer", 0x1BB73, &RaceState::hole_timer);
    byte("wheel_base", 0x1BBDD, &RaceState::wheel_base);
    const auto crack = [&](std::string n, std::uint32_t a, std::uint8_t stuntcar::race::Crack::*member) {
        f.push_back({"crack." + n, [=](const RaceState& s, const Car&) { return long(s.crack.*member); }, [=](const Ram& r) { return long(r.u8(a)); },
                     [=](RaceState& s, Car&, long v) { s.crack.*member = std::uint8_t(v); }});
    };
    crack("length", 0x1BB55, &stuntcar::race::Crack::length);
    crack("target", 0x1BBB6, &stuntcar::race::Crack::target);
    crack("row", 0x662AA, &stuntcar::race::Crack::row);
    crack("colour", 0x662AB, &stuntcar::race::Crack::colour);
    for (int i = 0; i < 5; ++i) {
        const auto b = std::size_t(i);
        f.push_back({"random" + std::to_string(i), [=](const RaceState& s, const Car&) { return long(s.random.bytes[b]); },
                     [=](const Ram& r) { return long(r.u8(0x625A2 + std::uint32_t(i))); },
                     [=](RaceState& s, Car&, long v) { s.random.bytes[b] = std::uint8_t(v); }});
    }
    // Dust and smoke particles.
    for (int i = 0; i < 32; ++i) {
        const auto k = std::size_t(i);
        const auto o = std::uint32_t(i * 2);
        const auto part = [&](std::string n, std::uint32_t base, std::int16_t stuntcar::race::Particle::*member) {
            f.push_back({"smoke" + std::to_string(i) + "." + n,
                         [=](const RaceState& s, const Car&) { return long(s.smoke.particles[k].*member); },
                         [=](const Ram& r) { return long(r.w(base + o)); },
                         [=](RaceState& s, Car&, long v) { s.smoke.particles[k].*member = std::int16_t(v); }});
        };
        part("x", 0x1C380, &stuntcar::race::Particle::x);
        part("y", 0x1C3C0, &stuntcar::race::Particle::y);
        part("vx", 0x1C400, &stuntcar::race::Particle::vx);
        part("vy", 0x1C440, &stuntcar::race::Particle::vy);
    }
    // The opponent.
    using stuntcar::race::Opponent;
    const auto o_word = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({"opponent." + n, [=](const RaceState& s, const Car&) { return long(std::int16_t(s.opponent.*member)); },
                     [=](const Ram& r) { return long(r.w(a)); },
                     [=](RaceState& s, Car&, long v) { s.opponent.*member = decltype(s.opponent.*member)(v); }});
    };
    const auto o_byte = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({"opponent." + n, [=](const RaceState& s, const Car&) { return long(std::uint8_t(s.opponent.*member)); },
                     [=](const Ram& r) { return long(r.u8(a)); },
                     [=](RaceState& s, Car&, long v) { s.opponent.*member = decltype(s.opponent.*member)(v); }});
    };
    const auto o_flag = [&](std::string n, std::uint32_t a, auto member) {
        f.push_back({"opponent." + n, [=](const RaceState& s, const Car&) { return long(s.opponent.*member); },
                     [=](const Ram& r) { return long(r.u8(a) >> 7); }, [=](RaceState& s, Car&, long v) { s.opponent.*member = v != 0; }});
    };
    const auto o_array = [&](std::string n, std::uint32_t a, auto member, int count, int stride = 2) {
        for (int i = 0; i < count; ++i) {
            const auto k = std::size_t(i);
            const auto at = a + std::uint32_t(i * stride);
            f.push_back({"opponent." + n + std::to_string(i), [=](const RaceState& s, const Car&) { return long((s.opponent.*member)[k]); },
                         [=](const Ram& r) { return long(stride == 2 ? r.w(at) : r.u8(at)); },
                         [=](RaceState& s, Car&, long v) { (s.opponent.*member)[k] = decltype((s.opponent.*member)[k])(v); }});
        }
    };
    f.push_back({"opponent.segment", [](const RaceState& s, const Car&) { return long(s.opponent.segment); },
                 [](const Ram& r) { return long(r.u8(0x1BB1D)); }, [](RaceState& s, Car&, long v) { s.opponent.segment = int(v); }});
    f.push_back({"opponent.rear_segment", [](const RaceState& s, const Car&) { return long(s.opponent.rear_segment); },
                 [](const Ram& r) { return long(r.u8(0x1BB40)); }, [](RaceState& s, Car&, long v) { s.opponent.rear_segment = int(v); }});
    o_word("progress", 0x1BB0C, &Opponent::progress);
    o_byte("progress_carry", 0x1BB3C, &Opponent::progress_carry);
    o_word("speed", 0x1BBEE, &Opponent::speed);
    o_word("lane", 0x1BBEC, &Opponent::lane);
    o_byte("target_speed", 0x1BB4A, &Opponent::target_speed);
    o_flag("braking", 0x1BB45, &Opponent::braking);
    o_byte("bump_timer", 0x1BBC0, &Opponent::bump_timer);
    o_byte("bump_phase", 0x1BBC2, &Opponent::bump_phase);
    o_array("wobble", 0x1BBBE, &Opponent::wobble, 2, 1);
    o_byte("lane_wobble", 0x1BBBD, &Opponent::lane_wobble);
    o_byte("bump_side", 0x1BB9D, &Opponent::bump_side);
    o_array("body", 0x1BD66, &Opponent::body, 4);
    o_array("road", 0x1BD86, &Opponent::road, 3);
    o_array("compression", 0x1BD5E, &Opponent::compression, 3);
    o_array("previous", 0x1BD6E, &Opponent::previous, 3);
    o_array("wheel_speed", 0x1BD76, &Opponent::wheel_speed, 3);
    o_array("force", 0x1BD7E, &Opponent::force, 3);
    o_byte("on_ground", 0x1BB44, &Opponent::on_ground);
    o_word("deepest", 0x63EDE, &Opponent::deepest);
    o_byte("half_track", 0x1BB43, &Opponent::half_track);
    o_word("rear_progress", 0x1BB12, &Opponent::rear_progress);
    f.push_back({"opponent.lift0", [](const RaceState& s, const Car&) { return long(s.opponent.lift[0]); }, [](const Ram& r) { return long(r.w(0x1BF64)); },
                 [](RaceState& s, Car&, long v) { s.opponent.lift[0] = std::int16_t(v); }});
    f.push_back({"opponent.lift1", [](const RaceState& s, const Car&) { return long(s.opponent.lift[1]); }, [](const Ram& r) { return long(r.w(0x1BF68)); },
                 [](RaceState& s, Car&, long v) { s.opponent.lift[1] = std::int16_t(v); }});
    f.push_back({"opponent.lift2", [](const RaceState& s, const Car&) { return long(s.opponent.lift[2]); }, [](const Ram& r) { return long(r.w(0x1BF66)); },
                 [](RaceState& s, Car&, long v) { s.opponent.lift[2] = std::int16_t(v); }});
    f.push_back({"opponent.lift3", [](const RaceState& s, const Car&) { return long(s.opponent.lift[3]); }, [](const Ram& r) { return long(r.w(0x1BF6A)); },
                 [](RaceState& s, Car&, long v) { s.opponent.lift[3] = std::int16_t(v); }});
    o_byte("touch_frames", 0x1BBC3, &Opponent::touch_frames);
    o_flag("knocking", 0x1BBEB, &Opponent::knocking);
    o_word("gap", 0x1BC3E, &Opponent::gap);
    o_word("distance", 0x1BC38, &Opponent::distance);
    f.push_back({"message", [](const RaceState&, const Car& c) { return long(c.launch_message); }, [](const Ram& r) { return long(r.u8(0x5E65B)); },
                 [](RaceState&, Car& c, long v) { c.launch_message = std::uint8_t(v); }});
    f.push_back({"message_groups", [](const RaceState&, const Car& c) { return long(c.launch_message_length); }, [](const Ram& r) { return long(r.u8(0x5E65A)); },
                 [](RaceState&, Car& c, long v) { c.launch_message_length = std::uint8_t(v); }});
    return f;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 4) {
            std::fprintf(stderr, "usage: stunt_car_racer_physcheck <content> <capture.bin> <track> [physics|place] [first] [count]\n");
            return 2;
        }
        const auto assets = stuntcar::data::load_assets(argv[1]);
        const auto circuits = stuntcar::data::load_circuits(assets.program);
        const auto tables = stuntcar::race::load_tables(assets.program);
        const auto& track = circuits.tracks.at(std::size_t(std::stoi(argv[3])));
        stuntcar::race::TrackContact contact(circuits, track);
        if (std::string(argv[2]) == "autodrive") {
            // Finds controls that drive whole laps in the port, for replaying
            // on the original (harness/racecap.py INPUTS): full throttle,
            // steering towards the piece's heading and the road's middle.
            // Prints the inputs, the laps and how often the car went over.
            const int track = std::stoi(argv[3]);
            const int frames = argc > 4 ? std::stoi(argv[4]) : 600;
            const int gain = argc > 5 ? std::stoi(argv[5]) : 8;
            const int dead = argc > 6 ? std::stoi(argv[6]) : 0x400;
            const bool flip = argc > 7 && std::stoi(argv[7]);
            const int across_sign = argc > 8 ? std::stoi(argv[8]) : 1;
            const int top = argc > 9 ? std::stoi(argv[9], nullptr, 0) : 0x2000;
            std::optional<stuntcar::race::Driver> rival;
            if (std::getenv("LEAGUE")) rival = stuntcar::race::Driver{9, assets.program.u8(0x1ED8A + 9)};
            stuntcar::race::Race race(assets.program, circuits, track, false, rival);
            std::vector<std::uint8_t> inputs;
            int crashes = 0;
            bool was_crashed = false;
            int previous = 0;
            for (int k = 0; k < frames; ++k) {
                const auto& c = race.player();
                std::uint8_t in = 0;
                if (race.state().phase == stuntcar::race::Phase::drive) {
                    if (c.recovery) {
                        in = (k / 4) % 2 ? 0x10 : 0; // tap fire to drop
                    } else {
                        // Keep to the middle: the offset and how fast it changes.
                        const int rate = c.across_centre - previous;
                        int error = across_sign * (c.across_centre * gain + rate * 16);
                        const bool drifting = std::abs(c.across_centre) > 0x60 || std::abs(rate) > 0x18;
                        in = drifting ? 2 : (c.speed > top ? 0 : 1);
                        if (error > dead) in |= flip ? 4 : 8;
                        else if (error < -dead) in |= flip ? 8 : 4;
                    }
                }
                if (std::getenv("TRACE") && k % 10 == 0)
                    std::fprintf(stderr, "%d ph %d rec %02x seg %d yaw %04x head %04x across %d crash %02x in %02x\n", k, int(race.state().phase), c.recovery,
                                 c.segment, std::uint16_t(c.yaw), std::uint16_t(c.heading), c.across_centre, c.crash_state, in);
                // (racecap.py counts inputs in driving frames only)
                if (race.state().phase == stuntcar::race::Phase::drive) inputs.push_back(in);
                previous = c.across_centre;
                race.step({bool(in & 1), bool(in & 2), bool(in & 4), bool(in & 8), bool(in & 16)});
                const bool crashed = std::int8_t(race.player().crash_state) < 0;
                if (crashed && !was_crashed) ++crashes;
                was_crashed = crashed;
            }
            std::string list;
            for (std::size_t k = 0; k < inputs.size();) {
                std::size_t n = 1;
                while (k + n < inputs.size() && inputs[k + n] == inputs[k]) ++n;
                char buf[16];
                std::snprintf(buf, sizeof buf, "%s%02x*%zu", list.empty() ? "" : ",", inputs[k], n);
                list += buf;
                k += n;
            }
            std::printf("laps %d crashes %d best %x:%02x.%02x\n%s\n", race.state().cars[0].laps, crashes, race.state().best_lap.minutes,
                        race.state().best_lap.seconds, race.state().best_lap.hundredths, list.c_str());
            return 0;
        }
        const auto capture = platform::read_file(argv[2]);
        if (argc > 5 && std::string(argv[4]) == "samples") {
            // A sampled capture (harness/racesample.py): the port runs the
            // whole race with the same inputs from the first sample's state
            // and is compared wherever the original was stopped.
            std::size_t window = 0;
            for (const auto& w : race_windows) window += w.length;
            const std::size_t record = 2 + window + 2, samples = capture.size() / record;
            std::vector<std::uint8_t> inputs;
            for (std::string list = argv[5]; !list.empty();) {
                const auto comma = list.find(',');
                const auto item = list.substr(0, comma);
                const auto star = item.find('*');
                const int repeat = star == std::string::npos ? 1 : std::stoi(item.substr(star + 1));
                for (int r = 0; r < repeat; ++r) inputs.push_back(std::uint8_t(std::stoi(item.substr(0, star), nullptr, 16)));
                list = comma == std::string::npos ? "" : list.substr(comma + 1);
            }
            stuntcar::race::Race race(assets.program, circuits, std::stoi(argv[3]));
            const auto cars = fields();
            const auto states = state_fields();
            {
                const Ram start{capture.data() + 2, true};
                for (const auto& f : cars) f.load(race.player_for_tools(), start);
                for (const auto& f : states) f.load(race.state_for_tools(), race.player_for_tools(), f.ram(start));
                race.state_for_tools().phase = stuntcar::race::Phase(capture[0]);
                race.state_for_tools().spawn_segment = race.track().car_start_segment;
            }
            std::size_t driven = 0;
            int exact = 0, shown = 0;
            for (std::size_t k = 1; k < samples; ++k) {
                const auto* at = capture.data() + k * record;
                const std::size_t count = std::size_t(at[2 + window] << 8 | at[3 + window]);
                const auto kind = stuntcar::race::Phase(at[0]);
                // Run to the same frame start: as many driving frames, in the
                // same kind of frame.
                for (int guard = 0; guard < 100000 && !(driven == count && race.state().phase == kind); ++guard) {
                    if (race.state().phase == stuntcar::race::Phase::over) break;
                    const bool drive = race.state().phase == stuntcar::race::Phase::drive;
                    const auto in = drive ? inputs[std::min(driven, inputs.size() - 1)] : std::uint8_t(0);
                    race.step({bool(in & 1), bool(in & 2), bool(in & 4), bool(in & 8), bool(in & 16)});
                    if (drive) ++driven;
                }
                const Ram expected{at + 2, true};
                std::string bad;
                int wrong = 0;
                const auto note = [&](const std::string& name, long mine, long theirs) {
                    if (mine == theirs) return;
                    if (wrong < 10) bad += " " + name + "=" + std::to_string(mine) + "/" + std::to_string(theirs);
                    ++wrong;
                };
                note("phase", long(race.state().phase), long(kind));
                note("driving_frames", long(driven), long(count));
                for (const auto& f : cars) note(f.name, f.value(race.player()), f.ram(expected));
                for (const auto& f : states) note(f.name, f.value(race.state(), race.player()), f.ram(expected));
                if (!wrong) ++exact;
                else if (shown++ < 20) std::printf("sample %zu (frame %zu, kind %d): %d differ:%s\n", k, count, int(kind), wrong, bad.c_str());
            }
            std::printf("%d of %zu samples exact\n", exact, samples - 1);
            return 0;
        }
        if (argc > 4 && std::string(argv[4]) == "race") {
            std::size_t window = 0;
            for (const auto& w : race_windows) window += w.length;
            if (capture.size() % (2 + window)) {
                old_race_capture = true;
                window -= std::end(race_windows)[-1].length;
            }
            const std::size_t record = 2 + window, frames = capture.size() / record;
            const int first = argc > 5 ? std::stoi(argv[5]) : 0;
            const int count = argc > 6 ? std::min(first + std::stoi(argv[6]), int(frames)) : int(frames);
            const Ram opening{capture.data() + 2, true};
            // A league race: the opponent's number and habits from the capture.
            std::optional<stuntcar::race::Driver> driver;
            if (std::int8_t(opening.u8(0x1CA22)) < 0) {
                const auto number = opening.u8(0x1CA29);
                driver = stuntcar::race::Driver{number, assets.program.u8(0x1ED8A + number)};
            }
            stuntcar::race::Race race(assets.program, circuits, std::stoi(argv[3]), false, driver);
            const auto cars = fields();
            const auto states = state_fields();
            {
                // Start from the captured frame: the car, the race state and
                // the phase the frame is in; the opponent's skill and segment
                // speeds as the original rolled them.
                const auto* at = capture.data() + std::size_t(first) * record;
                const Ram start{at + 2, true};
                if (auto* d = race.opponent_driver_for_tools()) {
                    std::vector<std::uint8_t> speeds;
                    for (std::size_t s = 0; s < race.track().segments.size(); ++s) speeds.push_back(start.u8(0x1C952 + std::uint32_t(s)));
                    d->set_skill(start.u8(0x63CE0), start.u8(0x63CE1), speeds);
                }
                for (const auto& f : cars) f.load(race.player_for_tools(), start);
                for (const auto& f : states) f.load(race.state_for_tools(), race.player_for_tools(), f.ram(start));
                race.state_for_tools().phase = stuntcar::race::Phase(at[0]);
                race.state_for_tools().spawn_segment = race.state_for_tools().last_segment;
                if (first == 0) race.state_for_tools().spawn_segment = race.track().car_start_segment;
            }
            if (const char* trial = std::getenv("TRIAL")) {
                // Plays the given inputs (driving frames) from the captured
                // state and reports the knocks between the cars.
                std::vector<std::uint8_t> inputs;
                for (std::string list = trial; !list.empty();) {
                    const auto comma = list.find(',');
                    const auto item = list.substr(0, comma);
                    const auto star = item.find('*');
                    const int repeat = star == std::string::npos ? 1 : std::stoi(item.substr(star + 1));
                    for (int r = 0; r < repeat; ++r) inputs.push_back(std::uint8_t(std::stoi(item.substr(0, star), nullptr, 16)));
                    list = comma == std::string::npos ? "" : list.substr(comma + 1);
                }
                std::size_t driven = 0;
                int knocks = 0, first_knock = -1;
                const int block = std::getenv("BLOCK") ? std::stoi(std::getenv("BLOCK")) : 0;
                std::vector<std::uint8_t> played;
                for (int k = 0; k < 100000 && driven < inputs.size() + std::size_t(block); ++k) {
                    const bool drive = race.state().phase == stuntcar::race::Phase::drive;
                    std::uint8_t in = 0;
                    if (drive && driven < inputs.size()) {
                        in = inputs[driven];
                    } else if (drive) {
                        // Block: into the opponent's lane as it comes close.
                        const auto& o = race.state().opponent;
                        const auto& c = race.player();
                        if (c.recovery) in = (driven / 4) % 2 ? 0x10 : 0;
                        else if (o.distance < 0x80) {
                            const int lane = std::uint8_t(o.lane), mine = c.rear_along;
                            if (std::getenv("CRAWL") && (driven % std::stoi(std::getenv("CRAWL"))) == 0) in |= 1;
                            if (lane > mine + 8) in |= 0x08;
                            else if (lane < mine - 8) in |= 0x04;
                        }
                    }
                    if (drive) played.push_back(in);
                    if (std::getenv("PRINT") && k % std::stoi(std::getenv("PRINT")) == 0) {
                        const auto& o = race.state().opponent;
                        std::printf("frame %d seg %d prog %04x lane %d dist %d flags %02x lift %d %d %d %d road %d %d %d launched %d\n", k,
                                    o.segment, o.progress, o.lane, o.distance, race.player().opponent_flags, o.lift[0], o.lift[1],
                                    o.lift[2], o.lift[3], o.road[0], o.road[1], o.road[2], int(race.player().launched));
                    }
                    race.step({bool(in & 1), bool(in & 2), bool(in & 4), bool(in & 8), bool(in & 16)});
                    if (drive) ++driven;
                    if (race.state().opponent.knocking) {
                        ++knocks;
                        if (first_knock < 0) first_knock = int(driven);
                    }
                }
                std::printf("knock frames %d, first at driving frame %d, laps %d/%d\n", knocks, first_knock,
                            race.state().cars[0].laps, race.state().cars[1].laps);
                std::string list;
                for (std::size_t k = 0; k < played.size();) {
                    std::size_t n = 1;
                    while (k + n < played.size() && played[k + n] == played[k]) ++n;
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "%s%02x*%zu", list.empty() ? "" : ",", played[k], n);
                    list += buf;
                    k += n;
                }
                std::printf("%s\n", list.c_str());
                return 0;
            }
            int exact = 0, shown = 0;
            for (int k = first; k + 1 < count; ++k) {
                const auto* at = capture.data() + std::size_t(k) * record;
                const auto in = at[1];
                const auto before = race.state().random;
                race.step({bool(in & 1), bool(in & 2), bool(in & 4), bool(in & 8), bool(in & 16)});
                if (std::getenv("DRAWS")) {
                    // How many numbers each side drew this frame.
                    const auto count = [](stuntcar::race::Random r, const std::array<std::uint8_t, 5>& target) {
                        for (int n = 0; n < 400; ++n, r.next())
                            if (r.bytes == target) return n;
                        return -1;
                    };
                    const Ram next_ram{at + record + 2, true};
                    std::array<std::uint8_t, 5> theirs{};
                    for (std::uint32_t i = 0; i < 5; ++i) theirs[i] = next_ram.u8(0x625A2 + i);
                    std::printf("frame %d draws port %d original %d\n", k + 1, count(before, race.state().random.bytes), count(before, theirs));
                }
                const auto* next = at + record;
                const Ram expected{next + 2, true};
                std::string bad;
                int wrong = 0;
                const auto note = [&](const std::string& name, long mine, long theirs) {
                    if (mine == theirs) return;
                    if (wrong < (std::getenv("ALL") ? 400 : 10)) bad += " " + name + "=" + std::to_string(mine) + "/" + std::to_string(theirs);
                    ++wrong;
                };
                note("phase", long(race.state().phase), long(next[0]));
                for (const auto& f : cars) note(f.name, f.value(race.player()), f.ram(expected));
                for (const auto& f : states) note(f.name, f.value(race.state(), race.player()), f.ram(expected));
                if (!wrong) ++exact;
                else if (shown++ < 30) std::printf("frame %d (kind %d, input %02x): %d differ:%s\n", k + 1, next[0], in, wrong, bad.c_str());
            }
            std::printf("%d of %d frames exact\n", exact, count - 1 - first);
            return 0;
        }
        std::size_t window = 0;
        for (const auto& w : windows) window += w.length;
        const std::size_t record = 1 + 2 * window, frames = capture.size() / record;
        const bool placing = argc > 4 && std::string(argv[4]) == "place";
        const bool driving = argc > 4 && std::string(argv[4]) == "drive";
        const int first = argc > 5 ? std::stoi(argv[5]) : 0;
        const int count = argc > 6 ? std::stoi(argv[6]) : int(frames);
        const auto list = fields();
        int exact = 0, shown = 0;
        Car driven;
        stuntcar::race::Random random;
        for (int k = first; k < first + count && k < int(frames); ++k) {
            const Ram before{capture.data() + std::size_t(k) * record + 1};
            const Ram after{capture.data() + std::size_t(k) * record + 1 + window};
            Car car;
            if (driving) {
                if (k == first) {
                    for (const auto& f : list) f.load(driven, before);
                    continue;
                }
                // Finish the previous frame, then this frame's controls.
                stuntcar::race::step_physics(driven, tables, contact, random);
                stuntcar::race::place(driven, tables, contact);
                stuntcar::race::end_frame(driven);
                const auto in = capture[std::size_t(k) * record];
                stuntcar::race::read_controls(driven, {bool(in & 1), bool(in & 2), bool(in & 4), bool(in & 8), bool(in & 16)});
                car = driven;
            } else {
                for (const auto& f : list) f.load(car, before);
                if (placing) stuntcar::race::place(car, tables, contact);
                else stuntcar::race::step_physics(car, tables, contact, random);
            }
            const Ram& expected = driving ? before : after;
            std::string bad;
            int wrong = 0;
            for (const auto& f : list) {
                const auto mine = f.value(car), theirs = f.ram(expected);
                if (mine != theirs) {
                    if (wrong < 8) bad += " " + f.name + "=" + std::to_string(mine) + "/" + std::to_string(theirs);
                    ++wrong;
                }
            }
            if (!wrong) ++exact;
            else if (shown++ < 25) std::printf("frame %d (input %02x, recovery %d): %d differ:%s\n", k, capture[std::size_t(k) * record],
                                               before.u8(0x1BBDF), wrong, bad.c_str());
        }
        std::printf("%d of %d frames exact\n", exact, count < int(frames) - first ? count : int(frames) - first);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
