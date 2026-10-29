#include "fa_18_interceptor_amiga/game/object_step.hpp"
#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include <algorithm>
#include <cstdlib>

namespace interceptor::game {
namespace {

using i16 = std::int16_t;
using i32 = std::int32_t;

constexpr std::uint32_t paths = 0x11DDC0;
constexpr std::uint32_t attack_choices = 0x120374; // per case 4 levels of (timer, role)
constexpr std::uint16_t as_player = 0x0100;       // motion +2 bit 0 (the trainer's)
constexpr std::uint8_t spinning = 0x02;
constexpr std::uint8_t behind_target = 0x20;       // byte +4 bit 5 ($11826A)

Waypoint place_of(const Aircraft& a) {
    return {i16(a.x >> 22), i16(a.z >> 22), i16((a.x & 0x3FFFFF) >> 8), i16((a.z & 0x3FFFFF) >> 8), a.y >> 8};
}

// $10D498: a record's place plus an offset turned by its view matrix
// (products over 16).
std::array<i32, 3> offset_from(const Aircraft& a, int ox, int oy, int oz) {
    const auto& v = a.view;
    return {a.x + ((v[0] * ox + v[1] * oy + v[2] * oz) >> 4), a.y + ((v[3] * ox + v[4] * oy + v[5] * oz) >> 4),
            a.z + ((v[6] * ox + v[7] * oy + v[8] * oz) >> 4)};
}

} // namespace

Aircraft* Records::craft(int record) const {
    if (record == 0) return player;
    for (auto& o : *objects) {
        if (o.record == record) return &o.craft;
    }
    return nullptr;
}

std::int16_t Records::distance(int record) const {
    if (record == 0) return player_distance;
    for (auto& o : *objects) {
        if (o.record == record) return o.course.distance;
    }
    return 0; // (a cleared record)
}

bool Records::active(int record) const {
    const auto* a = craft(record);
    return a && (a->state & 0x0040);
}

ObjectStep::ObjectStep(const amiga::LoadedImage& program, const FlightTables& tables, const ObjectLists& lists)
    : program_(program), tables_(tables), lists_(lists) {}

bool ObjectStep::before_flight(WorldObject& o, Records& records, StepWorld& world) const {
    auto& a = o.craft;
    if ((a.type & 0xF0) != 0x10) return true; // (ships do nothing here; missiles and others not built)
    a.timer = i16(a.timer - 1);
    if (!(a.state & 0x1000)) return true;
    if (a.handling & spinning) {
        // $1179C0: spinning down, it heads for the spot under it, its
        // speed going toward $1200 by $90.
        o.course.target = place_of(a);
        o.course.target.height = 0;
        i16 s = a.speed;
        s = i16(s > 0x1200 ? std::max(s - 0x90, 0x1200) : std::min(s + 0x90, 0x1200));
        a.speed = a.airspeed = s;
        a.motion &= 0xDFF7;
        return true;
    }
    if (!(a.motion & as_player) && !o.course.target.none()) aim_speed(o, records);
    follow(o, records, world);
    return true;
}

// $117A16: the speed it aims for - cruising, or matched to the record it
// follows by how close it is - averaged with its own and stepped by at most
// $90; the control byte's throttle bits push towards it.
void ObjectStep::aim_speed(WorldObject& o, const Records& records) const {
    auto& a = o.craft;
    auto& c = o.course;
    measure(o);
    const i16 speed = a.speed;
    const auto cruise = [&]() -> i16 {
        if (a.type == 0x15) return 0x1EC0;
        if (a.state & 0x0008) return 0x15C0;
        return 0x1D40;
    };
    i16 aim = 0;
    bool clamp = true;
    if (c.target_record == 0xFF) {
        if (a.type != 0x14) {
            aim = cruise();
        } else if (a.motion & flag::on_ground) {
            if (speed < 0x100) {
                a.throttle = 0;
                a.controls &= 0xFC;
                return;
            }
            aim = i16(speed >> 1);
            clamp = false;
        } else {
            aim = 0x1680;
            if ((a.y >> 8) <= 0x6C0) {
                if (a.gear & 0x80) a.gear ^= 0x80; // the gear comes down
                aim = 0x600;
            }
        }
    } else {
        const Aircraft* other = records.craft(c.target_record & 0x7F);
        aim = other ? other->speed : 0;
        const i16 d = c.distance;
        enum class Step { keep, match, cruise, slower, maybe_slower } step = Step::keep;
        if (a.surface & behind_target) {
            step = d <= 0x240 ? Step::match : d >= 0x600 ? Step::slower : Step::keep;
        } else if (!(a.motion & as_player) && a.role != 0 && a.role != 1) {
            step = d <= 0x360 ? Step::keep : a.role == 6 ? Step::match : Step::slower;
        } else if (d < 0x180) {
            step = Step::slower;
        } else {
            step = (o.pilot & 0x60) != 0x60 ? Step::maybe_slower : Step::match;
        }
        if (step == Step::match) {
            // Far: cruise; nearer: twice its speed, up to $1BC0.
            if (d >= 0x300 || i16(aim * 2) > 0x1BC0) aim = cruise();
            else aim = i16(aim * 2);
        } else if (step == Step::maybe_slower && d > 0x300) {
            step = Step::keep;
        }
        if (step == Step::slower || step == Step::maybe_slower) aim = i16(aim - std::min(aim >> 3, 0x420));
    }
    if (clamp && aim < 0x900) aim = 0x900;
    i16 next = i16((aim + speed) >> 1);
    if (std::abs(next - speed) >= 0x90) next = i16(next > speed ? speed + 0x90 : speed - 0x90);
    const std::uint8_t bits = next == speed ? 0 : next > speed ? 1 : 2;
    a.controls = std::uint8_t((a.controls & 0xFC) | bits);
}

// $118468: every few frames (more often when near) the distance to its
// point (+$4A; $7FFF when far), its behaviour (3 far, 4 near) and the
// weapon nibble of +$63 by distance.
void ObjectStep::measure(WorldObject& o) const {
    auto& a = o.craft;
    auto& c = o.course;
    a.throttle_steps = std::uint8_t(a.throttle_steps + 0x10);
    if (c.distance >= 0x480) {
        const int every = c.distance >= 0x900 ? 0x50 : 0x20;
        if ((a.throttle_steps & 0xF0) <= every) return;
    }
    a.throttle_steps &= 0x0F;
    if (c.target.none()) return;
    const auto far = [&] {
        c.distance = 0x7FFF;
        o.weapons |= 0xF0;
    };
    const i32 dx = std::abs((i32(i16(c.target.cell_x - i16(a.x >> 22))) << 14) + i16(c.target.fine_x - i16((a.x & 0x3FFFFF) >> 8)));
    const i32 dz = std::abs((i32(i16(c.target.cell_z - i16(a.z >> 22))) << 14) + i16(c.target.fine_z - i16((a.z & 0x3FFFFF) >> 8)));
    const i32 dy = std::abs(c.target.height - (a.y >> 8));
    if (dx > 0x7F00 || dy > 0x7F00 || dz > 0x7F00) {
        far();
        return;
    }
    const i16 d = length_estimate(tables_, std::uint16_t(dx), std::uint16_t(dy), std::uint16_t(dz)); // (x and the height the flat pair)
    c.distance = d;
    a.surface |= 0x01;
    if (d > 0x36C0) {
        o.weapons |= 0xF0;
        return;
    }
    if (d <= 0x1E00) {
        if (c.behaviour == 3) c.behaviour = 4;
    } else if (c.behaviour == 4) {
        c.behaviour = 3;
    }
    if (d > 0x1800) {
        far();
        return;
    }
    o.weapons = std::uint8_t((o.weapons & 0x0F) | (d <= 0x300 ? 0x10 : d <= 0xC00 ? 0x20 : 0x30));
}

void ObjectStep::measure_player(Aircraft& p, std::int16_t& distance, const Aircraft* locked) const {
    if (!locked) return;
    p.throttle_steps = std::uint8_t(p.throttle_steps + 0x10);
    if (distance >= 0x480) {
        const int every = distance >= 0x900 ? 0x50 : 0x20;
        if ((p.throttle_steps & 0xF0) <= every) return;
    }
    p.throttle_steps &= 0x0F;
    const auto t = place_of(*locked);
    const i32 dx = std::abs((i32(i16(t.cell_x - i16(p.x >> 22))) << 14) + i16(t.fine_x - i16((p.x & 0x3FFFFF) >> 8)));
    const i32 dz = std::abs((i32(i16(t.cell_z - i16(p.z >> 22))) << 14) + i16(t.fine_z - i16((p.z & 0x3FFFFF) >> 8)));
    const i32 dy = std::abs(t.height - (p.y >> 8));
    if (dx > 0x7F00 || dy > 0x7F00 || dz > 0x7F00) {
        distance = 0x7FFF;
        return;
    }
    distance = length_estimate(tables_, std::uint16_t(dx), std::uint16_t(dy), std::uint16_t(dz));
    p.surface |= 0x01;
}

// $117BA6: where its point is.
void ObjectStep::follow(WorldObject& o, Records& records, StepWorld& world) const {
    auto& a = o.craft;
    auto& c = o.course;
    const auto& m = program_;
    if (a.role == 8 && !(a.motion & as_player) && c.distance <= 0x480) {
        // Along its path: near the place it heads for, the next one; after
        // the last the autopilot lets go (state bit 7; a type $15 crashes).
        std::uint32_t at = paths + std::uint32_t(m.s16(paths + 2u * c.path));
        for (int guard = 0; guard < 64; ++guard) {
            const Waypoint w{m.s16(at), m.s16(at + 2), m.s16(at + 4), m.s16(at + 6), c.target.height};
            if (w.cell_x == c.target.cell_x && w.cell_z == c.target.cell_z && w.fine_x == c.target.fine_x &&
                w.fine_z == c.target.fine_z) {
                at += 10;
                if (m.s16(at) < 0) {
                    if (a.state & 0x0080) {
                        a.state &= ~0x0080;
                        if (a.type == 0x15) a.state |= 0x0200;
                    }
                } else {
                    c.target = {m.s16(at), m.s16(at + 2), m.s16(at + 4), m.s16(at + 6), m.s16(at + 8)};
                    c.distance = 0x7FFF;
                }
                break;
            }
            at += 10;
            if (m.s16(at) < 0) break; // (not on its path: message $35, not traced)
        }
    }
    if (a.role == 8) {
        // (Role 8 goes straight to the attack part; the record it measures
        // against is the player here - hypothesis: the original's a3 is
        // left over from the caller.)
        attack(o, *records.player, records.player_distance, world);
        return;
    }
    if (a.state & 0x0008) {
        // Touchable: with +1 bit 1, the radar's locked record is the point
        // (`$117EF8`; missiles' rule, others not built).
        if (a.state & 0x0002) a.state &= ~0x0001;
        return;
    }
    // (A point to head for is overwritten here too: such an aircraft
    // follows the player, `$117C60`.)
    Aircraft* target = records.player;
    std::int16_t target_distance = records.player_distance;
    bool to_player = true;
    if (c.target_record != 0xFF) {
        const int r = c.target_record & 0x7F;
        if (c.behaviour == 5) c.behaviour = 3;
        if (!records.active(r)) {
            // $1181DE: the record is gone: back to its own list entry's
            // place.
            if (a.state & 0x1000) {
                std::uint32_t at = world.list;
                for (int guard = 0; guard < 32 && at && m.s16(at) >= 0; ++guard, at += 10) {
                    if ((m.u16(at + 4) & 0xFF) == o.record) {
                        c.target = lists_.path(m.u16(at + 6));
                        c.target_record = 0xFF;
                        a.state &= ~0x0001;
                        break;
                    }
                }
            }
            world.attack_now = false;
            return;
        }
        a.state |= 0x0001;
        target = records.craft(r);
        target_distance = records.distance(r);
        to_player = r == 0;
    }
    if (!(c.target_record != 0xFF && to_player) && a.role != 8) {
        // $117F7E: near enough to the player (a box by the level), it is
        // the player it follows from now on.
        const i32 box = world.level >= 3 ? 0x300000 : world.level >= 2 ? 0x240000 : 0x180000;
        const auto& p = *records.player;
        if (std::abs(p.x - a.x) <= box && std::abs(p.y - a.y) <= box && std::abs(p.z - a.z) <= box) c.target_record = 0x80;
    }
    // $117FE2: the point is just behind the record (36 units); near
    // something, beside or above it by the pilot's bits.
    int ox = 0, oy = 0, oz = -0x24;
    if (a.motion & 0x0001) {
        oz = -0x30;
        if (!(o.pilot & 0x01)) ox = (o.pilot & 0x02) ? 0xA8 : -0x60;
        else oy = (o.pilot & 0x02) ? 0x60 : -0x60;
    }
    const auto p = offset_from(*target, ox, oy, oz);
    c.target = {i16(i16(p[0] >> 16) >> 6), i16(i16(p[2] >> 16) >> 6), i16((p[0] >> 8) & 0x3FFF), i16((p[2] >> 8) & 0x3FFF),
                p[1] >> 8};
    attack(o, *target, target_distance, world);
}

// $1180A6: now and then (the +$39 count at 1) it notes whether it sits
// behind the record it follows, heading its way ($11826A); while it does,
// a maneuver from the table by distance, speed and level.
void ObjectStep::attack(WorldObject& o, const Aircraft& target, std::int16_t target_distance, StepWorld& world) const {
    auto& a = o.craft;
    if (a.handling & spinning) {
        world.attack_now = false;
        return;
    }
    if ((a.throttle_steps & 0xF0) == 0x10) {
        bool behind = false;
        if (target_distance <= 0x3000) {
            const auto d = scaled_vector(tables_, 0xC0, i16((target.x - a.x) >> 8), i16((target.y - a.y) >> 8),
                                         i16((target.z - a.z) >> 8));
            const i32 along = i32(d[0]) * target.view[2] + i32(d[1]) * target.view[5] + i32(d[2]) * target.view[8];
            const i32 same = i32(target.view[2]) * a.view[2] + i32(target.view[5]) * a.view[5] + i32(target.view[8]) * a.view[8];
            behind = along < 0 && along <= -0x2C0000 && same >= 0 && same >= 0xD000000;
        }
        if (behind) a.surface |= behind_target;
        else a.surface &= ~behind_target;
    }
    // (Any role but 6 - and 8 unless mission 5 asks - while it sits there:
    // picked again every frame.)
    const bool may = a.role != 6 && (a.role != 8 || (world.mode == 5 && world.attack_now));
    if (!may || !(a.surface & behind_target)) {
        world.attack_now = false;
        return;
    }
    std::uint32_t at = attack_choices;
    const auto by_speed = [&] {
        const int diff = a.speed - target.speed;
        if (std::abs(diff) >= 0xC0) {
            at += 0x20;
            if (diff >= 0) at += 0x20;
        }
    };
    // By distance: up to $300, $C00, $1E00 and beyond; by the speeds
    // apart (and, past $300, whether the player's lock is on it just now)
    // except beyond $1E00.
    if (target_distance <= 0x300) {
        by_speed();
    } else {
        at += 0x60;
        if (target_distance > 0xC00) at += 0x60;
        if (target_distance > 0x1E00) {
            at += 0x60;
        } else {
            by_speed();
            if (world.locked == o.record && world.attack_now) {
                world.attack_now = false;
                at += 0x10;
            }
        }
    }
    const int level = std::min<int>(world.level, 3);
    a.timer = program_.s16(at + std::uint32_t(4 * level));
    const auto next = std::uint8_t(program_.s16(at + std::uint32_t(4 * level + 2)));
    if (a.role == 8 && (a.state & 0x0008)) a.state &= ~0x0008; // (and `$13AB99` / `$13AB9A` + 1)
    a.role = next;
    world.attack_now = false;
}

} // namespace interceptor::game
