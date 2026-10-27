#include "cannon_fodder_amiga/game/camera.hpp"
#include "amiga/adf.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace fodder::game {
namespace {

// Program addresses in the $80000 layout -> offsets in the file (the body
// starts 26 bytes in; tools/flat.py).
constexpr std::size_t file_offset(std::uint32_t address) {
    return address - 0x80000 + 0x1a;
}

constexpr std::uint32_t distance_table = 0xa128c;
constexpr std::uint32_t sine_table = 0xa168e;
constexpr std::uint32_t angle_table = 0xa1890;

} // namespace

GeometryTables::GeometryTables(std::span<const std::uint8_t> program) {
    if (program.size() < file_offset(angle_table) + 2048) {
        throw std::runtime_error("The game program is shorter than expected.");
    }
    for (std::size_t i = 0; i < distance_.size(); ++i) {
        distance_[i] = program[file_offset(distance_table) + i];
    }
    for (std::size_t i = 0; i < sine_.size(); ++i) {
        sine_[i] = std::int16_t(amiga::read_be16(program, file_offset(sine_table) + 2 * i));
    }
    for (std::size_t i = 0; i < angle_.size(); ++i) {
        angle_[i] = std::int16_t(amiga::read_be16(program, file_offset(angle_table) + 2 * i));
    }
}

int GeometryTables::distance(int dx, int dy) const {
    dx = std::abs(dx);
    dy = std::abs(dy);
    if (dx >= 320 || dy >= 320) {
        return 1000;
    }
    int shift = 0;
    while (dx > 31 || dy > 31) {
        dx >>= 1;
        dy >>= 1;
        ++shift;
    }
    return distance_[std::size_t(dx | dy << 5)] << shift;
}

std::optional<int> GeometryTables::direction(int dx, int dy, int top_bit) const {
    const bool left = dx < 0, up = dy < 0;
    // The routines work on 16-bit words.
    unsigned x = unsigned(std::abs(dx)) & 0xffff, y = unsigned(std::abs(dy)) & 0xffff;
    int bit = top_bit;
    while (bit >= 0 && !(x >> bit & 1) && !(y >> bit & 1)) {
        --bit;
    }
    if (bit - 4 >= 0) {
        x >>= bit - 4;
        y >>= bit - 4;
    }
    // Only the low five bits of each enter the index (asl #5 / or).
    const int octant = angle_[(x | y << 5) & 0x3ff];
    if (octant < 0) {
        return std::nullopt;
    }
    const int a = octant * 2;
    if (!left) {
        return up ? (0x80 + a) & 0x1fe : (0x80 - a) & 0x1fe;
    }
    return up ? (0x180 - a) & 0x1fe : (0x180 + a) & 0x1fe;
}

std::optional<Point> look_ahead_target(const GeometryTables& tables, Point leader, Point pointer_on_map,
                                       bool short_reach) {
    const Point origin{leader.x + 24, leader.y};
    const int dx = pointer_on_map.x - origin.x, dy = pointer_on_map.y - origin.y;
    const int cap = short_reach ? 100 : 140;
    const int reach = std::min(tables.distance(dx, dy), cap);
    const auto angle = tables.direction(dx, dy, 9);
    if (!angle) {
        return std::nullopt;
    }
    // muls.w then swap: the high word of the 32-bit product (arithmetic).
    const int step_x = (tables.sine(*angle) * reach) >> 16;
    const int step_y = (tables.sine(*angle + 0x80) * reach) >> 16;
    return Point{std::max(0, origin.x + step_x - 24), origin.y + step_y};
}

FollowAxis follow_x(FollowAxis state, int offset) {
    if (state.active && offset > 0x90 && offset < 0xa2) {
        state = {};
    }
    if (offset < -15) {
        state = {true, -2};
    }
    if (offset > 0x119) {
        state = {true, 2};
    }
    return state;
}

FollowAxis follow_y(FollowAxis state, int offset) {
    if (state.active && offset > 100 && offset < 0x8f) {
        state = {};
    }
    if (offset < -11) {
        state = {true, -2};
    }
    if (offset > 0xfe) {
        state = {true, 2};
    }
    return state;
}

namespace {

// One chase update ($8741A): aim at target - (144, 112), kept >= 0. Within
// 16 pixels the speed is the distance itself, otherwise it grows by 1/2 up
// to the maximum; the velocity is the direction's sine/4 times the whole
// speed, so 1/8 of the speed in pixels per frame.
void chase(CameraMotion& m, const GeometryTables& tables, Point target) {
    const int aim_x = std::max(0, target.x - 0x90), aim_y = std::max(0, target.y - 0x70);
    const int dx = aim_x - std::int16_t(m.x >> 16), dy = aim_y - std::int16_t(m.y >> 16);
    const int distance = tables.distance(dx, dy);
    if (distance <= 16) {
        m.speed = distance << 16;
    } else if (std::int16_t(m.speed >> 16) >= m.max_speed) {
        m.speed = std::int32_t(std::uint32_t(m.max_speed) << 16 | (std::uint32_t(m.speed) & 0xffff));
    } else {
        m.speed += 0x8000;
    }
    const auto angle = tables.direction(dx, dy, 14);
    if (!angle) {
        m.speed = std::max(0, m.speed - 0x8000);
        return;
    }
    if (m.speed != 0) {
        const int whole = std::int16_t(m.speed >> 16);
        m.velocity_x = (tables.sine(*angle) >> 2) * whole;
        m.velocity_y = (tables.sine(*angle + 0x80) >> 2) * whole;
    }
}

// The reversal hold for one axis: a change of direction (zero counts as
// positive) restarts it at 9; while it runs the axis does not move.
std::int32_t hold(std::int32_t velocity, int& sign, int& counter) {
    const int now = velocity < 0 ? -1 : 1;
    if (now != sign) {
        counter = 9;
        sign = now;
    }
    return counter != 0 ? 0 : velocity;
}

} // namespace

RingPosition ring_position(const CameraMotion& m) {
    const int x = m.x >> 16, y = m.y >> 16;
    return {x % 320, (y + x / 320) % 256};
}

void tile_drawer_tick(CameraMotion& m) {
    const auto ring = ring_position(m);
    const auto update = [](int sign, int& direction, int ring_now, int& drawer_ring, int& hold) {
        const int now = sign < 0 ? -1 : 1;
        bool restart = now == direction;
        if (!restart) {
            direction = now;
            restart = ring_now >> 4 == drawer_ring >> 4;
        }
        if (restart) {
            hold = 0;
            drawer_ring = ring_now;
        }
    };
    update(m.sign_x, m.drawer_direction_x, ring.x, m.drawer_ring_x, m.hold_x);
    update(m.sign_y, m.drawer_direction_y, ring.line, m.drawer_ring_line, m.hold_y);
}

void step_camera(CameraMotion& m, const GeometryTables& tables, const CameraFrame& frame) {
    std::int32_t vx = m.follow_x_axis.active ? 0 : m.velocity_x;
    std::int32_t vy = m.follow_y_axis.active ? 0 : m.velocity_y;
    std::int32_t limit = frame.tick_overdue ? 0 : 0x20000;
    if (frame.frozen) { // $86808: steps, velocities and the limit to 0 (the flags stay)
        m.follow_x_axis.step = m.follow_y_axis.step = 0;
        vx = vy = limit = 0;
    }
    vx += m.follow_x_axis.step * 0x10000;
    vy += m.follow_y_axis.step * 0x10000;
    vx = hold(vx, m.sign_x, m.hold_x);
    vy = hold(vy, m.sign_y, m.hold_y);
    // Limits in the original's word arithmetic: (size - 305) and
    // (size - 225) as unsigned words in the high half.
    const auto top_x = std::int32_t(std::uint32_t(std::uint16_t(frame.map_width - 0x131)) << 16);
    const auto top_y = std::int32_t(std::uint32_t(std::uint16_t(frame.map_height - 0xe1)) << 16);
    if (top_x < vx + m.x) vx = top_x - m.x;
    if (top_y < vy + m.y) vy = top_y - m.y;
    vx = std::clamp(vx, -limit, limit);
    vy = std::clamp(vy, -limit, limit);
    m.x = std::max(0, m.x + vx);
    m.y = std::max(0, m.y + vy);
    m.velocity_x = m.velocity_y = 0;
    chase(m, tables, frame.target);
    chase(m, tables, frame.target);
}

} // namespace fodder::game
