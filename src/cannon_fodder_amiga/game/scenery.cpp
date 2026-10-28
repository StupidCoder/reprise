#include "cannon_fodder_amiga/game/scenery.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
#include <algorithm>

namespace fodder::game {
namespace {

int whole(std::int32_t v) { return std::int16_t(std::uint32_t(v) >> 16); }
void set_whole(std::int32_t& v, int w) {
    v = std::int32_t((std::uint32_t(std::uint16_t(w)) << 16) | (std::uint32_t(v) & 0xffff));
}

void move(const GeometryTables& g, GameObject& o, Walker& w, bool* off = nullptr) { // $A0C9A
    if (off) *off = false;
    if (w.speed == 0) return;
    w.heading &= 0x1fe;
    o.x += (g.sine(w.heading) >> 2) * std::int16_t(w.speed);
    if (o.x < 0) {
        o.x = 0;
        if (off) *off = true;
    }
    o.y += (g.sine(w.heading + 0x80) >> 2) * std::int16_t(w.speed);
    if (o.y < 0) {
        o.y = 0;
        if (off) *off = true;
    }
}

// $9B24A: the object's slot becomes an explosion of the map ($27) at its
// place, 16 lower (entered past $9B25A's rounding to the tile grid).
void become_tile_explosion(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    const int x = whole(o.x), y = whole(o.y);
    o = GameObject{};
    o.used = true;
    world.walkers[std::size_t(slot)] = Walker{};
    world.bullets[std::size_t(slot)] = BulletState{};
    world.attachments[std::size_t(slot)] = Attachment{};
    set_whole(o.x, x);
    set_whole(o.y, std::int16_t(y + 0x10));
    if (y < 0 || whole(o.y) < 0) {
        set_whole(o.x, 1);
        set_whole(o.y, 1);
        free_object(world, slot);
        return;
    }
    request_sound(world, 5 + (world.frame_counter & 3), 0x1e, slot);
    o.kind = 0x27;
    o.graphic = 0x8e;
    world.walkers[std::size_t(slot)].state_timer = 1;
    o.side = -1;
    o.squad = -1;
    o.layer = 1;
}

} // namespace

// The two huts: $14 ($96ED6, graphic $99, thrown at 120 towards $1C0) and
// $19 ($970AC, graphic $9B, thrown at 110, towards $1C0 only in the ice;
// its soldiers come out 4 higher, and a failed sending ends its count).
void update_hut(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    const bool second = o.kind == 0x19;
    const int open = second ? 0x9b : 0x99;
    if (w.state == 7 || w.state == 5) { // $96F86 / $97158: hit by a blast, it is thrown up
        w.state = 6;
        o.graphic = open;
        const auto r = world.random();
        int turn = int(r & 0x1e);
        if (r & 0x8000) turn = -turn;
        w.heading = (!second || world.landscape == 2 ? 0x1c0 : 0) + turn;
        w.wait = int(r & 3) + 5;
        w.speed = second ? 0x6e : 120;
    }
    if (w.state == 6) {
        if (--w.wait != 0) {
            bool off = false;
            move(tables.geometry, o, w, &off);
            if (!off) {
                const auto probe = terrain.probe(whole(o.x), whole(o.y), 2, -3);
                if (!probe.outside && !probe.blocking && std::int16_t(w.speed) > 0) return;
            }
        }
        set_whole(o.x, std::max(whole(o.x) - 8, 0));
        set_whole(o.y, whole(o.y) - 8);
        become_tile_explosion(world, slot);
        return;
    }
    if (world.objects.enemies >= 10) { // the door stays open
        o.graphic = open;
        return;
    }
    o.layer = -1;
    const auto send = [&] {
        const int enemy = spawn_enemy(world, slot);
        if (second) {
            if (enemy >= 0) {
                auto& e = world.objects.slots[std::size_t(enemy)];
                set_whole(e.y, whole(e.y) - 4);
            } else {
                w.state_timer = 0;
                w.idle = 0xff;
            }
        }
        return enemy;
    };
    // The door: shut while a count runs down (enemies come out at 20 and
    // 10), then open for a while (shorter for higher skills), then the last
    // one comes out and the door shuts.
    if (std::int8_t(w.idle) >= 0) {
        w.idle = (w.idle - 1) & 0xff;
        if (w.idle == 20 || w.idle == 10) {
            send();
            return;
        }
        if (std::int8_t(w.idle) >= 0) {
            o.graphic = 0x7c;
            return;
        }
        o.graphic = open;
        request_sound(world, 0x10, 1, slot);
        w.state_timer = std::max(20 - world.skill_max, 0) * 8 + int(world.random() & 0xf);
    }
    if (--w.state_timer >= 0) return;
    w.idle = int(world.random() & 0xf) + 6;
    o.graphic = 0x7c;
    if (send() < 0 && !second) {
        w.state_timer = -1;
        w.idle = 0xff;
    }
}

// $97018: a hidden spawn point (a trench or the like): while buildings are
// left, enemies rise out of it (12 lines hidden), two at a time.
void update_spawn_point(World& world, int slot) {
    auto& w = world.walkers[std::size_t(slot)];
    if (world.buildings_left == 0) return;
    if (w.fall_count != 0) { // $2A: send one now
        const int enemy = spawn_enemy(world, slot);
        if (enemy >= 0) {
            auto& e = world.objects.slots[std::size_t(enemy)];
            set_whole(e.x, whole(e.x) + 6);
            set_whole(e.y, whole(e.y) - 5);
            e.hidden_lines = 12;
        }
        w.fall_count = 0;
        return;
    }
    world.objects.slots[std::size_t(slot)].layer = -1;
    if (std::int8_t(w.idle) >= 0) {
        w.idle = (w.idle - 1) & 0xff;
        if (w.idle == 20 || w.idle == 10 || std::int8_t(w.idle) < 0) {
            w.fall_count = -1;
            world.objects.slots[std::size_t(slot)].frame = 0;
            w.state_timer = int(world.random() & 0xff) + 10;
        }
        return;
    }
    if (--w.state_timer >= 0) return;
    w.idle = int(world.random() & 0xf) + 6;
    w.fall_count = 0;
}

// $9B35C: a trap. The leader stepping within 4 of it is caught (held at it,
// state 9: he writhes and dies); it shuts over two ticks either way at 5.
void update_trap(World& world, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    o.graphic = 0xc9;
    if (o.frame == 0) {
        const int leader = world.leader_slot;
        if (leader < 0) return;
        auto& l = world.objects.slots[std::size_t(leader)];
        auto& lw = world.walkers[std::size_t(leader)];
        if (lw.carried != 0 || lw.state != 0) return;
        const int dx = std::abs(whole(l.x) + 2 - whole(o.x)), dy = std::abs(whole(l.y) + 2 - whole(o.y));
        const int distance = dx >= 32 || dy >= 32 ? 1000 : tables.geometry.distance(dx, dy);
        if (distance >= 6) return;
        if (distance != 5) {
            set_whole(l.x, whole(o.x) - 3);
            set_whole(l.y, whole(o.y) + 1);
            lw.state = 9;
            lw.hit_timer = 0xff;
            l.height = 7;
        }
    }
    if (o.frame < 2) ++o.frame;
}

// $9A5F2: a house with smoke from its chimney (the next slot).
void update_house(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    o.graphic = 0xcd;
    o.frame = (o.frame + 1) & 3;
    if (slot + 1 >= object_slots) return;
    auto& smoke = world.objects.slots[std::size_t(slot + 1)];
    auto& sw = world.walkers[std::size_t(slot + 1)];
    smoke.graphic = 0xce;
    sw.fall_count = (sw.fall_count + 1) & 1;
    if (sw.fall_count == 0 && ++smoke.frame >= 6) smoke.frame = 0;
    smoke.height = 16;
}

void update_crate(World& world, const ProgramTables& tables, int slot) { // $9B3B0 / $9B3FC
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (w.state != 0) { // shot: it goes off
        o.kind = 0xc;
        return;
    }
    const bool grenades = o.kind == 0x25;
    o.graphic = grenades ? 0xc2 : 0xc3;
    // $9B448: the leader, on foot and well, within 6 of it picks it up.
    const int leader = world.leader_slot;
    if (leader < 0) return;
    const auto& l = world.objects.slots[std::size_t(leader)];
    const auto& lw = world.walkers[std::size_t(leader)];
    if (lw.carried != 0 || lw.state != 0) return;
    const int dx = std::abs(whole(l.x) + 2 - whole(o.x)), dy = std::abs(whole(l.y) + 2 - whole(o.y));
    const int distance = dx >= 32 || dy >= 32 ? 1000 : tables.geometry.distance(dx, dy);
    if (distance >= 6) return;
    auto& squad = world.squads[std::size_t(std::clamp(world.selected_squad, 0, 2))];
    (grenades ? squad.grenades : squad.rockets) += 4;
    squad.ammunition_changed = true;
    free_object(world, slot);
}

void update_bush(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) { // $97296
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.graphic = 0x8f + (o.kind - 0xd);
    if (w.entering == 0 && o.height == 0) {
        if (w.state == 7 || w.state == 5) {
            // Hit by a blast: thrown up and away, with a shadow.
            // (The original skips this while $81DCE is set; open.)
            w.state = 6;
            w.entering = 0xff;
            o.height = 1;
            w.height_fraction = 0;
            w.heading = int(world.random() & 0x1fe);
            w.speed = int(world.random() & 0x1f) + 20;
            w.vertical_speed = 0x1c0000;
            const int s = allocate_slot(world);
            if (s >= 0) {
                auto& sh = world.objects.slots[std::size_t(s)];
                sh.x = o.x;
                sh.y = o.y;
                set_whole(sh.x, whole(o.x) + 14);
                set_whole(sh.y, whole(o.y) + 16);
                sh.graphic = 0x8d;
                sh.kind = 0x23;
                sh.squad = -1;
                sh.layer = -1;
                world.attachments[std::size_t(s)] = {slot, o.kind};
            }
            return;
        }
        if (w.state <= 5) {
            w.entering = 0;
            return;
        }
    }
    // Flying: it lands where it can and goes off there.
    move(tables.geometry, o, w);
    std::int32_t v = w.vertical_speed - 0x28000;
    std::int32_t h =
        std::int32_t((std::uint32_t(std::uint16_t(o.height)) << 16) | std::uint16_t(w.height_fraction)) + v;
    if (h < 0) {
        h = 0;
        v = std::int32_t(std::uint32_t(-v) >> 2);
    }
    w.vertical_speed = v;
    o.height = std::int16_t(std::uint32_t(h) >> 16);
    w.height_fraction = int(std::uint32_t(h) & 0xffff);
    bool explode = o.height == 0 && std::int16_t(std::uint32_t(v) >> 16) == 0;
    if (!explode) {
        const auto probe = terrain.probe(whole(o.x), whole(o.y), 12, -3);
        if (o.height != 0) return;
        explode = probe.attribute == 5 || probe.attribute == 6 || probe.attribute == 9 || probe.attribute == 10;
    }
    if (explode) {
        o.kind = 0xc;
        w.waypoint_x = 0x5f5f; // the blast box starts 8 further right
        w.waypoint_y = 8;
    }
}

void update_bush_shadow(World& world, int slot) { // $953E2
    auto& o = world.objects.slots[std::size_t(slot)];
    const int owner = world.attachments[std::size_t(slot)].owner;
    if (owner < 0) {
        free_object(world, slot);
        return;
    }
    const auto& b = world.objects.slots[std::size_t(owner)];
    if (b.kind == 0xc || !b.used || whole(b.x) < 0 || b.hidden_lines != 0) {
        free_object(world, slot);
        return;
    }
    o.graphic = 0x8d;
    int frame = (int(unsigned(std::uint16_t(b.height)) >> 3)) + 1;
    if (frame > 2) frame = 1;
    o.frame = frame;
    const int lift = int(unsigned(std::uint16_t(b.height)) >> 2);
    set_whole(o.x, whole(b.x) + lift + 5);
    set_whole(o.y, whole(b.y) + lift - 3);
    if (b.graphic == 0x7c) o.graphic = 0x7c;
}

void update_animal(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) { // $9720C
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.graphic = 0x9e;
    if (w.state_timer == 0) {
        w.state_timer = 16;
        w.heading = 0x100;
    }
    if (w.heading > 0x100) w.state_timer = w.state_timer == 16 ? -16 : 16; // its turning way
    const auto old_x = o.x, old_y = o.y;
    move(tables.geometry, o, w);
    // It keeps to terrain kinds 4-6; elsewhere it stops and turns.
    const auto probe = terrain.probe(whole(o.x), whole(o.y), 8, -3);
    if (probe.attribute < 4 || probe.attribute > 6) {
        set_whole(o.x, whole(old_x));
        set_whole(o.y, whole(old_y));
        w.speed = 0;
        w.heading = (w.heading + w.state_timer) & 0x1fe;
    }
    if (w.speed < 4) ++w.speed;
    o.layer = 0;
}

void update_bird(World& world, int slot) { // $99808 / $998D8
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    const bool right = o.kind == 0x43;
    o.graphic = right ? 0xd4 : 0xd3;
    o.layer = 1;
    if (w.state_timer == 0) { // first tick: a wing position and a flap way
        w.animation_timer = 8;
        w.state_timer = 1;
        const auto r = world.random();
        o.frame = int(r & 3) + 1;
        if (r & 0x8000) w.state_timer = -1;
    }
    // Flapping frames 1-4 back and forth; 1.5 a tick, 2 on the down-stroke.
    int flap = w.state_timer;
    o.frame += flap;
    if (o.frame == 0 || o.frame >= 5) {
        flap = -flap;
        w.state_timer = flap;
    }
    o.x += right ? 0x18000 : -0x18000;
    if (flap < 0) o.x += right ? 0x8000 : -0x8000;
    if (whole(o.x) < 0) set_whole(o.x, 0x7d0);
    if (!o.on_screen) {
        // Off screen it waits a while, then comes in again from the side.
        if (w.animation_timer != 0) {
            --w.animation_timer;
        } else {
            w.animation_timer = 0x3f;
            const int r = int(world.random() & 0x3f);
            if (right) set_whole(o.x, std::max(world.snapshot.x - r, 0));
            else set_whole(o.x, r + world.snapshot.x + 0x140);
            set_whole(o.y, int(world.random() & 0xff) + world.snapshot.y);
        }
    }
    // $87906: a cry one tick in 128 in the jungle ($1A) and the ice ($1F);
    // (landscapes 3 and 4 would give it priority $7F).
    const int cry = world.landscape == 0 ? 0x1a : world.landscape == 2 ? 0x1f : -1;
    if (cry >= 0 && (world.random() & 0x7f) == 0xe) {
        request_sound(world, cry, world.landscape == 3 || world.landscape == 4 ? 0x7f : 0, slot);
    }
}

bool update_leader_marker(World& world, int slot) { // $954F6
    auto& o = world.objects.slots[std::size_t(slot)];
    const auto hide = [&] {
        o.graphic = 0x7c;
        o.frame = 0;
        return true;
    };
    if (world.selected_squad < 0 || world.leader_slot < 0) return hide();
    // (The original skips this test while $81DCE is set; open.)
    if (world.route_window[std::size_t(std::clamp(world.selected_squad, 0, 2))] != 0) return hide();
    const auto& l = world.objects.slots[std::size_t(world.leader_slot)];
    const auto& lw = world.walkers[std::size_t(world.leader_slot)];
    if (l.kind != 0 || lw.entering != 0 || (lw.state != 0 && lw.state < 0x32)) { // a new leader
        choose_leader(world);
        return hide();
    }
    if (whole(l.x) < 0 || whole(l.y) < 0) return hide();
    set_whole(o.x, whole(l.x) + 3);
    set_whole(o.y, whole(l.y));
    if (lw.carried != 0) return hide();
    o.graphic = 0x95;
    o.frame = lw.rank;
    // (A bobbing step is set up and then overwritten: the marker stays 18
    // above the leader.)
    auto& w = world.walkers[std::size_t(slot)];
    w.heading = 1;
    w.step_counter = 0;
    o.height = l.height + 0x12;
    return true;
}

} // namespace fodder::game
