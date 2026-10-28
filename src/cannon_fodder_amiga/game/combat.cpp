#include "cannon_fodder_amiga/game/combat.hpp"

namespace fodder::game {
namespace {

int whole(std::int32_t v) { return std::int16_t(std::uint32_t(v) >> 16); }
void set_whole(std::int32_t& v, int w) {
    v = std::int32_t((std::uint32_t(std::uint16_t(w)) << 16) | (std::uint32_t(v) & 0xffff));
}

constexpr int graphic_muzzle_spent = 0x7c, graphic_bullet = 0x7f, graphic_impact = 0x96, graphic_muzzle = 0x9d;
constexpr int kind_soldier = 0, kind_enemy = 5, kind_freed = 4, kind_bullet = 6, kind_shadow = 0x16, kind_blood = 0x17;
constexpr int most_bullets = 12;
// The hit tests return at the first hit; whether a hit stops or kills is the
// $81F4A result of the original (1 stopped, -1 killed).

enum class HitResult { none, killed, stopped };

} // namespace

namespace {

class Bullet {
public:
    Bullet(World& w, const Terrain& t, const ProgramTables& tables, int slot)
        : w_(w), t_(t), g_(tables.geometry), h_(tables.hits), o_(w.objects.slots[std::size_t(slot)]),
          s_(w.walkers[std::size_t(slot)]), b_(w.bullets[std::size_t(slot)]), slot_(slot) {}

    bool update() { // $97878
        b_.age = (b_.age + 1) & 0xff;
        if (b_.muzzle_ticks != 0) {
            if (b_.owner < 0) return false;
            const auto& owner = w_.objects.slots[std::size_t(b_.owner)];
            set_whole(o_.x, whole(owner.x) + b_.offset_x);
            set_whole(o_.y, whole(owner.y) + b_.offset_y);
            if (--b_.muzzle_ticks != 0) return true;
            o_.graphic = graphic_bullet;
            o_.frame = o_.side != 0 ? 3 : 0;
            set_whole(o_.x, whole(owner.x) + 7);
            set_whole(o_.y, whole(owner.y) + 5);
        }
        if (b_.ending != 0 || b_.lifetime == 0) return end_of_flight();
        --b_.lifetime;
        ++b_.flown;
        if (b_.past_target) {
            o_.layer = -1; // drawn under the others
            if (!move()) return free_slot();
        } else {
            if (!b_.spread_done) {
                if (const auto heading = g_.heading_toward(std::int16_t(s_.aim_x - whole(o_.x)),
                                                           std::int16_t(s_.aim_y - whole(o_.y)))) {
                    s_.heading = *heading;
                }
                if (b_.spread != 0) {
                    s_.heading = (s_.heading + b_.spread) & 0x1fe;
                    b_.spread_done = true;
                }
                if (b_.first_heading < 0) b_.first_heading = s_.heading;
            }
            if (!move()) return free_slot();
            s_.speed = b_.cruise_speed;
            if (!b_.spread_done) {
                const int distance = g_.distance(whole(o_.x) - s_.aim_x, whole(o_.y) - s_.aim_y);
                if (distance <= int(unsigned(std::uint16_t(s_.speed)) >> 3)) {
                    o_.layer = 0;
                    set_whole(o_.x, s_.aim_x);
                    set_whole(o_.y, s_.aim_y);
                    b_.past_target = true;
                }
            }
        }
        if (b_.age > 2) {
            const auto probe = t_.probe(whole(o_.x), whole(o_.y), 0, -9);
            if (probe.outside || probe.blocking) return end_of_flight();
        }
        const auto hit = hit_test();
        if (hit == HitResult::none) return true;
        if (hit == HitResult::stopped) return end_of_flight();
        b_.ending = 1; // $97A1A
        o_.layer = 1; // drawn over the others
        if (!shift_to_impact()) return true;
        o_.graphic = graphic_impact;
        o_.frame = 0;
        return true;
    }

    HitResult test_hit() { return hit_test(); }

private:
    // $A0C9A; false when the bullet left the map's top or left edge.
    bool move() {
        if (s_.speed == 0) return true;
        bool off = false; // $82700
        s_.heading &= 0x1fe;
        o_.x += (g_.sine(s_.heading) >> 2) * std::int16_t(s_.speed);
        if (o_.x < 0) { o_.x = 0; off = true; }
        o_.y += (g_.sine(s_.heading + 0x80) >> 2) * std::int16_t(s_.speed);
        if (o_.y < 0) { o_.y = 0; off = true; }
        return !off;
    }

    // $979B0
    HitResult hit_test() {
        if (o_.side != 0) return hit_soldiers() ? HitResult::stopped : HitResult::none;
        return hit_objects(whole(o_.x) - 6, whole(o_.x) + 10, whole(o_.y) - 10, whole(o_.y) + 6);
    }

    // $9D69A: the first hittable object overlapping the box. A solid one
    // stops the bullet; a killable one is hit, turned to the bullet's heading.
    HitResult hit_objects(int left, int right, int top, int bottom) {
        if (w_.outcome != World::Outcome::playing) return HitResult::none; // ($82A92: off once the phase has ended)
        w_.hit_flag = 0; // $81F4A
        for (int i = 0; i < object_slots; ++i) {
            auto& t = w_.objects.slots[std::size_t(i)];
            const int flag = h_.hittable(t.kind);
            if (flag == 0 || !t.used || whole(t.x) < 0) continue;
            const int tx = whole(t.x), ty = whole(t.y);
            if (right < tx || left > std::int16_t(tx + h_.width(t.kind))) continue;
            if (bottom < std::int16_t(ty - h_.above(t.kind)) || top > std::int16_t(ty + h_.below(t.kind))) continue;
            if (o_.height > h_.above(t.kind) + 8 + t.height || o_.height + 11 < t.height) continue;
            auto& tw = w_.walkers[std::size_t(i)];
            if (t.kind == kind_soldier && tw.state != 0xa) continue;
            if (flag != 2) {
                w_.hit_flag = 1;
                return HitResult::stopped;
            }
            w_.hit_flag = -1;
            tw.state = 1;
            tw.hit_timer = 0xff;
            // The original compares the headings +$368/+$468 in a way that
            // can never hold, so "from behind" always ends up cleared.
            tw.hit_from_behind = false;
            tw.heading = s_.heading;
            if (t.kind == kind_enemy) {
                tw.shot_by_roster = b_.owner_roster;
                tw.shot_by_mark = b_.owner_mark;
            }
            return HitResult::killed;
        }
        return HitResult::none;
    }

    // $A098A: an enemy bullet against the recruits' soldiers, in roster
    // order. Past its first 4 ticks of flight a bullet only hits a soldier
    // in cover (tall grass, water, or a squad set to walk slowly) one time
    // in eight. A hit counts as stopping the bullet.
    bool hit_soldiers() {
        if (w_.outcome != World::Outcome::playing) return false; // ($82A92)
        const int left = whole(o_.x), right = left + 6, top = whole(o_.y) - 9, bottom = whole(o_.y) - 4;
        for (const int i : w_.roster_slots) {
            if (i < 0) continue;
            auto& t = w_.objects.slots[std::size_t(i)];
            auto& tw = w_.walkers[std::size_t(i)];
            const int tx = whole(t.x), ty = whole(t.y);
            if (left > tx + 9 || right < tx) continue;
            if (top > ty - 6 || bottom < ty - 13) continue;
            if (t.height >= 11 || tw.carried != 0) continue;
            if (b_.flown > 4) {
                const bool covered = t.hidden_lines >= 5 || tw.wading || w_.squads[std::size_t(t.squad)].mode == 0;
                if (covered && (w_.random() & 7) != 0) continue;
            }
            if (t.kind == kind_soldier) {
                tw.heading = s_.heading;
                tw.state_timer = s_.heading;
            }
            tw.state = 1;
            tw.hit_timer = 0xff;
            return true;
        }
        return false;
    }

    bool shift_to_impact() {
        set_whole(o_.x, whole(o_.x) - 4);
        if (whole(o_.x) < 0) { off_map(); return false; }
        set_whole(o_.y, whole(o_.y) - 3);
        if (whole(o_.y) < 0) { off_map(); return false; }
        return true;
    }

    void off_map() {
        set_whole(o_.x, 0);
        set_whole(o_.y, 0x1000);
        free_slot();
    }

    bool end_of_flight() { // $97A5C
        if (b_.ending == 0) {
            hit_test();
            b_.ending = 0xff;
            o_.layer = -1; // drawn under the others
            if (!shift_to_impact()) return true;
            o_.frame = 3;
        }
        b_.lifetime = 0;
        ++o_.frame;
        if (o_.frame >= (b_.ending == 0xff ? 8 : 4)) free_slot();
        return true;
    }

    bool free_slot() { // $97A9A
        --w_.bullets_alive;
        free_object(w_, slot_);
        return true;
    }

    World& w_;
    const Terrain& t_;
    const GeometryTables& g_;
    const HitTables& h_;
    GameObject& o_;
    Walker& s_;
    BulletState& b_;
    int slot_;
};

} // namespace

int allocate_slot(World& world) {
    for (int i = 42; i >= 0; --i) {
        // (An object with a negative x counts as free, as in the original.)
        const auto& o = world.objects.slots[std::size_t(i)];
        if (o.used && whole(o.x) >= 0) continue;
        world.objects.slots[std::size_t(i)] = GameObject{};
        world.objects.slots[std::size_t(i)].used = true;
        world.walkers[std::size_t(i)] = Walker{};
        world.bullets[std::size_t(i)] = BulletState{};
        world.attachments[std::size_t(i)] = Attachment{};
        return i;
    }
    return -1;
}

int allocate_pair(World& world) {
    const auto is_free = [&](int i) {
        const auto& o = world.objects.slots[std::size_t(i)];
        return !o.used || whole(o.x) < 0;
    };
    for (int i = 0; i < 42; ++i) {
        if (!is_free(i) || !is_free(i + 1)) continue;
        for (const int s : {i, i + 1}) {
            world.objects.slots[std::size_t(s)] = GameObject{};
            world.objects.slots[std::size_t(s)].used = true;
            world.walkers[std::size_t(s)] = Walker{};
            world.bullets[std::size_t(s)] = BulletState{};
            world.attachments[std::size_t(s)] = Attachment{};
        }
        return i;
    }
    return -1;
}

void free_object(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    o.graphic = graphic_muzzle_spent;
    o.kind = kind_freed;
    o.frame = 0;
    o.removed = true;
}

void spawn_blood(World& world, int body) {
    const int slot = allocate_slot(world);
    if (slot < 0) return;
    const auto& from = world.objects.slots[std::size_t(body)];
    auto& o = world.objects.slots[std::size_t(slot)];
    o.x = from.x;
    o.y = from.y;
    set_whole(o.x, whole(from.x) + 3);
    set_whole(o.y, whole(from.y) - 8);
    o.graphic = graphic_impact;
    o.frame = 0;
    o.kind = kind_blood;
    o.height = from.height;
    o.squad = -1;
    o.layer = 1;
    world.walkers[std::size_t(slot)].state_timer = 1;
}

void cast_shadow(World& world, int body) {
    const auto& from = world.objects.slots[std::size_t(body)];
    if (!from.on_screen) return;
    const int slot = allocate_slot(world);
    if (slot < 0) return;
    auto& o = world.objects.slots[std::size_t(slot)];
    o.x = from.x;
    o.y = from.y;
    set_whole(o.x, whole(from.x) + 4);
    o.graphic = 0x8d;
    o.frame = 2;
    o.kind = kind_shadow;
    o.squad = -1;
    o.layer = -1;
    world.attachments[std::size_t(slot)] = {body, from.kind};
}

void update_blood(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    if (o.height != 0) --o.height; // runs down from the body's height
    if (--w.state_timer >= 0) return;
    w.state_timer = 1;
    if (++o.frame >= 4) free_object(world, slot);
}

void update_shadow(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    const auto& link = world.attachments[std::size_t(slot)];
    if (link.owner < 0) {
        free_object(world, slot);
        return;
    }
    const auto& body = world.objects.slots[std::size_t(link.owner)];
    if (!body.used || body.kind != link.owner_kind || body.graphic == graphic_muzzle_spent || body.hidden_lines != 0) {
        world.walkers[std::size_t(link.owner)].shadowed = false;
        free_object(world, slot);
        return;
    }
    // Under the body, further off the higher it flies.
    o.graphic = 0x97;
    o.frame = body.height > 8 ? 0 : 1;
    const int lift = int(unsigned(std::uint16_t(body.height)) >> 2);
    set_whole(o.x, whole(body.x) + lift + 5);
    set_whole(o.y, whole(body.y) + lift - 3);
}

int bullet_hit(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    // The callers tell a solid hit by $81F4A = 1, which only the test
    // against objects sets; a hit on a recruit leaves it as it was.
    if (Bullet(world, terrain, tables, slot).test_hit() == HitResult::none) return 0;
    return world.hit_flag == 1 ? 1 : 2;
}

bool update_bullet(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Bullet(world, terrain, tables, slot).update();
}

void update_fire_control(World& world, const FireTables& tables) {
    // The weapon follows the first recruit in the roster who is on the map
    // ($82A2C, the list $9DC18 builds each tick).
    for (const int slot : world.roster_slots) {
        if (slot < 0) continue;
        world.fire.stats = tables.weapons(world.walkers[std::size_t(slot)].rank);
        break;
    }
    if (world.fire.hold_timer >= 0) --world.fire.hold_timer;
}

void hold_fire(World& world, const FireTables& tables, bool first_press) {
    auto& fire = world.fire;
    world.firing = true;
    if (fire.hold_timer < 0) {
        fire.hold_timer = 0x21;
        if (world.leader_slot >= 0) world.walkers[std::size_t(world.leader_slot)].fire_delay = 0;
    }
    // The aim: the pointer on the map, 16 left and 8 up (x 0 becomes 1).
    int aim_x = std::int16_t(world.camera.x + world.pointer_x - 16);
    if (aim_x == 0) aim_x = 1;
    const int aim_y = std::int16_t(world.camera.y + world.pointer_y - 8);
    if (world.selected_squad < 0) return;
    const auto& members = world.squads[std::size_t(world.selected_squad)].members;
    for (const int m : members) {
        world.walkers[std::size_t(m)].aim_x = aim_x;
        world.walkers[std::size_t(m)].aim_y = aim_y;
    }

    // $9E65C: the next member's turn, every interval + 1 frames.
    if (!first_press && --fire.countdown >= 0) return;
    if (world.leader_slot < 0) return;
    fire.countdown = fire.stats.interval;
    fire.lifetime_bonus = 0;
    fire.speed_bonus = 0;
    // A lone soldier above rank 20 fires faster, with faster, shorter-lived bullets.
    if (std::int8_t(world.walkers[std::size_t(world.leader_slot)].rank) > 20 && members.size() == 1) {
        --fire.countdown;
        fire.lifetime_bonus = -3;
        fire.speed_bonus = 10;
    }
    const int count = int(members.size());
    if (count == 0) return;
    const auto& order = tables.firing_order(count);
    int member = fire.order_index < int(order.size()) ? order[std::size_t(fire.order_index)] : -1;
    if (member < 0 || member > count - 1) {
        member = 0;
        fire.order_index = 0;
    }
    ++fire.order_index;
    if (fire.order_index >= int(order.size()) || order[std::size_t(fire.order_index)] < 0) fire.order_index = 0;
    auto& turn = world.walkers[std::size_t(members[std::size_t(member)])];
    if (turn.pause == 0) turn.fire_delay = -1;
}

bool fire_bullet(World& world, const FireTables& tables, int shooter) {
    auto& from = world.objects.slots[std::size_t(shooter)];
    auto& fw = world.walkers[std::size_t(shooter)];
    // (The original also gives up while two flags of unknown use, $81DCE
    // and $81E02, are set; open.)
    if (fw.aim_x == 0) return false;
    if (shooter == world.leader_slot && fw.animation_state == 1) return false;
    const int slot = allocate_slot(world);
    if (slot < 0) return false;
    if (world.bullets_alive == most_bullets) {
        world.objects.slots[std::size_t(slot)].used = false; // (the original clears the slot to free)
        return false;
    }
    ++world.bullets_alive;
    fw.animation_state = 2; // the firing pose
    fw.aim_override = true;
    fw.animation_timer = 8;

    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    auto& b = world.bullets[std::size_t(slot)];
    o.x = from.x;
    o.y = from.y;
    o.graphic = graphic_muzzle;
    o.frame = (fw.facing >> 1) & 7;
    o.kind = kind_bullet;
    o.height = from.height + 6;
    o.side = from.side;
    o.squad = from.squad;
    if (from.kind != kind_soldier) {
        // Enemies: lifetime by their skill (+$62, open), at most 16.
        b.lifetime = std::min(8 + int(unsigned(std::uint16_t(fw.skill)) >> 3), 16);
    } else {
        b.owner_roster = fw.roster_index;
        b.owner_mark = 0xff;
        b.lifetime = world.fire.stats.bullet_lifetime + world.fire.lifetime_bonus;
    }
    if (from.side == 0) {
        b.cruise_speed = world.fire.stats.bullet_speed + world.fire.speed_bonus;
        w.speed = std::uniform_int_distribution<int>(0, 15)(world.random) * 8; // the first step
    } else {
        b.cruise_speed = 60 + fw.skill;
        w.speed = 24;
    }
    w.aim_x = fw.aim_x;
    w.aim_y = fw.aim_y;
    if (from.kind != kind_soldier) w.aim_x += 7;
    else w.aim_y += 15;
    b.owner = shooter;
    b.muzzle_ticks = 2;
    b.offset_x = 0;
    b.offset_y = o.frame == 4 ? -1 : 2;

    // The spread: up to +-7, the rank's spread for the squad's other
    // members, and for the leader his weapon's spread on three shots in four
    // (the fourth flies straight).
    int mask = 7;
    bool spread = true;
    if (from.side == 0) {
        if (shooter != world.leader_slot) {
            mask = tables.weapons(fw.rank).spread_mask;
        } else {
            world.fire.leader_shots = (world.fire.leader_shots + 1) & 3;
            if (world.fire.leader_shots == 0) spread = false;
            mask = world.fire.stats.spread_mask;
        }
    }
    if (spread) {
        const auto r = world.random();
        b.spread = int(r) & mask;
        if (r & 0x80) b.spread = -b.spread;
    }
    request_sound(world, (world.random() & 1) == 0 ? 0x11 : 0x10, 0, shooter);
    return true;
}

} // namespace fodder::game
