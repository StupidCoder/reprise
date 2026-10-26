// The marble's shine: eight points on the ball that turn as it rolls, drawn
// as small yellow dots on its sprite ($115DFA, $115D68, $10694C).
//
// Two vectors on the unit sphere (14-bit fixed point) are kept per marble;
// every update they turn about the horizontal axis across the direction of
// motion by an angle proportional to the speed (Rodrigues' rotation with
// table sine and cosine), one of them is brought back to unit length every
// eighth update, and eight points (each vector, its negative and the
// diagonals between them) are projected to the sprite.
#include "marble_madness_amiga/game/race.hpp"

namespace marble_madness::game {
namespace {

constexpr std::uint32_t square_root_table = 0x102000; // 65 words, by value >> 10
constexpr std::uint32_t cosine_table = 0x101F4C;      // words, by angle >> 6 (angle $1922 = pi / 2)
constexpr std::uint32_t start_x = 0x116555;           // 8 bytes: the points' first x and y
constexpr std::uint32_t start_y = 0x11655D;

std::int16_t s16(std::int32_t v) { return std::int16_t(v); }
std::int32_t muls(std::int32_t a, std::int32_t b) { return std::int32_t(s16(a)) * std::int32_t(s16(b)); }

// $115CC8: square root by interpolation in a table.
std::int16_t square_root(const amiga::LoadedImage& p, std::uint16_t v) {
    const std::uint32_t at = square_root_table + std::uint32_t(v >> 10) * 2;
    const std::int16_t lo = p.s16(at), hi = p.s16(at + 2);
    return s16(lo + (muls(v & 0x3FF, s16(hi - lo)) >> 10));
}

// $115D1C: cosine by interpolation.
std::int16_t cosine(const amiga::LoadedImage& p, std::int16_t angle) {
    const std::uint32_t at = cosine_table + std::uint32_t(angle >> 6) * 2;
    const std::int16_t lo = p.s16(at), hi = p.s16(at + 2);
    return s16(lo + (muls(angle & 0x3F, s16(hi - lo)) >> 6));
}

std::uint32_t divide(std::uint32_t a, std::uint32_t b) { return a == 0 || b == 0 ? 0 : a / b; } // $124898

} // namespace

// $115D68: the points' starting places.
void Race::reset_shine(Marble& m) {
    const auto& p = files_->program();
    m.shine_phase = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        m.shine_x[i] = s16(p.s8(start_x + std::uint32_t(i)) << 11);
        m.shine_y[i] = s16(p.s8(start_y + std::uint32_t(i)) << 11);
        m.shine_z[i] = 0;
    }
}

void Race::rotate_shine(Marble& m) {
    const auto& p = files_->program();
    std::int16_t a = 0, b = 0; // the velocity / 16
    if (m.region_code != 10) {
        a = s16(m.vx >> 4);
        b = s16(m.vy >> 4);
    }
    auto ua = std::uint16_t(a < 0 ? -a : a), ub = std::uint16_t(b < 0 ? -b : b);
    bool turn = true;
    if (ua == 0 && ub == 0) {
        if (course_ != data::Course::silly) {
            turn = false; // still: the points stay
        } else {
            a = b = 0x10; // on Silly the ball always turns
            ua = ub = 0x10;
        }
    }
    if (turn) {
        // The axis (dx, dy) across the motion, and the turn's size.
        std::int16_t dx, dy;
        std::uint16_t length;
        if (ua == 0 || ub == 0) {
            dx = ua != 0 ? 0x4000 : 0;
            dy = ua != 0 ? 0 : 0x4000;
            length = ua != 0 ? ua : ub;
        } else {
            const auto t = std::uint16_t(ua > ub ? divide(std::uint32_t(ub) << 14, ua) : divide(std::uint32_t(ua) << 14, ub));
            const auto s = std::uint16_t(((std::uint32_t(t) * t) >> 14) + 0x4000);
            const auto r = std::uint16_t(square_root(p, s));
            const auto q = std::uint16_t(divide(0x10000000, r));
            const auto other = std::uint16_t((std::uint32_t(t) * q) >> 14);
            dx = s16(q);
            dy = s16(other);
            if (ua <= ub) {
                dy = s16(q);
                dx = s16(other);
            }
            length = std::uint16_t((std::uint32_t(r) * (ua > ub ? ua : ub)) >> 14);
        }
        if (a >= 0) {
            dx = s16(-dx);
        }
        if (b < 0) {
            dy = s16(-dy);
        }
        std::int16_t angle = s16(std::int32_t(length / 7));
        if (course_ == data::Course::silly) {
            angle = s16(0x600 - angle);
            if (angle < 0) {
                angle = 0;
            }
        }
        const std::int16_t c = cosine(p, angle);
        const std::int16_t s = cosine(p, s16(0x1922 - angle));
        // The rotation matrix.
        const std::int16_t dx2 = s16(muls(dx, dx) >> 14), dy2 = s16(muls(dy, dy) >> 14);
        const std::int16_t r00 = s16((std::int32_t(dx2) * c >> 14) + dy2);
        const std::int16_t k = s16(muls(s16(0x4000 - c), dy) >> 14);
        const std::int16_t r01 = s16(muls(k, dx) >> 14);
        const std::int16_t r02 = s16(-muls(dx, s) >> 14);
        const std::int16_t r11 = s16(dx2 + (std::int32_t(dy2) * c >> 14));
        const std::int16_t r12 = s16(muls(dy, s) >> 14);
        const std::int16_t r20 = s16(-r02), r21 = s16(-r12), r22 = c;
        for (std::size_t i = 0; i < 2; ++i) {
            const std::int16_t x = m.shine_x[i], y = m.shine_y[i], z = m.shine_z[i];
            std::int16_t nx = s16((muls(r00, x) + muls(r01, y) + muls(r02, z)) >> 14);
            std::int16_t ny = s16((muls(r01, x) + muls(r11, y) + muls(r12, z)) >> 14);
            std::int16_t nz = s16((muls(r20, x) + muls(r21, y) + muls(r22, z)) >> 14);
            if (std::size_t(std::int8_t(m.shine_phase)) == i) {
                const auto length2 = std::uint32_t(muls(nx, nx) + muls(ny, ny) + muls(nz, nz));
                if (length2 < 0x1F80000 || length2 > 0x2080000) {
                    const auto f = square_root(p, std::uint16_t(divide(0x2000000, std::uint16_t(length2 >> 14))));
                    nx = s16(muls(nx, f) >> 14);
                    ny = s16(muls(ny, f) >> 14);
                    nz = s16(muls(nz, f) >> 14);
                }
            }
            m.shine_x[i] = nx;
            m.shine_y[i] = ny;
            m.shine_z[i] = nz;
        }
        for (auto* row : {&m.shine_x, &m.shine_y, &m.shine_z}) {
            auto& v = *row;
            const std::int16_t v0 = v[0], v1 = v[1];
            v[2] = s16(-v0);
            v[3] = s16(-v1);
            v[4] = s16(v1 + v0);
            v[5] = s16(-v0 - v1);
            v[6] = s16(v0 - v1);
            v[7] = s16(-v0 + v1);
        }
        if (++m.shine_phase == 8) {
            m.shine_phase = 0;
        }
    }
    // $1163C8: project the points onto the sprite, two per dot record. A
    // dizzy marble's points squash or stretch with its animation.
    int squash = 0;
    if (m.mode == Marble::dizzy) {
        const auto& md = files_->marble_data();
        const std::uint32_t frame = md.u32(m.dizzy_animation);
        if (frame == md.u32(marble_data_ + 0x1C)) {
            squash = 1;
        } else if (frame == md.u32(marble_data_ + 0x20)) {
            squash = 2;
        }
    }
    std::array<std::int16_t, 8> px{}, py{};
    for (std::size_t i = 0; i < 8; ++i) {
        const std::int16_t x = m.shine_x[i], y = m.shine_y[i];
        std::int16_t z = m.shine_z[i];
        if (squash == 1) {
            z = s16(z - (z >> 2));
        } else if (squash == 2) {
            z = s16((z >> 1) + z);
        }
        px[i] = s16((s16(s16(y - x) >> 10) + 0xE) >> 1);
        py[i] = s16((s16(s16(z - s16(s16(y + x) >> 1)) >> 10) + 0x10) >> 1);
    }
    for (std::size_t j = 0; j < 4; ++j) {
        m.shine_dots[j] = {px[2 * j + 1], s16(0x10 - py[2 * j + 1]), px[2 * j], s16(0x10 - py[2 * j])};
    }
}

} // namespace marble_madness::game
