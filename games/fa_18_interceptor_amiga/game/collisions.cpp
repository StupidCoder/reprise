#include "fa_18_interceptor_amiga/game/collisions.hpp"
#include <cstdlib>

namespace interceptor::game {

namespace {

using i32 = std::int32_t;

bool within(const Aircraft& a, i32 dx, i32 dy, i32 dz, const std::array<i32, 3>& at, i32 size) {
    return std::abs(a.x - dx - at[0]) <= size && std::abs(a.y - dy - at[1]) <= size &&
           std::abs(a.z - dz - at[2]) <= size;
}

} // namespace

int touch_bodies(std::vector<Body>& bodies, std::size_t self, const std::array<i32, 3>& before, bool& near) {
    near = false;
    auto& me = *bodies[self].aircraft;
    const std::uint8_t my_class = me.type & 0xF0;
    if (my_class == 0x20) return 0; // (ships move without touching)
    for (std::size_t k = 0; k < bodies.size(); ++k) {
        if (k == self) continue;
        auto& body = bodies[k];
        auto& o = *body.aircraft;
        // Active, not crashed or exploding, of another group, and one of
        // the two marked for touching (+1 bit 3).
        if (!body.active || (o.state & 0x0600) || body.group == bodies[self].group) continue;
        if (!(me.state & 0x0008) && !(o.state & 0x0008)) continue;
        // The boxes' half sizes (32-bit units): the near box and the hit box.
        i32 box = 0, hit = 0;
        const std::uint8_t o_class = o.type & 0xF0;
        if (o.type == 0x15) {
            box = 0x5000;
            hit = 0x1000;
        } else if (o_class == 0x30) {
            continue;
        } else if (o_class == 0x20) {
            continue; // (ships: the decks, carrier.hpp)
        } else if (my_class == 0 || my_class != o_class) {
            box = 0x10000;
            hit = 0x5000;
        } else if (!(me.motion & 0x0100) && !(o.motion & 0x0100)) {
            box = 0x7000;
            hit = 0x1000;
        } else {
            // (The training's pair, +2 bit 0: closer before they touch.)
            box = 0x6000;
            hit = 0x800;
        }
        if (!within(o, 0, 0, 0, before, box)) continue;
        near = true;
        o.motion |= 0x0001;
        // Hit: within the hit box now or half a move back ($11B812).
        if (!within(o, 0, 0, 0, before, hit) && !within(o, o.vx >> 1, o.vy >> 1, o.vz >> 1, before, hit)) continue;
        if (o.state & 0x1000) {
            o.state |= 0x0200;
            if (o_class != 0) {
                o.handling |= 0x02;
                o.motion |= 0x1000;
                o.role = 1;
            }
        }
        // $11C0BA: both marked when the mover is marked for touching.
        if (me.state & 0x0008) {
            o.handling |= 0x80;
            me.handling |= 0x80;
        } else {
            o.handling &= ~0x80;
            me.handling &= ~0x80;
        }
        return touch::object;
    }
    return 0;
}

} // namespace interceptor::game
