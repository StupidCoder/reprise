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
//
// The game-variable addresses below are research knowledge (game.md, "Car
// physics"); the game code itself never sees them.

#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/race/contact.hpp"
#include "stunt_car_racer_amiga/race/physics.hpp"
#include "stunt_car_racer_amiga/race/placement.hpp"
#include "platform/files.hpp"
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Window {
    std::uint32_t address, length;
};
const Window windows[] = {{0x1B800, 0x1400}, {0x5C6B8, 0x10}, {0x61AD4, 0x8}, {0x6125A, 0x8}, {0x620B6, 0x2},
                          {0x63CE2, 0x2},    {0x63EDE, 0x4},  {0x64AEC, 0x4}, {0x57C3C, 0x2}};

struct Ram {
    const std::uint8_t* bytes;
    std::uint8_t u8(std::uint32_t a) const {
        std::size_t at = 0;
        for (const auto& w : windows) {
            if (a >= w.address && a < w.address + w.length) return bytes[at + a - w.address];
            at += w.length;
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
    array_word("knock", 0x1BD76, &Car::knock, 3, 2);
    array_word("matrix", 0x1C230, &Car::matrix, 36, 2);
    lng("rest_length", 0x1BCA0, &Car::rest_length);
    byte("impact_frames", 0x1BB56, &Car::impact_frames); byte("damage_tolerance", 0x1BB01, &Car::damage_tolerance);
    byte("damage_window", 0x63CE2, &Car::damage_window); byte("bottomed", 0x1BB7D, &Car::bottomed);
    flag("damaged", 0x1BB54, &Car::damaged, 0x80); word("peak_impact", 0x1BC3A, &Car::peak_impact);
    byte("on_ground", 0x1BB7E, &Car::on_ground); flag("sliding", 0x1BBC1, &Car::sliding, 0x80);
    flag("impulse_pending", 0x1BB46, &Car::impulse_pending, 0xFF); word("impulse_left", 0x1BBEE, &Car::impulse_left);
    byte("knock_timer", 0x63EE0, &Car::knock_timer);
    custom("steer", [](Car& c, const Ram& r) { c.steer = std::int8_t(r.u8(0x1BBC6)); }, [](const Car& c) { return long(std::uint8_t(c.steer)); },
           [](const Ram& r) { return long(r.u8(0x1BBC6)); });
    flag("fire_up", 0x1BB70, &Car::fire_up, 0xFF); flag("latched", 0x1BBA8, &Car::accelerator_latched, 0x80);
    byte("boosting_flag", 0x1BB62, &Car::boost_timer); // placeholder, overwritten below
    f.pop_back();
    flag("boosting", 0x1BB62, &Car::boosting, 0x80);
    byte("accelerate_low", 0x1BAFA, &Car::accelerate_low); byte("accelerate_high", 0x1BAFB, &Car::accelerate_high);
    byte("boost_reload", 0x1BAFE, &Car::boost_reload); byte("boost_timer", 0x1BB3D, &Car::boost_timer); byte("boost_left", 0x1CA20, &Car::boost_left);
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
        const auto capture = platform::read_file(argv[2]);
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
                stuntcar::race::step_physics(driven, tables, contact);
                stuntcar::race::place(driven, tables, contact);
                stuntcar::race::end_frame(driven);
                const auto in = capture[std::size_t(k) * record];
                stuntcar::race::read_controls(driven, {bool(in & 1), bool(in & 2), bool(in & 4), bool(in & 8), bool(in & 16)});
                car = driven;
            } else {
                for (const auto& f : list) f.load(car, before);
                if (placing) stuntcar::race::place(car, tables, contact);
                else stuntcar::race::step_physics(car, tables, contact);
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
