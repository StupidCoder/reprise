#include "stunt_car_racer_amiga/race/placement.hpp"

namespace stuntcar::race {
namespace {

constexpr std::uint16_t eighth = 0x2000, quarter_mask = 0xC000;
constexpr std::int32_t world_span = 0x8000000; // 16 cells of $800000
constexpr std::int16_t half_cell = 0x800;
// Diagonal pieces: across = (x - z) * $B5/256 (about 1/sqrt 2).
constexpr std::uint8_t diagonal_gain = 0xB5;
// Over the edge: past $C0 from the centre line the car is at the road's side,
// past $100 it has gone over.
constexpr std::int16_t road_half = 0xC0, gone_over = 0x100;

std::int16_t times(int value, int factor) {
    return std::int16_t(((std::int32_t(std::int16_t(value)) * std::int16_t(factor)) << 1) >> 16);
}
int gain(std::uint8_t g) { return (g << 7) & 0x7FFF; }
std::uint8_t high(std::int16_t v) { return std::uint8_t(std::uint16_t(v) >> 8); }

// The camera: the quadrant the world is turned by, the car's cell and place
// in it, its position in the turned frame, and the eye height.
void camera(Car& c) {
    c.quadrant = std::uint16_t((std::uint16_t(c.yaw) + eighth) & quarter_mask);
    // The game nudges an exact zero fraction to 1.
    if (std::uint16_t(c.x) == 0) c.x |= 1;
    if (std::uint16_t(c.z) == 0) c.z |= 1;
    const auto hx = std::uint16_t(std::uint32_t(c.x) >> 15), hz = std::uint16_t(std::uint32_t(c.z) >> 15);
    c.cell_x = std::uint8_t(hx >> 8), c.fine_x = std::uint8_t(hx);
    c.cell_z = std::uint8_t(hz >> 8), c.fine_z = std::uint8_t(hz);
    switch (c.quadrant >> 14) {
    case 0: c.turned_x = c.x, c.turned_z = c.z; break;
    case 1: c.turned_z = c.x, c.turned_x = world_span - c.z; break;
    case 2: c.turned_x = world_span - c.x, c.turned_z = world_span - c.z; break;
    default: c.turned_z = world_span - c.x, c.turned_x = c.z; break;
    }
    c.height = std::int16_t(std::uint32_t(c.y) >> 11);
    std::int16_t lift = c.net_lift, base = 0x780;
    if (std::uint16_t(lift) >= 0x500) lift = std::int16_t(lift << 1), base = 0x280;
    lift = std::int16_t(lift + base);
    if (c.pitch < 0) lift = std::int16_t(lift - (c.pitch >> 1));
    c.eye = std::int16_t((lift >> 4) + c.height);
    c.corner_x = std::int16_t(-std::int16_t(std::uint16_t(std::uint32_t(c.turned_x) >> 12) & 0x7FF));
    c.corner_z = std::int16_t(-std::int16_t(std::uint16_t(std::uint32_t(c.turned_z) >> 12) & 0x7FF));
    c.view_yaw = std::int16_t(((std::uint16_t(c.yaw) + eighth) & 0x3FFE) - eighth);
}

// The segment under the car, or the one across the nearest cell edge when
// the car's cell has none.
int find_segment(const Car& c, const TrackContact& track) {
    if (c.cell_z >= 16 || c.cell_x >= 16) return -1;
    int s = track.segment_at(c.cell_x, c.cell_z);
    if (s >= 0) return s;
    const std::uint8_t fx = c.fine_x, fz = c.fine_z;
    int x = c.cell_x, z = c.cell_z;
    const bool sum_carries = fx + fz > 0xFF;
    if (fz < fx) {
        if (sum_carries) {
            if (x == 15) return -1;
            ++x;
        } else {
            if (z == 0) return -1;
            --z;
        }
    } else {
        if (sum_carries) {
            if (z == 15) return -1;
            ++z;
        } else {
            if (x == 0) return -1;
            --x;
        }
    }
    return track.segment_at(x, z);
}

struct Placing {
    Car& c;
    const Tables& t;
    const TrackContact& track;
    Piece piece;
    std::uint16_t frame{};   // the piece's heading in the camera's frame (bits 15-14)
    std::int16_t base_x{}, base_z{};

    void set_up(int segment) {
        piece = track.piece(segment);
        int dx = std::uint8_t(piece.layout->cell_x - c.cell_x), dz = std::uint8_t(piece.layout->cell_z - c.cell_z);
        switch (c.quadrant >> 14) {
        case 0: break;
        case 1: std::swap(dx, dz), dx = std::uint8_t(-dx); break;
        case 2: dx = std::uint8_t(-dx), dz = std::uint8_t(-dz); break;
        default: std::swap(dx, dz), dz = std::uint8_t(-dz); break;
        }
        // The cell's corner relative to the car (high byte: cells << 3).
        base_x = std::int16_t(std::uint16_t(std::uint8_t((dx << 3) + high(c.corner_x)) << 8) | std::uint8_t(c.corner_x));
        base_z = std::int16_t(std::uint16_t(std::uint8_t((dz << 3) + high(c.corner_z)) << 8) | std::uint8_t(c.corner_z));
        frame = std::uint16_t(c.quadrant - piece.heading);
    }

    // The car relative to the cell, in the piece's frame.
    std::pair<std::int16_t, std::int16_t> car_in_cell() const {
        switch (frame >> 14) {
        case 0: return {std::int16_t(-base_x), std::int16_t(-base_z)};
        case 1: return {std::int16_t(-base_z), std::int16_t(half_cell + base_x)};
        case 2: return {std::int16_t(half_cell + base_x), std::int16_t(half_cell + base_z)};
        default: return {std::int16_t(half_cell + base_z), std::int16_t(-base_x)};
        }
    }

    // A point of the shape (figures at byte `offset`) relative to the car.
    std::pair<std::int16_t, std::int16_t> shape_point(int offset) const {
        const auto u = std::uint16_t(piece.shape->figure(offset)), v = std::uint16_t(piece.shape->figure(offset + 2));
        const auto bx = std::uint16_t(base_x), bz = std::uint16_t(base_z);
        switch (frame >> 14) {
        case 0: return {std::int16_t(bx + u), std::int16_t(bz + v)};
        case 1: return {std::int16_t(bx - v + half_cell), std::int16_t(bz + u)};
        case 2: return {std::int16_t(bx - u + half_cell), std::int16_t(bz - v + half_cell)};
        default: return {std::int16_t(bx + v), std::int16_t(bz - u + half_cell)};
        }
    }

    void place() {
        const auto& shape = *piece.shape;
        if (std::int8_t(piece.placement) < 0) {
            place_on_arc();
        } else if (piece.placement & 0x40) {
            const auto [x, z] = car_in_cell();
            c.across = std::int16_t(times(x - z, gain(diagonal_gain)) - shape.figure(2));
            c.along = times(x + z, gain(shape.figure_byte(7)));
            c.heading = std::int16_t(shape.figure(4) + piece.heading);
        } else {
            const auto [x, z] = car_in_cell();
            c.across = std::int16_t(x - shape.figure(2));
            c.along = z;
            c.heading = std::int16_t(piece.heading);
        }
    }

    // Curves: the car's angle around the curve's centre gives its progress,
    // its distance from the centre its place across the road.
    void place_on_arc() {
        const auto& shape = *piece.shape;
        const auto [x, z] = shape_point(2);
        const auto p = polar(t, x, z);
        std::int16_t radius = p.length;
        const auto angle = std::int16_t(std::uint16_t(p.angle) + c.quadrant + 0x8000);
        const auto mirror = std::uint16_t(piece.mirrored ? 0x8000 : 0);
        c.heading = std::int16_t(std::uint16_t(angle) + 0x4000 - mirror);
        const int shift = std::int8_t(1 - std::int8_t(piece.placement & 3));
        const auto start = std::int16_t(std::uint16_t(shape.figure(6)) << 6);
        auto sweep = std::int16_t(std::uint16_t(angle) - std::uint16_t(start) - piece.heading);
        if (sweep < 0) sweep = std::int16_t(-sweep);
        auto along = times(sweep, gain(shape.figure_byte(8)));
        along = shift < 0 ? std::int16_t(std::uint16_t(along) << ((-shift) & 7)) : std::int16_t(std::uint16_t(along) >> (shift & 7));
        c.along = along;
        // Past the curve's end onto a type-4 piece: place on that one instead.
        if (std::int8_t(std::uint8_t(std::uint16_t(along) >> 7) + 2) >= std::int8_t(piece.points)) {
            const int kind = piece.layout->shape;
            if (kind == 1 || kind == 3) {
                const int n = track.segment_count();
                const int next = piece.reversed ? (piece.segment + n - 1) % n : (piece.segment + 1) % n;
                if (track.piece(next).layout->shape == 4) {
                    segment = next;
                    set_up(next);
                    place();
                    return;
                }
            }
        }
        // Refine the radius towards the true distance.
        const auto square = [](std::int16_t v) { return std::int32_t(v) * v; };
        const auto error = std::int16_t(std::uint32_t(square(x) + square(z) - square(radius)) >> 8);
        const auto step = std::int16_t(times(error, gain(t.radius_gain[std::size_t(piece.layout->shape)])) >> 4);
        radius = std::int16_t(std::uint16_t(radius) + std::uint16_t(step));
        auto across = std::int16_t(std::uint16_t(shape.figure(9)) - std::uint16_t(radius));
        if (piece.mirrored) across = std::int16_t(-across);
        c.across = across;
    }

    int segment{};
};

} // namespace

void place(Car& c, const Tables& t, const TrackContact& track) {
    camera(c);
    const int s = find_segment(c, track);
    c.off_grid = s < 0;
    if (c.off_grid) {
        c.crash_state = 0xC0;
        return;
    }
    Placing p{c, t, track, {}};
    p.segment = s;
    p.set_up(s);
    p.place();
    const int segment = p.segment;
    const auto& piece = p.piece;

    // At the road's side, or over its edge (a crash).
    c.off_road_side = 0;
    auto offset = std::int16_t(c.across - road_half);
    if (piece.reversed) offset = std::int16_t(-offset);
    c.across_centre = offset;
    const auto distance = std::int16_t(offset < 0 ? -offset : offset);
    if (distance >= road_half) c.off_road_side = offset < 0 ? 0x80 : 0x02;
    if (distance >= gone_over) {
        if (std::int8_t(c.crash_state) >= 0) {
            c.crash_state = 0x80;
            c.facing_back = std::int8_t(high(offset)) < 0;
            c.crash_started = true;
        }
    } else if (!(c.crash_state & 0x40)) {
        c.crash_state = 0;
        c.limit_select = 0;
    }
    c.segment = segment;

    // Progress in driving direction.
    const auto end = std::int16_t((piece.points / 2 - 1) << 8);
    auto progress = c.along;
    if (piece.reversed) progress = std::int16_t(end - progress);
    c.progress = progress;
    c.step_index = high(progress);
    c.near_end = std::int16_t(progress + 0x40) >= end;
}

} // namespace stuntcar::race
