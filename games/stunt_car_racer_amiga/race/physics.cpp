#include "stunt_car_racer_amiga/race/physics.hpp"
#include "stunt_car_racer_amiga/race/contact.hpp"
#include <algorithm>

namespace stuntcar::race {
namespace {

// Damping applied at both integration stages: x * 238 / 256.
constexpr int damping = 0xEE;
// Gravity, in the world frame (pointing down).
constexpr std::int16_t gravity_strength = 0x13D;
// Spring limits, damper gain and force limit.
constexpr std::int32_t spring_reach = 0x1400, spring_droop = -0x300;
constexpr int damper_gain = 0x114;
constexpr std::int16_t force_limit = 0x1200, force_clamped = 0x11FF;
// A hard landing: the force jumps to $400 from below $200.
constexpr std::int16_t hard_landing = 0x400, soft_before = 0x200;
// Impacts above (tolerance << 8) + $700 damage the car.
constexpr std::int16_t impact_threshold = 0x700, impact_floor = 0x600;
// Roll lift limit.
constexpr std::int16_t roll_lift_limit = 0x1000;
// The body's height is limited (high word).
constexpr std::int16_t height_limit = 0x3E8;
// Drag: firm while gripping, otherwise speed-proportional above $A00.
constexpr std::int16_t firm_drag = 0x6000, drag_dead_zone = 0xA00;
// Steering demand from the joystick.
constexpr std::int8_t steer_amount = 0x0F;
// Reverse drive force.
constexpr std::uint8_t reverse_low = 0x10, reverse_high = 0xFF;
// Airborne: the forward velocity's high byte in $78-$7F blocks the drive.
constexpr std::uint8_t airborne_from = 0x78;

std::int16_t damp(std::int16_t v) { return std::int16_t((std::int32_t(v) * damping) >> 8); }

// value * element >> 15, the game's fixed-point product (MULS, ASL #1, SWAP).
std::int16_t times(std::int16_t value, std::int16_t element) {
    return std::int16_t(((std::int32_t(value) * element) << 1) >> 16);
}

std::int16_t& slot(Car& c, int byte_offset) { return c.matrix[std::size_t(byte_offset / 2)]; }

// The orientation matrix from yaw (and yaw against the piece's heading),
// pitch and roll, then its products and cross terms.
void build_matrix(Car& c, const Tables& t) {
    const auto sy = sine(t, c.yaw), cy = cosine(t, c.yaw);
    for (const int o : {0x4, 0xC, 0xE, 0x14, 0x16}) slot(c, o) = sy;
    for (const int o : {0x6, 0x10, 0x12, 0x18, 0x1A}) slot(c, o) = cy;
    const auto relative = std::int16_t(c.yaw - c.heading);
    const auto sh = sine(t, relative), ch = cosine(t, relative);
    for (const int o : {0x34, 0x42, 0x44}) slot(c, o) = sh;
    for (const int o : {0x38, 0x3E, 0x46}) slot(c, o) = ch;
    slot(c, 0x8) = sine(t, c.pitch);
    const auto cp = cosine(t, c.pitch);
    for (const int o : {0xA, 0x1C, 0x1E}) slot(c, o) = cp;
    c.view_roll = c.roll;
    slot(c, 0x22) = cosine(t, c.roll);
    slot(c, 0x20) = sine(t, c.roll);

    const auto scale = [&](int from, int to, int step, std::int16_t by) {
        for (int o = from; o <= to; o += step) slot(c, o) = times(slot(c, o), by);
    };
    auto by = slot(c, 0x8);
    scale(0xC, 0x12, 2, by);
    scale(0x34, 0x38, 4, by);
    slot(c, 0x0) = slot(c, 0xC);
    slot(c, 0x2) = slot(c, 0x10);
    by = slot(c, 0xA);
    scale(0x4, 0x6, 2, by);
    scale(0x44, 0x46, 2, by);
    by = slot(c, 0x20);
    scale(0xC, 0x1C, 4, by);
    scale(0x34, 0x38, 4, by);
    by = slot(c, 0x22);
    scale(0xE, 0x1E, 4, by);
    scale(0x3E, 0x42, 4, by);
    slot(c, 0x28) = std::int16_t(slot(c, 0x18) - slot(c, 0xE));
    slot(c, 0x2A) = std::int16_t(-slot(c, 0x12) - slot(c, 0x14));
    slot(c, 0x2C) = std::int16_t(slot(c, 0x1A) + slot(c, 0xC));
    slot(c, 0x2E) = std::int16_t(slot(c, 0x10) - slot(c, 0x16));
    slot(c, 0x30) = std::int16_t(-slot(c, 0x1C));
    slot(c, 0x24) = std::int16_t(-slot(c, 0x20));
}

std::int16_t term(const Car& c, const Tables& t, std::int16_t value, int recipe) {
    return times(value, c.matrix[t.transform_terms[std::size_t(recipe)]]);
}

// Where the three points sit relative to the piece: from the yaw-against-
// heading terms of the matrix.
void place_points(Car& c) {
    const auto half = [&](int o) { return std::int16_t(slot(c, o) >> 1); };
    auto side = std::int16_t(half(0x3E) - half(0x34));
    auto along = std::int16_t(half(0x38) - half(0x42));
    const auto rear_side = std::int16_t(slot(c, 0x44) >> 5), rear_along = std::int16_t(slot(c, 0x46) >> 5);
    side = std::int16_t(side >> 5);
    along = std::int16_t(along >> 5);
    c.contact_offsets[2] = std::int16_t(-rear_side);
    c.contact_offsets[5] = std::int16_t(-rear_along);
    c.contact_offsets[0] = std::int16_t(rear_side - side);
    c.contact_offsets[1] = std::int16_t(rear_side + side);
    c.contact_offsets[3] = std::int16_t(rear_along - along);
    c.contact_offsets[4] = std::int16_t(rear_along + along);
}

// The body's height at the three points, tilted by pitch and roll.
void contact_heights(Car& c, const Tables& t) {
    const auto sp = std::int32_t(sine(t, c.pitch)) << 4;
    const auto sr = std::int32_t(sine(t, c.roll)) << 3;
    c.springs[2].contact = (c.y - sp) >> 8;
    const auto front = c.y + sp;
    c.springs[1].contact = (front - sr) >> 8;
    c.springs[0].contact = (front + sr) >> 8;
}

void velocity_to_body(Car& c, const Tables& t) {
    for (int k = 2; k >= 0; k -= 2) {
        const auto v = std::int16_t(term(c, t, c.vx, k) + term(c, t, c.vy, k + 3) + term(c, t, c.vz, k + 6));
        c.body_velocity[std::size_t(k)] = v;
    }
}

void speed_and_note(Car& c) {
    const auto f = c.body_velocity[2];
    c.speed = std::int16_t(f < 0 ? -f : f);
    if (!c.on_ground) {
        c.engine_note = std::int16_t(c.engine_note - std::int16_t(std::uint16_t(c.engine_note) >> 2));
    } else if (c.speed >= 0x800) {
        const auto s = std::uint32_t(std::uint16_t(c.speed << 1)) + 0x3000;
        c.engine_note = std::int16_t(s > 0xFFFF ? 0xFF00 : s);
    } else {
        c.engine_note = std::int16_t(c.speed << 3);
    }
}

void gravity_to_body(Car& c) {
    c.gravity[1] = times(std::int16_t(-gravity_strength), c.matrix[0xF]);
    c.gravity[2] = times(std::int16_t(-gravity_strength), c.matrix[0x4]);
    c.gravity[0] = times(gravity_strength, c.matrix[0xE]);
}

void spring_step(Car& c, Spring& s) {
    std::int32_t d = s.surface - s.contact - c.rest_length;
    s.compression = d;
    d = std::clamp(d, spring_droop, spring_reach);
    s.travel = std::int16_t(d);
    const auto delta = std::int16_t(s.travel - s.previous_travel);
    const auto f = std::int16_t(std::uint16_t((std::int32_t(delta) * damper_gain) >> 8) + std::uint16_t(s.travel));
    if (f < 0) {
        s.force = 0;
        c.impact_frames = 0;
        s.previous_travel = s.travel;
        return;
    }
    const auto before = s.force;
    s.force = f;
    if (f >= hard_landing && before < soft_before) ++c.bottomed;
    auto over = std::int16_t(f - std::int16_t(c.damage_tolerance << 8));
    if (over < 0 || over < impact_threshold) {
        c.impact_frames = 0;
        s.previous_travel = s.travel;
        return;
    }
    if (over >= c.peak_impact) c.peak_impact = over;
    over = std::int16_t(over - impact_floor);
    if (std::int8_t(c.tick) >= 0) {
        ++c.impact_frames;
        if (std::int8_t(c.impact_frames) < std::int8_t(c.damage_window)) {
            int severity = std::uint16_t(over) >> 8;
            severity = (severity + (severity >> 1)) & 0xFF;
            s.damage = std::uint8_t(std::min(255, severity + s.damage));
            c.damaged = true;
        }
    }
    if (s.force >= force_limit) s.force = force_clamped;
    s.previous_travel = s.travel;
}

void suspension(Car& c) {
    c.bottomed = 0;
    c.peak_impact = std::int16_t(c.peak_impact & 0x00FF); // the game clears only its high byte
    for (auto& s : c.springs) spring_step(c, s);
    const auto front = std::int16_t((c.springs[0].force + c.springs[1].force) >> 1);
    c.net_lift = std::int16_t((front + c.springs[2].force) >> 1);
    const auto lr = std::int16_t(c.springs[0].force - c.springs[1].force);
    auto r = std::int16_t((lr << 1) + lr);
    if (r < 0) r = std::int16_t(-r);
    if (r >= roll_lift_limit) r = roll_lift_limit;
    c.roll_lift = lr < 0 ? std::int16_t(-r) : r;
    c.pitch_lift = std::int16_t(front - c.springs[2].force);
    c.on_ground = std::uint8_t((std::uint16_t(c.net_lift) >> 8) | (std::uint16_t(c.net_lift) & 0xFF));
    if (c.on_ground || c.recovery) return;
    // Airborne: a nudge that keeps the nose from dropping (on some tracks only).
    std::int16_t nudge = -0x80;
    if (c.pitch >= 0) {
        if (c.pitch >= 0x1000) nudge = -0x100;
    } else if (c.track_number == 4) {
        nudge = -0x8;
    } else if (c.track_number != 7) {
        return;
    }
    nudge = std::int16_t(nudge - c.pitch_lift);
    if (nudge >= 0) return;
    const auto spin_high = std::uint8_t(std::uint16_t(c.spin_pitch) >> 8);
    if (std::int8_t(spin_high) >= 0 || spin_high == 0xFF) c.pitch_lift = nudge;
}

// Road slope under the car from the springs, and the net lift along the body axes.
void tyre_loads(Car& c, const Tables& t) {
    struct Slope {
        std::uint8_t clamped, half_angle;
    };
    const auto slope = [&](std::int16_t v) {
        int a = v < 0 ? -v : v;
        const int clamped = a < 0x100 ? a & 0xFF : 0xFF;
        return Slope{std::uint8_t(clamped), t.slope_half_angle[std::size_t(clamped >> 1)]};
    };
    const auto pitch_slope = std::int16_t((((c.springs[0].compression + c.springs[1].compression) >> 1) - c.springs[2].compression) >> 4);
    const auto pitch_sign = std::uint16_t(std::uint16_t(pitch_slope) ^ 0x8000);
    const auto ps = slope(pitch_slope);
    const auto roll_slope = std::int16_t((c.springs[0].compression - c.springs[1].compression) >> 3);
    const auto rs = slope(roll_slope);
    const auto fraction = [](std::uint8_t a, std::uint8_t b) { return std::uint8_t(((a * b) >> 8) & 0xFF); };
    const auto side_factor = fraction(ps.half_angle, rs.clamped);
    const auto up_factor = fraction(ps.half_angle, rs.half_angle);
    const auto project = [&](std::uint8_t factor, bool negative) {
        int f = factor;
        if (negative) f = (0 - f) & 0xFFFF;
        f = (f << 7) & 0xFFFF;
        return std::int16_t(((std::int32_t(c.net_lift) * std::int16_t(std::uint16_t(f))) << 1) >> 16);
    };
    c.loads[0] = project(side_factor, std::int8_t(std::uint16_t(roll_slope) >> 8) < 0);
    c.loads[1] = project(up_factor, false);
    c.loads[2] = project(ps.clamped, std::int8_t(pitch_sign >> 8) < 0);
}

// Spawn and crash recovery: level the body's roll, lower the car to its ride
// height, then hand over to normal driving.
void recover(Car& c, Random& random) {
    if (!c.recovery) return;
    const auto level = [&](std::uint8_t amount) {
        std::uint8_t target = 0x10, step = amount;
        if (c.facing_back) step = std::uint8_t(-std::int8_t(step)), target = 0xF0;
        const auto d = damp(std::int16_t(std::uint16_t(step) << 8));
        if (std::uint8_t(std::uint16_t(c.settle_roll) >> 8) != target) c.settle_roll = std::int16_t(c.settle_roll + d);
        c.roll = std::int16_t(c.settle_roll - std::int16_t(c.across_centre << 5));
        c.pitch_lift = 0;
        return std::uint8_t(std::uint16_t(c.settle_roll) >> 8) == target;
    };
    const auto lower = [&](std::uint8_t amount) {
        const auto d3 = std::int16_t(c.height - c.ride_height - std::int16_t(std::uint16_t(amount) << 8));
        auto e = std::int16_t((d3 >> 3) - 0x100);
        if (e < 0 && std::uint16_t(e) < 0xFE00) e = -512;
        c.loads[1] = std::int16_t(c.loads[1] - e);
        return std::int8_t(std::uint8_t(std::uint16_t(d3) >> 8) + 2);
    };
    const auto finish = [&] {
        c.recovery = 0;
        c.crash_state = 0;
        c.launch_flag = false;
        c.launched = true;
    };
    const auto clock = c.recovery;
    if (clock >= 0xE6) {
        c.settle_roll = std::int16_t((c.facing_back ? 0xD4 : 0x2C) << 8);
        --c.recovery;
    } else if (clock == 0xE5) {
        level(0);
        if (lower(3) >= 0) --c.recovery;
    } else if (clock == 0xE4) {
        lower(4);
        if (!level(0xFF)) return;
        // Down: a while before the car may go (a random wait in league races).
        const auto wait = std::uint8_t(0xA0 + (random.next() & 0x1F));
        c.recovery = c.league ? wait : 0x8C;
        c.launch_flag = true;
        c.launch_message_length = 4;
        c.launch_message = c.launched ? 0x3C : 0x2C;
    } else {
        level(0);
        lower(2);
        if (std::int8_t(c.tick) >= 0) {
            --c.recovery;
            if (!c.recovery) ++c.recovery;
        }
        if (!c.launched) {
            if (std::int8_t(c.recovery) >= 0) finish();
            return;
        }
        if (!c.fire_up) finish();
    }
}

void knock(Car& c) {
    if (c.knock_timer) --c.knock_timer;
    if (!c.impulse_pending) return;
    c.impulse_pending = false;
    c.impulse_left = std::int16_t(std::max(0, c.impulse_left - c.impulse[2]));
    const auto i = std::int16_t(c.impulse[1] >> 4);
    for (auto& k : c.knock) k = std::int16_t(k - i);
    for (std::size_t a = 0; a < 3; ++a) c.loads[a] = std::int16_t(c.loads[a] + c.impulse[a]);
    c.impulse = {};
    if (!c.knock_timer) c.knock_timer = 5;
}

std::int16_t grip(const Car& c) { return c.on_ground ? std::int16_t(c.loads[1] << 1) : std::int16_t(0); }

void drive_and_grip(Car& c) {
    c.body_force[1] = std::int16_t(c.gravity[1] + c.loads[1]);
    const auto low_bytes = std::uint8_t((std::uint16_t(c.drive) >> 8) | (std::uint16_t(c.body_velocity[2]) >> 8));
    if (std::int8_t(low_bytes) >= 0 && (c.drive & 0xFF)) c.drive = std::int16_t(c.drive - low_bytes);
    const auto magnitude = std::int16_t(c.drive < 0 ? -c.drive : c.drive);
    const auto g = grip(c);
    if (std::uint16_t(magnitude) >= std::uint16_t(g)) c.drive = c.drive >= 0 ? g : std::int16_t(-g);
    c.body_force[2] = std::int16_t(c.drive + c.loads[2] + c.gravity[2]);
    // Sideways: the tyres hold up to the grip, then slide.
    const auto wanted = std::int16_t(c.gravity[0] + c.loads[0]);
    auto demand = std::int16_t(wanted - c.body_velocity[0]);
    if (demand < 0) demand = std::int16_t(-demand);
    auto limit = grip(c);
    if (std::uint16_t(demand) < std::uint16_t(limit)) {
        c.body_force[0] = std::int16_t(c.loads[0] - c.body_velocity[0]);
        c.sliding = false;
        return;
    }
    if (c.body_velocity[0] < 0) limit = std::int16_t(-limit);
    c.body_force[0] = std::int16_t(wanted - limit);
    c.sliding = true;
}

void force_to_world(Car& c, const Tables& t) {
    for (int k = 2; k >= 0; --k)
        c.force[std::size_t(k)] = std::int16_t(term(c, t, c.body_force[0], k + 0x9) + term(c, t, c.body_force[1], k + 0xC) +
                                               term(c, t, c.body_force[2], k + 0xF));
}

void drag(Car& c) {
    int shift = 1;
    std::int16_t coefficient = firm_drag;
    bool chosen = false;
    if (c.on_ground) {
        auto s = std::uint8_t(std::uint16_t(c.previous_forward_load) >> 8);
        if (std::int8_t(s) < 0) s ^= 0xFF;
        if (s >= 3 || std::int8_t(c.crash_state) < 0) {
            chosen = true;
        } else if (c.stalled) {
            shift = 3;
            chosen = true;
        }
    }
    if (!chosen) {
        if (!c.recovery) {
            const auto mag = [](std::int16_t v) { return std::int16_t(v < 0 ? -v : v); };
            coefficient = std::max({mag(c.body_velocity[0]), mag(c.body_velocity[1]), mag(c.body_velocity[2])});
            shift = 5;
            if (std::int8_t(c.drag_flags) < 0 && std::int8_t(c.opponent_flags) >= 0)
                coefficient = std::uint16_t(coefficient) >= std::uint16_t(drag_dead_zone)
                                  ? std::int16_t(std::uint16_t(coefficient) - drag_dead_zone)
                                  : std::int16_t(0);
        } else {
            shift = 3;
        }
    }
    const auto apply = [&](std::int16_t v, std::int16_t& f) {
        const auto h = std::int16_t(std::int16_t((std::int32_t(v) * coefficient) >> 16) >> shift);
        f = std::int16_t(f - h);
    };
    apply(c.vx, c.force[0]);
    apply(c.vy, c.force[1]);
    apply(c.vz, c.force[2]);
}

void torques(Car& c) {
    auto p = std::int16_t(c.pitch_lift - (c.spin_pitch >> 4));
    if (c.on_ground) p = std::int16_t(p + (c.body_force[2] >> 2));
    c.torque_pitch = p;
    c.torque_roll = std::int16_t(c.roll_lift - (c.spin_roll >> 4));
    c.spin_pitch = std::int16_t(c.spin_pitch + damp(c.torque_pitch));
    c.spin_yaw = std::int16_t(c.spin_yaw + damp(c.torque_yaw));
    c.spin_roll = std::int16_t(c.spin_roll + damp(c.torque_roll));
}

void spin_to_world(Car& c, const Tables& t) {
    for (int k = 1; k >= 0; --k) {
        const auto r = std::int16_t(term(c, t, c.spin_pitch, k + 0x12) + term(c, t, c.spin_yaw, k + 0x14));
        (k == 0 ? c.rate_pitch : c.rate_yaw) = r;
    }
    c.rate_roll = std::int16_t(times(c.rate_yaw, c.matrix[0x4]) + c.spin_roll);
}

void limit_angle(std::int16_t& angle, std::int16_t& spin, std::int16_t positive, std::int16_t negative) {
    std::int16_t limit;
    if (angle >= 0) {
        if (std::uint16_t(positive) >= std::uint16_t(angle)) return;
        limit = positive;
    } else {
        if (std::uint16_t(negative) < std::uint16_t(angle)) return;
        limit = negative;
    }
    angle = limit;
    if ((limit ^ spin) >= 0) spin = 0;
}

void integrate(Car& c, const Tables& t) {
    c.vx = std::int16_t(c.vx + damp(c.force[0]));
    c.vy = std::int16_t(c.vy + damp(c.force[1]));
    c.vz = std::int16_t(c.vz + damp(c.force[2]));
    c.x += std::int32_t(damp(c.vx)) << 6;
    c.y += std::int32_t(damp(c.vy)) << 7;
    c.z += std::int32_t(damp(c.vz)) << 6;
    if (std::int16_t(c.y >> 16) >= height_limit) c.y = (std::int32_t(height_limit) << 16) | (c.y & 0xFFFF);
    c.pitch = std::int16_t(c.pitch + damp(c.rate_pitch));
    c.yaw = std::int16_t(c.yaw + damp(c.rate_yaw));
    c.roll = std::int16_t(c.roll + damp(c.rate_roll));
    const std::size_t set = std::int8_t(c.limit_select) < 0 && c.off_track == 0xE0 ? 1 : 0;
    limit_angle(c.pitch, c.spin_pitch, t.angle_limits[set], t.angle_limits[set + 2]);
    limit_angle(c.roll, c.spin_roll, t.angle_limits[set], t.angle_limits[set + 2]);
    auto high = std::uint8_t(std::uint16_t(c.pitch) >> 8);
    if (std::int8_t(high) < 0) high = std::uint8_t(-std::int8_t(high));
    c.pitch_at_limit = std::int8_t(high) >= 0x0F;
    c.view_pitch = std::int16_t(-c.pitch);
}

} // namespace

void read_controls(Car& c, const Controls& in) {
    if (!c.on_ground) c.steer = 0;
    else if (c.recovery) c.steer = std::int8_t(c.recovery);
    else if (in.left && !in.right) c.steer = std::int8_t(-steer_amount);
    else if (in.right) c.steer = steer_amount;
    else c.steer = 0;
    c.fire_up = !in.fire;

    std::uint8_t low = 0, high = 0;
    const auto forward_high = std::uint8_t(std::uint16_t(c.body_velocity[2]) >> 8);
    const bool airborne = std::int8_t(forward_high) >= 0 && forward_high >= airborne_from;
    if (!airborne && !c.recovery && !c.stalled) {
        bool accelerate = false;
        if (in.accelerate && !in.brake) {
            accelerate = true;
        } else if (in.brake) {
            low = reverse_low, high = reverse_high;
            c.accelerator_latched = false;
        } else if (c.accelerator_latched) {
            accelerate = true;
        }
        if (accelerate) {
            low = c.accelerate_low, high = c.accelerate_high;
            c.accelerator_latched = true;
        }
    }
    c.drive = std::int16_t(high << 8 | low);

    // Boost: fire with the accelerator doubles the drive while boost is left.
    c.boosting = false;
    if (!c.fire_up && !c.stalled && (c.accelerator_latched || in.accelerate || in.brake) && c.boost_left) {
        if (std::int8_t(c.tick) >= 0) {
            --c.boost_timer;
            if (std::int8_t(c.boost_timer) < 0) {
                // One boost spent: BCD + $99, kept within the race's allowance.
                c.boost_timer = c.boost_reload;
                int low = (c.boost_left & 0xF) + 9, high = (c.boost_left >> 4) + 9;
                if (low > 9) low -= 10, ++high;
                if (high > 9) high -= 10;
                const auto left = std::uint8_t(high << 4 | low);
                c.boost_left = left < c.boost_allowance ? left : c.boost_allowance;
            }
        }
        c.boosting = true;
        c.drive = std::int16_t(c.drive << 1);
    }
}

void end_frame(Car& c) {
    c.edge_side = std::uint8_t(c.edge_side << 2); // once per front wheel
    c.near_end = false;                           // the drawing has used it
    ++c.frame_count;
    const int sum = c.tick_phase + 0xEE;
    c.tick_phase = std::uint8_t(sum);
    c.tick = sum > 0xFF ? 0 : 0xFF;
}

void step_physics(Car& c, const Tables& t, TrackContact& track, Random& random) {
    build_matrix(c, t);
    place_points(c);
    track.sample_surface(c);
    contact_heights(c, t);
    velocity_to_body(c, t);
    speed_and_note(c);
    gravity_to_body(c);
    suspension(c);
    tyre_loads(c, t);
    recover(c, random);
    c.previous_forward_load = c.loads[2];
    knock(c);
    // A hard landing thuds (sound 3), at most every fifth frame, louder with
    // the impact.
    if (c.thud_holdoff) --c.thud_holdoff;
    c.thud = false;
    if (c.bottomed) {
        const auto peak = std::uint8_t(std::uint16_t(c.peak_impact) >> 8);
        auto volume = std::uint8_t(std::max(7, int(peak)) << 2); // a byte: large impacts wrap
        c.thud_volume = volume >= 0x40 ? std::uint8_t(0x40) : volume;
        if (!c.thud_holdoff) {
            c.thud = true;
            c.thud_holdoff = 5;
        }
    }
    if (c.armed) {
        drive_and_grip(c);
        track.follow(c, t);
        force_to_world(c, t);
        drag(c);
        torques(c);
        spin_to_world(c, t);
    }
    integrate(c, t);
}

} // namespace stuntcar::race
