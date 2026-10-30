#include "stunt_car_racer_amiga/race/smoke.hpp"
#include <algorithm>

namespace stuntcar::race {
namespace {

// A particle below row $80 of the view (or right of column $100) is gone
// and parked at row $D2; clearing parks them all at $D4.
constexpr std::int16_t bottom = 0x80, right = 0x100, spark_right = 0xFE, parked = 0xD2, cleared = 0xD4;
constexpr std::int16_t gravity = 2;
// Puffs: their size by (particle + frame clock) & 15, and how far left of
// the particle each size is drawn ($60FAC, $60F9C).
constexpr std::array<int, 16> puff_sizes{3, 6, 7, 2, 1, 5, 0, 4, 0, 5, 1, 2, 7, 6, 2, 7};
constexpr std::array<int, 8> puff_offsets{0x20, 0x20, 0x20, 0x28, 0x18, 0x20, 0x20, 0x20};
// View pixels to the screen buffer's: puffs are placed in screen pixels.
constexpr int view_x = 0x20, view_y = 0x10;

std::uint8_t speed_byte(const Car& c) { return std::uint8_t(std::uint16_t(c.speed) >> 8); }

} // namespace

void Smoke::clear() {
    for (auto& p : particles) p.y = cleared;
}

void Smoke::step(const Car& c, Random& random, std::uint8_t frame_clock) {
    sparks.clear();
    puffs.clear();
    const bool crashed = std::int8_t(c.crash_state) < 0;
    // Over the edge and the crane not yet there: smoke from the wreck, at
    // most as strong as speed $10. (The draw sets the hiss's pitch.)
    if (!c.recovery && crashed) {
        const auto strength = std::int8_t(speed_byte(c)) < 0x10 ? speed_byte(c) : std::uint8_t(0x10);
        random.next();
        run(c, random, 16, strength, true, frame_clock);
    }
    // Scraping along the road's edge, or the engine stalled: dust and sparks,
    // stronger with speed (at most $32).
    if (!c.edge_side && !c.stalled) return;
    if (crashed) return;
    if (std::int8_t(speed_byte(c)) < 1) return;
    const auto strength = std::min<std::uint8_t>(speed_byte(c), 0x32);
    random.next(); // the scrape's sound
    run(c, random, 32, strength, false, frame_clock);
}

void Smoke::run(const Car& c, Random& random, int count, std::uint8_t strength, bool crashed, std::uint8_t frame_clock) {
    if (!c.on_ground) {
        clear();
        return;
    }
    // Draw each particle where it is, then move it.
    for (int i = count - 1; i >= 0; --i) {
        auto& p = particles[std::size_t(i)];
        if (!draw(p, i, crashed, frame_clock)) continue;
        p.vy = std::int16_t(p.vy + gravity);
        p.y = std::int16_t(p.y + p.vy);
        p.x = std::int16_t(p.x + p.vx);
    }
    // Throw up the ones that have fallen out of sight again.
    for (int i = count - 1; i >= 0; --i) {
        auto& p = particles[std::size_t(i)];
        if (std::uint16_t(p.y) < std::uint16_t(bottom)) continue;
        int up = strength >> (crashed ? 1 : 2);
        up += random.next() & 7;
        p.vy = std::int16_t(~up);
        if (crashed) {
            p.x = random.next();
            p.y = std::int16_t(0x76 + (random.next() & 7));
        } else {
            p.x = std::int16_t(0x40 + (random.next() & 0x7F));
            p.y = std::int16_t(0x77 + (random.next() & 7));
        }
        p.vx = std::int16_t((p.x - 0x80) >> 3);
        draw(p, i, crashed, frame_clock);
    }
}

bool Smoke::draw(Particle& p, int index, bool crashed, std::uint8_t frame_clock) {
    const auto kill = [&] {
        p.y = parked;
        return false;
    };
    if (std::uint16_t(p.y) >= std::uint16_t(bottom) || std::uint16_t(p.x) >= std::uint16_t(right) || p.y < 1) return kill();
    if (crashed) {
        const int size = puff_sizes[std::size_t((index + frame_clock) & 15)];
        puffs.push_back({p.x - puff_offsets[std::size_t(size)] + view_x, p.y + view_y, size});
        return true;
    }
    if (std::uint16_t(p.x) >= std::uint16_t(spark_right)) return kill();
    sparks.push_back({p.x, p.y});
    return true;
}

} // namespace stuntcar::race
