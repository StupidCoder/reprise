#include "stunt_car_racer_amiga/race/contact.hpp"
#include <algorithm>

namespace stuntcar::race {
namespace {

// The road is $180 wide in the across units the placement uses; a point within
// $30 beyond the edge tapers the surface down, further out it falls away.
constexpr int road_width = 0x180, edge_taper = 0x30;
constexpr std::int32_t fallen = 0x1000;
// Heading errors beyond $1E00 turn the car by the excess; steering is not
// applied while the error's high byte is $1E or more.
constexpr int large_error = 0x1E00;
constexpr std::uint8_t large_error_high = 0x1E;

std::int16_t times(int value, int factor) {
    return std::int16_t(((std::int32_t(std::int16_t(value)) * std::int16_t(factor)) << 1) >> 16);
}
int gain(std::uint8_t g) { return (g << 7) & 0x7FFF; }
std::uint8_t high(std::int16_t v) { return std::uint8_t(std::uint16_t(v) >> 8); }
std::uint8_t low(std::int16_t v) { return std::uint8_t(v); }

// One frame's sampling state for one car.
struct Sampler {
    const TrackContact& contact;
    Car& car;
    Piece piece;
    std::uint8_t lateral{};      // 0-255 across the strip
    std::int16_t longitudinal{}; // high byte: cross-section, low byte: fraction
    int point{};                 // first of the four corner points
    bool past_edge{};
    std::int16_t edge_offset{};

    void enter(int segment) { piece = contact.piece(segment); }

    // Crossing into the next or previous piece along the strip.
    void cross() {
        const int count = contact.segment_count();
        bool from_end;
        const bool forwards = std::int8_t(high(longitudinal) ^ (piece.reversed ? 0x80 : 0)) >= 0;
        enter(forwards ? (piece.segment + 1) % count : (piece.segment + count - 1) % count);
        from_end = forwards ? piece.reversed : !piece.reversed;
        if (from_end) {
            point = (piece.points - 4) & 0xFF;
            if (std::int8_t(high(longitudinal)) < 0) return;
        } else {
            point = 0;
            if (std::int8_t(high(longitudinal)) >= 0) return;
        }
        const auto flip = [](std::uint8_t v) { return std::uint8_t(v ? 256 - v : 0xFF); };
        longitudinal = std::int16_t((longitudinal & 0xFF00) | flip(low(longitudinal)));
        lateral = flip(lateral);
    }

    std::int32_t interpolate(std::int16_t near_left, std::int16_t near_right, std::int16_t far_left, std::int16_t far_right) const {
        return interpolate_road(near_left, near_right, far_left, far_right, lateral, low(longitudinal));
    }
};

} // namespace

std::int32_t interpolate_road(std::int16_t near_left, std::int16_t near_right, std::int16_t far_left, std::int16_t far_right,
                              std::uint8_t lateral, std::uint8_t fraction) {
    const std::int32_t t = lateral;
    const std::int32_t near = (near_right - near_left) * t + (std::int32_t(near_left) << 8);
    std::int32_t d = (far_right - far_left) * t + (std::int32_t(far_left) << 8);
    const auto f = std::uint32_t(fraction);
    d -= near;
    const bool big = (d < 0 ? -d : d) >= 0x8000;
    if (big) d >>= 3;
    std::int32_t p;
    if (std::int16_t(d) < 0) {
        const auto w = std::uint16_t(-std::int16_t(d));
        p = -std::int32_t((std::uint32_t(w) * f) & ~0xFFu);
    } else {
        p = std::int32_t(std::uint32_t(std::uint16_t(std::int16_t(d))) * f);
    }
    if (big) p <<= 3;
    return (p >> 8) + near;
}

std::int16_t Piece::point_height(int point) const {
    const int step = point >> 1;
    const bool wide = layout->wide_profiles;
    const auto base = (point & 1) ? layout->right_base : layout->left_base;
    const auto& profile = (point & 1) ? *right : *left;
    return std::int16_t(std::int16_t(std::uint16_t(profile.offset(step, wide) + base)) >> 5);
}

TrackContact::TrackContact(const data::Circuits& circuits, const data::Track& track) : circuits_(circuits), track_(track) {
    grid_.fill(-1);
    for (std::size_t s = 0; s < track.segments.size(); ++s)
        grid_[std::size_t(track.segments[s].cell_z * 16 + track.segments[s].cell_x)] = std::int16_t(s);
}

int TrackContact::segment_at(int cell_x, int cell_z) const {
    if (cell_x < 0 || cell_x > 15 || cell_z < 0 || cell_z > 15) return -1;
    return grid_[std::size_t(cell_z * 16 + cell_x)];
}

Piece TrackContact::piece(int segment) const {
    Piece p;
    p.segment = segment;
    p.layout = &track_.segments.at(std::size_t(segment));
    p.shape = &circuits_.shapes.at(std::size_t(p.layout->shape));
    p.left = &circuits_.profiles.at(std::size_t(p.layout->left_profile));
    p.right = &circuits_.profiles.at(std::size_t(p.layout->right_profile));
    p.heading = std::uint16_t(p.layout->heading << 14);
    p.reversed = p.layout->reversed;
    p.points = int(p.shape->steps.size()) * 2;
    p.mirrored = p.shape->mirrored();
    p.placement = p.shape->placement();
    return p;
}

void TrackContact::sample_surface(Car& car) {
    Sampler s{*this, car, piece(car.segment)};
    car.off_track = 0;
    for (int k = 2; k >= 0; --k) {
        if (s.piece.segment != car.segment) s.enter(car.segment);
        // Across: the point's side offset from the piece's centre line.
        const auto across = std::int16_t((car.contact_offsets[std::size_t(k)] >> 4) + car.across);
        int t;
        s.past_edge = false;
        if (std::uint16_t(across) >= road_width) {
            s.past_edge = true;
            s.edge_offset = across;
            t = across < 0 ? 0 : 0xFF;
        } else {
            t = times(across < 0 ? -across : across, gain(s.piece.shape->across_gain()));
            // A lateral fraction of 1 or more reads the byte at address $FF,
            // which holds 0 (the game means #$FF).
            if (t >= 0x100) t = 0;
        }
        // The sample uses the fraction as it is (the corner heights are taken
        // in reverse on reversed pieces); the rear wheel's copy is flipped.
        s.lateral = std::uint8_t(t);
        if (k == 2) car.rear_along = std::uint8_t(s.piece.reversed ? t ^ 0xFF : t);
        // Along: the point's offset forwards plus the car's progress.
        s.longitudinal = std::int16_t(times(car.contact_offsets[std::size_t(k + 3)] >> 3, gain(s.piece.shape->along_gain())) +
                                      car.along);
        s.point = (high(s.longitudinal) << 1) & 0xFF;
        if (std::int8_t(s.point) < 0 || std::int8_t(s.point) >= std::int8_t(s.piece.points - 2)) s.cross();
        std::int16_t h[4];
        if (s.piece.reversed) {
            const int v = (s.piece.points - s.point - 4) & 0xFF;
            for (int n = 0; n < 4; ++n) h[3 - n] = s.piece.point_height((v + n) & 0xFF);
        } else {
            for (int n = 0; n < 4; ++n) h[n] = s.piece.point_height((s.point + n) & 0xFF);
        }
        std::int32_t surface = s.interpolate(h[0], h[1], h[2], h[3]);
        if (s.past_edge) {
            const int v = s.edge_offset;
            int over = v < 0 ? -v : std::abs(road_width - v);
            const auto fall = [&] {
                surface = fallen;
                car.off_track = std::uint8_t((car.off_track >> 1) | 0x80);
            };
            if (over > edge_taper) {
                fall();
            } else {
                const auto tapered = surface - std::int32_t((over & 0xFF) << 4) - 0x100;
                if (tapered < fallen) {
                    fall();
                } else {
                    surface = tapered;
                    car.edge_side = ((high(s.edge_offset) ^ (s.piece.reversed ? 0x80 : 0)) & 0x80) ? 0x80 : 0x40;
                }
            }
        }
        // Fast or pitched: take the sample; otherwise smooth it with the last.
        auto& spring = car.springs[std::size_t(k)];
        auto pitch_high = high(car.pitch);
        if (std::int8_t(pitch_high) < 0) pitch_high = std::uint8_t(-std::int8_t(pitch_high));
        if (std::int8_t(high(car.speed)) >= 0x0A || std::int8_t(pitch_high) > 5) {
            spring.surface = surface;
        } else {
            const auto a = std::uint32_t(spring.surface), b = std::uint32_t(surface);
            const auto sum = a + b;
            spring.surface = std::int32_t(((sum < a) ? 0x80000000u : 0u) | (sum >> 1));
        }
    }
}

void TrackContact::follow(Car& car, const Tables& tables) {
    Piece p = piece(car.segment);
    const std::uint16_t raised = p.reversed ? 0x8000 : 0;
    // The car's heading against the piece's (flipped on reversed pieces), with
    // the piece's bias.
    int d4 = std::int16_t(std::uint16_t(car.heading - car.yaw) ^ raised);
    std::size_t bias = 0;
    if (std::int8_t(p.placement) < 0) {
        bias = 1;
        if (std::int16_t(std::uint16_t(p.mirrored ? 0x8000 : 0) ^ raised) < 0) bias = 2;
    }
    d4 = std::int16_t(d4 + tables.heading_bias[bias]);
    int magnitude = std::int16_t(d4) < 0 ? -std::int16_t(d4) : std::int16_t(d4);
    car.heading_error = std::int16_t(magnitude);
    const auto signed_error = std::int16_t(d4);
    car.heading_error_scaled = std::uint16_t(magnitude) >= 0x800 ? std::int16_t(0x7FFF) : std::int16_t(magnitude << 4);
    if (std::uint8_t(p.points / 2 - 1 - car.step_index) < 2) p = piece((car.segment + 1) % segment_count());
    car.curve_side = std::uint8_t((p.mirrored ? 0x80 : 0) ^ (p.reversed ? 0x80 : 0));
    const auto steer_gain = p.shape->steer_gain();

    int turn = 0;
    int torque = 0;
    const auto apply_turn = [&] {
        if (std::int8_t(high(signed_error)) < 0) turn = std::int16_t(-turn);
        car.yaw = std::int16_t(car.yaw + turn);
    };
    const auto finish = [&](bool turned) {
        if (turned) apply_turn();
        torque = std::int16_t(torque - car.spin_yaw);
        // The disk's protection check passes on a genuine disk (the port).
        if (!car.on_ground) torque = 0;
        car.torque_yaw = std::int16_t(torque);
    };
    // Steering: a torque from the joystick, scaled by the forward speed.
    const auto steer = [&](std::uint8_t g) {
        int v = times(car.body_velocity[2], gain(g));
        if (std::int8_t(car.steer) < 0) v = std::int16_t(-v);
        torque = std::int16_t(v) >> 3;
        return high(car.heading_error) < large_error_high;
    };
    // Following: turn towards the piece's heading, by the excess for a large
    // error, else in proportion to the error and the speed.
    const auto follow_road = [&] {
        car.steer = std::int8_t(high(signed_error));
        int factor = car.heading_error & 0xFF;
        if (high(car.heading_error)) {
            const int excess = std::int16_t(car.heading_error - large_error);
            if (excess >= 0) {
                turn = excess;
                finish(true);
                return;
            }
            factor = 0xFF;
        }
        int v = std::int16_t(car.body_velocity[2] < 0 ? -car.body_velocity[2] : car.body_velocity[2]);
        v = std::int16_t(v + 0xA00);
        if (std::int16_t(v) < 0) v = 0x7F00;
        v = times(v, gain(std::uint8_t(factor)));
        v = std::uint16_t(v) >> 7;
        if (std::uint8_t(v) == 0) ++v;
        turn = v;
        finish(true);
    };

    if (car.steer == 0) {
        torque = 0;
        if (std::int8_t(p.placement) < 0) {
            car.steer = std::int8_t(car.curve_side);
            if (steer(steer_gain)) finish(false);
            else follow_road();
            return;
        }
        follow_road();
        return;
    }
    const auto against = std::uint8_t(std::uint8_t(car.steer) ^ high(signed_error));
    std::uint8_t g;
    if (std::int8_t(p.placement) < 0) {
        if (std::int8_t(std::uint8_t(car.steer) ^ car.curve_side) < 0) {
            car.steer = std::int8_t(car.curve_side);
            g = std::uint8_t(steer_gain - 0x23);
            if (steer(g)) finish(false);
            else follow_road();
            return;
        }
        g = std::uint8_t(steer_gain + 0x2D);
    } else {
        g = steer_gain;
    }
    if (std::int8_t(against) >= 0) g = std::uint8_t(g + high(car.heading_error_scaled));
    if (steer(g)) finish(false);
    else follow_road();
}

} // namespace stuntcar::race
