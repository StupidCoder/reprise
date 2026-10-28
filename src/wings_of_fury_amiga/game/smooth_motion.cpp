#include "wings_of_fury_amiga/game/smooth_motion.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace wings::game {
namespace {

// Further than this in one tick is a jump (a new object in a used slot, a
// plane made ready, a camera reset), not motion.
constexpr int reach = 64;

// A pixel value (world x, height) a fraction of the way back from `now` to
// `then`; positions are 16-bit words to the game, so the difference is too.
int between(int then, int now, double t) {
    const int d = std::int16_t(now - then);
    if (std::abs(d) > reach) return now;
    return now - int(std::lround(d * (1 - t)));
}

// Whether a value moved forward by at most `most` in the tick.
bool drifting(int then, int now, int most) {
    const int d = std::int16_t(now - then);
    return d >= 0 && d <= most;
}

// The same for a 16.16 position.
std::int32_t between_fixed(std::int32_t then, std::int32_t now, double t) {
    const std::int64_t d = std::int64_t(now) - then;
    if (std::llabs(d) > std::int64_t(reach) << 16) return now;
    return std::int32_t(now - std::llround(double(d) * (1 - t)));
}

} // namespace

void place_between(FlightWorld& world, PlayerFlight& plane, const FlightWorld& before, const PlayerFlight& was,
                   double t) {
    if (t >= 1 || plane.eighth_scale != was.eighth_scale) return; // (the 1/8 view switched: a new scale)
    plane.x = between(was.x, plane.x, t);
    plane.altitude = between(was.altitude, plane.altitude, t);
    plane.sea = between(was.sea, plane.sea, t);
    world.lift = between(before.lift, world.lift, t);

    for (std::size_t i = 0; i < world.ships.size(); ++i) {
        auto& s = world.ships[i];
        const auto& b = before.ships[i];
        if (s.present != b.present || s.first != b.first) continue;
        s.bob = between(b.bob, s.bob, t);
        auto& planes = s.planes.planes;
        if (planes.size() != b.planes.planes.size()) continue;
        for (std::size_t p = 0; p < planes.size(); ++p) {
            if (planes[p].flag != b.planes.planes[p].flag) continue;
            planes[p].x = between(b.planes.planes[p].x, planes[p].x, t);
            planes[p].altitude = between(b.planes.planes[p].altitude, planes[p].altitude, t);
        }
    }

    for (std::size_t i = 0; i < world.enemies.planes.size(); ++i) {
        auto& o = world.enemies.planes[i];
        const auto& b = before.enemies.planes[i];
        if (o.state == 0 || b.state == 0) continue;
        o.x = between(b.x, o.x, t);
        o.altitude = between(b.altitude, o.altitude, t);
    }
    for (std::size_t i = 0; i < world.enemies.airfields.size(); ++i) {
        auto& a = world.enemies.airfields[i];
        const auto& b = before.enemies.airfields[i];
        if (a.shown_takeoff != 0 && b.shown_takeoff != 0) a.shown_takeoff = between(b.shown_takeoff, a.shown_takeoff, t);
    }

    // The soldiers (and the guns, which stay): the same list in the same
    // order when nothing came or went.
    auto& sprites = world.islands.sprites;
    if (sprites.size() == before.islands.sprites.size()) {
        for (std::size_t i = 0; i < sprites.size(); ++i) {
            const auto& b = before.islands.sprites[i];
            if (sprites[i].gun != b.gun) continue;
            sprites[i].x = between(b.x, sprites[i].x, t);
            sprites[i].y = between(b.y, sprites[i].y, t);
        }
    }

    auto& e = world.effects;
    for (std::size_t i = 0; i < e.objects.size(); ++i) {
        auto& o = e.objects[i];
        const auto& b = before.effects.objects[i];
        if (!o.active() || o.listed != b.listed || o.kind != b.kind || o.kind == FlyingObject::Kind::blast) continue;
        o.shown_x = between(b.shown_x, o.shown_x, t);
        o.shown_y = between(b.shown_y, o.shown_y, t);
    }
    for (std::size_t i = 0; i < e.puffs.size(); ++i) {
        auto& p = e.puffs[i];
        const auto& b = before.effects.puffs[i];
        // (A puff only shrinks and drifts 1 .. 2 pixels a tick up and to the
        // right; anything else is a new puff in a freed slot.)
        if (p.shown_size == 0 || b.shown_size == 0 || p.shown_size > b.shown_size) continue;
        if (!drifting(b.shown_x, p.shown_x, 2) || !drifting(b.shown_y, p.shown_y, 2)) continue;
        p.shown_x = between(b.shown_x, p.shown_x, t);
        p.shown_y = between(b.shown_y, p.shown_y, t);
    }
    for (std::size_t i = 0; i < e.sparks.size(); ++i) {
        auto& s = e.sparks[i];
        const auto& b = before.effects.sparks[i];
        // (Sparks rise 1 .. 3 pixels a tick up and to the right.)
        if (!s.active || !b.active) continue;
        if (!drifting(b.x >> 16, s.x >> 16, 3) || !drifting(b.y >> 16, s.y >> 16, 3)) continue;
        s.x = between_fixed(b.x, s.x, t);
        s.y = between_fixed(b.y, s.y, t);
    }
}

} // namespace wings::game
