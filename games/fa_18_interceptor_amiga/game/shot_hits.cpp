#include "fa_18_interceptor_amiga/game/shot_hits.hpp"
#include "fa_18_interceptor_amiga/data/shapes.hpp"
#include <cstdlib>

namespace interceptor::game {
namespace {

using i16 = std::int16_t;
using i32 = std::int32_t;

constexpr std::uint32_t hit_models = 0x12DC64; // a long per hit model: its faces
constexpr i32 small_box = 0x140;               // `$13AC96`
constexpr i32 large_box = 0x800;               // for models marked $10 at +7

} // namespace

bool ShotHits::inside(const Shot& s, const WorldObject& o) const {
    const auto& m = program_;
    const auto shape = data::read_shape(m, o.shape_type);
    if (!shape.model) return false;
    // +$7B: the drawn model's byte +1 (the port takes the nearest level's):
    // the high nibble less of the scale, the low the hit model; bit 7 none.
    const std::uint8_t hit = m.u8(shape.model + 1);
    if (hit & 0x80) return false;
    int shift = (o.scale & 15) - (hit >> 4);
    if (shift < 0) shift = 0;
    const auto& a = o.craft;
    // The model's points turned by the view matrix / 64, products over 256
    // (`$112006`, as the drawer leaves them at +$A4).
    const auto point = [&](int offset) {
        const std::uint32_t at = shape.model + 10 + std::uint32_t(offset);
        const i32 p[3] = {m.s16(at), m.s16(at + 2), m.s16(at + 4)};
        std::array<i16, 3> out{};
        for (std::size_t r = 0; r < 3; ++r) {
            const i32 sum = p[0] * (a.view[3 * r] >> 6) + p[1] * (a.view[3 * r + 1] >> 6) + p[2] * (a.view[3 * r + 2] >> 6);
            out[r] = i16(sum >> 8);
        }
        return out;
    };
    const i16 sx = i16((s.x & 0x3FFFFF) >> 8), sz = i16((s.z & 0x3FFFFF) >> 8);
    const i32 sy = s.y >> 8;
    std::uint32_t at = m.u32(hit_models + 4u * (hit & 15));
    for (int guard = 0; guard < 64; ++guard, at += 6) {
        std::uint16_t d2 = m.u16(at);
        const std::uint16_t d3 = m.u16(at + 2), d4 = m.u16(at + 4);
        if (d2 & 0x8000) return true; // (past the last face: inside them all)
        const bool cross = d2 & 0x4000;
        d2 &= 0x3FFF;
        const auto p0 = point(d2);
        std::array<i16, 3> n{};
        if (!cross) {
            // ($11B5F2: the normal as the difference of two points.)
            const auto p1 = point(d3), p2 = point(d4);
            n = {i16(p2[0] - p1[0]), i16(p2[1] - p1[1]), i16(p2[2] - p1[2])};
        } else {
            // ($11B60A: the cross product of the two edges, over 16.)
            const auto p1 = point(d3), p2 = point(d4);
            const i32 u[3] = {i16(p1[0] - p0[0]), i16(p1[1] - p0[1]), i16(p1[2] - p0[2])};
            const i32 v[3] = {i16(p2[0] - p0[0]), i16(p2[1] - p0[1]), i16(p2[2] - p0[2])};
            n = {i16((u[1] * v[2] - u[2] * v[1]) >> 4), i16((u[2] * v[0] - u[0] * v[2]) >> 4),
                 i16((u[0] * v[1] - u[1] * v[0]) >> 4)};
        }
        const i16 px = i16((p0[0] >> shift) + i16((a.x & 0x3FFFFF) >> 8));
        const i32 py = (p0[1] >> shift) + (a.y >> 8);
        const i16 pz = i16((p0[2] >> shift) + i16((a.z & 0x3FFFFF) >> 8));
        const i32 dot = i32(n[0]) * i16(sx - px) + i32(n[1]) * i16(sy - py) + i32(n[2]) * i16(sz - pz);
        if (dot < 0) return false;
    }
    return false;
}

bool ShotHits::test(Shot& s, std::vector<WorldObject>& objects, int& cannon_hits) const {
    const auto& m = program_;
    int tested = 0;
    for (auto& o : objects) {
        if (tested >= 6) break;
        auto& a = o.craft;
        if (!o.active() || (a.x >> 22) != (s.x >> 22) || (a.z >> 22) != (s.z >> 22)) continue;
        ++tested;
        // The box: the type record's model marked $10 at +7 (ships) the
        // large one.
        const std::uint32_t base = m.u32(0x115F4C + std::uint32_t(o.shape_type) * 20 + 4);
        std::uint16_t w = m.u16(base);
        if (!(w & 0x8000)) w = m.u16(base + 4);
        const bool marked = m.u8(base + (w & 0x3FFF) + 7) & 0x10;
        const i32 box = marked ? large_box : small_box;
        if (std::abs(i16(i16((a.x & 0x3FFFFF) >> 8) - i16((s.x & 0x3FFFFF) >> 8))) >= box ||
            std::abs((a.y >> 8) - (s.y >> 8)) >= box ||
            std::abs(i16(i16((a.z & 0x3FFFFF) >> 8) - i16((s.z & 0x3FFFFF) >> 8))) >= box)
            continue;
        // Eight eighths of the move ($11B29A); none hit: back where it was.
        const Shot start = s;
        bool hit = false;
        for (int step = 0; step < 8 && !hit; ++step) {
            s.x += s.ex;
            s.y += s.ey;
            s.z += s.ez;
            // (Ships, marked: `$11B4A8`'s hull test - here their box,
            // hypothesis.)
            hit = marked || inside(s, o);
        }
        if (!hit) {
            s.x = start.x;
            s.y = start.y;
            s.z = start.z;
            continue;
        }
        if (marked) return true;
        // $11B30C: the log's cannon hits; the record's third hit crashes it
        // - an aircraft spins down (role 1).
        ++cannon_hits;
        const int damage = (o.damage & 15) + 1;
        if (damage >= 3) {
            a.state |= 0x0200;
            if (a.type != 0x15 && (a.type & 0xF0) != 0) {
                a.handling |= 0x02;
                a.motion |= 0x1000;
                a.role = 1;
            }
        } else {
            o.damage = std::uint8_t((o.damage & 0xF0) | damage);
        }
        return true;
    }
    return false;
}

} // namespace interceptor::game
