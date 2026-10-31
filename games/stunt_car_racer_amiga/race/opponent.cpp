#include "stunt_car_racer_amiga/race/opponent.hpp"
#include <algorithm>
#include <cstdlib>

namespace stuntcar::race {
namespace {

// Tables in the program.
constexpr std::uint32_t skill_table = 0x1FE2C;   // masks, bases (top speed); masks, bases (curves); +$20 super league
constexpr std::uint32_t car_table = 0x1FE6C;     // bytes 4/5: the opponent's push
constexpr std::uint32_t start_table = 0x1F0C2;   // shape kinds a car may start on (bit 7: curves)
constexpr std::uint32_t half_track_table = 0x1EF4A;
constexpr std::uint32_t wobble_table = 0x5BE34;
constexpr int car_entry = 11;

// Track load: 256 draws before the skill is rolled ($63AA2); segment speeds
// start at $7C and rise by $A a segment (backwards round the track) up to
// $7F, drop to the curve speed less $A on curves, and a marked pair value
// marks the next three segments too.
constexpr int skill_warm_up = 256;
constexpr std::uint8_t first_speed = 0x7C, speed_step = 0x0A, marked = 0x80;
constexpr int marked_run = 3;
// The start: four steps into the start segment, lane $4C, the body $68 +
// 0-$7F above the road.
constexpr std::uint16_t start_progress = 0x0400;
constexpr std::uint8_t start_lane = 0x4C;
// Suspension: rest $28 ($7C on curves), compressions no deeper than -$60,
// spring rate $114/256, limits 0..$3FF; the rear wheels at most $128 apart,
// rear and front $170; corners drawn $50 above the wheels.
constexpr std::int16_t rest = 0x28, curve_rest = 0x7C, lowest = -0x60, spring_top = 0x3FF, spring_rate = 0x114;
constexpr std::int16_t rear_span = 0x128, length_span = 0x170, corner_lift = 0x50;
constexpr std::int16_t hop = 0xA0;
// Speed: the push less $19 while bucking, the drag (speed × speed byte >> 6;
// $14 less in the slipstream), the push less the speed (twice on curves, and
// $23 more), the road's slope.
constexpr std::uint8_t bucking_push = 0x19, slipstream = 0x14, curve_extra = 0x23;
constexpr std::uint8_t settle_speed = 0x0E;
// Steering: the lane moves 9 a frame towards its target when $10 or more
// away, within $20..$E0; the lanes it takes.
constexpr std::uint8_t lane_step = 9, lane_near = 0x10, lane_low = 0x20, lane_high = 0xE1;

std::int16_t damp(std::int32_t v) { return std::int16_t((v * 0xEE) >> 8); }
std::int16_t times(int value, int factor) {
    return std::int16_t(((std::int32_t(std::int16_t(value)) * std::int16_t(factor)) << 1) >> 16);
}
int gain(std::uint8_t g) { return (g << 7) & 0x7FFF; }
std::uint8_t high(std::uint16_t v) { return std::uint8_t(v >> 8); }

} // namespace

OpponentDriver::OpponentDriver(const data::Program& program, const data::Track& track, const TrackContact& contact,
                               Driver driver, bool super_league)
    : track_(track), contact_(contact), driver_(driver), super_league_(super_league) {
    for (std::size_t k = 0; k < skills_.size(); ++k) skills_[k] = program.u8(skill_table + std::uint32_t(k));
    for (std::size_t k = 0; k < start_allowed_.size(); ++k) start_allowed_[k] = program.u8(start_table + std::uint32_t(k));
    // (indexed by the body's roll / 16; past its 32 entries the game reads on)
    for (std::size_t k = 0; k < half_tracks_.size(); ++k) half_tracks_[k] = program.u8(half_track_table + std::uint32_t(k));
    for (std::size_t k = 0; k < wobbles_.size(); ++k) wobbles_[k] = program.u8(wobble_table + std::uint32_t(k));
    const auto car = car_table + std::uint32_t(super_league ? car_entry : 0);
    push_ = std::uint16_t(program.u8(car + 5) << 8 | program.u8(car + 4));
    int total = 0;
    for (int s = 0; s < int(track.segments.size()); ++s) {
        start_distance_.push_back(total << 5);
        total += steps(s);
    }
    length_ = total << 5;
}

int OpponentDriver::steps(int segment) const { return contact_.piece(segment).points / 2 - 1; }

bool OpponentDriver::curve(int segment) const {
    return std::int8_t(start_allowed_[std::size_t(track_.segments[std::size_t(segment)].shape)]) < 0;
}

void OpponentDriver::prepare(int track_number, Random& random) {
    for (int k = 0; k < skill_warm_up; ++k) random.next();
    const auto t = std::size_t(track_number + (super_league_ ? 0x20 : 0));
    const auto pick = [&](std::size_t base) {
        const auto r = random.next();
        return std::uint8_t((r & skills_[base + t]) + skills_[base + 8 + t]);
    };
    top_speed_ = pick(0);
    curve_speed_ = pick(0x10);
    // Target speeds, backwards from the last segment: a marked pair value
    // is taken as it is (bit 7 marks it and the next three found); curves
    // drop to the curve speed and the run restarts $A below it; straights
    // rise by $A a segment while that stays below $80.
    const int count = int(track_.segments.size());
    segment_speeds_.assign(std::size_t(count), 0);
    int run = 0;
    std::uint8_t speed = first_speed;
    for (int s = count - 1; s >= 0; --s) {
        bool found = false;
        for (int k = int(track_.segment_marks.size()) - 1; k >= 0; --k) {
            const auto [segment, value] = track_.segment_marks[std::size_t(k)];
            if (segment != s) continue;
            segment_speeds_[std::size_t(s)] = value;
            if (value & marked) run = marked_run;
            speed = value & 0x7F;
            found = true;
            break;
        }
        if (found) continue;
        std::uint8_t value;
        if (curve(s)) {
            speed = std::uint8_t(curve_speed_ - speed_step);
            value = curve_speed_;
        } else {
            const auto up = std::uint8_t(speed + speed_step);
            if (std::int8_t(up) >= 0) speed = up;
            value = speed;
        }
        if (run) --run, value |= marked;
        segment_speeds_[std::size_t(s)] = value;
    }
}

void OpponentDriver::start(Opponent& o, Random& random) const {
    o = Opponent{};
    o.segment = track_.car_start_segment;
    o.progress = start_progress;
    o.lane = start_lane;
    place(o);
    const auto up = std::int16_t((random.next() & 0x7F) + 0x68);
    for (std::size_t k = 0; k < 3; ++k) o.body[k] = std::int16_t(o.road[k] + up);
    o.body[3] = up;
}

// The suspension ($63EE2): three springs from the body to the road; the
// wheels' vertical speeds integrate with the game's damping; the wheels are
// kept within the car's length and width of each other.
void OpponentDriver::suspension(Opponent& o, Random& random) const {
    const auto piece = contact_.piece(o.segment);
    const std::int16_t r = std::int8_t(piece.placement) < 0 ? curve_rest : rest;
    o.deepest = std::int16_t(0x8000);
    std::uint16_t any = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        auto d = std::int16_t(o.road[i] - o.body[i]);
        if (d >= o.deepest) o.deepest = d;
        d = std::int16_t(d + r);
        if (d < lowest) d = lowest;
        const auto length = d;
        auto c = std::int16_t(((std::int32_t(std::int16_t(d - o.previous[i])) * spring_rate) >> 8) + length);
        if (c < 0) c = 0;
        if (c > spring_top) c = spring_top;
        any |= std::uint16_t(c);
        o.compression[i] = std::int16_t(c - r);
        o.previous[i] = length;
    }
    o.on_ground = std::uint8_t((any >> 8) | (any & 0xFF));
    const auto sum = std::int16_t(o.compression[0] + o.compression[1] + o.compression[2]);
    for (std::size_t i = 0; i < 3; ++i)
        o.force[i] = std::int16_t(std::int16_t(sum + o.compression[i] + std::int16_t(o.compression[i] << 2)) >> 3);
    // Some drivers hop: now and then, with the front settled, it jumps.
    if ((driver_.habits & 0x04) && !((std::uint16_t(o.wheel_speed[2]) | std::uint16_t(o.force[2])) & 0xFFFC) &&
        !(random.next() & 0x0F))
        o.wheel_speed[2] = hop;
    for (std::size_t i = 0; i < 3; ++i) {
        o.wheel_speed[i] = std::int16_t(o.wheel_speed[i] + damp(o.force[i]));
        o.body[i] = std::int16_t(o.body[i] + (damp(o.wheel_speed[i]) >> 1));
    }
    // Keep the wheels together: the higher of a pair comes down to the span
    // and the pair's speeds are averaged.
    const auto average = [&](std::size_t a, std::size_t b) {
        const auto v = std::int16_t(std::int16_t(o.wheel_speed[a] + o.wheel_speed[b]) >> 1);
        o.wheel_speed[a] = o.wheel_speed[b] = v;
    };
    std::int16_t first_difference{};
    const auto hold = [&](std::size_t a, std::size_t b, std::int16_t span) {
        const auto difference = std::int16_t(o.body[a] - o.body[b]);
        if (b == 1) first_difference = difference;
        const auto over = std::int16_t(span - std::abs(difference));
        if (over < 0) {
            o.body[difference < 0 ? b : a] = std::int16_t(o.body[difference < 0 ? b : a] + over);
            if (b != 2) {
                average(0, b);
                return;
            }
            average(0, 1);
            average(2, 1);
            average(0, 1);
        }
        // Airborne with a rear wheel not rising faster than the front by
        // $10: it noses down.
        if (b == 2 && !o.on_ground) {
            const auto d = std::int16_t(o.wheel_speed[a] - o.wheel_speed[2]);
            if (d < 0x10) {
                o.wheel_speed[0] = std::int16_t(o.wheel_speed[0] + 4);
                o.wheel_speed[1] = std::int16_t(o.wheel_speed[1] + 4);
                o.wheel_speed[2] = std::int16_t(o.wheel_speed[2] - 4);
            }
        }
    };
    hold(0, 1, rear_span);
    hold(first_difference < 0 ? 1 : 0, 2, length_span);
    const auto left = std::int16_t(o.body[0] + corner_lift), right = std::int16_t(o.body[1] + corner_lift);
    const auto front = std::int16_t(o.body[2] + corner_lift);
    const auto half = std::int16_t(std::int16_t(right - left) >> 1);
    o.lift = {left, right, std::int16_t(front - half), std::int16_t(front + half)};
}

// Rough segments make it buck: for 16 slow-clock frames the rear wheels and
// the lane wobble from a table ($5BD1E).
void OpponentDriver::bumps(Opponent& o, const Car& player, Random& random) const {
    if (!o.on_ground) return;
    const auto piece = contact_.piece(o.segment);
    o.wobble = {};
    o.lane_wobble = 0;
    if (o.bump_timer) {
        const auto timer = o.bump_timer;
        if (std::int8_t(player.tick) >= 0) --o.bump_timer;
        const auto phase = std::size_t((timer + o.bump_phase) & 0x0F);
        auto w = wobbles_[phase];
        std::size_t side = 0;
        if (std::int8_t(w) < 0) w = std::uint8_t(-std::int8_t(w)), side = 1;
        o.wobble[side] = w;
        o.lane_wobble = wobbles_[(phase + 5) & 0x0F];
    } else if (std::int8_t(segment_speeds_[std::size_t(o.segment)]) >= 0 && std::int8_t(player.opponent_flags) >= 0 &&
               std::int8_t(piece.placement) >= 0 && std::int8_t(o.bump_side) < 0) {
        o.bump_phase = (o.bump_side & 0x40) ? 0x10 : 0x08;
        if (driver_.number >= (random.next() & 0x1F)) o.bump_timer = 0x10;
    }
    o.bump_side = std::uint8_t(((piece.reversed ? 0x80 : 0) >> 1) ^ piece.placement);
}

// The push for this frame ($63C04).
void OpponentDriver::push(Opponent& o) const {
    std::uint8_t low = std::uint8_t(push_), up = high(push_);
    if (o.bump_timer) low = std::uint8_t(low - bucking_push);
    if (!o.on_ground) low = up = 0;
    o.acceleration = std::uint16_t(up << 8 | low);
}

// The segment's target speed ($63C3C): over it, it brakes; well under it on
// a marked segment, or after braking, it pushes twice as hard.
void OpponentDriver::aim_speed(Opponent& o) const {
    if (!o.on_ground) return;
    const auto mark = segment_speeds_[std::size_t(o.segment)];
    auto target = mark;
    if (std::int8_t(target) >= 0 && target >= top_speed_) target = top_speed_;
    o.target_speed = target & 0x7F;
    const auto now = high(o.speed);
    const auto difference = std::uint8_t(now - o.target_speed);
    if (now >= o.target_speed) {
        o.braking = true;
        if (!difference) return;
        o.acceleration = std::uint16_t(-std::int16_t(o.acceleration));
        if (difference < settle_speed) return;
    }
    if (!(mark & marked) && std::int8_t(difference) < 0 && o.braking) {
        if (difference >= 0xFE) return;
        o.braking = false;
    }
    o.acceleration = std::uint16_t(o.acceleration << 1);
}

// The speed ($63B08): push less drag, the slope of the road under its wheels.
void OpponentDriver::change_speed(Opponent& o, const Car& player) const {
    std::int16_t drag = 0;
    if (std::int16_t(o.speed) >= 0) {
        auto factor = std::uint8_t(o.speed >> 7);
        if (std::int8_t(player.drag_flags) < 0 && std::int8_t(player.opponent_flags) < 0)
            factor = factor >= slipstream ? std::uint8_t(factor - slipstream) : 0;
        drag = std::int16_t(std::int16_t(high(o.speed) * factor) >> 6);
        if (o.on_ground && std::int16_t(o.acceleration) >= 0) {
            const auto piece = contact_.piece(o.segment);
            auto a = std::int16_t(o.acceleration - high(o.speed));
            if (std::int8_t(piece.placement) < 0) a = std::int16_t(a - high(o.speed) - curve_extra);
            o.acceleration = std::uint16_t(a);
        }
    }
    auto change = std::int16_t(std::int16_t(o.acceleration) - drag);
    if (o.on_ground) {
        const auto slope = std::int16_t((std::uint16_t(o.road[0] + o.road[1]) >> 1) - o.road[2]);
        auto steep = std::int16_t(std::abs(slope));
        if (std::uint16_t(steep) >= 0x200) steep = 0x1FE;
        steep = std::int16_t(steep >> 1);
        auto pull = std::int16_t(steep + (steep >> 2));
        if (slope < 0) pull = std::int16_t(-pull);
        change = std::int16_t(change + pull);
    }
    const auto speed = std::int16_t(std::int16_t(o.speed) + damp(change));
    o.speed = std::uint16_t(speed < 0 ? 0 : speed);
}

// A frame's ride ($63996): once the player's car has been dropped.
void OpponentDriver::drive(Opponent& o, Car& player, Random& random) const {
    if (!player.launched) return;
    suspension(o, random);
    bumps(o, player, random);
    push(o);
    aim_speed(o);
    change_speed(o, player);
    const auto piece = contact_.piece(o.segment);
    const auto along = damp(times(std::int16_t(o.speed), gain(piece.shape->along_gain())));
    const std::int32_t moved = std::int32_t(along) << 3;
    const int carry = o.progress_carry + (moved & 0xFF);
    o.progress_carry = std::uint8_t(carry);
    o.progress = std::uint16_t(o.progress + (moved >> 8) + (carry > 0xFF ? 1 : 0));
    const int n = steps(o.segment);
    if (high(o.progress) >= n) {
        o.progress = std::uint16_t(((high(o.progress) - n) << 8) | (o.progress & 0xFF));
        o.segment = (o.segment + 1) % int(track_.segments.size());
    }
}

// The gap between the cars along the track ($6076C); the player's opponent
// flag tells whether it is ahead the shorter way round.
void OpponentDriver::measure(Opponent& o, Car& player) const {
    auto gap = std::int16_t(std::int16_t(o.progress - std::uint16_t(player.progress)) >> 3);
    gap = std::int16_t(gap + distance_to(o.segment) - distance_to(player.segment));
    o.gap = gap;
    auto sign = std::uint16_t(gap);
    const auto plain = std::uint16_t(std::abs(gap));
    auto shortest = std::uint16_t(length_ - plain);
    if (shortest >= plain) shortest = plain;
    else sign ^= 0x8000;
    o.distance = shortest;
    player.opponent_flags = high(sign);
}

// Steering ($636C0): its lane against the player's, by distance.
void OpponentDriver::steer(Opponent& o, Car& player) const {
    player.drag_flags = 0;
    o.lane_target = std::uint8_t(o.lane);
    auto gap = std::uint8_t(std::uint8_t(o.lane) - player.rear_along);
    std::uint8_t side = 0;
    if (std::uint8_t(o.lane) < player.rear_along) gap = std::uint8_t(-std::int8_t(gap)), side = 0xFF;
    o.lane_gap = gap, o.lane_side = side;
    const auto pass_side = [&] {   // $63942: the side away from the player
        if (o.lane_gap >= 0x38) return;
        const auto lane = player.rear_along;
        if (std::int8_t(o.lane_side) < 0) o.lane_target = lane < 0x60 ? 0xE0 : 0x20;
        else o.lane_target = lane >= 0xA0 ? 0x20 : 0xE0;
    };
    const auto dodge = [&] {       // $63926
        if (o.lane_gap >= 0x38) return;
        o.lane_target = std::int8_t(o.lane_side) < 0 ? 0x20 : 0xE0;
    };
    const auto keep = [&] {        // $6383E: its own lane
        std::uint8_t lane = (driver_.habits & 0x08) ? 0x6E : 0x40;
        if (driver_.number & 1) lane = std::uint8_t(~lane);
        o.lane_target = lane;
    };
    enum { move, look_ahead } then = move;
    const auto near = std::uint8_t(o.distance);
    if (high(o.distance)) {
        keep();
        then = look_ahead;
    } else {
        if (near < 0x40 && (std::int8_t(player.opponent_flags) < 0 || o.lane_gap < 0x32)) --player.drag_flags;
        bool touching = false;
        if (near < 0x10 && o.lane_gap < 0x32 && std::uint16_t(player.across) < 0x180) {
            touch(o, player);
            touching = true;
        } else {
            o.touch_frames = 0;
            o.knocking = false;
        }
        if (touching || near < 0x18) {
            if ((driver_.habits & 0x08) && std::int8_t(player.opponent_flags) >= 0 && near >= 0x0E) {
                keep();
                then = look_ahead;
            } else {
                dodge();
            }
        } else if (std::int8_t(player.opponent_flags) < 0) {
            pass_side();
        } else if (near < 0x32) {
            if (driver_.habits & 0x02) o.lane_target = player.rear_along;
            else pass_side();
            then = look_ahead;
        } else if (near < 0xC8 && (driver_.habits & 0x20)) {
            pass_side();
            then = look_ahead;
        } else {
            keep();
            then = look_ahead;
        }
    }
    // On or before a curve it takes the middle.
    if (then == look_ahead) {
        const int count = int(track_.segments.size());
        for (int k = 0, s = o.segment; k < 2; ++k, s = (s + 1) % count)
            if (curve(s)) o.lane_target = 0x80;
    }
    // Move towards the target (or with the wobble).
    std::uint8_t step;
    const auto w = o.lane_wobble;
    if (std::int8_t(w) < 0) {
        if (w >= 0xF0) return;
        step = std::uint8_t(-lane_step);
    } else if (w) {
        if (w < lane_near) return;
        step = lane_step;
    } else {
        const auto d = std::uint8_t(o.lane_target - std::uint8_t(o.lane));
        if (!d) return;
        if (o.lane_target < std::uint8_t(o.lane)) {
            if (d >= 0xF0) return;
            step = std::uint8_t(-lane_step);
        } else {
            if (d < lane_near) return;
            step = lane_step;
        }
    }
    const auto lane = std::uint8_t(std::uint8_t(o.lane) + step);
    if (!o.on_ground || lane >= lane_high || lane < lane_low) return;
    o.lane = std::uint16_t((o.lane & 0xFF00) | lane);
}

// Touching ($63CE4): a knock for the player's car (sideways, vertical, and
// along by the difference in speed), and damage to its springs.
void OpponentDriver::touch(Opponent& o, Car& player) const {
    if (!player.launched) return;
    if (!o.on_ground || !player.on_ground) {
        const auto d = std::int16_t(player.height - o.body[0]);
        auto apart = std::int16_t(std::abs(std::int16_t(d + 0x28)));
        if (apart >= 0xC0) {
            o.touch_frames = 3;
            return;
        }
        if (o.touch_frames) {
            --o.touch_frames;
            auto k = std::int16_t(0x100 - apart);
            if (d < 0) k = std::int16_t(-k);
            player.impulse[1] = std::int16_t(k << 4);
        }
    }
    if (o.lane_gap < 0x2D && std::uint8_t(o.distance) < 8)
        player.impulse[0] = std::int16_t(((std::int8_t(o.lane_side) < 0 ? 8 : 0xF8) << 8) | (player.impulse[0] & 0xFF));
    if (!o.knocking) {
        std::int16_t bias = 3;
        auto d = std::int16_t(std::int16_t(o.speed) - player.body_velocity[2]);
        if (d < 0) bias = -3;
        player.impulse[2] = std::int16_t((d >> 1) + bias);
    }
    player.impulse_pending = true;
    o.knocking = true;
    int harm = 0x200 + std::abs(player.impulse[0]) + std::abs(player.impulse[1]) + std::abs(player.impulse[2]);
    harm = (harm & 0xFFFF) >> 8;
    for (auto& s : player.springs) s.damage = std::uint8_t(std::min(0xFF, s.damage + harm));
    player.damaged = true;
}

void OpponentDriver::take_knock(Opponent& o, const Car& player) {
    const auto speed = std::int16_t(std::int16_t(o.speed) - player.knock_taken[2]);
    o.speed = std::uint16_t(speed < 0 ? 0 : speed);
    const auto i = std::int16_t(player.knock_taken[1] >> 4);
    for (auto& v : o.wheel_speed) v = std::int16_t(v - i);
}

// Where its wheels are ($5A186): the rear axle $40 behind its place, the
// road heights under the rear wheels (either side of its lane) and under
// the front, half a step on.
void OpponentDriver::place(Opponent& o) const {
    const int count = int(track_.segments.size());
    int segment = o.segment;
    auto at = std::int16_t(o.progress - 0x40);
    if (at < 0) {
        segment = (segment + count - 1) % count;
        at = std::int16_t(at + (steps(segment) << 8));
    }
    o.rear_segment = segment;
    o.rear_progress = std::uint16_t(at);
    auto piece = contact_.piece(segment);
    const int point = (high(o.rear_progress) << 1) & 0xFF;
    std::array<std::int16_t, 6> h{};
    for (int n = 0; n < 4; ++n) h[std::size_t(n)] = piece.point_height((point + n) & 0xFF);
    if (point + 4 >= piece.points) {
        const auto next = contact_.piece((segment + 1) % count);
        h[4] = next.point_height(2), h[5] = next.point_height(3);
    } else {
        h[4] = piece.point_height((point + 4) & 0xFF), h[5] = piece.point_height((point + 5) & 0xFF);
    }
    o.half_track = half_tracks_[std::size_t(std::uint16_t(std::abs(std::int16_t(o.body[0] - o.body[1]))) >> 4)];
    o.rough = false;
    const auto road_at = [&](std::size_t i, std::uint16_t lane, std::uint8_t fraction) {
        o.road[i] = std::int16_t(interpolate_road(h[0], h[1], h[2], h[3], std::uint8_t(lane), fraction) >> 3);
    };
    const auto fraction = std::uint8_t(o.rear_progress);
    road_at(0, std::uint16_t(o.lane - o.half_track), fraction);
    road_at(1, std::uint16_t(o.lane + o.half_track), fraction);
    if (std::abs(std::int16_t(h[0] - h[2])) >= 0x14) o.rough = true;
    const int front = fraction + 0x80;
    if (front > 0xFF) h[0] = h[2], h[1] = h[3], h[2] = h[4], h[3] = h[5];
    road_at(2, o.lane, std::uint8_t(front));
    if (std::abs(std::int16_t(h[0] - h[2])) >= 0x14) o.rough = true;
}

} // namespace stuntcar::race
