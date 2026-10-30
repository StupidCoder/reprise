#include "stunt_car_racer_amiga/race/race.hpp"
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

} // namespace

CarSpec car_spec(const data::Program& program, bool super_league) {
    const auto at = car_table + std::uint32_t(super_league ? car_entry : 0);
    return {program.u8(at + 2), program.u8(at + 3), program.u8(at + 6), program.u8(at + 9)};
}

Race::Race(const data::Program& program, const data::Circuits& circuits, int track, bool super_league)
    : track_(circuits.tracks.at(std::size_t(track))), tables_(load_tables(program)),
      contact_(circuits, track_) {
    for (std::size_t k = 0; k < start_allowed_.size(); ++k) start_allowed_[k] = program.u8(start_table + std::uint32_t(k));
    const auto spec = car_spec(program, super_league);
    car_.accelerate_low = spec.accelerate_low;
    car_.accelerate_high = spec.accelerate_high;
    car_.boost_reload = spec.boost_reload;
    car_.damage_tolerance = spec.damage_tolerance;
    car_.damage_window = track_.trailer[super_league ? 1 : 0];
    car_.track_number = std::uint8_t(track);
    spawn(track_.car_start_segment);
    // The race loop's lead-in: a placement, one unarmed physics frame, a
    // placement; then the drive forces are switched on.
    place(car_, tables_, contact_);
    end_frame(car_);
    step_physics(car_, tables_, contact_);
    place(car_, tables_, contact_);
    end_frame(car_);
    car_.armed = true;
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
    const auto& piece = track_.segments[std::size_t(segment)];
    const bool relaunch = car_.launched;
    const Car kept = car_;
    car_ = Car{};
    // What the spawn does not clear (it clears the body, forces, travels and
    // the placement).
    for (std::size_t k = 0; k < 3; ++k) {
        car_.springs[k].surface = kept.springs[k].surface;
        car_.springs[k].contact = kept.springs[k].contact;
        car_.springs[k].compression = kept.springs[k].compression;
        car_.springs[k].damage = kept.springs[k].damage;
    }
    car_.accelerate_low = kept.accelerate_low, car_.accelerate_high = kept.accelerate_high;
    car_.boost_reload = kept.boost_reload, car_.boost_left = kept.boost_left;
    car_.damage_tolerance = kept.damage_tolerance, car_.damage_window = kept.damage_window;
    car_.track_number = kept.track_number, car_.launched = kept.launched;
    car_.armed = kept.armed, car_.tick = kept.tick, car_.tick_phase = kept.tick_phase;
    car_.frame_count = kept.frame_count, car_.facing_back = kept.facing_back;

    car_.segment = segment;
    car_.recovery = 0xF0;
    car_.x = std::int32_t((piece.cell_x * 128 + cell_centre) << 16);
    car_.z = std::int32_t((piece.cell_z * 128 + cell_centre) << 16);
    car_.y = hanging_height;
    const int turn = (piece.shape == 4 || piece.shape == 10) ? 0x20 : 0; // diagonals: 45 degrees more
    car_.yaw = std::int16_t(std::uint8_t(((piece.heading << 6) ^ (piece.reversed ? 0x80 : 0)) + turn) << 8);
    place(car_, tables_, contact_);
    step_physics(car_, tables_, contact_);
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

void Race::step(const Controls& controls) {
    read_controls(car_, controls);
    step_physics(car_, tables_, contact_);
    place(car_, tables_, contact_);
    end_frame(car_);
}

} // namespace stuntcar::race
