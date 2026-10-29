#include "fa_18_interceptor_amiga/game/carrier.hpp"
#include "fa_18_interceptor_amiga/data/shapes.hpp"
#include <cmath>

namespace interceptor::game {
namespace {

constexpr std::uint32_t free_flight_objects = 0x11E61C; // ($11DFB2's first list)
constexpr std::uint32_t paths = 0x11DDC0;
constexpr std::uint32_t deck_lists = 0x12E298;          // the carrier's ($12EF74 for others)
constexpr int wire_points[3] = {54, 55, 56};            // +$1E8, +$1EE, +$1F4 of the rotated points ($11BA18;
                                                        // other ships' are 44-46)
constexpr std::int64_t heading_along = 0xE000000;       // the wire's direction test

} // namespace

Carrier::Carrier(const amiga::LoadedImage& m) {
    // $11D318: the entry's path gives the place: coarse cells x and z
    // (<< 22), offsets (<< 8), height (<< 8).
    const std::uint32_t entry = free_flight_objects;
    shape_type = m.u16(entry) / 20;
    const std::uint32_t path = paths + std::uint32_t(m.s16(paths + m.u16(entry + 6)));
    position = {(m.s16(path) << 22) + (m.s16(path + 4) << 8), m.s16(path + 8) << 8,
                (m.s16(path + 2) << 22) + (m.s16(path + 6) << 8)};
    // The deck height: the type's data block, the long at +2 (bit 31 a flag).
    const std::uint32_t block = m.u32(0x115F4C + std::uint32_t(shape_type) * 20 + 16);
    deck_height = int(m.u32(block + 2) & 0x7FFFFFFF);
    // The model the rotated points come from: the nearest level's.
    const auto shape = data::read_shape(m, shape_type);
    shift_ = m.u8(shape.model + 6) & 15;
    const int count = m.u8(shape.model + 8);
    for (int i = 0; i < count; ++i) {
        const std::uint32_t at = shape.model + 10 + std::uint32_t(6 * i);
        model_.push_back({m.s16(at), m.s16(at + 2), m.s16(at + 4)});
    }
    // The deck: from the first list's outline on, point offsets (6 bytes a
    // point), each outline's last with bit 15, until $FFFF ($11B98C).
    std::uint32_t at = m.u32(deck_lists);
    std::vector<int> outline;
    for (int guard = 0; guard < 128; ++guard) {
        const int w = m.u16(at);
        at += 2;
        if (w == 0xFFFF) break;
        outline.push_back((w & 0xFFF) / 6);
        if ((w & 0x8000) && outline.size() > 1) {
            deck_.push_back(std::move(outline));
            outline.clear();
        }
    }
}

std::array<double, 2> Carrier::point(int index) const {
    if (index < 0 || index >= int(model_.size())) return {0, 0};
    // (At rest with heading 0 its rotated points are the model's own.)
    return {double(model_[std::size_t(index)][0] >> shift_), double(model_[std::size_t(index)][2] >> shift_)};
}

Aircraft Carrier::start_on_deck(const amiga::LoadedImage& program, const FlightTables& tables) const {
    Aircraft a = start_at_place(program, tables, 0);
    // $10D750: on the deck ($C8 in byte +4), at the deck's height, with the
    // carrier's angles, at its place plus (11, 0, 104) turned by its view
    // matrix (products over 16).
    a.attitude = {0, 0, 0};
    a.body = body_matrix(tables, a.attitude);
    a.view = view_matrix(tables, a.attitude);
    a.x = position[0] + ((11 * body[0] + 0 * body[3] + 104 * body[6]) >> 4);
    a.z = position[2] + ((11 * body[2] + 0 * body[5] + 104 * body[8]) >> 4);
    a.y = (deck_height << 8) + 0x708;
    a.surface = 0xC8;
    a.deck_height = std::int16_t(deck_height);
    return a;
}

bool Carrier::touch(Aircraft& a, const amiga::LoadedImage& program) const {
    (void)program;
    a.surface &= 0x3F; // ($11B6C0)
    const double cx = position[0] / 256.0, cz = position[2] / 256.0;
    // The aircraft's two test points: its model's points 0 (the nose) and
    // 3 (under the tail), turned by its body, in world units.
    static constexpr int nose[3] = {0, -10, 384}, tail[3] = {32, 0, -184};
    const auto turned = [&](const int* p) {
        const double s = 1.0 / 8; // (the F/A-18's scale nibble 3)
        const double x = p[0] * s, y = p[1] * s, z = p[2] * s;
        const auto& b = a.body;
        return std::array<double, 3>{a.x / 256.0 + (b[0] * x + b[3] * y + b[6] * z) / 16384.0,
                                     a.y / 256.0 + (b[1] * x + b[4] * y + b[7] * z) / 16384.0,
                                     a.z / 256.0 + (b[2] * x + b[5] * y + b[8] * z) / 16384.0};
    };
    const auto inside = [&](double px, double pz) {
        // Inside one of the outlines: its edges all see the point on the
        // same side.
        for (const auto& outline : deck_) {
            int sign = 0;
            bool in = true;
            for (std::size_t i = 0; i < outline.size() && in; ++i) {
                const auto p0 = point(outline[i]), p1 = point(outline[(i + 1) % outline.size()]);
                const double e = (p1[0] - p0[0]) * (pz - cz - p0[1]) - (p1[1] - p0[1]) * (px - cx - p0[0]);
                const int s = e > 0 ? 1 : e < 0 ? -1 : 0;
                if (s == 0) continue;
                if (sign == 0) sign = s;
                else if (s != sign) in = false;
            }
            if (in) return true;
        }
        return false;
    };
    bool hull = false;
    const std::array<std::array<double, 3>, 2> test{turned(nose), turned(tail)};
    for (int k = 0; k < 2; ++k) {
        const auto& p = test[std::size_t(k)];
        if (!inside(p[0], p[2])) continue;
        if (p[1] < deck_height) {
            // Below the deck inside its outline: the hull (hypothesis: the
            // original tests the hull's faces, `$12E314`).
            hull = p[1] < deck_height - 4;
            continue;
        }
        a.surface |= std::uint8_t(k == 0 ? 0x40 : 0x80);
        a.deck_height = std::int16_t(deck_height);
        if (k == 1 && (a.motion & flag::on_ground)) {
            // $11BA3E: the wires' zone, between the points 44-46 and
            // half a side further, and the aircraft heading along the deck.
            const auto A = point(wire_points[0]), B = point(wire_points[1]), C = point(wire_points[2]);
            const double px = p[0] - cx, pz = p[2] - cz;
            const double ax = A[0] - C[0], az = A[1] - C[1];
            const double bx = B[0] - C[0], bz = B[1] - C[1];
            // D: half of A - C beyond A, in the model's units, then scaled.
            const auto& ma = model_[std::size_t(wire_points[0])];
            const auto& mc = model_[std::size_t(wire_points[2])];
            const double dx = double((ma[0] + ((ma[0] - mc[0]) >> 1)) >> shift_);
            const double dz = double((ma[2] + ((ma[2] - mc[2]) >> 1)) >> shift_);
            const bool zone = ax * (C[1] - pz) - az * (C[0] - px) >= 0 && ax * (B[1] - pz) - az * (B[0] - px) < 0 &&
                              bx * (C[1] - pz) - bz * (C[0] - px) < 0 && bx * (dz - pz) - bz * (dx - px) >= 0;
            const std::int64_t along = std::int64_t(a.view[2]) * body[2] + std::int64_t(a.view[8]) * body[8];
            if (zone && along >= heading_along) a.motion |= 0x4000;
            else if ((a.motion & 0xC000) != 0xC000) a.motion &= ~0x4000;
        }
    }
    return hull;
}

} // namespace interceptor::game
