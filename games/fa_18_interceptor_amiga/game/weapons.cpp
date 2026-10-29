#include "fa_18_interceptor_amiga/game/weapons.hpp"
#include "fa_18_interceptor_amiga/game/messages.hpp"
#include "fa_18_interceptor_amiga/game/sounds.hpp"
#include <algorithm>

namespace interceptor::game {

namespace {

using i16 = std::int16_t;
using i32 = std::int32_t;

// The messages ($1321C8 records 38-41), shown for a while (bit 14).
constexpr std::uint16_t flare_fired = 0x4026, no_flares = 0x4027, chaff_dispensed = 0x4028, no_chaff = 0x4029;

// A vector given in the aircraft's axes, turned into the world by the rows
// of its view matrix (+$92), as the original's sums of products.
std::array<i32, 3> to_world(const Matrix& m, i32 x, i32 y, i32 z) {
    return {m[0] * x + m[1] * y + m[2] * z, m[3] * x + m[4] * y + m[5] * z, m[6] * x + m[7] * y + m[8] * z};
}

} // namespace

void Weapons::reset() {
    stores_ = Stores{};
    firing_ = launch_asked_ = false;
    decoy_asked_ = 0;
    burst_left_ = 5;
    burst_hold_ = 0;
    flare_time_ = chaff_time_ = 0;
    shots_ = {};
    missiles_ = {};
}

void Weapons::select(Sounds& sounds) {
    sounds.refuse();
    // The nibble steps down by $10, from 0 back to $30.
    switch (stores_.selected) {
    case Weapon::none: stores_.selected = Weapon::amraam; break;
    case Weapon::amraam: stores_.selected = Weapon::sidewinder; break;
    case Weapon::sidewinder: stores_.selected = Weapon::gun; break;
    case Weapon::gun: stores_.selected = Weapon::none; break;
    }
}

void Weapons::trigger(bool down) {
    if (!down) {
        firing_ = false;
        return;
    }
    if (stores_.selected == Weapon::gun) firing_ = true;
    else if (stores_.selected != Weapon::none) launch_asked_ = true;
}

void Weapons::flare(Messages& messages) {
    // (The count goes down first: the 16th press finds none left.)
    if (--stores_.flares > 0) {
        flare_time_ = 30;
        decoy_asked_ = 2;
        messages.post(flare_fired);
    } else {
        stores_.flares = 0;
        messages.post(no_flares);
    }
}

void Weapons::chaff(Messages& messages) {
    if (--stores_.chaff > 0) {
        chaff_time_ = 30;
        decoy_asked_ = 1;
        messages.post(chaff_dispensed);
    } else {
        stores_.chaff = 0;
        messages.post(no_chaff);
    }
}

int Weapons::count() const {
    switch (stores_.selected) {
    case Weapon::gun: return stores_.rounds;
    case Weapon::sidewinder: return stores_.sidewinders;
    case Weapon::amraam: return stores_.amraams;
    default: return 0;
    }
}

void Weapons::move_missiles(const Aircraft& player, const FlightRules& rules, Sounds& sounds, const Aircraft* locked,
                            int locked_record, const Autopilot* autopilot) {
    if (flare_time_ > 0) --flare_time_;
    if (chaff_time_ > 0) --chaff_time_;
    for (auto& slot : missiles_) {
        if (!slot) {
            // A launch asked for takes the first free record; the request
            // goes even with nothing left of the weapon.
            if (!launch_asked_) continue;
            launch_asked_ = false;
            const bool amraam = stores_.selected == Weapon::amraam;
            int& left = amraam ? stores_.amraams : stores_.sidewinders;
            if (left == 0) continue;
            // $1176D8: the engine's event and its restart eight frames on.
            sounds.event(EngineSound::missile);
            sounds.restart_after(8);
            slot = launched(player, stores_.selected, --left);
        }
        if (!fly_missile(*slot, rules, tables_, locked, locked_record, autopilot)) slot.reset();
    }
}

Missile Weapons::launched(const Aircraft& player, Weapon weapon, int left) const {
    Missile m;
    m.body = player;
    auto& b = m.body;
    b.role = 0;
    // The station: the count left after this one, Sidewinders at 0-1 and
    // AMRAAMs at 2-5 of the aircraft's table ($117956; the F-16's $117932).
    m.type = weapon == Weapon::amraam ? 1 : 0;
    const int station = weapon == Weapon::amraam ? left + 2 : left;
    b.type = std::uint8_t(m.type);
    const std::uint32_t table = (player.type == 0x10 ? 0x117932 : 0x117956) + std::uint32_t(6 * station);
    const auto offset = to_world(player.view, program_.s16(table), program_.s16(table + 2), program_.s16(table + 4));
    b.x += offset[0] >> 6;
    b.y += offset[1] >> 6;
    b.z += offset[2] >> 6;
    m.life = m.type == 0 ? 150 : 200;
    m.fresh = true;
    // Off the ground (its velocity's height dropped if it was on it), no
    // turning, no sink.
    if (b.motion & flag::on_ground) b.vy = 0;
    b.motion &= ~(flag::on_ground | 0x0004);
    b.state |= flag::moves;
    b.pitch_rate = b.yaw_rate = b.roll_rate = 0;
    b.bank_side = b.bank_side_was = b.bank_turn = 0;
    b.sink = 0;
    b.state |= 0x11C2;
    b.ground_drag = 0x14; // +$26: it steers once this has counted down to 9
    return m;
}

bool Weapons::fly_missile(Missile& m, const FlightRules& rules, const FlightTables& tables, const Aircraft* locked,
                          int locked_record, const Autopilot* autopilot) {
    // $117E4A: the missile's own step. Until its last 50 frames its speed
    // rises by $1E0 to $4200 and it heads for the radar's locked record
    // (`$117F06`; with none it glides toward height 0, a further $240 toward
    // $4200, `$117E8E`); over its last 50 frames it falls to $3000 by $240
    // toward height 0, and from 12 on it is fading.
    if (--m.life <= 0) return false;
    auto& b = m.body;
    const auto toward = [&](int aim) {
        b.speed = i16(b.speed > aim ? std::max(b.speed - 0x240, aim) : std::min(b.speed + 0x240, aim));
    };
    if (!m.fading && m.life >= 50) {
        b.speed = i16(std::min(b.speed + 0x1E0, 0x4200));
        b.state &= ~0x0001;
        if (locked && locked_record > 0) {
            m.course.target = {i16(locked->x >> 22), i16(locked->z >> 22), i16((locked->x & 0x3FFFFF) >> 8),
                               i16((locked->z & 0x3FFFFF) >> 8), locked->y >> 8};
            m.course.target_record = std::uint8_t(locked_record | 0x80);
        } else {
            m.course.target.height = 0;
            toward(0x4200);
        }
    } else {
        if (m.life < 12) m.fading = true;
        m.course.target.height = 0;
        toward(0x3000);
    }
    b.airspeed = b.speed;
    // $11A352: the object step flies it on (from its second frame): its
    // autopilot, its turn, the velocity step.
    FlightRules coasting = rules;
    coasting.world = nullptr;
    if (autopilot) {
        b.timer = i16(m.life);
        autopilot->fly(b, m.pilot, PilotWorld{}, &m.course);
    }
    if (m.fresh) {
        m.fresh = false;
        return true;
    }
    if (autopilot) {
        const int divisor = (b.handling & 0x20) ? 10 : b.ground_drag > 0 ? std::min<int>(b.ground_drag, 15) : 0;
        turn_object(b, Autopilot::object_turn(b, m.pilot, divisor), coasting, tables);
    }
    coast(b, coasting, tables);
    return true;
}

void Weapons::move_shots(const Aircraft& player, bool outside, std::uint16_t frame, Sounds& sounds,
                         const std::function<bool(Shot&)>& hits) {
    burst_hold_ = std::int8_t(burst_hold_ - 1);
    bool spawned = false; // (one a frame)
    int busy = 0;
    for (auto& slot : shots_) {
        if (slot) --slot->life;
        if ((firing_ || decoy_asked_) && !slot) {
            if (spawned || burst_left_ == 0 || stores_.rounds < 1) {
                if (burst_hold_ > 0) ++busy;
            } else {
                spawned = true;
                --burst_left_;
                ++busy;
                Shot::Kind kind = Shot::Kind::round;
                if (decoy_asked_ == 2) kind = Shot::Kind::flare;
                else if (decoy_asked_ == 1) kind = Shot::Kind::chaff;
                if (decoy_asked_ == 0) {
                    sounds.gun(outside);
                    stores_.rounds = std::max(stores_.rounds - 3, 0);
                    burst_hold_ = 20;
                } else {
                    sounds.noise(0x1C, 0x30);
                    decoy_asked_ = 0;
                }
                slot = fired(kind, player, frame);
            }
        }
        if (!slot) continue;
        if (slot->life < 1) {
            slot.reset();
        } else if (!slot->hit && hits && hits(*slot)) {
            // ($1075A6: it sparks there for two frames.)
            slot->hit = true;
            slot->life = 2;
        } else {
            fly_shot(*slot);
        }
    }
    if (busy == 0) {
        burst_hold_ = 0;
        burst_left_ = 5;
    }
}

void Weapons::fly_shot(Shot& s) {
    if (s.hit) return;
    // $11AE96's end: rounds fall $100 a frame, chaff slows by 1/64 and
    // falls, flares slow by a quarter; the first frame stands still.
    switch (s.kind) {
    case Shot::Kind::round: s.vy -= 0x100; break;
    case Shot::Kind::chaff:
        s.vx -= s.vx >> 6;
        s.vz -= s.vz >> 6;
        s.vy -= 0x100;
        break;
    case Shot::Kind::flare:
        s.vx -= s.vx >> 2;
        s.vz -= s.vz >> 2;
        s.vy -= s.vy >> 2;
        break;
    }
    if (!s.moved) {
        s.moved = true;
    } else {
        s.x += s.vx;
        s.y += s.vy;
        s.z += s.vz;
    }
    // $10754C: below height 0 it sparks for two frames, put back along its
    // move onto height 0 ($11B45A: by whole units of height, the steps'
    // ratios in 16ths).
    if (s.y < 0) {
        s.hit = true;
        s.life = 2;
        const i16 fall = i16(s.vy >> 8);
        if (fall != 0) {
            const i32 below = -i16(s.y >> 8);
            s.x += (i32(i16((s.vx << 4) / fall)) * below) >> 4;
            s.z += (i32(i16((s.vz << 4) / fall)) * below) >> 4;
            s.y = 0;
        }
    }
}

Shot Weapons::fired(Shot::Kind kind, const Aircraft& player, std::uint16_t frame) const {
    Shot s;
    s.kind = kind;
    // $1077D8: where it leaves the aircraft (in its axes: x, y, z ahead)
    // and how long it lives.
    i32 lx = 0, ly = 0, lz = 0;
    switch (s.kind) {
    case Shot::Kind::round:
        if (player.type != 0x11) { lx = 6; ly = 1; lz = 0x18; }
        else lz = 0x38;
        s.life = 20;
        break;
    case Shot::Kind::flare:
        ly = -8;
        lz = -0x38;
        s.life = 60;
        break;
    case Shot::Kind::chaff:
        ly = -8;
        s.life = 30;
        break;
    }
    const auto start = to_world(player.view, lx, ly, lz);
    s.x = player.x + (start[0] >> 6);
    s.y = player.y + (start[1] >> 6);
    s.z = player.z + (start[2] >> 6);
    // The direction: rounds ahead and a little up, by the frame (a spread
    // of $3D-$45); flares and chaff down.
    i32 ahead = 0, up = -0x40;
    if (s.kind == Shot::Kind::round) {
        ahead = 0x400;
        static constexpr i32 lift[4] = {0x43, 0x3D, 0x45, 0x40};
        up = lift[frame & 3];
    }
    const auto& v = player.view;
    const std::array<i32, 3> aim{(v[2] * ahead + v[1] * up) >> 4, (v[5] * ahead + v[4] * up) >> 4,
                                 (v[8] * ahead + v[7] * up) >> 4};
    // $107AFE: the speed, rounds at the aircraft's speed / 64 + $C6 and
    // a spread by the frame.
    int length = s.kind == Shot::Kind::flare ? 5 : 0xD;
    if (s.kind == Shot::Kind::round) {
        static constexpr int spread[4] = {0, 0x10, 0, -0x10};
        length = (player.speed >> 6) + spread[frame & 3] + 0xC6;
    }
    const auto d = scaled_vector(tables_, i16(length), i16((aim[0] - (start[0] >> 6)) >> 8),
                                 i16((aim[1] - (start[1] >> 6)) >> 8), i16((aim[2] - (start[2] >> 6)) >> 8), 3);
    // ($107C24: the frame's move is eight sub-steps for the hit tests.)
    s.ex = d[0];
    s.ey = d[1];
    s.ez = d[2];
    s.vx = i32(d[0]) << 3;
    s.vy = i32(d[1]) << 3;
    s.vz = i32(d[2]) << 3;
    if (s.kind != Shot::Kind::round) {
        // Flares and chaff leave with the aircraft's own velocity.
        s.vx += player.vx;
        s.vy += player.vy;
        s.vz += player.vz;
    }
    return s;
}

} // namespace interceptor::game
