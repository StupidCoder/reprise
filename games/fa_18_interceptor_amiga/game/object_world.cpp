#include "fa_18_interceptor_amiga/game/object_world.hpp"
#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include <algorithm>

namespace interceptor::game {

void ObjectWorld::spawn(const ObjectChoice& choice) {
    const auto spawned = lists_.spawn(choice, tables_);
    objects_ = spawned.objects;
    list_start_ = spawned.first_entry;
    aircraft_limit_ = spawned.aircraft_limit;
    aircraft_ = spawned.aircraft;
    destroyed_ = 0;
    areas_.reset();
    pod_.reset();
    escort_ = 0;
    outcome_wait_ = 0;
    smoke_ = {};
    trail_.reset();
    add_missile_slots();
}

void ObjectWorld::add_missile_slots() {
    for (const int n : {9, 11, 13}) {
        if (record(n)) continue;
        WorldObject free;
        free.record = n;
        free.craft.state = 0x0010;
        objects_.push_back(free);
    }
    std::stable_sort(objects_.begin(), objects_.end(),
                     [](const WorldObject& a, const WorldObject& b) { return a.record < b.record; });
}

namespace {

Waypoint place_of(const Aircraft& a) {
    return {std::int16_t(a.x >> 22), std::int16_t(a.z >> 22), std::int16_t((a.x & 0x3FFFFF) >> 8),
            std::int16_t((a.z & 0x3FFFFF) >> 8), a.y >> 8};
}
Waypoint place_of(const std::array<std::int32_t, 3>& p) {
    return {std::int16_t(p[0] >> 22), std::int16_t(p[2] >> 22), std::int16_t((p[0] & 0x3FFFFF) >> 8),
            std::int16_t((p[2] & 0x3FFFFF) >> 8), p[1] >> 8};
}

} // namespace

void ObjectWorld::launch(WorldObject& missile, const Frame& frame) {
    auto* shooter = record(missile.record - 1);
    if (!shooter) return;
    const auto& s = shooter->craft;
    // $1170A2: in control, not spinning, its target (a record) there and
    // not spinning or marked (state bit 0), in its attack (+$64 bits 5-6)
    // with state bit 0, and a missile ready (+$63: Sidewinder or AMRAAM).
    // (`$13AAAC`, a launch asked for, is never set.)
    if ((s.state & 0x8700) || (s.handling & 0x02) || !(s.state & 0x0040)) return;
    if (shooter->course.target_record & 0x80) {
        const int n = shooter->course.target_record & 0x7F;
        const WorldObject* t = n == 0 ? nullptr : record(n);
        const Aircraft* target = n == 0 ? frame.player : t ? &t->craft : nullptr;
        if (!target || !(target->state & 0x0040) || (target->handling & 0x02) || (target->state & 0x0001)) return;
    }
    if ((shooter->pilot & 0x60) != 0x60 || !(s.state & 0x0001)) return;
    const int weapon = shooter->weapons & 0xF0;
    if (weapon != 0x20 && weapon != 0x30) return;
    // $11767E (`$13AA79`, always set): every 8th frame, 6th from level 2,
    // 4th from level 3 (by the frame counter's low bits), twice as often
    // with the ECM on; then one from its stores ($1176B8).
    int every = frame.level >= 3 ? 3 : frame.level >= 2 ? 5 : 7;
    if (frame.ecm) every >>= 1;
    if (frame.counter & every) return;
    const bool amraam = weapon == 0x30;
    int left = amraam ? shooter->stores & 15 : shooter->stores >> 4;
    if (left == 0) return;
    const auto stores = shooter->stores;
    --left;
    shooter->stores = std::uint8_t(amraam ? (stores & 0xF0) | left : (left << 4) | (stores & 15));
    // $1176D8: the aircraft's record copied, with its target; no pilot, no
    // place to head for yet.
    const auto m = Weapons::launch(step_.program(), s, amraam ? Weapon::amraam : Weapon::sidewinder, left);
    missile.craft = m.body;
    missile.craft.timer = std::int16_t(m.life);
    missile.shape_type = m.type;
    missile.group = shooter->group;
    missile.area = shooter->area;
    missile.scale = shooter->scale;
    missile.damage = shooter->damage;
    missile.weapons = shooter->weapons;
    missile.stores = stores;
    missile.pilot = 0;
    missile.course = shooter->course;
    missile.course.target.cell_x = -1;
}

bool ObjectWorld::step_missile(WorldObject& o, const Frame& frame) {
    auto& a = o.craft;
    bool lock_lost = false;
    const auto missile_of = [&] {
        Missile m;
        m.body = a;
        m.type = a.type;
        m.life = a.timer;
        m.fading = a.state & 0x0400;
        m.fresh = a.state & 0x0100;
        m.stopped = (a.state & 0x0400) && !(a.state & 0x1000);
        m.pilot = o.pilot;
        m.course = o.course;
        return m;
    };
    const auto keep = [&](const Missile& m) {
        a = m.body;
        a.timer = std::int16_t(m.life);
        o.pilot = m.pilot;
        o.course = m.course;
        if (m.fading) a.state |= 0x0400;
        if (!m.fresh) a.state &= ~0x0100;
        if (m.stopped) a.state &= ~0x1000;
    };
    auto m = missile_of();
    // $1182F2: its target record's place; on the player the latest decoy
    // of its kind instead.
    Weapons::Homing homing;
    homing.place = o.course.target;
    const int n = o.course.target_record & 0x7F;
    const WorldObject* t = n == 0 ? nullptr : record(n);
    if (n == 0 && frame.player) homing.place = place_of(*frame.player);
    else if (t) homing.place = place_of(t->craft);
    if (n == 0) {
        const auto& decoy = m.type == 0 ? frame.flare : frame.chaff;
        if (decoy) {
            homing.place = place_of(*decoy);
            homing.decoy = decoy;
        }
    }
    const bool touches = !m.fresh && !m.fading;
    const std::array<std::int32_t, 3> before{a.x, a.y, a.z};
    FlightRules rules;
    rules.flying = frame.flying;
    if (!Weapons::fly_missile(m, rules, tables_, nullptr, -1, &autopilot_, &homing)) {
        a.state &= ~0x0040; // (its time out)
        return false;
    }
    keep(m);
    if (!touches || (a.state & 0x0400)) return false; // ($11A7DA)
    // $11A7EA: its touch; hitting a record it goes ($11AA70); below the
    // ground it blows up, on land with smoke.
    auto list = frame.bodies;
    const std::size_t self = list.size() + std::size_t(&o - objects_.data());
    for (auto& other : objects_) list.push_back({&other.craft, other.group, other.active()});
    bool near = false;
    if (touch_bodies(list, self, before, near) == touch::object) {
        a.handling |= 0x02;
        a.state &= ~0x0040;
        return false;
    }
    std::array<std::int32_t, 3> smoke_at{};
    bool smoke = false;
    auto ground = missile_of();
    if (Weapons::hit_ground(ground, before, frame.world, smoke_at, smoke)) {
        keep(ground);
        if (smoke) add_smoke(smoke_at, true);
        if (frame.locked == o.record) lock_lost = true;
        return lock_lost;
    }
    // $11AA80 for a missile: crashed it blows up; the near bit follows the
    // touch, and losing it marks +$20 bit 5 ($11AAE8).
    if (a.state & 0x0200) {
        a.state = std::uint16_t((a.state | 0x0400) & ~0x0200);
        a.timer = 15;
        if (frame.locked == o.record) lock_lost = true;
    } else if (near) {
        a.motion |= 0x0002;
    } else if (a.motion & 0x0002) {
        a.motion &= ~0x0002;
        a.handling |= 0x20;
    }
    return lock_lost;
}

void Trail::update(Aircraft& a) {
    places[std::size_t(next)] = {a.x, a.y, a.z};
    if (count < 5) places[std::size_t(next + 1)] = {a.x, a.y, a.z};
    if (++next > 5) next = 0;
    if (count < 6) ++count;
    if (a.airspeed != 0 && (a.motion & 0x1000)) { // (+$6E)
        a.trail = std::uint8_t(std::min(count - 1, 4));
        return;
    }
    if (a.trail == 0 || --a.trail == 0) next = count = 0;
}

void ObjectWorld::add_smoke(const std::array<std::int32_t, 3>& at, bool ground) {
    for (std::size_t slot = 0; slot < smoke_.size(); ++slot) {
        if (smoke_[slot]) continue;
        // (The loop's counter, 15 down, picks the ground's two shapes.)
        const int type = !ground ? 0x11 : (slot & 1) ? 0x12 : 0x13;
        smoke_[slot] = Smoke{type, 0x7F, {at[0] >> 8, at[1] >> 8, at[2] >> 8}};
        return;
    }
}

void ObjectWorld::age_smoke() {
    if (outcome_wait_ != 0) return;
    for (auto& s : smoke_) {
        if (s) --s->timer;
    }
}

WorldObject* ObjectWorld::record(int number) {
    for (auto& o : objects_) {
        if (o.record == number) return &o;
    }
    return nullptr;
}

const WorldObject* ObjectWorld::record(int number) const {
    for (const auto& o : objects_) {
        if (o.record == number) return &o;
    }
    return nullptr;
}

bool ObjectWorld::active(int number) const {
    for (const auto& o : objects_) {
        if (o.record == number) return o.active();
    }
    return false;
}

void ObjectWorld::trigger_areas(const Aircraft& player, int mode, int level, const std::function<int()>& draw) {
    areas_.check(std::int16_t(player.x >> 22), std::int16_t(player.z >> 22), objects_,
                 [&](const ObjectLists::TriggerArea& area, int index) {
                     ObjectChoice choice;
                     choice.mode = mode;
                     choice.level = level;
                     choice.variant_random = draw();
                     ObjectLists::Spawned offsets;
                     return lists_.spawn_area(area, index, objects_, aircraft_, aircraft_limit_, choice, offsets, tables_);
                 });
}

void ObjectWorld::clear_near() {
    for (auto& o : objects_) o.craft.motion &= ~0x0001;
}

bool ObjectWorld::step(const Frame& frame) {
    bool lock_lost = false;
    if (outcome_wait_ == 0) {
        Records records{frame.player, frame.player_distance, &objects_};
        StepWorld world;
        world.locked = frame.locked;
        world.mode = frame.mode;
        world.level = frame.level;
        world.list = list_start_;
        for (auto& o : objects_) {
            auto& a = o.craft;
            if (!o.active() && frame.launches && (o.record == 9 || o.record == 11 || o.record == 13)) launch(o, frame);
            if (!o.active()) continue;
            if ((a.type & 0xF0) == 0) {
                if (step_missile(o, frame)) lock_lost = true;
                continue;
            }
            if ((a.type & 0xF0) != 0x10) continue;
            step_.before_flight(o, records, world);
            // $11A380: one record a frame by the counter looks whether it
            // has left the area that spawned it.
            if ((frame.counter & 0x1F) == o.record) areas_.send_home(o);
            // $11A398: the explosion's end: an aircraft not touchable in the
            // air counts as destroyed (`$13AB9B`) - not the cruise missile
            // (type $15), which always goes; on the ground or out of control
            // the record goes (its timer 20), else it falls on.
            if (a.state & 0x0400) {
                if (a.timer <= 0) {
                    a.state &= ~0x0400;
                    const bool missile = a.type == 0x15;
                    if (!missile && !(a.state & 0x0008) && !(a.motion & flag::on_ground)) ++destroyed_;
                    if (missile || (a.motion & flag::on_ground) || (a.state & 0x8000)) {
                        a.state &= ~(0x0040 | 0x4000 | 0x2000);
                        a.timer = 0x14;
                        continue;
                    }
                }
                a.state &= ~0x2000;
            }
            if (!(a.state & 0x1000) && !(a.state & 0x0100)) continue;
            PilotWorld pilot_world;
            pilot_world.mode = world.mode;
            pilot_world.level = world.level;
            if (frame.flying) autopilot_.fly(a, o.pilot, pilot_world, &o.course); // ($11A440)
            FlightControls c;
            c.recorded = a.controls;
            const auto roll_before = a.attitude.roll;
            FlightRules rules;
            rules.world = frame.world;
            rules.flying = frame.flying;
            const std::array<std::int32_t, 3> before{a.x, a.y, a.z};
            if (a.state & 0x1000) fly(a, c, rules, tables_);
            if (frame.flying) Autopilot::level_out(a, o.pilot, roll_before);
            if (a.state & 0x0400) continue; // (no touches while it blows up)
            // $11A7EA: its touch after the move; hitting something it goes
            // ($11AA70) - the cruise missile blows up instead ($11A9F6).
            const auto blow_up = [&] {
                a.state = std::uint16_t((a.state | 0x0400) & ~0x0200);
                a.timer = 15;
                if (frame.locked == o.record) lock_lost = true;
            };
            auto list = frame.bodies;
            const std::size_t self = list.size() + std::size_t(&o - objects_.data());
            for (auto& other : objects_) list.push_back({&other.craft, other.group, other.active()});
            bool near = false;
            if (touch_bodies(list, self, before, near) == touch::object) {
                a.handling |= 0x02; // ($11A9DE: it spins)
                if (a.type == 0x15) {
                    blow_up();
                    continue;
                }
                // $11AA04: an aircraft burns and is the trail's - and is
                // taken off ($11AA70).
                a.motion |= 0x1000;
                a.role = 1;
                a.state &= ~0x0040;
                continue;
            }
            // $11AA80, nothing hit: spinning onto the ground it stops and
            // blows up; else (not the pod's class $30) it is in control
            // again (+0 bit 15), and a crashed record blows up here, before
            // the frame's end sees it ($11AA4A); or the near bit (+3 bit 1)
            // follows the touch's near box.
            if ((a.handling & 0x02) && (a.motion & flag::on_ground)) {
                a.state &= ~0x1000;
                blow_up();
                continue;
            }
            if ((a.type & 0xF0) == 0x30) continue;
            a.state &= ~0x8000;
            if (a.state & 0x0200) blow_up();
            else if (near) a.motion |= 0x0002;
            else a.motion &= ~0x0002;
        }
        aircraft_limit_ += world.turned_hostile; // ($1181CA)
        aircraft_ += world.turned_hostile;
    }
    // ($1169D0, with the drawing:) a crashed record blows up for 15
    // frames; the radar's lock on it goes ($118588).
    for (auto& o : objects_) {
        auto& a = o.craft;
        if (!o.active() || !(a.state & 0x0200)) continue;
        a.state = std::uint16_t((a.state & ~0x0200) | 0x0400);
        a.timer = 15;
        if (frame.locked == o.record) lock_lost = true;
    }
    if (outcome_wait_ == 0) pod_step();
    return lock_lost;
}

void ObjectWorld::drop_pod(const Aircraft& player) {
    if (pod_) return;
    WorldObject pod;
    pod.craft = player;
    auto& a = pod.craft;
    a.player = false;
    a.type = 0x31;
    pod.shape_type = 11;
    pod.record = 1;
    pod.group = 0;
    pod.scale = 3;
    a.state = std::uint16_t(((a.state & ~0x8000) | 0x10C0 | 0x0100 | 0x0002) & 0xFFFF);
    a.pitch_rate = a.yaw_rate = a.roll_rate = 0;
    a.sink = 0;
    if (a.motion & flag::on_ground) a.vy = 0;
    a.motion &= ~(flag::on_ground | 0x0004);
    a.ground_drag = 0x14;
    const auto& v = a.view;
    a.x += (v[1] * -3) >> 6;
    a.y += (v[4] * -3) >> 6;
    a.z += (v[7] * -3) >> 6;
    const auto push = scaled_vector(tables_, 0x3C0, std::int16_t(-(v[1] >> 2)), std::int16_t(-(v[4] >> 2)),
                                    std::int16_t(-(v[7] >> 2)));
    a.vx += push[0];
    a.vy += push[1];
    a.vz += push[2];
    a.timer = -20;
    pod_ = pod;
}

// $117C82 (class $30, each frame): drag on the drift by the timer, the
// fall toward $300 down (−$3C0 a frame from its third frame), the move.
// Reaching the ground it is down (+0 bit 15) where it was before that
// frame's move, at height 0, and moves no more; its drift still changes
// and its timer runs out (clamped to 4).
void ObjectWorld::pod_step() {
    if (!pod_) return;
    auto& a = pod_->craft;
    std::int32_t vx = a.vx, vy = a.vy, vz = a.vz;
    if (a.state & 0x8000) {
        if (a.timer > 0) a.timer = std::int16_t(a.timer >= 5 ? 4 : a.timer - 1);
        if (vx < 0xC0) vx += 0xC;
        vy = 0;
        vz += vz > 0 ? -1 : 1;
    } else {
        int shift = 2;
        if (a.timer < 0) {
            ++a.timer;
            shift = 6;
        } else {
            ++a.timer;
            if (a.timer >= 3) shift = 3;
        }
        const std::int32_t drift = std::max(std::abs(vx), std::abs(vz));
        const std::int32_t limit = std::max(0, 0xF0 - a.timer);
        if (drift >= limit) {
            vx -= vx >> shift;
            vz -= vz >> shift;
        }
        if (a.timer >= 3 && vy > 0) vy -= 0x3C0;
        else if (vy > -0x300) vy -= 0x3C;
        else vy = (vy - 0x300) >> 1;
    }
    a.vx = vx;
    a.vy = vy;
    a.vz = vz;
    a.airspeed = std::int16_t(std::max({std::abs(vx), std::abs(vy), std::abs(vz)}) >> 2);
    if (a.state & 0x8000) return;
    if (a.y + vy <= 0) {
        a.y = 0;
        a.state |= 0x8000;
        return;
    }
    a.x += vx;
    a.y += vy;
    a.z += vz;
}

// `$13AA9E` as the outcome's countdown (the objects stand meanwhile): true
// while it runs.
bool ObjectWorld::outcome_wait(int frames) {
    if (outcome_wait_ == 0) outcome_wait_ = frames;
    return --outcome_wait_ != 0;
}

// $118650 (3), $11867C (4), $1187BA (5), $118914 (6), $118998 (7),
// $118B1C (8 and others).
ObjectWorld::Outcome ObjectWorld::objective(int mission, const Aircraft& player, int level, int confirmation) {
    Outcome out;
    switch (mission) {
    case 3:
        out.result = confirmation < 0 ? 1 : 0;
        return out;
    case 4: {
        // Air Force One (record 4): landed slowly with both bandits
        // (records 8, 10) gone - or destroyed.
        const auto* one = record(4);
        if (!one || (one->craft.x >> 8) == 0) return out; // (cell and fine x 0: not spawned yet)
        if (one->active() && (!(one->craft.motion & flag::on_ground) || one->craft.speed > 0x320 || active(8) ||
                              active(10)))
            return out;
        if (outcome_wait(5)) {
            out.turn = outcome_wait_ == 1 && one->active();
            out.turn_distance = -1;
            return out;
        }
        if (one->active()) {
            out = {1, false, 0, true, 4};
        } else {
            out.result = (one->craft.handling & 0x80) ? -3 : -2;
        }
        return out;
    }
    case 5: {
        // The stolen F-16s (records 4, 6, touchable while stolen): east of
        // cell $70 they are lost; flown beside long enough (a box by the
        // level, 200 frames) they go home along their paths; then the
        // bandits must be down.
        auto* a = record(4);
        auto* b = record(6);
        if (!(a && a->active())) std::swap(a, b);
        bool check_done = true;
        if (a && a->active()) {
            if (!(a->craft.state & 0x0008)) {
                check_done = false;
            } else {
                if ((a->craft.x >> 22) >= 0x70) {
                    out.result = -2;
                    return out;
                }
                const std::int32_t box = level >= 3 ? 0x18000 : level >= 2 ? 0x24000 : 0x30000;
                const bool beside = std::abs(player.x - a->craft.x) <= box && std::abs(player.y - a->craft.y) <= box &&
                                    std::abs(player.z - a->craft.z) <= box;
                if (beside) {
                    if (escort_ >= 0) {
                        --escort_;
                    } else {
                        for (auto* o : {a, b}) {
                            if (o) o->course.target = lists_.path(2 * o->course.path);
                        }
                    }
                } else if (escort_ >= 0) {
                    escort_ = 0xC8;
                }
                check_done = false;
            }
        }
        if (!check_done && escort_ >= 0) return out;
        if (aircraft_limit_ > destroyed_) return out;
        if (outcome_wait(2)) return out;
        out.result = 1;
        return out;
    }
    case 6: {
        // The rescue pod, landed and still: within $10000 of the downed
        // pilot (record 11) or too far.
        if (!pod_ || !(pod_->craft.state & 0x8000) || pod_->craft.timer > 0) return out;
        const auto* pilot = record(11);
        const bool near = pilot && std::abs(pilot->craft.x - pod_->craft.x) <= 0x10000 &&
                          std::abs(pilot->craft.z - pod_->craft.z) <= 0x10000;
        out.result = near ? 1 : -2;
        return out;
    }
    case 7: {
        // The cruise missile (record 4): its path ended (autopilot let go)
        // or gone - shot down (spinning) or exploded.
        const auto* missile = record(4);
        if (!missile || (missile->craft.x >> 8) == 0) return out;
        if (missile->active() && (missile->craft.state & 0x0080)) return out;
        if (outcome_wait(2)) {
            out.turn = outcome_wait_ == 1 && missile->active();
            out.turn_distance = 3;
            return out;
        }
        if (missile->craft.handling & 0x02) out.result = 1;
        else out = {-2, false, 0, true, 7};
        return out;
    }
    default:
        // $118B1C: the list's aircraft all destroyed.
        if (aircraft_limit_ > destroyed_) return out;
        if (outcome_wait(2)) return out;
        out.result = 1;
        return out;
    }
}

ObjectWorld::Counters ObjectWorld::counters() const {
    return {aircraft_, aircraft_limit_, destroyed_, escort_, outcome_wait_, areas_.inside(), list_start_};
}

void ObjectWorld::set(const Counters& c, std::vector<WorldObject> objects, std::optional<WorldObject> pod) {
    pod_ = std::move(pod);
    aircraft_ = c.aircraft;
    aircraft_limit_ = c.aircraft_limit;
    destroyed_ = c.destroyed;
    escort_ = c.escort;
    outcome_wait_ = c.outcome_wait;
    areas_.set_inside(c.inside);
    list_start_ = c.list_start;
    objects_ = std::move(objects);
    add_missile_slots();
}

} // namespace interceptor::game
