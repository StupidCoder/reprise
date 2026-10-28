#include "cannon_fodder_amiga/game/vehicles.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
#include "cannon_fodder_amiga/game/scenery.hpp"
#include <algorithm>
#include <cstdlib>

namespace fodder::game {
namespace {

int whole(std::int32_t v) { return std::int16_t(std::uint32_t(v) >> 16); }
void set_whole(std::int32_t& v, int w) {
    v = std::int32_t((std::uint32_t(std::uint16_t(w)) << 16) | (std::uint32_t(v) & 0xffff));
}

constexpr int kind_explosion = 0xc;
constexpr int kind_big_explosion = 0x59;
constexpr int kind_nothing = 4;
constexpr int kind_piece = 0x29;
constexpr int graphic_hidden = 0x7c;

// A vehicle's height above the ground as the original's long at +$1E
// (16.16): the drawing height and its fraction.
std::int32_t height32(const GameObject& o, const Walker& w) {
    return std::int32_t((std::uint32_t(std::uint16_t(o.height)) << 16) | std::uint32_t(w.height_fraction & 0xffff));
}
void set_height32(GameObject& o, Walker& w, std::int32_t h) {
    o.height = std::int16_t(std::uint32_t(h) >> 16);
    w.height_fraction = int(std::uint32_t(h) & 0xffff);
}

std::int16_t random_word(World& world) { return std::int16_t(world.random() & 0xffff); }

// What the vehicle routines share for one update.
class Vehicle {
public:
    Vehicle(World& w, const Terrain& t, const ProgramTables& tables, int slot)
        : w_(w), t_(t), tables_(tables), g_(tables.geometry), slot_(slot), o_(w.objects.slots[std::size_t(slot)]),
          s_(w.walkers[std::size_t(slot)]) {}

    // ---- ground vehicles ($9BBAA) ----

    // $9BBAA: drive towards +$26/$28 (types 2, 3, 10).
    void jeep_core() {
        int volume = s_.speed;
        if (volume != 0 || o_.side != 0 || w_.driven_vehicle == slot_) {
            volume = std::min(volume + 16, 64);
            engine_sound(0x38 + std::min(volume >> 4, 3), volume);
        }
        s_.is_vehicle = true;
        if (destroyed()) return;
        o_.graphic = 0xa5;
        bounce(0x20000);
        if (o_.height < 3) steer(s_.waypoint_x, s_.waypoint_y, 0x48);
        const int to_go = g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y));
        if (to_go < 30) {
            if (to_go < s_.speed) s_.speed = to_go;
        } else {
            if (o_.hidden_lines == 0) {
                w_.jitter_next_effect = true;
                const int puff = exhaust_puff();
                if (puff >= 0) w_.objects.slots[std::size_t(puff)].layer = -1;
            }
            if (!(s_.speed == 0 && turn_ != 0) && o_.hidden_lines < 4) {
                s_.speed += 3;
                // (Kind $52 always ends at 24: the original's cap compares
                // after it has already set the speed.)
                if (o_.kind == 0x52) s_.speed = 0x18;
                else if (s_.speed >= 0x32) s_.speed = 0x32;
            }
        }
        if (o_.height > 4) s_.speed += 0x1c;
        save_position();
        move();
        if (o_.height >= 9) {
            s_.speed += 0x40;
        } else {
            ground_terrain();
            if (o_.height <= 4) o_.height = 0;
        }
        shadow();
        crush(whole(o_.x), whole(o_.x) + 0x1c, whole(o_.y) - 0x10, whole(o_.y), o_.height, o_.height + 0xe);
    }

    // $99F92 / $99F9A: the recruits' jeeps.
    void recruits_jeep(int type) {
        s_.vehicle_type = type;
        if (s_.state != 0) {
            o_.side = 0;
            jeep_core();
            o_.side = 0;
            return;
        }
        if (type == 3) jeep_gun();
        o_.side = 0;
        jeep_core();
        o_.side = 0;
        helper(1).kind = kind_nothing;
    }

    // $98A40 / $98A48 / $98A50: the enemy's jeeps; they hunt recruits (and
    // civilians) within 210 they can see (or within 40), now and then
    // firing; type 10 just drives right.
    void enemy_jeep(int type) {
        s_.vehicle_type = type;
        if (type == 10) s_.waypoint_x = whole(o_.x) + 0x28;
        if (s_.state != 0) {
            o_.side = 1;
            jeep_core();
            o_.side = 1;
            return;
        }
        if (type != 10) {
            if (const int target = scan_for({0, 0x3d, 0x3e, 0x46}); target >= 0) {
                if ((w_.random() & 3) == 0) s_.fire_request = true;
                const auto& t = w_.objects.slots[std::size_t(target)];
                s_.aim_x = s_.waypoint_x = whole(t.x);
                s_.aim_y = s_.waypoint_y = whole(t.y);
            }
        }
        o_.side = 1;
        jeep_core();
        if (type == 3) jeep_gun();
        helper(1).kind = kind_nothing;
        auto& piece = helper(2);
        piece.kind = kind_piece;
        set_whole(piece.x, whole(o_.x) + 0xd);
        set_whole(piece.y, whole(o_.y) - 0x13 + o_.hidden_lines);
        piece.height = o_.height;
    }

    // $99C7C: the armed jeep's gun (type 3): the driven one fires on every
    // second right-button tick, the enemy's when told to; the shot comes from
    // one barrel and the other in turn.
    void jeep_gun() {
        if (o_.side != 0) {
            if (!s_.fire_request) return;
        } else {
            if (w_.driven_vehicle != slot_ || !w_.right_held) return;
            if (!s_.fire_request) {
                s_.fire_request = false;
                s_.gun_toggle = !s_.gun_toggle;
                if (s_.gun_toggle) return;
            }
        }
        s_.fire_request = false;
        static constexpr int offsets[16][4] = { // $99CF4: per frame, two barrels (x, y)
            {0xb, 0x5, 0x12, 0x5}, {0x6, 0x5, 0xa, 0x6}, {0x0, 0x7, 0x6, 0x9}, {-2, 0x5, -1, 0x9},
            {-2, -6, -2, 0x2}, {-2, -6, -2, 0x1}, {0x5, -4, 0x8, -8}, {0x7, -4, 0xe, -8},
            {0xa, -8, 0x12, -8}, {0x10, -8, 0x16, -6}, {0x11, -8, 0x16, -2}, {0x13, -2, 0x19, 0x2},
            {0x16, -4, 0x16, 0x2}, {0x16, -2, 0x14, 0x5}, {0x18, 0x2, 0x16, 0x7}, {0x19, 0x5, 0x14, 0x8}};
        const auto& b = offsets[std::size_t(o_.frame & 15)];
        const int ox = o_.x, oy = o_.y;
        set_whole(o_.x, whole(o_.x) + (s_.barrel ? b[2] : b[0]));
        set_whole(o_.y, whole(o_.y) + (s_.barrel ? b[3] : b[1]));
        if (heavy_shot()) {
            s_.barrel = !s_.barrel;
            request_sound(w_, 0x10, 0x1e, slot_);
        }
        o_.x = ox;
        o_.y = oy;
    }

    // $99D74: a shot of the jeep's gun (kind $4D), like a bullet: 50 + the
    // shooter's skill ticks, its speed + 80, the frame's heading +- 7.
    bool heavy_shot() {
        if (w_.end_count != 0) return false;
        const int slot = allocate_slot(w_);
        if (slot < 0 || w_.bullets_alive == 12) return false;
        ++w_.bullets_alive;
        auto& b = w_.objects.slots[std::size_t(slot)];
        auto& bw = w_.walkers[std::size_t(slot)];
        auto& bs = w_.bullets[std::size_t(slot)];
        b.x = o_.x;
        b.y = o_.y;
        b.graphic = 0x7f;
        b.frame = 3;
        bs.lifetime = 9;
        b.kind = 0x4d;
        set_height32(b, bw, height32(o_, s_) + 0x60000);
        b.hidden_lines = o_.hidden_lines;
        b.side = o_.side;
        b.layer = 0;
        bs.cruise_speed = 0x32 + s_.skill;
        bw.speed = s_.speed + 0x50;
        bw.heading = ((((o_.frame - 1) ^ 0xf) & 0xf) << 5);
        bs.owner = slot_;
        bs.muzzle_ticks = 2;
        bs.offset_x = 0;
        bs.offset_y = 2;
        bs.first_heading = -1;
        const auto r = random_word(w_);
        int spread = r & 7;
        if (std::int8_t(r) < 0) spread = -spread;
        bw.heading += spread;
        bs.age = 0;
        return true;
    }

    // ---- tanks ($999E6) ----

    // $999E6: a tank (type 4): body $D1 heading for +$26/$28, turret $D2 in
    // the next slot aimed at the pointer (driven) or the target (enemy).
    void tank_core() {
        int volume = s_.speed;
        if (volume != 0 || o_.side != 0 || w_.driven_vehicle == slot_) {
            volume = std::min(volume + 16, 32);
            engine_sound(2, volume);
        }
        s_.vehicle_type = 4;
        s_.is_vehicle = true;
        if (destroyed()) return;
        o_.graphic = 0xd1;
        steer(s_.waypoint_x, s_.waypoint_y, s_.speed + 0x20);
        const int to_go = g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y));
        if (to_go < 30) {
            if (to_go < s_.speed) s_.speed = to_go;
        } else {
            w_.jitter_next_effect = true;
            const int puff = exhaust_puff();
            if (puff >= 0) w_.objects.slots[std::size_t(puff)].layer = -1;
            if (s_.speed == 0 && turn_ != 0) {
                s_.speed -= 4;
            } else {
                s_.fall_count = (s_.fall_count + 1) & 1;
                if (s_.fall_count == 0 && ++s_.speed >= 0x18) s_.speed = 0x18;
            }
        }
        save_position();
        move();
        ground_terrain();
        o_.height = 0;
        tank_turret();
        crush(whole(o_.x), whole(o_.x) + 0x1e, whole(o_.y) - 0x14, whole(o_.y), o_.height, o_.height + 0xe);
    }

    void tank_turret() { // $99AE8
        auto& t = helper(1);
        t.graphic = 0xd2;
        t.x = o_.x;
        set_whole(t.x, whole(o_.x) + 0xa);
        t.y = o_.y;
        set_whole(t.y, whole(o_.y) + 1 + o_.hidden_lines);
        t.height = o_.height + 9;
        t.hidden_lines = o_.hidden_lines;
        const int fx = whole(t.x) + 8, fy = whole(t.y) - 5 - t.height;
        int tx = 0, ty = 0;
        if (o_.side != 0) {
            if (s_.aim_x < 0 || (s_.aim_x & 0x8000)) {
                // No target: the turret wanders every 16th tick.
                if ((w_.tick_counter & 0xf) != 0) return;
                t.frame = (t.frame + ((w_.random() & 1) ? 1 : -1)) & 0xf;
                return;
            }
            tx = s_.aim_x;
            ty = s_.aim_y;
        } else {
            if (w_.driven_vehicle != slot_) return;
            tx = w_.camera.x + w_.pointer_x - 0x10;
            ty = w_.camera.y + w_.pointer_y;
        }
        if (const auto d = g_.direction(tx - fx, ty - fy, 9)) {
            t.frame = ((((*d - 0x10) >> 5) + 8) ^ 0xf) & 0xf;
        }
    }

    // $99BE2: the tank fires a shell from its turret.
    void tank_fire() {
        if (!s_.fire_held && !s_.fire_request) return;
        s_.fire_request = false;
        auto& t = helper(1);
        auto& tw = w_.walkers[std::size_t(slot_ + 1)];
        tw.aim_x = s_.aim_x;
        tw.aim_y = s_.aim_y;
        const int tx = t.x, ty = t.y;
        set_whole(t.x, whole(t.x) + muzzle_[std::size_t(t.frame & 15)][0]);
        set_whole(t.y, whole(t.y) + muzzle_[std::size_t(t.frame & 15)][1]);
        w_.launch_offset = {0, 0};
        const int m = Vehicle(w_, t_, tables_, slot_ + 1).launch_missile(false);
        t.x = tx;
        t.y = ty;
        if (m >= 0) {
            auto& mo = w_.objects.slots[std::size_t(m)];
            mo.height += 0x11;
            w_.walkers[std::size_t(m)].speed = 0x3c;
            request_sound(w_, 5, 0x1e, slot_);
            Vehicle(w_, t_, tables_, slot_ + 1).exhaust_puff();
            s_.fire_held = false;
        } else {
            s_.fire_held = true;
        }
    }

    // $999AA (kind $41, the recruits'): a tank they drive; the right button
    // fires.
    void recruits_tank() {
        if (s_.state == 5) s_.state = 0;
        if (s_.state != 0) {
            o_.side = 0;
            tank_core();
            o_.side = 0;
            return;
        }
        o_.side = 0;
        tank_core();
        o_.side = 0;
        tank_fire();
        helper(1).kind = kind_nothing;
    }

    // $99668 (kind $45): the enemy's tank: a recruit within 250 it can see
    // becomes its aim (and its destination now and then); beyond 50 it
    // fires one tick in 32.
    void enemy_tank() {
        if (s_.state == 5) s_.state = 0;
        if (s_.state != 0) {
            o_.side = 1;
            tank_core();
            o_.side = 1;
            if (helper(2).kind == kind_piece) free_object(w_, slot_ + 2);
            return;
        }
        if (s_.stopped != 0) --s_.stopped;
        bool found = false;
        if (s_.stopped == 0) {
            const int index = s_.target;
            const int r = index >= 0 && index < 30 ? w_.recruit_lists[std::size_t(index)] : -1;
            if (r >= 0) {
                const auto& t = w_.objects.slots[std::size_t(r)];
                if (t.hidden_lines == 0 && !sheltered(whole(t.x) + 8, whole(t.y) - 5)) {
                    const int d = g_.distance(whole(t.x) - whole(o_.x), whole(t.y) - whole(o_.y));
                    w_.target_distance2 = d;
                    if (d < 0xfa && !t_.sight_blocked(whole(o_.x), whole(o_.y), whole(t.x) + 0xf, whole(t.y) - 0xa)) {
                        found = true;
                        s_.aim_x = whole(t.x);
                        s_.aim_y = whole(t.y);
                        s_.stopped = 0x5a;
                        if (whole(o_.x) == s_.waypoint_x && whole(o_.y) == s_.waypoint_y && (w_.random() & 0x3f) == 0) {
                            s_.waypoint_x = whole(t.x);
                            s_.waypoint_y = whole(t.y);
                        }
                        if (d > 0x32 && (w_.random() & 0x1f) == 4) {
                            s_.fire_request = true;
                            tank_fire();
                        }
                    }
                }
            }
            if (!found) {
                if (++s_.target >= 0x1e) s_.target = 0;
                s_.aim_x = -1;
            }
        }
        o_.side = 1;
        tank_core();
        o_.side = 1;
        if (moved_back_) {
            s_.stopped = 0;
            s_.waypoint_x = whole(o_.x);
            s_.waypoint_y = whole(o_.y);
        }
        helper(1).kind = kind_nothing;
        auto& piece = helper(2);
        piece.kind = kind_piece;
        set_whole(piece.x, whole(o_.x) + 0xf);
        set_whole(piece.y, whole(o_.y) - 0x16 - o_.hidden_lines);
        piece.height = o_.height;
    }

    // ---- gun turrets ($98B6A) ----

    void turret(int type, int side) {
        o_.side = side;
        s_.vehicle_type = type;
        // In the desert and the moors a soft blast does not hurt it.
        if ((w_.landscape == 3 || w_.landscape == 4) && s_.state == 5) s_.state = 0;
        if (s_.state == 7 || s_.state == 5) {
            o_.kind = kind_explosion;
            s_.waypoint_x = std::int16_t(0x8005);
            s_.waypoint_y = std::int16_t(0xfffe);
            if (o_.side != 0) free_object(w_, slot_ + 1);
            return;
        }
        o_.graphic = 0xd2;
        s_.is_vehicle = true;
        bool aimed = false;
        w_.homing_ready = false;
        if (o_.side != 0) {
            const std::vector<int> kinds = w_.objectives[7] ? std::vector<int>{0, 0x3d, 0x3e, 0x46}
                                                            : std::vector<int>{0};
            const int target = scan_for(kinds);
            if (target < 0) {
                fire_turret();
                place_piece();
                return;
            }
            if (w_.target_distance > 0x28) {
                const int r = int(w_.random());
                if (((w_.skill_average < 5) ? (r & 0x3f) : (r & 0x1f)) == 4) s_.fire_request = true;
            }
            w_.homing_ready = true;
            const auto& t = w_.objects.slots[std::size_t(target)];
            s_.aim_x = whole(t.x);
            s_.aim_y = whole(t.y);
            w_.homing_target = target;
            w_.target_height = t.height;
            aimed = true;
            point_at(s_.aim_x, s_.aim_y);
        } else {
            if (w_.driven_vehicle != slot_) return;
            point_at(w_.camera.x + w_.pointer_x - 0x10, w_.camera.y + w_.pointer_y);
            aimed = true;
        }
        (void)aimed;
        fire_turret();
        place_piece();
    }

    void point_at(int x, int y) {
        const int fx = whole(o_.x) + 8, fy = whole(o_.y) - 5;
        if (const auto d = g_.direction(x - fx, y - fy, 9)) {
            o_.frame = ((((*d - 0x10) >> 5) + 8) ^ 0xf) & 0xf;
        }
    }

    void place_piece() {
        if (o_.side == 0) return;
        auto& piece = helper(1);
        piece.kind = kind_piece;
        set_whole(piece.x, whole(o_.x) + 5);
        set_whole(piece.y, whole(o_.y) - 0xf - o_.hidden_lines);
        piece.height = o_.height;
    }

    // $98CCE: a turret fires from its muzzle: a missile, or a homing one for
    // type 9 with a target; the enemy's do not shoot at targets higher than 9.
    void fire_turret() {
        if (!s_.fire_held && !s_.fire_request) return;
        s_.fire_request = false;
        const int ox = o_.x, oy = o_.y;
        set_whole(o_.x, whole(o_.x) + muzzle_[std::size_t(o_.frame & 15)][0]);
        set_whole(o_.y, whole(o_.y) + muzzle_[std::size_t(o_.frame & 15)][1]);
        w_.launch_offset = {0, 0};
        int m = -2;
        if (s_.vehicle_type == 9 && w_.homing_ready) {
            m = launch_homing(w_.homing_target);
        } else if (!(o_.side != 0 && w_.target_height > 9)) {
            m = launch_missile(false);
        }
        if (m >= 0) {
            auto& mo = w_.objects.slots[std::size_t(m)];
            mo.height += 0x11;
            w_.walkers[std::size_t(m)].speed = 0x3c;
            request_sound(w_, 0x2c, 0x1e, slot_);
            exhaust_puff();
            s_.fire_held = false;
        } else if (m == -1) {
            s_.fire_held = true;
        }
        o_.x = ox;
        o_.y = oy;
    }

    // ---- missiles ----

    // $9B760: a missile (kind $2D) and its shadow from the vehicle (offset
    // by the launch offset), aimed 16 below +$2E/$30 (the enemy's spread at
    // random). Its slot, or -1.
    int launch_missile(bool) {
        if (w_.end_count != 0) return -1;
        auto& in_air = w_.rockets_in_air[std::size_t(std::clamp(o_.side, 0, 1))];
        if (in_air == 2 || s_.aim_x == 0) return -1;
        const int slot = allocate_pair(w_);
        if (slot < 0) return -1;
        ++in_air;
        auto& m = w_.objects.slots[std::size_t(slot)];
        auto& sh = w_.objects.slots[std::size_t(slot + 1)];
        auto& mw = w_.walkers[std::size_t(slot)];
        m.x = sh.x = o_.x;
        m.y = sh.y = o_.y;
        set_whole(m.x, whole(m.x) + w_.launch_offset[0]);
        set_whole(sh.x, whole(sh.x) + w_.launch_offset[0]);
        if (whole(m.x) < 0) return -1;
        set_whole(m.y, whole(m.y) + w_.launch_offset[1]);
        set_whole(sh.y, whole(sh.y) + w_.launch_offset[1]);
        if (whole(m.y) < 0) return -1;
        mw.wall_check = s_.vehicle_type == 0;
        set_height32(m, mw, height32(o_, s_));
        m.height -= 0xd;
        mw.waypoint_x = s_.aim_x;
        mw.waypoint_y = s_.aim_y + 0x10;
        if (o_.side != 0) {
            spread_aim(mw);
        } else {
            --mw.waypoint_x;
            mw.waypoint_y += 3;
        }
        m.graphic = 0xa3;
        sh.graphic = 0x7e;
        m.frame = 0;
        sh.frame = 1;
        mw.speed = s_.speed;
        w_.walkers[std::size_t(slot + 1)].speed = s_.speed;
        mw.step_counter = 1;
        sh.height = 0;
        set_whole(m.y, whole(m.y) + 1);
        set_whole(m.x, whole(m.x) + 3);
        m.kind = 0x2d;
        sh.kind = 3;
        m.hidden_lines = sh.hidden_lines = 0;
        m.side = sh.side = o_.side;
        m.layer = 0;
        sh.layer = -1;
        mw.state = 0;
        w_.attachments[std::size_t(slot + 1)] = {slot, m.kind};
        request_sound(w_, 0x2d, 0xf, slot_);
        return slot;
    }

    // $9B93E: a homing missile ($61 the recruits', $2E the enemy's) at
    // `target`.
    int launch_homing(int target) {
        if (w_.end_count != 0) return -1;
        auto& in_air = w_.rockets_in_air[std::size_t(std::clamp(o_.side, 0, 1))];
        if (in_air == 2 || s_.aim_x == 0) return -1;
        const int slot = allocate_pair(w_);
        if (slot < 0) return -1;
        ++in_air;
        auto& m = w_.objects.slots[std::size_t(slot)];
        auto& sh = w_.objects.slots[std::size_t(slot + 1)];
        auto& mw = w_.walkers[std::size_t(slot)];
        m.x = sh.x = o_.x;
        m.y = sh.y = o_.y;
        set_whole(m.x, whole(m.x) + w_.launch_offset[0]);
        set_whole(sh.x, whole(sh.x) + w_.launch_offset[0]);
        if (whole(m.x) < 0) return -1;
        set_whole(m.y, whole(m.y) + w_.launch_offset[1]);
        set_whole(sh.y, whole(sh.y) + w_.launch_offset[1]);
        if (whole(m.y) < 0) return -1;
        mw.homing_target = target;
        set_height32(m, mw, height32(o_, s_));
        mw.waypoint_x = s_.aim_x;
        mw.waypoint_y = s_.aim_y + 0x10;
        m.graphic = 0xa3;
        sh.graphic = 0x7e;
        m.frame = 0;
        sh.frame = 1;
        mw.speed = 0;
        w_.walkers[std::size_t(slot + 1)].speed = 0;
        mw.thrust = o_.side != 0 ? 0x400 : 0x10000;
        sh.height = 0;
        set_whole(m.y, whole(m.y) + 1);
        set_whole(m.x, whole(m.x) + 3);
        m.kind = o_.side != 0 ? 0x2e : 0x61;
        sh.kind = 3;
        m.hidden_lines = sh.hidden_lines = 0;
        m.side = sh.side = o_.side;
        m.layer = 0;
        sh.layer = -1;
        mw.state = 0;
        w_.attachments[std::size_t(slot + 1)] = {slot, m.kind};
        request_sound(w_, 0x2c, 0xf, slot_);
        return slot;
    }

    // $9AC2C: the enemy's aim is off by up to 63 (31 in the harder phases)
    // either way.
    void spread_aim(Walker& m) {
        for (int* v : {&m.waypoint_x, &m.waypoint_y}) {
            const auto r = random_word(w_);
            int d = w_.skill_average < 5 ? (r & 0x3f) : (r & 0x1f);
            if (r < 0) d = -d;
            *v += d;
        }
    }

    // $9B886: a missile flies to its aim point, speeding up, sinking to 4;
    // there (or off the map, or in a wall for a turret's) it explodes big.
    bool missile() {
        w_.jitter_next_effect = true;
        smoke_trail();
        o_.graphic = 0xa3;
        const int d = g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y), 16, 1000);
        if (d > 7) {
            if (o_.height > 4) set_height32(o_, s_, height32(o_, s_) - 0xa000);
            head_for(s_.waypoint_x, s_.waypoint_y);
            bool off = false;
            move(&off);
            if (!off) {
                o_.frame = ((((s_.heading >> 5) - 1) ^ 0xf) & 0xf);
                if (s_.speed < 0x60) s_.speed += s_.step_counter;
                if ((w_.tick_counter & 3) == 0) s_.step_counter <<= 1;
                if (!s_.wall_check) return true;
                if (!t_.probe(whole(o_.x), whole(o_.y), 2, -9).blocking) return true;
            }
        }
        explode_missile();
        return true;
    }

    // $9BA52: a homing missile follows its target (8 right and below it),
    // matching its height; it slows in turns (the enemy's) and explodes at
    // it.
    bool homing_missile() {
        if (s_.state != 0) {
            explode_missile();
            return true;
        }
        w_.jitter_next_effect = true;
        smoke_trail();
        o_.graphic = 0xa3;
        const int target = s_.homing_target;
        if (target < 0 || !w_.objects.slots[std::size_t(target)].used ||
            whole(w_.objects.slots[std::size_t(target)].x) < 0) {
            s_.speed += 2;
            o_.height -= 2;
            if (o_.height < 0) {
                o_.height = 0;
                explode_missile();
                return true;
            }
            move();
            return true;
        }
        const auto& t = w_.objects.slots[std::size_t(target)];
        const int tx = whole(t.x) + 8, ty = whole(t.y) + 8;
        const int d = g_.distance(tx - whole(o_.x), ty - whole(o_.y));
        if (d <= (s_.speed >> 4) + 1) {
            explode_missile();
            return true;
        }
        if (t.height > 0x10) {
            if (o_.height < t.height) set_height32(o_, s_, height32(o_, s_) + 0x8000);
            else if (o_.height > t.height) set_height32(o_, s_, height32(o_, s_) - 0x8000);
        } else if (d <= 0x30) {
            o_.height += (t.height - o_.height) >> 3;
        } else if (t_.sight_blocked(whole(o_.x), whole(o_.y), tx, ty)) {
            if (o_.height < 0x18) set_height32(o_, s_, height32(o_, s_) + 0x28000);
        } else if (o_.height > 8) {
            set_height32(o_, s_, height32(o_, s_) - 0x12000);
        }
        head_for(tx, ty);
        move();
        const int before = o_.frame;
        o_.frame = ((((s_.heading >> 5) - 1) ^ 0xf) & 0xf);
        if (o_.side != 0 && o_.frame != before) {
            if (s_.speed > 0x1e) {
                s_.thrust = 0;
                s_.speed -= 2;
                if (s_.speed < 0) s_.speed = 0;
            }
            return true;
        }
        if (s_.speed > 0x3c) {
            s_.speed = 0x3c;
        } else {
            s_.speed += std::int16_t(std::uint32_t(s_.thrust) >> 16);
            s_.thrust += 0x200;
        }
        return true;
    }

    void explode_missile() { // $9B926 / $9BB92
        o_.kind = kind_big_explosion;
        set_whole(o_.y, whole(o_.y) - 4);
        auto& in_air = w_.rockets_in_air[std::size_t(std::clamp(o_.side, 0, 1))];
        if (in_air > 0) --in_air;
        free_object(w_, slot_ + 1);
    }

    // ---- helicopters ($955E0) ----

    // $955E0: a helicopter (graphic $8B) with its rotor ($8C, next slot) and
    // shadow ($8D, the slot after). It climbs to 32 and flies to +$26/$28,
    // slowing within 96; the recruits' hovers turning to the pointer, lands
    // when told (not lower than the ground allows), the enemy's lands where
    // it has arrived on open ground. Shot down it spins and falls, then
    // explodes; its rotor flies off.
    void helicopter_core() {
        bool rotor_running = false;
        const int effect = tables_.soldiers.rotor_sound(int(w_.random() & 0xe) >> 1);
        const bool loud = o_.side == 0 ? (o_.height >= 2 || w_.driven_vehicle == slot_) : o_.height > 1;
        if (loud) {
            rotor_running = true;
            engine_sound(effect, std::max(o_.height, 0x10));
        }
        s_.is_vehicle = true;
        if (s_.state != 0) {
            if (o_.kind == 0x6b && w_.rescue_x >= 0) {
                spin_unhurt();
                rotor(rotor_running);
                return;
            }
            crash();
            return;
        }
        o_.graphic = 0x8b;
        bool climb = true;
        if (s_.wait == 0) {
            const int d = g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y));
            to_target_ = d;
            if (d < 0x60 && (d >> 1) <= s_.speed) {
                climb = false;
                const int half = d >> 1;
                s_.speed = half;
                if (o_.side == 0) {
                    if (s_.land) {
                        descend(0xc000);
                        landing_height();
                    }
                } else if (half == 0) {
                    enemy_landing();
                }
            }
        }
        if (climb) {
            bool steady = true;
            if (o_.height < 0x20) {
                ++o_.height;
                steady = o_.height >= 0x12;
            }
            if (steady) {
                if (s_.speed < 0x30) s_.speed += 2;
                if (to_target_ < 0x60 && s_.land) descend(0xc000);
            }
        }
        if (s_.waypoint_x >= 0 && s_.waypoint_y >= 0 && o_.height != 0) move();
        bool set_frame = true;
        if (o_.side == 0 && s_.speed == 0 && o_.height >= 0x20) {
            // Hovering: it turns to the pointer.
            s_.speed = 0x1e;
            turn_and_move(w_.camera.x + w_.pointer_x - 0x10, w_.camera.y + w_.pointer_y + 0x20);
            s_.speed = 0;
        } else {
            steer(s_.waypoint_x, s_.waypoint_y, 0x3a);
            set_frame = false;
        }
        if (set_frame && (o_.side == 0 || s_.speed != 0)) o_.frame = s_.facing;
        rotor(rotor_running);
        auto& sh = helper(2);
        sh.graphic = 0x8d;
        sh.frame = std::min(o_.height >> 4, 2);
        sh.layer = -1;
        set_whole(sh.x, whole(o_.x) + (o_.height >> 1) + 9);
        set_whole(sh.y, whole(o_.y) + (o_.height >> 1) - 1);
        if (o_.side == 0 && o_.height != 0) {
            crush(whole(o_.x), whole(o_.x) + 0x1e, whole(o_.y) - 0x14, whole(o_.y), o_.height, o_.height + 0xe);
        }
        o_.side = 0;
    }

    void descend(std::int32_t by) {
        std::int32_t h = height32(o_, s_) - by;
        if (h < 0) h = 0;
        set_height32(o_, s_, h);
    }

    // $A2246 with $A22B8: a recruit under a bridge or in a cave is out of a
    // vehicle's reach (either kind of a split tile).
    bool sheltered(int x, int y) const {
        const int a = t_.tile_attribute(x, y);
        if (a & 0x8000) return tables_.mission.sheltered(a & 15) || tables_.mission.sheltered((a >> 4) & 15);
        return tables_.mission.sheltered(a & 15);
    }

    // $9A630: it keeps above trees, water and walls (12, 14 or 20).
    void landing_height() {
        const int a = t_.probe(whole(o_.x), whole(o_.y), 0xf, -0xa).attribute;
        int least = 0;
        if (a == 1 || a == 4 || a == 5 || a == 6 || a == 0xb) least = 0xc;
        else if (a == 2 || a == 9 || a == 0xa) least = 0xe;
        else if (a == 3) least = 0x14;
        if (o_.height < least) ++o_.height;
    }

    // $95840: the enemy's lands where the ground is open under it and 16 to
    // either side.
    void enemy_landing() {
        for (const int dx : {0, -0x10, 0x10}) {
            const int a = t_.tile_attribute(whole(o_.x) + dx, whole(o_.y));
            const bool bad = (a & 0x8000) ? (tables_.mission.civilian_avoids(a & 15) ||
                                             tables_.mission.civilian_avoids((a >> 4) & 15))
                                          : tables_.mission.civilian_avoids(a & 15);
            if (bad) return;
        }
        if (o_.height != 0) descend(0x8000);
    }

    void rotor(bool running) {
        auto& r = helper(1);
        r.graphic = 0x8c;
        r.x = o_.x;
        r.y = o_.y;
        set_whole(r.y, whole(o_.y) + 1);
        r.height = o_.height + 0xe;
        if (running || height32(o_, s_) != 0) {
            if (o_.height >= 0xc || (w_.tick_counter & 1) == 0) r.frame = (r.frame + 1) & 3;
        }
    }

    // $95716: the last mission's helicopter shrugs off hits: one turn round.
    void spin_unhurt() {
        if (s_.flags75 != 0x71) {
            s_.flags75 = 0x71;
            s_.hits = o_.frame;
        }
        o_.frame = (o_.frame + 1) & 0xf;
        if (o_.frame == s_.hits) {
            s_.flags75 = 0;
            s_.state = 0;
        }
    }

    void crash() { // $95656
        if (!s_.crashing) {
            s_.crashing = true;
            s_.vertical_speed = 0x10000;
            s_.fall_count = int(w_.random() & 0xf) + 0x13;
        }
        w_.jitter_next_effect = true;
        exhaust_puff();
        move();
        s_.vertical_speed += 0x2000;
        o_.frame = (o_.frame + std::int16_t(std::uint32_t(s_.vertical_speed) >> 16)) & 0xf;
        if (--s_.fall_count >= 0) {
            std::int32_t h = height32(o_, s_) - 0x18000;
            if (h >= 0) {
                set_height32(o_, s_, h);
                rotor(true);
                return;
            }
            set_height32(o_, s_, 0);
        }
        o_.kind = kind_explosion;
        s_.waypoint_x = std::int16_t(0x8010);
        s_.waypoint_y = std::int16_t(0xfff7);
        if (slot_ + 1 < object_slots) {
            auto& r = helper(1);
            auto& rw = w_.walkers[std::size_t(slot_ + 1)];
            r.kind = 0x35;
            rw.heading = int(w_.random() & 0x1fe);
            rw.speed = 0x60;
        }
        free_object(w_, slot_ + 2);
        if (w_.enemy_helicopter) free_object(w_, slot_ + 3);
    }

    // $9BFE8: turn by up to 8 (16 when fast) a tick towards (x, y) and fly.
    void turn_and_move(int x, int y) {
        if (s_.wait != 0) --s_.wait;
        if (s_.wait == 0) {
            const int old = s_.heading;
            head_for(x, y);
            const int diff = std::int16_t(s_.heading - old);
            turn_ = tables_.soldiers.turn(((((diff >> 5) - 1) ^ 0xf) & 0xf));
            s_.heading = old;
        }
        int d = turn_ * 8;
        if (s_.speed > 0x18) d *= 2;
        s_.heading = (s_.heading + d) & 0x1fe;
        s_.facing = ((((s_.heading >> 5) - 1) ^ 0xf) & 0xf);
        if (s_.speed != 0) o_.frame = s_.facing >> 1;
    }

    // $9A954: the recruits' helicopter (types 5-8: none, grenades,
    // missiles, homing missiles).
    void recruits_helicopter(int type) {
        s_.vehicle_type = type;
        w_.enemy_helicopter = false;
        if (s_.state != 0) {
            o_.side = 0;
            helicopter_core();
            o_.side = 0;
            return;
        }
        helicopter_weapons();
        o_.side = 0;
        helicopter_core();
        o_.side = 0;
        helper(1).kind = kind_nothing;
        helper(2).kind = kind_nothing;
    }

    // $9842C: the last mission's helicopters fly themselves to the rescue
    // point once it is known, and land there.
    void rescue_autopilot() {
        if (s_.flags75 != 0) return;
        s_.waypoint_x = whole(o_.x);
        s_.waypoint_y = whole(o_.y);
        if (w_.rescue_x < 0) return;
        s_.waypoint_x = w_.rescue_x;
        s_.waypoint_y = w_.rescue_y;
        if (g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y), 0x3f, 1000) < 0x2c) {
            s_.flags75 = 0xff;
            s_.land = true;
        }
    }

    // $9A7B6: the helicopter's weapon on a right-button press (or a shot
    // that waited for a free slot), only above 31.
    void helicopter_weapons() {
        if (!s_.fire_held && !s_.fire_request) return;
        s_.fire_request = false;
        if (o_.height <= 0x1f) return;
        bool fired = true;
        switch (s_.vehicle_type) {
        case 6: fired = drop_grenade(); break;
        case 7:
        case 8: {
            w_.launch_offset = {0xe, -0xc};
            fired = false;
            if (s_.vehicle_type == 8) {
                const int target = homing_target_at_pointer();
                if (target >= 0) fired = launch_homing(target) >= 0;
            }
            if (!fired) fired = launch_missile(false) >= 0;
            break;
        }
        default: return;
        }
        s_.fire_held = !fired;
    }

    int homing_target_at_pointer() { return homing_target_near_pointer(w_, tables_, o_.side); }

    // $9AC76: a grenade dropped at the aim point (the enemy's spread): its
    // flight lasts the distance / 5 (8 to 100 ticks).
    bool drop_grenade() {
        if (w_.end_count != 0) return false;
        auto& in_air = w_.grenades_in_air[std::size_t(std::clamp(o_.side, 0, 1))];
        if (in_air == 2 || s_.aim_x == 0) return false;
        const int slot = allocate_pair(w_);
        if (slot < 0) return false;
        ++in_air;
        auto& g = w_.objects.slots[std::size_t(slot)];
        auto& sh = w_.objects.slots[std::size_t(slot + 1)];
        auto& gw = w_.walkers[std::size_t(slot)];
        g.x = sh.x = o_.x;
        g.y = sh.y = o_.y;
        g.graphic = 0x7d;
        sh.graphic = 0x7e;
        gw.waypoint_x = s_.aim_x;
        gw.waypoint_y = s_.aim_y + 6;
        if (o_.side != 0) spread_aim(gw);
        int flight = g_.distance(gw.waypoint_x - whole(o_.x), gw.waypoint_y - whole(o_.y));
        flight = std::min(std::max(flight, 0x28) / 5, 0x64);
        gw.state_timer = flight;
        gw.speed = 0x32;
        w_.walkers[std::size_t(slot + 1)].speed = 0x32;
        g.height = o_.height + 2;
        gw.height_fraction = 0;
        sh.height = 0;
        set_whole(g.y, whole(g.y) + 1);
        set_whole(g.x, whole(g.x) + 3);
        gw.vertical_speed = 0; // (the original works out a lift and then clears it)
        g.kind = 2;
        sh.kind = 3;
        g.hidden_lines = sh.hidden_lines = 0;
        g.side = sh.side = o_.side;
        g.layer = 0;
        sh.layer = -1;
        gw.state = 0;
        w_.attachments[std::size_t(slot + 1)] = {slot, g.kind};
        if (o_.side != 0) gw.state_timer += 10;
        return true;
    }

    // $9ADC0: the enemy's helicopter. It waits at its home a while (longer
    // for lower skills), then flies to random open places; while out it
    // attacks a recruit in sight within 250 by its type, drops soldiers
    // when it lands, and flies home when the patrol is over.
    void enemy_helicopter(int type) {
        s_.vehicle_type = type;
        w_.enemy_helicopter = true;
        if (s_.state == 1) s_.state = 0;
        if (s_.state != 0) {
            o_.side = 1;
            helicopter_core();
            o_.side = 1;
            w_.enemy_helicopter = false;
            place_helicopter_piece();
            return;
        }
        if (s_.idle == 0) {
            s_.rest_x = whole(o_.x);
            s_.rest_y = whole(o_.y);
            s_.idle = 0xff;
        }
        if (s_.stopped != 0) --s_.stopped;
        const auto wander = [&] { // $9AE78
            if (s_.stopped != 0) return;
            const int tx = int(w_.random() & 0x7f) + 4;
            if (tx >= t_.width_tiles()) return;
            const int ty = int(w_.random() & 0x3f) + 4;
            if (ty >= t_.height_tiles()) return;
            const int x = tx * 16 + int(w_.random() & 0xf), y = ty * 16 + int(w_.random() & 0xf);
            const int a = t_.tile_attribute(x, y);
            const bool avoid = (a & 0x8000) ? (tables_.mission.civilian_avoids(a & 15) ||
                                               tables_.mission.civilian_avoids((a >> 4) & 15))
                                            : tables_.mission.civilian_avoids(a & 15);
            if (avoid) return;
            s_.waypoint_x = x;
            s_.waypoint_y = y;
            s_.stopped = 0x5a;
        };
        bool at_home = false;
        if (o_.height == 0 && g_.distance(s_.rest_x - whole(o_.x), s_.rest_y - whole(o_.y)) <= 0x14) at_home = true;
        if (at_home) {
            const bool may_leave = s_.patrol >= 0x1f4 && !(o_.kind == 0x6b && w_.rescue_x < 0);
            if (may_leave && (w_.random() & 1) == 0) {
                wander();
            } else {
                const int p = s_.patrol + 1 + w_.skill_average;
                if (p <= 0x7fff) s_.patrol = p;
            }
        } else {
            bool keep = false;
            if (type != 5) {
                const int index = std::clamp(s_.target, 0, 29);
                const int r = w_.recruit_lists[std::size_t(index)];
                if (r >= 0) {
                    const auto& t = w_.objects.slots[std::size_t(r)];
                    if (t.hidden_lines == 0 && !sheltered(whole(t.x) + 8, whole(t.y) - 5)) {
                        const int d = g_.distance(whole(t.x) - whole(o_.x), whole(t.y) - whole(o_.y));
                        w_.target_distance2 = d;
                        if (d < 0xfa) keep = attack(r);
                    }
                }
                if (!keep && ++s_.target >= 0x1e) s_.target = 0;
            }
            if (s_.patrol != 0) {
                --s_.patrol;
                if ((w_.random() & 0x1f) == 0) {
                    wander();
                } else if (o_.height == 0 && (w_.random() & 0xf) == 0) {
                    spawn_enemy(w_, slot_);
                }
            } else {
                s_.waypoint_x = s_.rest_x;
                s_.waypoint_y = s_.rest_y;
            }
        }
        o_.side = 1;
        helicopter_core();
        o_.side = 1;
        w_.enemy_helicopter = false;
        helper(1).kind = kind_nothing;
        helper(2).kind = kind_nothing;
        place_helicopter_piece();
    }

    void place_helicopter_piece() { // $9AFD0
        static constexpr int at[16][2] = {{0xe, -0x10}, {0xb, -0xf}, {0xa, -0x10}, {0xa, -0x14}, {0xa, -0x17},
                                          {0xa, -0x17}, {0xb, -0x18}, {0xd, -0x19}, {0xe, -0x19}, {0x10, -0x19},
                                          {0x12, -0x17}, {0x12, -0x16}, {0x13, -0x17}, {0x13, -0x14},
                                          {0x13, -0x10}, {0x13, -0xf}};
        auto& piece = helper(3);
        piece.kind = kind_piece;
        set_whole(piece.x, whole(o_.x) + at[o_.frame & 15][0]);
        set_whole(piece.y, whole(o_.y) + at[o_.frame & 15][1]);
        piece.height = o_.height;
    }

    // $9B042: the attack on recruit `r` by the helicopter's type; true to
    // keep the target (15 ticks in 16).
    bool attack(int r) {
        const auto& t = w_.objects.slots[std::size_t(r)];
        const int tx = whole(t.x), ty = whole(t.y);
        if (s_.vehicle_type != 7 && s_.vehicle_type != 8) {
            s_.waypoint_x = std::max(tx + 0x14 - 0x1e, 0);
            s_.waypoint_y = ty;
            s_.aim_x = tx;
            s_.aim_y = ty;
            if (o_.height > 0x1f && (w_.tick_counter & 0xf) == 0) {
                drop_grenade();
                if ((w_.random() & 0xf) == 0) return false;
            }
            return true;
        }
        s_.waypoint_x = s_.aim_x = tx;
        s_.waypoint_y = s_.aim_y = ty;
        const int d = w_.target_distance2;
        if (d <= 0x3c) {
            if (s_.speed >= 0 || s_.speed >= -0x22) s_.speed -= 3; // backs off
        } else if (d < 0x8c) {
            if (o_.height >= 0x19 && (w_.tick_counter & 0xf) == 0) {
                w_.launch_offset = {0xe, -0xc};
                if (s_.vehicle_type == 8) {
                    const int m = launch_homing(r);
                    if (m >= 0) {
                        w_.walkers[std::size_t(m)].thrust = 0x2000;
                        w_.walkers[std::size_t(m)].speed = 0x14;
                    }
                } else {
                    launch_missile(false);
                }
            }
            if (s_.speed >= 0) {
                s_.speed -= 4;
                if (s_.speed < 0) s_.speed = 0;
            }
        }
        return (w_.random() & 0xf) != 0;
    }

    // ---- the recruits in their vehicles ----

    // $99E5C: a gun shot of a jeep (kind $4D): two steps a tick (one once
    // it has reached its aim point), a flash where it hurts something, a
    // puff where it ends.
    bool heavy_shot_update() {
        auto& b = w_.bullets[std::size_t(slot_)];
        b.age = (b.age + 1) & 0xff;
        if (b.lifetime == 0) return shot_ends();
        --b.lifetime;
        ++s_.step_counter;
        bool off = false;
        bool second_step = true;
        if (s_.wait == 0) {
            move(&off);
            if (off) return shot_gone();
            const int d = g_.distance(s_.aim_x - whole(o_.x), s_.aim_y - whole(o_.y));
            if (d <= int(unsigned(std::uint16_t(s_.speed)) >> 3)) {
                o_.layer = 0;
                set_whole(o_.x, s_.aim_x);
                set_whole(o_.y, s_.aim_y);
                s_.wait = 0xff;
                second_step = false;
            }
        }
        if (second_step) {
            o_.layer = -1;
            move(&off);
            if (off) return shot_gone();
        }
        if (b.age > 3 && t_.probe(whole(o_.x), whole(o_.y), 0, -9).blocking) return shot_ends();
        const int hit = bullet_hit(w_, t_, tables_, slot_);
        if (hit == 0) return true;
        if (hit == 1) return shot_ends();
        s_.idle = 1;
        o_.layer = 1;
        set_whole(o_.y, whole(o_.y) - 5);
        if (whole(o_.y) < 0) return shot_gone();
        o_.graphic = 0x96;
        o_.frame = 0;
        return true;
    }
    // $994BC: a civilian's spear (kind $47): turned once to its aim point
    // (with its spread), then straight on at its cruising speed; it hurts a
    // recruit it reaches (a flash) or ends in a wall or after its time.
    bool spear_update() {
        auto& b = w_.bullets[std::size_t(slot_)];
        b.age = (b.age + 1) & 0xff;
        if (b.lifetime == 0) return spear_ends();
        --b.lifetime;
        ++s_.step_counter;
        bool off = false;
        if (s_.wait == 0) {
            save_position();
            if (!b.spread_done) {
                head_for(s_.aim_x, s_.aim_y);
                if (b.spread != 0) s_.heading = (s_.heading + b.spread) & 0x1fe;
                b.spread_done = true;
                if (b.first_heading < 0) b.first_heading = s_.heading;
            }
            move(&off);
            if (off) return shot_gone();
            s_.speed = b.cruise_speed;
        } else {
            o_.layer = -1;
            move(&off);
            if (off) return shot_gone();
        }
        if (b.age > 2 && t_.probe(whole(o_.x), whole(o_.y), 0, -9).blocking) return spear_ends();
        const int hit = bullet_hit(w_, t_, tables_, slot_);
        if (hit == 0) {
            if (o_.graphic == 0xd7) o_.frame = (((((s_.heading >> 5) - 1) ^ 0xf) & 0xf) >> 1);
            return true;
        }
        if (hit == 1) return spear_ends();
        s_.idle = 1;
        o_.layer = 1;
        set_whole(o_.y, whole(o_.y) - 5);
        if (whole(o_.y) < 0) return shot_gone();
        o_.graphic = 0x96;
        o_.frame = 0;
        request_sound(w_, 0x2a, 0xf, slot_);
        return true;
    }
    bool spear_ends() { // $99604
        if (s_.idle == 0) {
            bullet_hit(w_, t_, tables_, slot_);
            s_.idle = 0xff;
            o_.layer = -1;
            set_whole(o_.y, whole(o_.y) - 5);
            if (whole(o_.y) < 0) return shot_gone();
            o_.frame = 3;
            request_sound(w_, 0x2a, 0xf, slot_);
        }
        w_.bullets[std::size_t(slot_)].lifetime = 0;
        if (s_.idle == 1) { // $995EC
            o_.graphic = 0x96;
            if (++o_.frame >= 4) return shot_gone();
            return true;
        }
        o_.graphic = 0x7f;
        if (++o_.frame >= 8) return shot_gone();
        return true;
    }

    bool shot_ends() { // $99F3A
        if (s_.idle == 0) {
            bullet_hit(w_, t_, tables_, slot_);
            s_.idle = 0xff;
            o_.layer = -1;
            set_whole(o_.y, whole(o_.y) - 5);
            if (whole(o_.y) < 0) return shot_gone();
            o_.frame = 3;
        }
        w_.bullets[std::size_t(slot_)].lifetime = 0;
        if (s_.idle == 1) { // $99F22: the flash
            o_.graphic = 0x96;
            if (++o_.frame >= 4) return shot_gone();
            return true;
        }
        o_.graphic = 0x7f;
        if (++o_.frame >= 8) return shot_gone();
        return true;
    }
    bool shot_gone() {
        --w_.bullets_alive;
        free_object(w_, slot_);
        return true;
    }

    bool falling_rotor() { // $9A694
        o_.frame = (o_.frame + 1) & 3;
        bool off = false;
        move(&off);
        if (off) {
            o_.kind = kind_explosion;
        } else {
            if (--s_.speed < 0) s_.speed = 0;
            const std::int32_t h = height32(o_, s_) - 0x8000;
            if (h < 0) {
                set_height32(o_, s_, 0);
                o_.kind = kind_explosion;
            } else {
                set_height32(o_, s_, h);
            }
        }
        smoke_trail();
        return true;
    }

    // ---- shared ----

    // $98E64: one candidate a tick from the object table (the scan index
    // runs through the 43 slots): a living object of the listed kinds within
    // 210, seen or within 64 (within 40 always). Its slot, or -1.
    int scan_for(const std::vector<int>& kinds) {
        const int index = std::clamp(s_.target, 0, object_slots - 1);
        const auto& t = w_.objects.slots[std::size_t(index)];
        const auto next = [&] {
            if (++s_.target >= object_slots) s_.target = 0;
            return -1;
        };
        if (!t.used || whole(t.x) < 0) return next();
        if (std::find(kinds.begin(), kinds.end(), t.kind) == kinds.end()) return next();
        const int d = g_.distance(whole(t.x) - whole(o_.x), whole(t.y) - whole(o_.y));
        w_.target_distance = d;
        if (d >= 0xd2) return next();
        if (d > 0x28 && t_.sight_blocked(whole(o_.x), whole(o_.y), whole(t.x), whole(t.y)) && d >= 0x40) return next();
        return index;
    }

    // $9C058: a vehicle hit by a blast becomes an explosion (its helpers
    // freed); a bump (state $32) reverses it, then it rolls back to a stop.
    bool destroyed() {
        if (s_.state == 5 || s_.state == 7) {
            o_.kind = kind_explosion;
            s_.waypoint_x = std::int16_t(0x8010);
            s_.waypoint_y = std::int16_t(0xfff7);
            free_object(w_, slot_ + 1);
            if (o_.side != 0) free_object(w_, slot_ + 2);
            return true;
        }
        if (s_.state == 0x32) {
            s_.speed = -s_.speed;
            s_.state = 0x33;
        } else if (s_.state == 0x33) {
            s_.speed += 6;
            if (s_.speed >= 0) {
                s_.state = 0;
                s_.speed = 0;
            }
        } else {
            return false;
        }
        move();
        shadow();
        return true;
    }

    // $9C03A: height and vertical speed; landing bounces back up at half
    // speed, gravity `gravity` (16.16) a tick.
    void bounce(std::int32_t gravity) {
        std::int32_t v = s_.vertical_speed;
        std::int32_t h = height32(o_, s_) + v;
        if (h < 0) {
            h = 0;
            v = std::int32_t(std::uint32_t(-v) >> 1);
        }
        set_height32(o_, s_, h);
        s_.vertical_speed = v - gravity;
    }

    // $9BCF4: turn towards (x, y), at most `limit` - speed (times the facing
    // step's sign) a tick, the picture following the heading.
    void steer(int x, int y, int limit) {
        const int old = s_.heading;
        head_for(x, y);
        const int wanted = s_.heading;
        // $A0C5C
        int diff = std::int16_t(wanted - old);
        const int size = std::abs(diff);
        turn_ = tables_.soldiers.turn(((((diff >> 5) - 1) ^ 0xf) & 0xf));
        s_.heading = old;
        int d = (limit - s_.speed) * turn_;
        if (std::abs(std::int16_t(d)) > size) d = d < 0 ? -size : size;
        s_.heading = (s_.heading + d) & 0x1fe;
        bool turn_picture = false;
        const int type = s_.vehicle_type;
        if (type == 2 || type == 3) turn_picture = s_.speed > 8;
        else if (type == 4) turn_picture = true;
        else if (s_.speed > 6 && (w_.tick_counter & 1) == 0) turn_picture = s_.speed > 8;
        if (!turn_picture) return;
        const int sign = d == 0 ? 0 : (-d > 0 ? 1 : -1);
        // $A06C4: a frame step towards the wanted heading's picture (in the
        // turn's direction).
        const int target_frame = ((((wanted >> 5) - 1) ^ 0xf) & 0xf);
        int step = tables_.soldiers.turn((o_.frame - target_frame) & 15);
        if (step == 0) return;
        if (step != sign && sign != 0) step = sign;
        o_.frame = (o_.frame + step) & 15;
    }

    // $9BDC2: the ground under a jeep: water slows it and hides its wheels
    // (deep water drowns the recruits' in time; the enemy's do not go in),
    // bumps toss it up, ramps launch it, cliffs drop it, walls stop it.
    void ground_terrain() {
        const int was_in_water = o_.hidden_lines;
        if (corners_blocked()) {
            blocked();
            return;
        }
        const int a = t_.probe(whole(o_.x), whole(o_.y), 0xf, -0xa).attribute;
        s_.terrain = a;
        if (a == 1 || a == 2) {
            lift();
            return;
        }
        if (a == 0xe) {
            if (s_.speed != 0) {
                o_.height = 1;
                s_.vertical_speed = std::int32_t(std::uint32_t(std::uint16_t(s_.speed)) << 16 >> 2);
                s_.speed <<= 2;
            }
            return;
        }
        if (a == 3) {
            blocked();
            return;
        }
        if (a == 9 || a == 0xa) {
            s_.sink = (s_.sink + 4) & 0xff;
            set_whole(o_.y, whole(o_.y) + s_.sink);
            return;
        }
        if (s_.sink != 0) s_.state = 5;
        if (a == 7) {
            if (s_.speed != 0) {
                o_.height ^= 1;
                if (o_.height != 0 && s_.speed > 0x10) s_.speed -= 2;
            }
            return;
        }
        if (o_.side != 0) {
            if (a == 4 || a == 5 || a == 6 || a == 0xb) {
                blocked();
                return;
            }
        } else if (a == 4) {
            o_.hidden_lines = 1;
            return;
        } else if (a == 5) {
            o_.hidden_lines = 3;
            return;
        } else if (a == 6 || a == 0xb) {
            if (s_.sink != 0) s_.state = 0;
            w_.jitter_next_effect = true;
            splash();
            if (o_.hidden_lines >= 0xa0) {
                o_.hidden_lines = 0x1e;
                s_.state = 7;
                return;
            }
            o_.hidden_lines += 2;
            s_.speed -= 0xc;
            if (s_.speed < 0) s_.speed = 0;
            return;
        }
        o_.hidden_lines = 0;
        if (was_in_water != 0) lift();
    }

    void lift() { // $9BF30: out of the water or over a bump
        if (o_.height != 0) return;
        if (s_.speed < 0x14) {
            if (s_.speed == 0) return;
            o_.height = (w_.frame_counter & 1) + 3;
            return;
        }
        o_.height = 1;
        s_.vertical_speed = std::int32_t(std::uint32_t(std::uint16_t(s_.speed)) << 16 >> 3);
    }

    bool corners_blocked() const { // $9BD96
        for (const auto& c : {std::array<int, 2>{10, -13}, {20, -13}, {10, -7}, {20, -7}}) {
            if (t_.probe(whole(o_.x), whole(o_.y), c[0], c[1]).blocking) return true;
        }
        return false;
    }

    void blocked() { // $9BE82
        if (o_.side != 0) {
            s_.waypoint_x = whole(o_.x);
            s_.waypoint_y = whole(o_.y);
        }
        s_.speed = std::int16_t(-s_.speed) >> 1;
        put_back();
        if (t_.probe(whole(o_.x), whole(o_.y), 0xf, -0xa).attribute == 3) s_.state = 5;
        destroyed();
    }

    // $9BF9C: the shadow in the next slot, offset by the height (hidden in
    // water).
    void shadow() {
        auto& sh = helper(1);
        sh.graphic = 0x8d;
        sh.frame = std::min(o_.height >> 4, 2);
        set_whole(sh.x, whole(o_.x) + (o_.height >> 1) + 9);
        set_whole(sh.y, whole(o_.y) + (o_.height >> 1) - 1);
        if (o_.hidden_lines != 0) {
            sh.frame = 0;
            sh.graphic = graphic_hidden;
        }
    }

    // $98220: the first object of another side in the box (the first 30
    // slots; not buildings $48/$6A, not soldiers in a vehicle) is knocked
    // over; a vehicle knocked over takes this one with it.
    void crush(int left, int right, int top, int bottom, int low, int high) {
        if (w_.outcome != World::Outcome::playing) return; // ($82A92: off once the phase has ended)
        for (int i = 0; i < 30; ++i) {
            auto& t = w_.objects.slots[std::size_t(i)];
            if (i == slot_ || !t.used || whole(t.x) < 0) continue;
            if (tables_.hits.hittable(t.kind) == 0 || t.side == o_.side) continue;
            if (t.kind == 0x48 || t.kind == 0x6a) continue;
            const int tx = whole(t.x), ty = whole(t.y);
            if (left > tx || right < tx || top > ty || bottom < ty) continue;
            if (high < t.height || low > t.height + 0xe) continue;
            auto& tw = w_.walkers[std::size_t(i)];
            if (t.kind == 0 && tw.carried != 0) continue;
            tw.state = 5;
            if (tw.is_vehicle) s_.state = 5;
            return;
        }
    }

    // $9AB72: an exhaust puff ($39) at the vehicle while it is on screen and
    // fewer than 20 objects are drawn; the jitter flag scatters it. Its slot.
    int exhaust_puff() {
        if (!o_.on_screen || w_.drawn_objects >= 20) return -1;
        const int slot = allocate_slot(w_);
        if (slot < 0) return -1;
        auto& p = w_.objects.slots[std::size_t(slot)];
        p.x = o_.x;
        p.y = o_.y;
        if (w_.jitter_next_effect) {
            const int dx = (int(w_.random() & 0x1f) - 8);
            set_whole(p.x, whole(p.x) + dx);
            const int dy = int((unsigned(std::uint16_t(dx)) >> 4) & 0xf) - 8;
            set_whole(p.y, whole(p.y) - dy);
            w_.jitter_next_effect = false;
        }
        p.height = o_.height;
        p.hidden_lines = 0;
        p.graphic = 0xcc;
        p.frame = 4;
        p.layer = 1;
        p.kind = 0x39;
        return slot;
    }

    // $9AAFC: smoke behind a missile ($30).
    void smoke_trail() {
        if (!o_.on_screen || w_.drawn_objects >= 20) return;
        const int slot = allocate_slot(w_);
        if (slot < 0) return;
        auto& p = w_.objects.slots[std::size_t(slot)];
        p.x = o_.x;
        p.y = o_.y;
        if (!w_.jitter_next_effect) {
            set_whole(p.y, whole(p.y) - 3);
        } else {
            set_whole(p.y, whole(p.y) + 2);
            w_.jitter_next_effect = false;
        }
        p.height = o_.height;
        p.hidden_lines = 0;
        p.graphic = 0xc6;
        p.frame = 0;
        p.kind = 0x30;
    }

    // $9897C: a splash ($56/$57) at the vehicle.
    void splash() {
        request_sound(w_, 0xe, 0xa, slot_);
        if (!o_.on_screen || w_.drawn_objects >= 20) return;
        const int slot = allocate_slot(w_);
        if (slot < 0) return;
        auto& p = w_.objects.slots[std::size_t(slot)];
        p.x = o_.x;
        p.y = o_.y;
        if (w_.jitter_next_effect) {
            const int dx = int(w_.random() & 0x1f) - 5;
            set_whole(p.x, whole(p.x) + dx);
            set_whole(p.y, whole(p.y) - (int((unsigned(std::uint16_t(dx)) >> 4) & 0xf) - 10));
            w_.jitter_next_effect = false;
        }
        p.height = o_.height;
        p.hidden_lines = 0;
        p.layer = 1;
        if ((w_.random() & 1) == 0) {
            p.graphic = 0xde;
            p.frame = 4;
            p.kind = 0x56;
        } else {
            p.graphic = 0xdf;
            p.frame = 0;
            p.kind = 0x57;
        }
    }

    // $A3B60: the loudest engine of the tick plays (the driven vehicle's
    // always): its distance from the leader sets the volume.
    void engine_sound(int effect, int param) {
        if (w_.engine.locked || w_.end_count != 0 || w_.leader_slot < 0) return;
        int volume = 0x40;
        if (w_.leader_slot == slot_) {
            w_.engine.locked = true;
        } else {
            const auto& l = w_.objects.slots[std::size_t(w_.leader_slot)];
            const int d = g_.distance(whole(o_.x) - whole(l.x), whole(o_.y) - whole(l.y), 640, 640);
            volume = std::max(0x40 - (std::max(d - 0xdc, 0) >> 3), 0);
            if (volume <= 10 || volume <= w_.engine.volume) return;
        }
        w_.engine.volume = volume;
        w_.engine.effect = effect;
        w_.engine.param = param;
    }

    // Movement helpers (as the soldiers' in walking.cpp).
    bool head_for(int x, int y) { // $A0AAC
        const auto h = g_.heading_toward(std::int16_t(x - whole(o_.x)), std::int16_t(y - whole(o_.y)));
        if (!h) return false;
        s_.heading = *h;
        return true;
    }
    void move(bool* off = nullptr) { // $A0C9A
        if (off) *off = false;
        if (s_.speed == 0) return;
        s_.heading &= 0x1fe;
        o_.x += (g_.sine(s_.heading) >> 2) * std::int16_t(s_.speed);
        if (o_.x < 0) {
            o_.x = 0;
            if (off) *off = true;
        }
        o_.y += (g_.sine(s_.heading + 0x80) >> 2) * std::int16_t(s_.speed);
        if (o_.y < 0) {
            o_.y = 0;
            if (off) *off = true;
        }
    }
    void save_position() { // $94FFA
        moved_back_ = false;
        old_x_ = o_.x;
        old_y_ = o_.y;
    }
    void put_back() { // $95010
        moved_back_ = true;
        o_.x = old_x_;
        o_.y = old_y_;
    }
    GameObject& helper(int n) {
        static GameObject none;
        const int i = slot_ + n;
        return i < object_slots ? w_.objects.slots[std::size_t(i)] : none;
    }

    World& w_;
    const Terrain& t_;
    const ProgramTables& tables_;
    const GeometryTables& g_;
    int slot_;
    GameObject& o_;
    Walker& s_;
    int turn_{};
    int to_target_{};
    bool moved_back_{};
    std::int32_t old_x_{}, old_y_{};

    // $99BA2: the muzzle by the turret's frame.
    static constexpr int muzzle_[16][2] = {{0, 7}, {-9, 6}, {-14, 3}, {-16, 0}, {-19, -7}, {-19, -12}, {-16, -16},
                                           {-10, -20}, {-2, -22}, {6, -20}, {12, -15}, {16, -12}, {18, -6},
                                           {16, 0}, {12, 2}, {6, 5}};
};

// The boarding point of a vehicle ($95A88 / $9A87E): 16 right (4 for a
// turret) and 9 above its position.
std::array<int, 2> boarding_point(const World& world, int vehicle) {
    const auto& v = world.objects.slots[std::size_t(vehicle)];
    const int type = world.walkers[std::size_t(vehicle)].vehicle_type;
    return {whole(v.x) + ((type == 0 || type == 1) ? 4 : 0x10), whole(v.y) - 9};
}

} // namespace

bool update_jeep(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    Vehicle v(world, terrain, tables, slot);
    switch (world.objects.slots[std::size_t(slot)].kind) {
    case 0x3f: v.recruits_jeep(2); return true;
    case 0x40: v.recruits_jeep(3); return true;
    case 0x50: v.enemy_jeep(2); return true;
    case 0x51: v.enemy_jeep(3); return true;
    case 0x52: v.enemy_jeep(10); return true;
    default: return false;
    }
}

bool update_tank(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    Vehicle v(world, terrain, tables, slot);
    if (world.objects.slots[std::size_t(slot)].kind == 0x41) v.recruits_tank();
    else v.enemy_tank();
    return true;
}

bool update_turret(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    Vehicle v(world, terrain, tables, slot);
    switch (world.objects.slots[std::size_t(slot)].kind) {
    case 0x4e: v.turret(0, 0); return true;
    case 0x4f: v.turret(1, 0); return true;
    case 0x54: v.turret(0, 1); return true;
    case 0x55: v.turret(1, 1); return true;
    case 0x69: v.turret(9, 1); return true;
    default: return false;
    }
}

bool update_missile(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    Vehicle v(world, terrain, tables, slot);
    if (world.objects.slots[std::size_t(slot)].kind == 0x2d) return v.missile();
    return v.homing_missile();
}

bool update_helicopter(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    Vehicle v(world, terrain, tables, slot);
    switch (world.objects.slots[std::size_t(slot)].kind) {
    case 0x31: v.recruits_helicopter(6); return true;
    case 0x32: v.recruits_helicopter(5); return true;
    case 0x33: v.recruits_helicopter(7); return true;
    case 0x34: v.recruits_helicopter(8); return true;
    case 0x65: v.rescue_autopilot(); v.recruits_helicopter(6); return true;
    case 0x66: v.rescue_autopilot(); v.recruits_helicopter(5); return true;
    case 0x67: v.rescue_autopilot(); v.recruits_helicopter(7); return true;
    case 0x68: v.rescue_autopilot(); v.recruits_helicopter(8); return true;
    case 0x28: v.enemy_helicopter(6); return true;
    case 0x2a: v.enemy_helicopter(5); return true;
    case 0x2b: v.enemy_helicopter(7); return true;
    case 0x2c: case 0x6b: v.enemy_helicopter(8); return true;
    default: return false;
    }
}

bool update_falling_rotor(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Vehicle(world, terrain, tables, slot).falling_rotor();
}

int homing_target_near_pointer(const World& world, const ProgramTables& tables, int side) { // $9A99A
    int px = world.camera.x + world.pointer_x - 0x10;
    if (px == 0) px = 1;
    const int py = world.camera.y + world.pointer_y + 8;
    for (int i = 0; i < object_slots; ++i) {
        const auto& t = world.objects.slots[std::size_t(i)];
        if (!t.used || whole(t.x) < 0) continue;
        if (tables.hits.homing(t.kind) == 0 || t.side == side) continue;
        if (tables.geometry.distance(whole(t.x) - px, whole(t.y) - t.height - py, 0x1f, 1000) < 0x16) return i;
    }
    return -1;
}

bool update_spear(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Vehicle(world, terrain, tables, slot).spear_update();
}

bool update_heavy_shot(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Vehicle(world, terrain, tables, slot).heavy_shot_update();
}

void update_boarding(World& world) {
    // $9A8FC: a squad's count runs out: everybody not yet in is put in.
    for (int squad = 0; squad < 3; ++squad) {
        auto& count = world.boarding_count[std::size_t(squad)];
        if (count == 0 || --count != 0) continue;
        for (const int m : world.squads[std::size_t(squad)].members) world.walkers[std::size_t(m)].state = 0x5a;
    }
    // $9A730: the selected squad all in the same vehicle: it is the squad's
    // vehicle, and its "leader" (the camera and the tests follow it).
    world.driven_vehicle = -1;
    if (world.selected_squad < 0) return;
    const auto& members = world.squads[std::size_t(world.selected_squad)].members;
    int vehicle = -1;
    for (const int m : members) {
        const auto& w = world.walkers[std::size_t(m)];
        if (w.carried == 0) return;
        vehicle = w.vehicle;
    }
    if (vehicle < 0) return;
    world.driven_vehicle = vehicle;
    world.leader_slot = vehicle;
}

bool ride(World& world, const ProgramTables&, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (w.carried == 0) return false;
    o.graphic = graphic_hidden;
    o.frame = 0;
    w.state = 0;
    const int vehicle = w.vehicle;
    if (vehicle < 0) return true;
    const auto& v = world.objects.slots[std::size_t(vehicle)];
    if (!v.used || whole(v.x) < 0 || v.kind == kind_explosion) {
        // The vehicle is gone: he is thrown out, hit.
        w.carried = 0;
        w.boarding = -1;
        w.vehicle = -1;
        w.state = 1;
        w.hit_timer = 0xff;
        w.heading = int(world.random() & 0x1fe);
    }
    o.x = v.x;
    o.y = v.y;
    o.height = v.height;
    return true;
}

void board(World& world, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (w.boarding < 0) return;
    const auto at = boarding_point(world, w.boarding);
    if (tables.geometry.distance(at[0] - (whole(o.x) + 4), at[1] - whole(o.y), 0x1f, 1000) >= 0xd) return;
    w.vehicle = w.boarding;
    w.carried = 0xff;
    choose_leader(world);
    if (o.squad >= 0 && o.squad < 3) {
        const auto& members = world.squads[std::size_t(o.squad)].members;
        if (!members.empty() && members.front() == slot) world.boarding_count[std::size_t(o.squad)] = 0x190;
    }
}

int board_by_puff(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (w.state == 0x5a) {
        if (w.boarding < 0) {
            w.state = 0;
            return -1;
        }
        w.state = 0x5b;
        o.graphic = 0xcc;
        o.frame = 4;
        return 1;
    }
    // State $5B: the puff (the soldier walks on under it), then he is at the
    // vehicle's door.
    if (--o.frame >= 0) return 2; // the caller steps him at speed $30
    if (w.boarding < 0) {
        w.state = 0;
        return -1;
    }
    const auto at = boarding_point(world, w.boarding);
    set_whole(o.x, at[0]);
    set_whole(o.y, at[1]);
    o.frame = 0;
    o.graphic = graphic_hidden;
    w.state = 0;
    return -1;
}

void choose_vehicle(World& world, int x, int y, const HitTables& hits) { // $9EEB0
    if (world.selected_squad < 0) return;
    auto& members = world.squads[std::size_t(world.selected_squad)].members;
    for (const int m : members) world.walkers[std::size_t(m)].boarding = -1;
    for (int i = 0; i < object_slots; ++i) {
        const auto& v = world.objects.slots[std::size_t(i)];
        const auto& vw = world.walkers[std::size_t(i)];
        if (!v.used || whole(v.x) < 0 || !vw.is_vehicle || v.side != 0 || v.height != 0) continue;
        int left = whole(v.x);
        if (vw.vehicle_type == 0 || vw.vehicle_type == 1) left -= 8;
        if (x < left || x > left + hits.width(v.kind)) continue;
        if (y < whole(v.y) - hits.above(v.kind) - 0x14 || y > whole(v.y) + hits.below(v.kind)) continue;
        for (const int m : members) world.walkers[std::size_t(m)].boarding = i;
        return;
    }
}

void drive(World& world, const Terrain& terrain, bool press, int pointer_x, int pointer_y, const HitTables& hits) {
    const int vehicle = world.driven_vehicle;
    if (vehicle < 0 || world.leader_slot < 0) return;
    if (!world.left_held) {
        world.vehicle_click_latched = false;
        return;
    }
    if (world.vehicle_click_latched) return;
    auto& v = world.objects.slots[std::size_t(vehicle)];
    auto& vw = world.walkers[std::size_t(vehicle)];
    if (press) {
        // $9EF96: a click on the own vehicle.
        const int x = pointer_x - 0xf, y = pointer_y - 3;
        const bool on_it = v.used && whole(v.x) >= 0 && vw.is_vehicle && v.side == 0 && x >= whole(v.x) &&
                           x <= whole(v.x) + hits.width(v.kind) &&
                           y >= whole(v.y) - v.height - hits.above(v.kind) - 0x14 && y <= whole(v.y) - v.height;
        if (on_it && v.height != 0) {
            vw.land = true; // a flying helicopter lands
            world.vehicle_click_latched = true;
            return;
        }
        if (on_it && !terrain.probe(whole(v.x), whole(v.y), 8, -3).blocking) {
            // Everybody out.
            if (world.selected_squad >= 0) {
                world.boarding_count[std::size_t(world.selected_squad)] = 0;
                for (const int m : world.squads[std::size_t(world.selected_squad)].members) {
                    auto& mo = world.objects.slots[std::size_t(m)];
                    auto& mw = world.walkers[std::size_t(m)];
                    if (mw.carried == 0) continue;
                    mw.carried = 0;
                    const int from = mw.vehicle;
                    mw.vehicle = -1;
                    mw.boarding = -1;
                    const auto& fv = world.objects.slots[std::size_t(from)];
                    mo.x = fv.x;
                    mo.y = fv.y;
                    const int type = world.walkers[std::size_t(from)].vehicle_type;
                    if (type != 0 && type != 1) {
                        set_whole(mo.x, whole(mo.x) + 0xf);
                        set_whole(mo.y, whole(mo.y) - 0xa);
                    }
                    mw.waypoint_x = whole(mo.x) - 6;
                    mw.waypoint_y = whole(mo.y) + 0x10;
                }
            }
            world.vehicle_click_latched = true;
            vw.land = true;
            world.both_buttons = false;
            return;
        }
        vw.land = false;
    }
    // $9EE02: drive towards the pointer.
    int x = std::max(pointer_x - 0x1c, 0);
    int y = pointer_y + 6;
    if (y < 0x14) y = 0x14;
    if (vw.vehicle_type >= 5 && vw.vehicle_type <= 8) y += 0x20;
    vw.waypoint_x = x;
    vw.waypoint_y = y;
}

void fire_vehicle(World& world) { // $9F4F4
    const int vehicle = world.driven_vehicle;
    if (vehicle < 0) return;
    auto& vw = world.walkers[std::size_t(vehicle)];
    vw.fire_request = true;
    int x = world.camera.x + world.pointer_x - 0x10;
    if (x == 0) x = 1;
    vw.aim_x = x;
    vw.aim_y = world.camera.y + world.pointer_y - 8;
}

} // namespace fodder::game

