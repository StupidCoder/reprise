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
    turned_ = model_;
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

void Carrier::place(const Aircraft& ship, const FlightTables& tables) {
    (void)tables;
    position = {ship.x, ship.y, ship.z};
    attitude = ship.attitude;
    body = ship.body;
    view_ = ship.view;
    std::array<int, 9> v{};
    for (std::size_t i = 0; i < 9; ++i) v[i] = std::int16_t(ship.view[i]) >> 6;
    for (std::size_t k = 0; k < model_.size(); ++k) {
        const auto& p = model_[k];
        for (std::size_t i = 0; i < 3; ++i) {
            const std::int32_t sum = p[0] * v[3 * i] + p[1] * v[3 * i + 1] + p[2] * v[3 * i + 2];
            turned_[k][i] = std::int16_t(sum >> 8);
        }
    }
}

std::array<double, 2> Carrier::point(int index) const {
    if (index < 0 || index >= int(turned_.size())) return {0, 0};
    return {double(std::int16_t(turned_[std::size_t(index)][0]) >> shift_),
            double(std::int16_t(turned_[std::size_t(index)][2]) >> shift_)};
}

Aircraft Carrier::start_on_deck(const amiga::LoadedImage& program, const FlightTables& tables) const {
    Aircraft a = start_at_place(program, tables, 0);
    // $10D750: on the deck ($C8 in byte +4), at the deck's height, with the
    // carrier's angles, at its place plus (11, 0, 104) turned by its view
    // matrix (products over 16).
    a.attitude = attitude;
    a.body = body_matrix(tables, a.attitude);
    a.view = view_matrix(tables, a.attitude);
    a.x = position[0] + ((11 * body[0] + 0 * body[3] + 104 * body[6]) >> 4);
    a.z = position[2] + ((11 * body[2] + 0 * body[5] + 104 * body[8]) >> 4);
    a.y = (deck_height << 8) + 0x708;
    a.surface = 0xC8;
    a.deck_height = std::int16_t(deck_height);
    return a;
}

bool Carrier::touch(Aircraft& a, const Aircraft& before) const {
    using i16 = std::int16_t;
    a.surface &= 0x3F; // ($11B6C0)
    // Everything in fine positions (the position within its cell, / 256)
    // and words, as the original compares them.
    const i16 cx = i16((position[0] & 0x3FFFFF) >> 8), cz = i16((position[2] & 0x3FFFFF) >> 8);
    const auto fine = [&](int index) {
        const auto& p = turned_[std::size_t(index)];
        return std::array<i16, 3>{i16((i16(p[0]) >> shift_) + cx), i16(i16(p[1]) >> shift_), i16((i16(p[2]) >> shift_) + cz)};
    };
    // The aircraft's two test points: its model's points 0 (the nose) and
    // 3 (under the tail), turned as the drawer turns them (by its view
    // matrix / 64, products over 256) and shifted by its scale nibble.
    static constexpr int nose[3] = {0, -10, 384}, tail[3] = {32, 0, -184};
    const auto turned = [&](const int* p) {
        std::array<std::int32_t, 3> out{};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& v = before.view;
            const std::int32_t sum = p[0] * (v[3 * i] >> 6) + p[1] * (v[3 * i + 1] >> 6) + p[2] * (v[3 * i + 2] >> 6);
            out[i] = i16(i16(sum >> 8) >> 3); // (the F/A-18's scale nibble 3)
        }
        return std::array<std::int32_t, 3>{i16(i16((before.x & 0x3FFFFF) >> 8) + out[0]), (before.y >> 8) + out[1],
                                           i16(i16((before.z & 0x3FFFFF) >> 8) + out[2])};
    };
    // The cross product's sign of the edge p0 -> p1 (unshifted) with the
    // way from p0 (shifted, placed) to the point: below 0 is inside.
    const auto side = [&](int from, int to, i16 px, i16 pz) {
        const auto& p0 = turned_[std::size_t(from)];
        const auto& p1 = turned_[std::size_t(to)];
        const i16 ex = i16(p1[0] - p0[0]), ez = i16(-(p1[2] - p0[2]));
        const auto f0 = fine(from);
        return std::int32_t(ez) * i16(-(f0[0] - px)) + std::int32_t(ex) * i16(-(f0[2] - pz));
    };
    const auto inside = [&](i16 px, i16 pz) {
        for (const auto& outline : deck_) {
            bool in = true;
            for (std::size_t i = 0; i < outline.size() && in; ++i) {
                if (side(outline[i], outline[(i + 1) % outline.size()], px, pz) >= 0) in = false;
            }
            if (in) return true;
        }
        return false;
    };
    const int deck_y = deck_.empty() ? deck_height : fine(deck_.front().front())[1];
    bool hull = false;
    const std::array<std::array<std::int32_t, 3>, 2> test{turned(nose), turned(tail)};
    for (int k = 0; k < 2; ++k) {
        const auto& p = test[std::size_t(k)];
        const i16 px = i16(p[0]), pz = i16(p[2]);
        if (i16(p[1]) < deck_y) {
            // Below the deck inside its outline: the hull (hypothesis: the
            // original tests the hull's faces, `$12E314`).
            if (inside(px, pz)) hull = i16(p[1]) < deck_y - 4;
            continue;
        }
        if (!inside(px, pz)) continue;
        if (k == 1) {
            // $11BA3E: the wires' zone, on the ground only: between the
            // lines through C - A and C - B (points 56, 54, 55) and the
            // line through D = A + (A - C) / 2 parallel to C - B, and the
            // aircraft heading along the deck.
            bool zone = false;
            if (a.motion & flag::on_ground) {
                const int A = wire_points[0], B = wire_points[1], C = wire_points[2];
                const auto& ta = turned_[std::size_t(A)];
                const auto& tc = turned_[std::size_t(C)];
                const auto& tb = turned_[std::size_t(B)];
                const auto fa = fine(A), fb = fine(B), fc = fine(C);
                (void)fa;
                i16 ex = i16(ta[0] - tc[0]), ez = i16(-(ta[2] - tc[2]));
                const auto cross = [&](i16 qx, i16 qz) {
                    return std::int32_t(ez) * i16(qx - px) + std::int32_t(ex) * i16(qz - pz);
                };
                zone = cross(fc[0], fc[2]) >= 0 && cross(fb[0], fb[2]) < 0;
                if (zone) {
                    ex = i16(tb[0] - tc[0]);
                    ez = i16(-(tb[2] - tc[2]));
                    const i16 dx = i16(ta[0] + (i16(ta[0] - tc[0]) >> 1)), dz = i16(ta[2] + (i16(ta[2] - tc[2]) >> 1));
                    const i16 fdx = i16((dx >> shift_) + cx), fdz = i16((dz >> shift_) + cz);
                    zone = cross(fc[0], fc[2]) < 0 && cross(fdx, fdz) >= 0;
                }
                if (zone) {
                    const std::int64_t along = std::int64_t(before.view[2]) * view_[2] + std::int64_t(before.view[8]) * view_[8];
                    zone = along >= heading_along;
                }
            }
            if (zone) a.motion |= 0x4000;
            else if ((a.motion & 0xC000) != 0xC000) a.motion &= ~0x4000;
        }
        a.surface |= std::uint8_t(k == 0 ? 0x40 : 0x80);
        a.deck_height = std::int16_t(deck_y);
    }
    return hull;
}

} // namespace interceptor::game
