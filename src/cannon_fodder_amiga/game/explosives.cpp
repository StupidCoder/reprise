#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/vehicles.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include <cstdlib>

namespace fodder::game {
namespace {

int whole(std::int32_t v) { return std::int16_t(std::uint32_t(v) >> 16); }
void set_whole(std::int32_t& v, int w) {
    v = std::int32_t((std::uint32_t(std::uint16_t(w)) << 16) | (std::uint32_t(v) & 0xffff));
}
std::int32_t long_height(const GameObject& o, const Walker& w) {
    return std::int32_t((std::uint32_t(std::uint16_t(o.height)) << 16) | std::uint16_t(w.height_fraction));
}
void set_long_height(GameObject& o, Walker& w, std::int32_t h) {
    o.height = std::int16_t(std::uint32_t(h) >> 16);
    w.height_fraction = int(std::uint32_t(h) & 0xffff);
}

constexpr int kind_soldier = 0, kind_enemy = 5, kind_grenade = 2, kind_follower = 3, kind_explosion = 0xc,
              kind_rocket = 0x21, kind_tile_explosion = 0x27, kind_debris = 0x2f, kind_smoke = 0x30,
              kind_splash_down = 0x56, kind_splash_up = 0x57;
constexpr int graphic_none = 0x7c, graphic_fire = 0x8e, graphic_air_burst = 0xc0;

GameObject& object(World& w, int slot) { return w.objects.slots[std::size_t(slot)]; }
Walker& walker(World& w, int slot) { return w.walkers[std::size_t(slot)]; }


void move(const GeometryTables& g, GameObject& o, Walker& w) { // $A0C9A
    if (w.speed == 0) return;
    w.heading &= 0x1fe;
    o.x += (g.sine(w.heading) >> 2) * std::int16_t(w.speed);
    if (o.x < 0) o.x = 0;
    o.y += (g.sine(w.heading + 0x80) >> 2) * std::int16_t(w.speed);
    if (o.y < 0) o.y = 0;
}

void head_for(const GeometryTables& g, const GameObject& o, Walker& w, int x, int y) { // $A0AAC
    if (const auto h = g.heading_toward(std::int16_t(x - whole(o.x)), std::int16_t(y - whole(o.y)))) w.heading = *h;
}

// The explosion's box hurts the recruits ($A098A without the first-hit
// stop): every soldier on the ground in it is thrown along the explosion's
// heading (the original works out a heading away from the blast for him
// and then overwrites it with the explosion's own).
void blast_soldiers(World& world, int blast, int left, int right, int top, int bottom) {
    if (world.outcome != World::Outcome::playing) return; // ($82A92)
    if (world.bullet_proof && world.enemy_blast[std::size_t(blast)]) return; // (cheat)
    const auto& b = object(world, blast);
    const auto& bw = walker(world, blast);
    for (const int i : world.roster_slots) {
        if (i < 0) continue;
        auto& t = object(world, i);
        auto& tw = walker(world, i);
        const int tx = whole(t.x), ty = whole(t.y);
        if (left > std::int16_t(tx + 9) || right < tx) continue;
        if (top > std::int16_t(ty - 6) || bottom < std::int16_t(ty - 13)) continue;
        if (t.height >= 11) continue;
        if (t.kind == kind_soldier) {
            tw.heading = bw.heading;
            tw.state_timer = bw.heading;
        }
        tw.state = 1;
        tw.hit_timer = 0xff;
        (void)b;
    }
}

// $9D69A for explosions: every hittable object in the box (soldiers only
// while writhing) is knocked over along the explosion's heading (state 5;
// 7 for the big blasts of kind $59, which destroy vehicles and buildings).
void blast_objects(World& world, const HitTables& hits, int blast, int left, int right, int top, int bottom) {
    if (world.outcome != World::Outcome::playing) return; // ($82A92: off once the phase has ended)
    const auto& b = object(world, blast);
    const auto& bw = walker(world, blast);
    for (int i = 0; i < object_slots; ++i) {
        auto& t = object(world, i);
        const int flag = hits.hittable(t.kind);
        if (flag == 0 || !t.used || whole(t.x) < 0) continue;
        const int tx = whole(t.x), ty = whole(t.y);
        if (right < tx || left > std::int16_t(tx + hits.width(t.kind))) continue;
        if (bottom < std::int16_t(ty - hits.above(t.kind)) || top > std::int16_t(ty + hits.below(t.kind))) continue;
        if (b.height > hits.above(t.kind) + 8 + t.height || b.height + 11 < t.height) continue;
        auto& tw = walker(world, i);
        if (t.kind == kind_soldier && tw.state != 0xa) continue;
        tw.state = b.kind == 0x59 ? 7 : 5; // $9D75A: the big blasts mark 7
        tw.hit_timer = 0xff;
        tw.heading = bw.heading;
    }
}

// $AAEE0: blast points around the explosion go into the map-damage ring
// (negated y for the big blasts of kind $59).
void mark_blast(World& world, const BlastTables& tables, const GameObject& o) {
    const bool big = o.kind == 0x59;
    int x = whole(o.x) + 4, y = whole(o.y) - 8;
    for (const auto& p : tables.points()) {
        if ((p[0] & 0xffff) == 0x8000) break;
        x = std::int16_t(x + p[0]);
        if (x < 0) break;
        y = std::int16_t(y + p[1]);
        if (y < 0) break;
        world.blast_points[std::size_t(world.blast_write)] = {x, big ? -y : y};
        if (++world.blast_write >= int(world.blast_points.size())) world.blast_write = 0;
        world.blast_count = std::min(world.blast_count + 1, int(world.blast_points.size()));
    }
}

bool effects_allowed(const World& world, const GameObject& o) { return o.on_screen && world.drawn_objects < 20; }

// $9897C: a splash where an explosion goes off in water.
void splash(World& world, int blast) {
    request_sound(world, 0x2b, 0xf, blast);
    const auto& from = object(world, blast);
    if (!effects_allowed(world, from)) return;
    const int slot = allocate_slot(world);
    if (slot < 0) return;
    auto& o = object(world, slot);
    o.x = from.x;
    o.y = from.y;
    if (world.jitter_next_effect) {
        const int r = int(world.random() & 0x1f) - 5;
        set_whole(o.x, whole(o.x) + r);
        set_whole(o.y, whole(o.y) - (((unsigned(std::uint16_t(r)) >> 4) & 0xf) - 10));
        world.jitter_next_effect = false;
    }
    o.height = from.height;
    o.layer = 1;
    if ((world.random() & 1) == 0) {
        o.graphic = 0xde;
        o.frame = 4;
        o.kind = kind_splash_down;
    } else {
        o.graphic = 0xdf;
        o.frame = 0;
        o.kind = kind_splash_up;
    }
}

// $9AA26: a piece of debris (with its shadow) thrown out in a direction.
void throw_debris(World& world, int blast, int direction) {
    if (world.drawn_objects >= 20) return;
    const int slot = allocate_pair(world);
    if (slot < 0) return;
    const auto& from = object(world, blast);
    auto& d = object(world, slot);
    auto& s = object(world, slot + 1);
    d.x = from.x;
    d.y = from.y;
    set_whole(d.x, whole(from.x) + 2);
    set_whole(d.y, whole(from.y) - 4);
    set_whole(s.x, whole(d.x));
    set_whole(s.y, whole(d.y));
    d.height = from.height;
    walker(world, slot).height_fraction = walker(world, blast).height_fraction;
    d.graphic = 0xc5;
    s.graphic = 0x7e;
    walker(world, slot).speed = 12;
    walker(world, slot + 1).speed = 12;
    d.kind = kind_debris;
    s.kind = kind_follower;
    s.layer = -1;
    walker(world, slot).heading = direction;
    walker(world, slot).vertical_speed = 0xa8000;
}

} // namespace

bool throw_grenade(World& world, const ProgramTables& tables, int thrower) {
    auto& t = object(world, thrower);
    auto& tw = walker(world, thrower);
    const auto fail = [&] {
        if (thrower == world.leader_slot) world.both_buttons = false;
        return false;
    };
    // (The original also gives up while $81DCE is set; open.)
    const int side = std::clamp(t.side, 0, 1);
    if (world.grenades_in_air[std::size_t(side)] == 2 || tw.aim_x == 0) return fail();
    if (t.kind == kind_enemy && tables.geometry.distance(tw.aim_x - whole(t.x), tw.aim_y - whole(t.y)) >= 130) return fail();
    const int slot = allocate_pair(world);
    if (slot < 0) return fail();
    if (t.kind == kind_soldier) {
        auto& squad = world.squads[std::size_t(std::clamp(t.squad, 0, 2))];
        if (squad.grenades == 0) {
            object(world, slot).used = false;
            object(world, slot + 1).used = false;
            return fail();
        }
        --squad.grenades;
        squad.ammunition_changed = true;
    }
    ++world.grenades_in_air[std::size_t(side)];
    tw.animation_step = 1;
    tw.aim_override = true;
    tw.animation_state = 1; // throwing

    auto& g = object(world, slot);
    auto& gw = walker(world, slot);
    auto& s = object(world, slot + 1);
    auto& sw = walker(world, slot + 1);
    g.x = s.x = t.x;
    g.y = s.y = t.y;
    g.graphic = 0x7d;
    s.graphic = 0x7e;
    gw.waypoint_x = tw.aim_x; // the target
    gw.waypoint_y = std::int16_t(tw.aim_y + 6);
    // The fuse: a fifth of the distance (at least 4, at most 100 ticks); the
    // throw goes up at half that, at most 14.
    int distance = tables.geometry.distance(gw.waypoint_x - whole(t.x), gw.waypoint_y - whole(t.y));
    distance = std::min(std::max(distance, 20) / 5, 100);
    gw.state_timer = distance;
    gw.vertical_speed = std::min(std::int32_t(distance) << 15, std::int32_t(0xe0000));
    gw.speed = sw.speed = 50;
    g.height = 7;
    set_whole(g.y, whole(g.y) + 1);
    set_whole(g.x, whole(g.x) + 3);
    g.kind = kind_grenade;
    s.kind = kind_follower;
    g.side = s.side = t.side;
    g.squad = t.squad;
    s.layer = -1;
    if (t.kind == kind_enemy) gw.state_timer += 28;
    gw.squeeze = 4; // ticks in the hand
    world.attachments[std::size_t(slot)] = {thrower, t.kind};
    if (thrower == world.leader_slot) world.both_buttons = false;
    return true;
}

bool launch_rocket(World& world, const ProgramTables& tables, int launcher) {
    auto& t = object(world, launcher);
    auto& tw = walker(world, launcher);
    const auto fail = [&] {
        if (launcher == world.leader_slot) world.both_buttons = false;
        return false;
    };
    const int side = std::clamp(t.side, 0, 1);
    if (world.rockets_in_air[std::size_t(side)] == 2 || tw.aim_x == 0) return fail();
    if (t.kind == kind_enemy && tables.geometry.distance(tw.aim_x - whole(t.x), tw.aim_y - whole(t.y)) >= 130) return fail();
    const int slot = allocate_pair(world);
    if (slot < 0) return fail();
    if (t.kind == kind_soldier) {
        auto& squad = world.squads[std::size_t(std::clamp(t.squad, 0, 2))];
        if (squad.rockets == 0) {
            object(world, slot).used = false;
            object(world, slot + 1).used = false;
            return fail();
        }
        --squad.rockets;
        squad.ammunition_changed = true;
    }
    ++world.rockets_in_air[std::size_t(side)];
    tw.animation_step = 1;
    tw.aim_override = true;
    tw.animation_state = 3; // the launching pose
    auto& r = object(world, slot);
    auto& rw = walker(world, slot);
    auto& s = object(world, slot + 1);
    auto& sw = walker(world, slot + 1);
    r.x = s.x = t.x;
    r.y = s.y = t.y;
    rw.waypoint_x = tw.aim_x;
    rw.waypoint_y = std::int16_t(tw.aim_y + 16);
    r.graphic = graphic_none;
    s.graphic = 0x7e;
    s.frame = 1;
    rw.speed = sw.speed = 100;
    r.height = 10;
    set_whole(r.y, whole(r.y) + 1);
    set_whole(r.x, whole(r.x) + 3);
    r.kind = kind_rocket;
    if (t.side == 0 && (tw.flags75 & 1)) {
        // $9B616: a recruit with homing rockets: at what is near the pointer
        // a homing missile, else a missile (not slowed by walls unless he is
        // in squad 2 or 3).
        const int target = homing_target_near_pointer(world, tables, t.side);
        if (target >= 0) {
            rw.homing_target = target;
            r.kind = 0x61;
            rw.thrust = 0x10000;
            rw.state = 0;
            rw.speed = sw.speed = 0;
        } else {
            r.kind = 0x2d;
        }
        rw.wall_check = t.squad != 0;
    }
    world.attachments[std::size_t(slot)] = {launcher, t.kind};
    s.kind = kind_follower;
    r.side = s.side = t.side;
    r.squad = t.squad;
    s.layer = -1;
    rw.squeeze = 6; // ticks on the shoulder
    if (launcher == world.leader_slot) world.both_buttons = false;
    return true;
}

bool update_grenade(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    auto& o = object(world, slot);
    auto& w = walker(world, slot);
    const auto& g = tables.geometry;
    const int side = std::clamp(o.side, 0, 1);
    if (w.state != 0 || w.state_timer == 0) { // $980AE: goes off
        --world.grenades_in_air[std::size_t(side)];
        if (o.hidden_lines < 8) {
            o.kind = kind_explosion;
            free_object(world, slot + 1);
        } else { // sunk
            set_whole(o.x, 0);
            set_whole(o.y, 0);
            free_object(world, slot);
            free_object(world, slot + 1);
        }
        return true;
    }
    if (w.squeeze != 0) { // still in the hand
        if (--w.squeeze == 0) request_sound(world, 0xf, 0xf, slot);
        const auto& owner = object(world, world.attachments[std::size_t(slot)].owner);
        o.x = owner.x;
        o.y = owner.y;
        set_whole(o.y, whole(o.y) + 1);
        set_whole(o.x, whole(o.x) + 3);
        o.graphic = graphic_none;
        o.frame = 0;
        return true;
    }
    o.graphic = 0x7d;
    const int distance = g.distance(whole(o.x) - w.waypoint_x, whole(o.y) - w.waypoint_y);
    if (distance <= 1) w.speed = 0;
    if (distance <= 4) w.speed = int(unsigned(std::uint16_t(w.speed)) >> 1);
    auto& shadow = walker(world, slot + 1);
    if (w.speed != 0) {
        --w.speed;
        const auto old_x = o.x, old_y = o.y;
        world.saved_frame = o.frame;
        head_for(g, o, w, w.waypoint_x, w.waypoint_y);
        move(g, o, w);
        // $95028: holes set it off; water and grass hide it; walls turn it.
        const auto probe = terrain.probe(whole(o.x), whole(o.y), 2, -3);
        if (!probe.outside && !probe.blocking) {
            if ((probe.attribute == 9 || probe.attribute == 10) && o.height <= 1) {
                w.state_timer = 1;
            } else if ((probe.attribute == 4 || probe.attribute == 5 || probe.attribute == 6) && o.height == 0) {
                o.hidden_lines = 6;
                object(world, slot + 1).hidden_lines = 6;
            }
        } else if (o.height < 8) {
            set_whole(o.x, whole(old_x));
            const auto again = terrain.probe(whole(o.x), whole(o.y), 2, -3);
            w.heading = ((!again.outside && !again.blocking ? 0x100 : 0x200) - w.heading) & 0x1fe;
            o.x = old_x;
            o.y = old_y;
        }
        shadow.heading = w.heading;
    }
    // Height: gravity 1.5 a tick, falling no faster than 8; bounces at half
    // speed. In water or grass it stops.
    std::int32_t h = long_height(o, w);
    bool settled = false;
    if (w.slow_target == 0 && o.hidden_lines != 0) {
        w.speed = 0;
        settled = true;
    }
    if (!settled) {
        if (std::int16_t(std::uint32_t(w.vertical_speed) >> 16) < -8) { // the whole part only
            w.vertical_speed = std::int32_t((0xfff8u << 16) | (std::uint32_t(w.vertical_speed) & 0xffff));
        }
        w.vertical_speed -= 0x18000;
        const std::int32_t v = w.vertical_speed;
        const auto sink = [&] {
            if (o.hidden_lines < 10) o.hidden_lines = std::int16_t(o.hidden_lines - std::int16_t(std::uint32_t(v) >> 16));
        };
        if (w.slow_target != 0 && v < 0 && o.height < 2) {
            h = 0;
            sink();
        } else {
            h += v;
            if (std::int16_t(std::uint32_t(h) >> 16) < 0) {
                h = 0;
                if (w.slow_target != 0) sink();
                else w.vertical_speed = std::int32_t(std::uint32_t(-v) >> 1);
            }
        }
        set_long_height(o, w, h);
    }
    if (w.speed == 0) o.height = 0;
    o.frame = std::min(int(unsigned(std::uint16_t(o.height)) >> 4), 3);
    object(world, slot + 1).frame = o.frame;
    --w.state_timer;
    return true;
}

void update_follower(World& world, int slot) { // $980F0
    if (slot == 0) return;
    auto& o = object(world, slot);
    const auto& leader = object(world, slot - 1);
    // A freed slot reads $8000 there in the original.
    set_whole(o.x, leader.used ? whole(leader.x) : -0x8000);
    set_whole(o.y, leader.used ? whole(leader.y) : whole(leader.y));
    o.hidden_lines = leader.hidden_lines;
}

bool update_rocket(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) { // $9B6B6
    auto& o = object(world, slot);
    auto& w = walker(world, slot);
    const auto& g = tables.geometry;
    if (w.squeeze != 0) {
        if (--w.squeeze == 0) request_sound(world, 0x2e, 0xf, slot);
        const auto& owner = object(world, world.attachments[std::size_t(slot)].owner);
        o.x = owner.x;
        o.y = owner.y;
        set_whole(o.y, whole(o.y) + 1);
        set_whole(o.x, whole(o.x) + 3);
        o.graphic = graphic_none;
        o.frame = 0;
        return true;
    }
    o.graphic = 0xa3;
    bool explode = g.distance(whole(o.x) - w.waypoint_x, whole(o.y) - w.waypoint_y) <= 7;
    if (!explode) {
        head_for(g, o, w, w.waypoint_x, w.waypoint_y);
        move(g, o, w);
        o.frame = (((w.heading >> 5) - 1) ^ 0xf) & 0xf; // 16 directions
        const auto probe = terrain.probe(whole(o.x), whole(o.y), 2, -9);
        explode = probe.outside || probe.blocking;
    }
    if (explode) {
        o.kind = kind_explosion;
        --world.rockets_in_air[std::size_t(std::clamp(o.side, 0, 1))];
        free_object(world, slot + 1);
    }
    return true;
}

bool update_explosion(World& world, const ProgramTables& tables, int slot) { // $973A6
    auto& o = object(world, slot);
    auto& w = walker(world, slot);
    const auto& steps = tables.blasts;
    const auto off_map = [&] {
        set_whole(o.x, 1);
        set_whole(o.y, 1);
        free_object(world, slot);
        return true;
    };
    if (o.graphic == graphic_none) {
        free_object(world, slot);
        return true;
    }
    if (o.graphic != graphic_air_burst && o.graphic != graphic_fire) {
        // $974E6: goes off. (The cheat's record: still the side of what
        // exploded, the enemy's 1.)
        world.enemy_blast[std::size_t(slot)] = o.side == 1;
        if (o.on_screen) {
            mark_blast(world, steps, o);
            if (o.hidden_lines != 0) {
                splash(world, slot);
            } else {
                const int direction = int(world.random() & 0x1fe);
                throw_debris(world, slot, direction);
                throw_debris(world, slot, (direction + 0x100) & 0x1fe);
            }
            world.jolt = 9;
        }
        set_whole(o.x, whole(o.x) - 6);
        if (whole(o.x) < 0) return off_map();
        set_whole(o.y, whole(o.y) + 4);
        if (whole(o.y) < 0) return off_map();
        if (std::int16_t(w.waypoint_x) < 0) { // an offset given by the source
            set_whole(o.x, whole(o.x) + (w.waypoint_x & 0x7fff));
            if (whole(o.x) < 0) return off_map();
            set_whole(o.y, whole(o.y) + w.waypoint_y);
            if (whole(o.y) < 0) return off_map();
            w.waypoint_x = 0;
        }
        request_sound(world, 5 + (world.frame_counter & 3), 0x1e, slot);
        o.graphic = graphic_fire;
        o.frame = 0;
        w.state_timer = 1;
        o.side = -1;
        o.squad = -1;
        o.layer = 0;
        return true;
    }
    if (whole(o.x) < 0 || whole(o.y) < 0) return off_map();
    // The box, smaller with each frame.
    int left = whole(o.x) + 8, right = whole(o.x) + 0x24, top = whole(o.y) - 0x20, bottom = whole(o.y) - 6;
    if (w.waypoint_x == 0x5f5f) left += w.waypoint_y;
    const int frames = o.frame + (o.graphic == graphic_air_burst ? 2 : 0);
    for (int k = 0; k < frames; ++k) {
        const auto s = steps.step(k);
        left -= s[0];
        right -= s[1];
        top += s[0];
        bottom += s[1];
    }
    // Every other tick soldiers, else the other objects.
    w.skill = ~w.skill;
    if (std::int16_t(w.skill) >= 0) blast_soldiers(world, slot, left, right, top, bottom);
    else blast_objects(world, tables.hits, slot, left, right, top, bottom);
    if (--w.state_timer >= 0) return true;
    w.state_timer = 1;
    const auto drift = steps.step(frames);
    set_whole(o.x, whole(o.x) - drift[1]);
    if (whole(o.x) < 0) {
        free_object(world, slot);
        return true;
    }
    set_whole(o.y, whole(o.y) + drift[0]);
    if (whole(o.y) < 0) {
        free_object(world, slot);
        return true;
    }
    ++o.frame;
    if (o.graphic == graphic_air_burst) {
        if (o.frame >= 4) free_object(world, slot);
        return true;
    }
    if (o.height != 0 && o.frame >= 2) { // in the air: the burst picture
        o.graphic = graphic_air_burst;
        o.frame = 0;
    }
    if (o.frame >= 7) free_object(world, slot);
    return true;
}

bool update_tile_explosion(World& world, const ProgramTables& tables, int slot) { // $9B152
    auto& o = object(world, slot);
    auto& w = walker(world, slot);
    const auto& steps = tables.blasts;
    if (o.graphic == graphic_none) {
        free_object(world, slot);
        return true;
    }
    if (whole(o.x) < 0 || whole(o.y) < 0) {
        set_whole(o.x, 1);
        set_whole(o.y, 1);
        free_object(world, slot);
        return true;
    }
    w.skill = ~w.skill;
    const int frames = o.frame + (o.graphic == graphic_air_burst ? 2 : 0);
    if (std::int16_t(w.skill) >= 0) {
        int left = whole(o.x) + 8, right = whole(o.x) + 0x24, top = whole(o.y) - 0x20, bottom = whole(o.y) - 6;
        if (w.waypoint_x == 0x5f5f) left += w.waypoint_y;
        for (int k = 0; k < frames; ++k) { // (paired differently from $C's)
            const auto s = steps.step(k);
            left -= s[0];
            right -= s[0];
            top += s[1];
            bottom += s[1];
        }
        blast_soldiers(world, slot, left, right, top, bottom);
    } else {
        blast_objects(world, tables.hits, slot, whole(o.x), whole(o.x) + 0x1e, whole(o.y) - 0x1e, whole(o.y));
    }
    const auto drift = steps.step(frames);
    set_whole(o.x, whole(o.x) - drift[1]);
    if (whole(o.x) < 0) {
        free_object(world, slot);
        return true;
    }
    set_whole(o.y, whole(o.y) + drift[0]);
    if (whole(o.y) < 0) {
        free_object(world, slot);
        return true;
    }
    ++o.frame;
    if (o.graphic == graphic_air_burst) {
        if (o.frame >= 4) free_object(world, slot);
        return true;
    }
    if (o.height != 0 && o.frame >= 2) {
        o.graphic = graphic_air_burst;
        o.frame = 0;
    }
    if (o.frame >= 3) free_object(world, slot);
    return true;
}

void update_debris(World& world, const ProgramTables& tables, int slot) { // $9AAB2
    auto& o = object(world, slot);
    auto& w = walker(world, slot);
    move(tables.geometry, o, w);
    std::int32_t v = w.vertical_speed;
    std::int32_t h = long_height(o, w) + v;
    if (h < 0) {
        h = 0;
        v = std::int32_t(std::uint32_t(-v) >> 1);
    }
    set_long_height(o, w, h);
    v -= 0x22000;
    w.vertical_speed = v;
    if (o.frame != 0 || (o.height == 0 && std::int16_t(std::uint32_t(v) >> 16) <= 1)) {
        if (++o.frame >= 3) {
            free_object(world, slot);
            return;
        }
    }
    // $9AAFC: a puff of smoke behind it.
    if (!effects_allowed(world, o)) return;
    const int puff = allocate_slot(world);
    if (puff < 0) return;
    auto& p = object(world, puff);
    p.x = o.x;
    p.y = o.y;
    if (!world.jitter_next_effect) {
        set_whole(p.y, whole(p.y) - 3);
    } else {
        set_whole(p.y, whole(p.y) + 2);
        world.jitter_next_effect = false;
    }
    p.height = o.height;
    p.graphic = 0xc6;
    p.kind = kind_smoke;
}

void update_smoke(World& world, int slot) { // $9AB56
    auto& o = object(world, slot);
    auto& w = walker(world, slot);
    w.fall_count = (w.fall_count + 1) & 1;
    if (w.fall_count == 0 && ++o.frame == 4) free_object(world, slot);
}

void update_splash(World& world, int slot) { // $98A1A / $98A30
    auto& o = object(world, slot);
    if (o.kind == kind_splash_down) {
        request_sound(world, 0x2b, 0xf, slot);
        if (--o.frame < 0) free_object(world, slot);
    } else if (++o.frame == 6) {
        free_object(world, slot);
    }
}

int apply_map_damage(World& world, Terrain& terrain, const BlastTables& tables) { // $AAFCE
    if (world.map_damage_drawing) { // the last change is drawn this tick
        world.map_damage_drawing = false;
        return -1;
    }
    while (world.blast_count > 0) {
        const auto point = world.blast_points[std::size_t(world.blast_read)];
        int x = point[0], y = point[1];
        if (x < 0) return -1;
        const bool big = y < 0;
        y = std::abs(y);
        const int cell = (int(unsigned(y) >> 4)) * terrain.width_tiles() + (int(unsigned(x) >> 4));
        const int tile = terrain.cell(cell) & 0x1ff;
        const int damaged = terrain.swap(tile);
        const auto advance = [&] {
            if (++world.blast_read >= int(world.blast_points.size())) world.blast_read = 0;
            --world.blast_count;
        };
        // No damaged form, or a tile of the landscape's list that only big
        // blasts change: the point is passed over.
        if (damaged < 0 || (!big && tables.immune(world.landscape, tile))) {
            advance();
            continue;
        }
        const bool on_screen = x > world.snapshot.x && x < world.snapshot.x + 0x120 && y > world.snapshot.y &&
                               y < world.snapshot.y + 0xe0;
        if (on_screen) {
            // $9B25A: the tile blows up.
            const int slot = allocate_slot(world);
            if (slot < 0) return -1; // tried again next tick
            auto& o = object(world, slot);
            set_whole(o.x, x & 0xfff0);
            set_whole(o.y, (y & 0xfff0) + 0x10);
            request_sound(world, 5 + (world.frame_counter & 3), 0x1e, slot);
            o.kind = kind_tile_explosion;
            o.graphic = graphic_fire;
            walker(world, slot).state_timer = 1;
            o.side = -1;
            o.squad = -1;
            o.layer = 1;
            world.map_damage_drawing = true;
        }
        terrain.set_cell(cell, damaged);
        advance();
        return cell;
    }
    return -1;
}

} // namespace fodder::game
