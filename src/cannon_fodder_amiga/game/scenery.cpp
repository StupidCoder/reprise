#include "cannon_fodder_amiga/game/scenery.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
#include <algorithm>
#include <optional>

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

// $9B448: the selected squad's leader can touch the object: on foot and
// well, the squad not in a vehicle; his distance (from 2 right and below his
// position, the estimate with a 32 limit). The callers take 6 as touching
// too (the routine's last compare leaves "equal" as its result).
std::optional<int> leader_touch(const World& world, const GeometryTables& geometry, const GameObject& o) {
    if (world.driven_vehicle >= 0 || world.leader_slot < 0) return std::nullopt;
    const auto& l = world.objects.slots[std::size_t(world.leader_slot)];
    const auto& lw = world.walkers[std::size_t(world.leader_slot)];
    if (lw.carried != 0 || lw.state != 0) return std::nullopt;
    return geometry.distance(whole(l.x) + 2 - whole(o.x), whole(l.y) + 2 - whole(o.y), 32, 1000);
}

namespace {

// $96F86 / $97158 / $988E6: a building hit by a big blast is thrown up and
// away (state 6) for a few ticks, then becomes an explosion of the map.
// `towards_bottom`: thrown towards $1C0 (down), else towards 0 (right).
// True while it flies (or is gone).
bool thrown_building(World& world, const Terrain& terrain, const ProgramTables& tables, int slot, int graphic,
                     int speed, bool towards_bottom) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (w.state == 7 || w.state == 5) {
        w.state = 6;
        o.graphic = graphic;
        const auto r = world.random();
        int turn = int(r & 0x1e);
        if (r & 0x8000) turn = -turn;
        w.heading = (towards_bottom ? 0x1c0 : 0) + turn;
        w.wait = int(r & 3) + 5;
        w.speed = speed;
    }
    if (w.state != 6) return false;
    if (--w.wait != 0) {
        bool off = false;
        move(tables.geometry, o, w, &off);
        if (!off) {
            const auto probe = terrain.probe(whole(o.x), whole(o.y), 2, -3);
            if (!probe.outside && !probe.blocking && std::int16_t(w.speed) > 0) return true;
        }
    }
    set_whole(o.x, std::max(whole(o.x) - 8, 0));
    set_whole(o.y, whole(o.y) - 8);
    become_tile_explosion(world, slot);
    return true;
}

// The door of a hut or the like: shut while a count runs down (someone
// comes out at 20 and 10), then open for a while (shorter for higher
// skills), then the last one comes out and the door shuts. `send` returns
// whether someone came out.
template <typename Send>
void door(World& world, int slot, int open_graphic, Send send) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.layer = -1;
    if (std::int8_t(w.idle) >= 0) {
        w.idle = (w.idle - 1) & 0xff;
        if (w.idle == 20 || w.idle == 10) {
            send(true);
            return;
        }
        if (std::int8_t(w.idle) >= 0) {
            o.graphic = 0x7c;
            return;
        }
        o.graphic = open_graphic;
        request_sound(world, 0x10, 1, slot);
        w.state_timer = std::max(20 - world.skill_max, 0) * 8 + int(world.random() & 0xf);
    }
    if (--w.state_timer >= 0) return;
    w.idle = int(world.random() & 0xf) + 6;
    o.graphic = 0x7c;
    send(false);
}

// $9A20A: a civilian of the building's kind comes out (on screen, fewer
// than 20 objects drawn, the phase not won, fewer than 10 enemies out);
// walking down ($1C0 +- 30) for 8-23 ticks. Its slot, or -1.
int release_civilian(World& world, int from, int kind) {
    const auto& b = world.objects.slots[std::size_t(from)];
    if (!b.on_screen || world.drawn_objects >= 20 || world.outcome == World::Outcome::won ||
        world.objects.enemies >= 10) {
        return -1;
    }
    const int slot = allocate_slot(world);
    if (slot < 0) return -1;
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o = GameObject{};
    w = Walker{};
    world.bullets[std::size_t(slot)] = BulletState{};
    world.attachments[std::size_t(slot)] = Attachment{};
    o.used = true;
    o.kind = kind;
    set_whole(o.x, whole(b.x) - 6);
    set_whole(o.y, whole(b.y) + 4);
    o.graphic = 0x7c;
    o.start_delay = 0;
    o.side = 2;
    const auto r = world.random();
    int turn = int(r & 0x1e);
    if (r & 0x8000) turn = -turn;
    w.heading = 0x1c0 + turn;
    w.wait = int(r & 0xf) + 8;
    return slot;
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
    if (thrown_building(world, terrain, tables, slot, open, second ? 0x6e : 120, !second || world.landscape == 2)) return;
    if (world.objects.enemies >= 10) { // the door stays open
        o.graphic = open;
        return;
    }
    door(world, slot, open, [&](bool at_count) {
        const int enemy = spawn_enemy(world, slot);
        if (second && enemy >= 0) {
            auto& e = world.objects.slots[std::size_t(enemy)];
            set_whole(e.y, whole(e.y) - 4);
        }
        if (enemy < 0 && (second || !at_count)) {
            w.state_timer = second ? 0 : -1;
            w.idle = 0xff;
        }
    });
}

// $98836 / $9881E: a hole in the ground enemies climb out of (graphic $E0),
// like hut $14; kind $64 shrugs off its first 105 big blasts.
void update_enemy_hole(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (o.kind == 0x64 && w.state == 7 && w.hits < 0x69) {
        ++w.hits;
        w.state = 0;
    }
    if (thrown_building(world, terrain, tables, slot, 0xe0, 0x78, true)) return;
    if (world.objects.enemies >= 10) {
        o.graphic = 0xe0;
        return;
    }
    door(world, slot, 0xe0, [&](bool at_count) {
        if (spawn_enemy(world, slot) < 0 && !at_count) {
            w.state_timer = -1;
            w.idle = 0xff;
        }
    });
}

// $98D68 / $98D74 / $98D80: a building that civilians ($3D, $3E or the
// hostage-like $46) come out of, like hut $19. Thrown down by a blast it
// fails a phase that must keep civilians safe ($82A0E).
void update_civilian_building(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    const int kind = o.kind == 0x4a ? 0x3d : o.kind == 0x4b ? 0x3e : 0x46;
    if (thrown_building(world, terrain, tables, slot, 0x9b, 0x6e, world.landscape == 2)) {
        if (world.objectives[3]) fail_objective(world); // $82A0E: objective 4
        return;
    }
    if (world.objects.enemies >= 10) {
        o.graphic = 0x9b;
        return;
    }
    door(world, slot, 0x9b, [&](bool) {
        const int c = release_civilian(world, slot, kind);
        if (c >= 0) {
            auto& co = world.objects.slots[std::size_t(c)];
            set_whole(co.y, whole(co.y) - 4);
        } else {
            w.state_timer = 0;
            w.idle = 0xff;
        }
    });
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
        const auto distance = leader_touch(world, tables.geometry, o);
        if (!distance || *distance >= 6) return;
        if (*distance != 5) {
            auto& l = world.objects.slots[std::size_t(world.leader_slot)];
            auto& lw = world.walkers[std::size_t(world.leader_slot)];
            set_whole(l.x, whole(o.x) - 3);
            set_whole(l.y, whole(o.y) + 1);
            lw.state = 9;
            lw.hit_timer = 0xff;
            l.height = 7;
        }
    }
    if (o.frame < 2) ++o.frame;
}

// $9B2D6 (kind $36): a mine the leader sets off by stepping within 6 of
// it (or a blast): a big explosion ($59).
void update_mine(World& world, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.layer = -1;
    if (w.state == 0) {
        o.graphic = 0xc7;
        const auto distance = leader_touch(world, tables.geometry, o);
        if (!distance || *distance > 6) return;
    }
    o.kind = 0x59;
}

// $9B2FA (kind $37): a trip mine. A walking leader (on the ground, not in
// cover) stepping on it trips (state $32); ten ticks later, or when shot,
// it explodes.
void update_trip_mine(World& world, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.layer = -1;
    if (w.state != 0 || (w.fall_count != 0 && ++w.fall_count >= 10)) {
        o.kind = 0xc;
        return;
    }
    o.graphic = 0xc8;
    const auto distance = leader_touch(world, tables.geometry, o);
    if (!distance || *distance > 6) return;
    auto& l = world.objects.slots[std::size_t(world.leader_slot)];
    auto& lw = world.walkers[std::size_t(world.leader_slot)];
    if (lw.state != 0 || lw.speed == 0 || l.hidden_lines != 0 || l.height != 0) return;
    lw.state = 0x32;
    w.fall_count = 1;
}

// $96E7E (kind $12): a flickering decoration, three frames, a step every
// second tick.
void update_flicker(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.graphic = 0x94;
    o.layer = -1;
    w.state_timer = (w.state_timer ^ 1) & 1;
    if (w.state_timer != 0) return;
    if (++o.frame >= 3) o.frame = 0;
}

// $96EAE (kind $13): something drifting left 2 a tick across the map,
// flapping every second tick; past the left edge it starts again at 2000.
void update_drifter(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.graphic = 0x98;
    o.layer = 1;
    w.state_timer = (w.state_timer + 1) & 1;
    if (w.state_timer == 0) o.frame ^= 1;
    set_whole(o.x, whole(o.x) - 2);
    if (whole(o.x) < 0) set_whole(o.x, 2000);
}

// $971F4 (kind $1A): an animated decoration (four frames), drawn first.
void update_animated(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    o.graphic = 0x9c;
    o.layer = -1;
    o.frame = (o.frame + 1) & 3;
}

// $9ABFC (kind $29): the piece a vehicle carries (placed after it; the
// vehicle puts it in place each tick): four frames, a step every second
// tick; on its own it sinks 24 a tick, lifted as much (so it stays put on
// the screen while its place in the drawing order grows).
void update_vehicle_piece(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.layer = 0;
    o.graphic = 0xc4;
    w.idle = (w.idle + 1) & 1;
    if (w.idle == 0) o.frame = (o.frame + 1) & 3;
    set_whole(o.y, whole(o.y) + 0x18);
    o.height = std::int16_t(o.height + 0x18);
}

// $9ABE6 (kind $39): a puff counting its frames down every second tick.
void update_puff(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    w.fall_count = (w.fall_count + 1) & 1;
    if (w.fall_count != 0) return;
    if (--o.frame < 0) free_object(world, slot);
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
    // The leader touching it picks it up.
    const auto distance = leader_touch(world, tables.geometry, o);
    if (!distance || *distance > 6) return;
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

// $98E48 (kind $49): the hostages' rescue tent; a big blast destroys it.
void update_rescue_tent(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    o.graphic = 0xdd;
    world.rescue_tent = slot;
    if (world.walkers[std::size_t(slot)].state == 7) o.kind = 0x59;
}

// $987B6 (kind $5A): the civilians' house. Its door shuts for a while when
// one comes in (with a knock); blown away, the phase is failed.
void update_civilian_house(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    world.civilian_house = slot;
    if (thrown_building(world, terrain, tables, slot, 0x9b, 0x6e, world.landscape == 2)) {
        world.civilian_house = -1;
        fail_objective(world);
        return;
    }
    o.layer = -1;
    if (w.fall_count != 0) {
        if (--w.fall_count == 0) request_sound(world, 0x29, 1, slot);
        if (o.graphic == 0x9b) request_sound(world, 0x29, 1, slot);
        o.graphic = 0x7c;
        return;
    }
    o.graphic = 0x9b;
}

// $98714 (kind $5C): a glinting thing (random frames) that explodes when
// the leader comes within 10.
void update_booby_trap(World& world, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (w.state == 0) {
        o.graphic = 0xe2;
        if (w.aim_x == 0) {
            w.aim_x = -1;
            w.fall_count = int(world.random() & 0x1e);
        }
        w.fall_count = (w.fall_count + 2) & 0x1e;
        static constexpr int frames[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 1}; // $98796
        o.frame = frames[w.fall_count >> 1];
        if (world.leader_slot < 0) return;
        const auto& l = world.objects.slots[std::size_t(world.leader_slot)];
        if (tables.geometry.distance(whole(l.x) - (whole(o.x) + 4), whole(l.y) - (whole(o.y) - 2)) > 10) return;
    }
    o.kind = 0xc;
    w.waypoint_x = std::int16_t(0x8005);
    w.waypoint_y = std::int16_t(0xfffd);
}

// $98608 … $98590 (kinds $5D-$60, $62): power-ups the leader picks up (a
// shot sets them off): $5D the top rank; $5E homing rockets and 50 rockets;
// $5F protection; $60 all three for the leader; $62 all three for the whole
// squad.
void update_power_up(World& world, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (w.state != 0) {
        o.kind = 0xc;
        return;
    }
    switch (o.kind) {
    case 0x5d: o.graphic = 0x95; o.frame = 0xf; break;
    case 0x5e: o.graphic = 0xe4; o.frame = 0; break;
    case 0x5f: o.graphic = 0xe3; o.frame = 0; break;
    case 0x60: o.graphic = 0xe5; o.frame = 0; break;
    default: o.graphic = 0xe6; o.frame = 0; break;
    }
    const auto distance = leader_touch(world, tables.geometry, o);
    if (!distance || *distance > 6) return;
    if (o.kind == 0x62 && world.selected_squad < 0) return;
    auto& lw = world.walkers[std::size_t(world.leader_slot)];
    const int squad = std::clamp(world.selected_squad, 0, 2);
    const auto rockets = [&] {
        world.squads[std::size_t(squad)].ammunition_changed = true;
        world.squads[std::size_t(squad)].rockets = 0x32;
    };
    switch (o.kind) {
    case 0x5d:
        lw.rank = 15;
        world.panel_dirty = true;
        break;
    case 0x5e:
        lw.flags75 |= 1;
        rockets();
        break;
    case 0x5f:
        lw.flags75 |= 2;
        break;
    case 0x60:
        lw.flags75 |= 3;
        lw.rank = 15;
        world.panel_dirty = true;
        rockets();
        break;
    default:
        for (const int m : world.squads[std::size_t(squad)].members) {
            world.walkers[std::size_t(m)].flags75 |= 3;
            world.walkers[std::size_t(m)].rank = 15;
        }
        world.panel_dirty = true;
        rockets();
        break;
    }
    free_object(world, slot);
}

// $984F4 (kind $63, the last mission): a place a recruit must stand on (on
// foot, well, within 9); once every one is taken the rescue point is set
// 40 below the recruit who took the last, and they flash.
void update_rescue_marker(World& world, const ProgramTables& tables, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o.graphic = 0xe7;
    o.layer = -1;
    int on_it = -1;
    for (const int r : world.recruit_lists) {
        if (r < 0) break;
        const auto& t = world.objects.slots[std::size_t(r)];
        const auto& tw = world.walkers[std::size_t(r)];
        if (tw.carried != 0 || tw.state != 0) continue;
        if (tables.geometry.distance(whole(t.x) - (whole(o.x) - 2), whole(t.y) - (whole(o.y) - 8), 0x20, 1000) < 9) {
            on_it = r;
            break;
        }
    }
    if (on_it >= 0) {
        ++world.markers_taken;
        if (w.flags75 == 0) {
            world.last_marker_recruit = on_it;
            w.flags75 = 0xff;
        }
    } else {
        w.flags75 = 0;
    }
    if (world.rescue_x < 0) {
        o.frame = 0;
    } else {
        w.idle_frame = (w.idle_frame + 2) & 6;
        static constexpr int frames[4] = {0, 1, 2, 3}; // $98480
        o.frame = frames[w.idle_frame >> 1];
    }
}

// $982D2 / $982EE / $98308 (kinds $6C-$6E, the last mission): sturdy
// trees that sway the faster the more big blasts they have taken; after 70,
// 105 or 175 they go up in a big explosion.
void update_sturdy_tree(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    const int limit = o.kind == 0x6c ? 0x46 : o.kind == 0x6d ? 0x69 : 0xaf;
    if (w.state == 7) {
        if (w.hits >= limit) {
            o.kind = 0x59;
            return;
        }
        ++w.hits;
        w.state = 0;
    }
    o.frame = 0;
    const int step = ((w.hits & 0xff) + 0x28) << 8;
    const int sum = std::int16_t(w.target) + std::int16_t(step);
    w.target = std::int16_t(sum);
    if (sum >= -0x8000 && sum <= 0x7fff) return;
    w.fall_count += 2;
    if (w.fall_count >= 8) w.fall_count = 0;
    static constexpr int graphics[4] = {0x93, 0x8f, 0x91, 0x8f}; // $98362
    static constexpr int heights[4] = {1, 0, 4, 0};              // $9836A
    o.graphic = graphics[w.fall_count >> 1];
    o.height = heights[w.fall_count >> 1];
}

// $98488 at the phase's start and $984B6 / $9837A once a tick (the last
// mission's rescue and its time limit).
void count_rescue_markers(World& world) {
    world.markers = 0;
    for (int i = 0; i < 30; ++i) {
        const auto& o = world.objects.slots[std::size_t(i)];
        if (o.used && whole(o.x) >= 0 && o.kind == 0x63) ++world.markers;
    }
    world.rescue_x = -1;
}

void update_last_mission(World& world) {
    // $984B6: every marker taken: the rescue point.
    if (world.markers != 0 && world.markers == world.markers_taken && world.rescue_x < 0 &&
        world.last_marker_recruit >= 0) {
        const auto& r = world.objects.slots[std::size_t(world.last_marker_recruit)];
        world.rescue_x = whole(r.x);
        world.rescue_y = whole(r.y) + 0x28;
    }
    world.markers_taken = 0;
    // $9837A: in the last phase, once the panel's count has run out, the
    // place blows up around the view (an explosion every 4th tick) and
    // after 40 ticks the phase is lost.
    if (world.phase != 0x47 || world.time_left != 0) return;
    if (world.doom_count == 0) world.doom_count = 0x28;
    if (--world.doom_count == 0) fail_objective(world);
    if ((world.tick_counter & 3) != 0) return;
    const int s = allocate_slot(world);
    if (s < 0) return;
    auto& e = world.objects.slots[std::size_t(s)];
    set_whole(e.x, world.snapshot.x + int(world.random() & 0xff) + 10);
    set_whole(e.y, world.snapshot.y + int(world.random() & 0xff) - 0x14);
    e.kind = 0xc;
}

} // namespace fodder::game

