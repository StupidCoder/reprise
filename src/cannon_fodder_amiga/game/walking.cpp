#include "cannon_fodder_amiga/game/walking.hpp"
#include "amiga/adf.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace fodder::game {
namespace {

constexpr std::size_t file_offset(std::uint32_t address) {
    return address - 0x80000 + 0x1a; // program body starts 26 bytes into the file
}

// Positions are 16.16; many rules read or write only the whole part.
int whole(std::int32_t v) { return std::int16_t(std::uint32_t(v) >> 16); }
void set_whole(std::int32_t& v, int w) {
    v = std::int32_t((std::uint32_t(std::uint16_t(w)) << 16) | (std::uint32_t(v) & 0xffff));
}

// Per-update scratch the original keeps in globals (each update sets them
// before use; World keeps the ones that carry over).
struct Pass {
    std::int32_t old_x{}, old_y{}; // $817B0, $817B4
    bool moved_back{};             // $8213A: position was put back (or snapped)
    int turn{};                    // $81CEE: facing change for the animation
    int held_frame{};              // $8213C: frame kept while idle
    bool bumped{};                 // $82880
};

class Soldier {
public:
    Soldier(World& w, const Terrain& t, const ProgramTables& tables, int slot)
        : w_(w), t_(t), g_(tables.geometry), tables_(tables.soldiers), fire_tables_(tables.fire), tables_all_(tables),
          o_(w.objects.slots[std::size_t(slot)]),
          s_(w.walkers[std::size_t(slot)]), slot_(slot),
          squad_(o_.squad >= 0 && o_.squad < 3 ? w.squads[std::size_t(o_.squad)] : no_squad_) {}

    bool update() {
        if (s_.carried != 0) return false;
        if (const int handled = states(); handled >= 0) return handled != 0;
        const bool other_squad = squad_.merged_into < 0 && o_.squad != w_.selected_squad;
        if (other_squad) return false; // AI comes later
        w_.other_squad = false;
        w_.last_mode = squad_.mode;
        if (w_.firing && !fire()) return false;
        // $96960 with $81FCE clear: straight to the walking branch, where
        // the firing and throwing poses have their own paths ($9699C).
        if (s_.animation_state == 1 || s_.animation_state == 2) {
            if (w_.last_mode != 2) {
                if (s_.aim_override) return aim();
                face_pointer();
                animate();
                return true;
            }
            return walk_firing();
        }
        return walk();
    }

    bool update_enemy() { // $9DADE
        if (s_.carried != 0) return false;
        if (const int handled = states(); handled >= 0) return handled != 0;
        w_.other_squad = false;
        bool walking = false;
        if (s_.wait != 0) {
            --s_.wait; // walks on the way it turned
            walking = true;
        } else {
            seek_target();
            if (!enemy_fire()) return false;
            w_.saved_frame = o_.frame;
            walking = s_.waypoint_x >= 0 && s_.waypoint_y >= 0 && head_for(s_.waypoint_x, s_.waypoint_y);
        }
        if (walking) {
            save_position();
            turn_facing();
            set_speed();
            if (s_.pause != 0 && --s_.pause != 0) return true;
            step();
            w_.walked = true;
        } else {
            w_.walked = false;
            look_at_aim();
        }
        o_.side = 1;
        animate();
        if (!w_.walked && !w_.other_squad) o_.frame = 1;
        return true;
    }

private:
    // $95BB2: the states before walking: 1 handled, 0 not rebuilt yet, -1
    // the normal state (go on).
    int states() {
        if (s_.state == 6) return lie(true);
        if (s_.state == 2) return lie(false);
        if (s_.state == 3) return 0; // falling into a hole
        if (whole(o_.y) >= t_.height()) set_whole(o_.y, t_.height());
        if (s_.squeeze != 0 || s_.entering == 1) return 0;
        if (s_.state == 0xa) return writhe();
        if (s_.state == 0) return -1;
        if (s_.state >= 0x32) return 0; // trips and the like
        return s_.hit_timer != 0 ? start_falling() : fall();
    }

    // $9D894: the enemy's target. It looks at one recruit a tick (in the
    // order of the squads' lists); a live one on the ground within 200 and
    // in sight (or within 40, or within 64 behind cover) is the target: the
    // enemy aims at him and closes in to a spot near him (its own, by its
    // slot). Without one it stands, now and then looking elsewhere, and
    // fires at random.
    void seek_target() {
        w_.other_squad = true;
        p_.turn = 0;
        if (s_.target < 0) { // follows the selected squad's leader
            ++s_.target;
            w_.other_squad = false;
            if (w_.leader_slot >= 0) approach(w_.leader_slot);
            reload_fire_delay();
            return;
        }
        const int target = s_.target < 30 ? w_.recruit_lists[std::size_t(s_.target)] : -1;
        bool seen = false;
        int distance = 0;
        if (target >= 0 && (w_.random() & 0x3f) != 0) {
            const auto& t = w_.objects.slots[std::size_t(target)];
            const auto& tw = w_.walkers[std::size_t(target)];
            if (t.used && t.kind == 0 && (tw.state == 0 || tw.state >= 0x32) && t.height < 13) {
                const int tx = whole(t.x), ty = whole(t.y);
                distance = g_.distance(tx - whole(o_.x), ty - whole(o_.y));
                if (distance <= 200) {
                    if (distance <= 40 || !t_.sight_blocked(whole(o_.x), whole(o_.y), tx, ty)) {
                        seen = true;
                        s_.building = -1;
                        w_.other_squad = false;
                    } else if (distance < 64) {
                        seen = true;
                        distance = -1; // (the sight test's result stands in for the distance)
                    }
                }
            }
        }
        if (seen) {
            // Within 135 and standing at its spot (the original tests an
            // object left over in a register from an earlier routine; taken
            // here as the enemy itself): aim at the target's chest.
            if (distance <= 135 && at_waypoint()) {
                const auto& t = w_.objects.slots[std::size_t(target)];
                s_.aim_x = whole(t.x);
                s_.aim_y = whole(t.y) - 14;
                reload_fire_delay();
            }
        } else {
            if (s_.building >= 0) {
                const auto& b = w_.objects.slots[std::size_t(s_.building)];
                if (b.used && b.kind == 0x48) { // heads for its building
                    next_target();
                    w_.other_squad = false;
                    s_.waypoint_x = whole(b.x) + 12;
                    s_.waypoint_y = whole(b.y);
                    return;
                }
                s_.building = -1;
            }
            if ((w_.random() & 0xf) == 0) {
                s_.aim_x = std::int16_t(s_.aim_x + std::int8_t(w_.random()));
                s_.aim_y = std::int16_t(s_.aim_y + std::int8_t(w_.random()));
            }
            s_.waypoint_x = whole(o_.x);
            s_.waypoint_y = whole(o_.y);
            s_.fire_delay = 0;
            next_target();
        }
        if (!w_.other_squad) {
            const int t = s_.target < 30 ? w_.recruit_lists[std::size_t(s_.target)] : -1;
            if (t >= 0) approach(t);
        }
        reload_fire_delay();
    }

    void next_target() {
        if (++s_.target >= 30) s_.target = 0;
    }

    // $9DA54: aim at the slot's object and walk to a spot next to it, up to
    // 31 away in a direction given by the enemy's own slot (its byte offset
    // in the original's table, masked to an angle).
    void approach(int slot) {
        const auto& t = w_.objects.slots[std::size_t(slot)];
        const int angle = (slot_ * 0x76) & 0x1fe;
        const int dx = (g_.sine(angle) >> 8) >> 2, dy = (g_.sine(angle + 0x80) >> 8) >> 2;
        s_.aim_x = whole(t.x);
        s_.waypoint_x = std::int16_t(whole(t.x) + dx);
        s_.aim_y = whole(t.y);
        s_.waypoint_y = std::int16_t(whole(t.y) + dy);
    }

    // $9DAA6: a new fire delay when none is running: shorter for better
    // enemies, with less spread.
    void reload_fire_delay() {
        if (s_.fire_delay != 0) return;
        const unsigned skill = std::uint16_t(s_.skill);
        const int r = int(w_.random() & 0xffff) & int(0x1fu >> ((skill >> 2) & 63));
        const int base = std::max(50 - int(std::int16_t(skill << 2)), 0);
        s_.fire_delay = std::max(r + base, 5);
    }

    // $96230 for enemies: the delay runs down to a shot (then -1: again at
    // once if the shot failed). From phase 5 on, one shot in 32 is a grenade.
    bool enemy_fire() {
        if (s_.fire_delay == 0) return true;
        if (s_.fire_delay > 0) {
            if (--s_.fire_delay != 0) return true;
            s_.fire_delay = -1;
        }
        bool bullet = true, grenade = false;
        if (o_.kind == 5 && w_.phase > 4 && (w_.random() & 0x1f) == 0) {
            bullet = false;
            grenade = true;
        }
        return use_weapons(bullet, grenade, false);
    }

    // $96262: the weapons' part of $96230. Wading soldiers hold their fire.
    // After a bullet or grenade the soldier stands for a moment (not in a
    // fast squad); a rocket pushes him back (pause and wait).
    bool use_weapons(bool bullet, bool grenade, bool rocket) {
        if (s_.wading) return true;
        if (o_.kind == 0x24) return false; // (other enemy kinds: not rebuilt yet)
        if (bullet) {
            s_.fire_delay = fire_bullet(w_, fire_tables_, slot_) ? 0 : -1;
            if (w_.last_mode != 2) s_.pause = 15;
        }
        if (grenade) {
            s_.fire_delay = throw_grenade(w_, tables_all_, slot_) ? 0 : -1;
            if (w_.last_mode != 2) s_.pause = 12;
        }
        if (rocket) {
            s_.fire_delay = launch_rocket(w_, tables_all_, slot_) ? 0 : -1;
            if (s_.pause < 10) s_.pause += 5;
            s_.wait = s_.pause;
        }
        return true;
    }

    // The picture of a body lying in the given direction (+$20 by facing;
    // +$10 when shot from behind).
    int lying_picture(int direction) const {
        const int facing = (((direction >> 5) - 1) ^ 0xf) & 0xe;
        return tables_.picture(o_.side, (s_.hit_from_behind ? 0x10 : 0x20) + facing);
    }

    // $95C42: hit. The body is thrown up and back along the bullet's
    // heading, spinning half of the time, and bleeds.
    bool start_falling() {
        s_.hit_timer = 0;
        // (With $81DCA set the original marks the body to bleed on; open.)
        if (!s_.shadowed) {
            cast_shadow(w_, slot_);
            s_.shadowed = true;
        }
        o_.graphic = lying_picture(s_.heading);
        o_.frame = 0;
        s_.fall_count = 0;
        if (s_.vertical_speed < 0xb0000) {
            s_.vertical_speed += std::int32_t(std::uniform_int_distribution<int>(2, 9)(w_.random)) << 16;
        }
        s_.state_timer = 2;
        request_sound(w_, tables_.cry(int(w_.random())), 0x14, slot_);
        if (s_.speed < 30) s_.speed += 15;
        s_.fall_heading = s_.heading;
        s_.spin = (w_.random() & 1) == 0 ? s_.heading : -1;
        spawn_blood(w_, slot_);
        s_.waypoint_x = -1; // bleeds again on landing
        if (s_.state == 9) {
            s_.speed = 0;
            s_.state = 0xa;
            return writhe();
        }
        return fall();
    }

    // $95DA0: the body flies, bounces and comes to rest; then it lies
    // (state 6), or one time in 128 on open ground writhes (state $A).
    bool fall() {
        if (s_.spin >= 0) {
            s_.fall_heading = s_.spin;
            s_.spin = (s_.spin + s_.speed * 4) & 0x1fe;
            o_.graphic = lying_picture(s_.spin);
        }
        save_position();
        w_.saved_frame = o_.frame;
        move();
        const int heading = s_.heading;
        if (o_.height < 12) {
            react_to_terrain();
        } else { // high in the air the ground does not stop it
            const auto x = o_.x, y = o_.y;
            react_to_terrain();
            o_.x = x;
            o_.y = y;
        }
        s_.heading = heading;
        if (o_.height == 0 && (s_.wading || s_.slow_target != 0)) o_.hidden_lines = 6;
        // Height in 16.16: up by the vertical speed, which gravity lowers by
        // 1.5 a tick; on the ground a quarter bounces back (not in water or
        // grass, which also stop the slide).
        const std::int32_t v = s_.vertical_speed;
        std::int32_t height =
            std::int32_t((std::uint32_t(std::uint16_t(o_.height)) << 16) | std::uint16_t(s_.height_fraction)) + v;
        std::int32_t next = v;
        if (height < 0) {
            height = 0;
            next = std::int32_t(std::uint32_t(-v) >> 2);
            if (o_.hidden_lines != 0) {
                s_.speed = 0;
                next = 0;
            }
        }
        o_.height = std::int16_t(std::uint32_t(height) >> 16);
        s_.height_fraction = int(std::uint32_t(height) & 0xffff);
        s_.vertical_speed = next - 0x18000;
        if (s_.speed != 0) {
            s_.speed -= 2;
            if (s_.speed < 0) s_.speed = 0;
        }
        if (s_.waypoint_x != 0 && o_.height == 0) {
            spawn_blood(w_, slot_);
            s_.waypoint_x = 0;
        }
        if (--s_.state_timer == 0) {
            s_.state_timer = 6;
            if (o_.frame < 1) ++o_.frame;
        }
        if (s_.fall_count < 20) {
            ++s_.fall_count;
            return true;
        }
        if (o_.height != 0) return true;
        if (o_.hidden_lines == 0 && s_.slow_target == 0 && (w_.random() & 0x7f) == 0) {
            s_.spin = 500;
            s_.state = 0xa;
            return true;
        }
        s_.state = 6;
        s_.heading = s_.fall_heading;
        s_.state_timer = 0;
        s_.pause = 1;
        return true;
    }

    // $95D28: writhing on the ground for a while (and bleeding), then the
    // fall goes on. A writhing soldier can be hit by the squad's bullets.
    bool writhe() {
        if (--s_.spin < 0) {
            s_.state = 1;
            return true;
        }
        o_.graphic = o_.side == 0 ? 0x21 : 0x63;
        if ((s_.fall_count & 7) == 0) {
            spawn_blood(w_, slot_);
            o_.frame = o_.frame != 0 ? 0 : 1;
        }
        if (--s_.fall_count >= 0) return true;
        s_.fall_count = std::uniform_int_distribution<int>(8, 15)(w_.random);
        request_sound(w_, tables_.cry(int(w_.random())), 0, slot_);
        return true;
    }

    // $95FCC (state 6) / $96060 (state 2): the body lies for 15 ticks,
    // blinking from the 7th, sinking in water or grass; then the recruit
    // is dead. State 6 keeps the body in place against the terrain.
    bool lie(bool in_place) {
        if (++s_.state_timer >= 15) return die();
        if (s_.state_timer >= 7 && o_.graphic != 0x7c) {
            o_.graphic = 0x7c;
        } else {
            o_.graphic = lying_picture(s_.heading);
        }
        const auto x = o_.x, y = o_.y;
        const int hidden = o_.hidden_lines;
        react_to_terrain();
        o_.hidden_lines = hidden;
        if (in_place) {
            o_.x = x;
            o_.y = y;
        }
        if (s_.wading || s_.slow_target != 0) {
            if (o_.hidden_lines >= 13) o_.hidden_lines = 11;
            o_.hidden_lines += 2;
        }
        return true;
    }

    // $96CAA for the player's soldiers: counted as a casualty, his name and
    // rank go to the list of the fallen, his roster place is cleared.
    bool die() {
        if (o_.side != 0) return enemy_dies();
        ++w_.casualties;
        // (The hall of fame by kills, $96DF6, is not rebuilt yet.)
        w_.fallen.push_back({s_.recruit, s_.rank});
        if (s_.roster_index >= 0) w_.roster_slots[std::size_t(s_.roster_index)] = -1;
        free_object(w_, slot_);
        return true;
    }

    // $96D48: a dead enemy counts as a kill, for the recruit whose bullet
    // it was (or else for the selected squad's first member).
    bool enemy_dies() {
        if (o_.kind == 5 || o_.kind == 0x24) {
            ++w_.enemies_killed;
            --w_.objects.enemies;
            int recruit = -1;
            if (s_.shot_by_mark != 0) {
                s_.shot_by_mark = 0;
                recruit = s_.shot_by_roster >= 0 && s_.shot_by_roster < 8 ? s_.shot_by_roster : 0;
            } else if (w_.selected_squad >= 0) {
                const auto& members = w_.squads[std::size_t(w_.selected_squad)].members;
                if (!members.empty()) recruit = w_.walkers[std::size_t(members.front())].roster_index;
            }
            if (recruit >= 0) w_.kills[std::size_t(recruit)] = std::min(w_.kills[std::size_t(recruit)], 998) + 1;
        }
        free_object(w_, slot_);
        return true;
    }

    // $968B2: the fire branch of the selected squad. Wading soldiers hold
    // their fire; the leader's both-button fire throws a grenade (squad
    // weapon 1) or fires a rocket (3) at once; the others fire bullets.
    bool fire() {
        if (s_.wading) return true;
        bool bullet = true, grenade = false, rocket = false;
        if (slot_ == w_.leader_slot && w_.both_buttons && (squad_.weapon == 3 || squad_.weapon == 1)) {
            bullet = false;
            (squad_.weapon == 3 ? rocket : grenade) = true;
            s_.fire_delay = 1;
        }
        // $96230: fire_delay 0 waits for the squad's turn, -1 fires now, a
        // count fires when it runs out.
        if (s_.fire_delay == 0) return true;
        if (s_.fire_delay > 0 && --s_.fire_delay != 0) return true;
        return use_weapons(bullet, grenade, rocket);
    }

    // $969B8: a fast squad walks on while firing, facing the pointer, slower
    // the further the heading is from the facing ($9C4B8).
    bool walk_firing() {
        w_.saved_frame = o_.frame;
        s_.aim_override = false;
        face_pointer();
        if (s_.waypoint_x < 0 || s_.waypoint_y < 0) return idle();
        save_position();
        if (head_for(s_.waypoint_x, s_.waypoint_y)) {
            set_speed();
            if (!s_.wading && s_.slow_target == 0) {
                // facing steps between the heading's facing and the soldier's
                // (the first comparison unmasked, as in the original)
                int target = ((s_.heading >> 5) - 1) ^ 0xf, steps = 0;
                while (target != s_.facing && steps < 7) {
                    target = (target + 2) & 0xe;
                    ++steps;
                }
                s_.speed = tables_.firing_speed(steps);
            }
            step();
        }
        if (at_waypoint()) take_next_waypoint();
        if (!p_.moved_back) return true;
        if (s_.aim_override) return aim();
        face_pointer();
        animate();
        return true;
    }

    bool walk() {
        s_.idle = 0;
        w_.saved_frame = o_.frame;
        w_.keep_frame = false;
        set_speed();
        if (s_.waypoint_x < 0 || s_.waypoint_y < 0) return idle();
        if (s_.wait != 0) {
            --s_.wait;
        } else if (!head_for(s_.waypoint_x, s_.waypoint_y)) {
            if (at_waypoint()) take_next_waypoint();
            return idle();
        }
        save_position();
        turn_facing();
        if (s_.aim_override) return aim();
        if (s_.pause != 0) {
            --s_.pause;
            return true;
        }
        step();
        if (at_waypoint()) take_next_waypoint();
        if (p_.moved_back) {
            s_.aim_override = false;
            face_pointer();
            return true;
        }
        s_.step_counter = 0;
        animate();
        return true;
    }

    bool idle() { // $96AEA
        if (s_.aim_override) return aim();
        if (s_.pause != 0) {
            --s_.pause;
            s_.aim_override = false;
            face_pointer();
            return true;
        }
        s_.idle = 0xff;
        if (w_.last_mode == 0) {
            p_.turn = 0;
            animate();
            o_.frame = 0;
            return true;
        }
        if (w_.selected_squad == o_.squad) s_.aim_override = false;
        face_pointer();
        return true;
    }

    bool aim() { // $96AAA
        s_.aim_override = false;
        look_at_aim();
        return true;
    }

    void look_at_aim() { // $96B98
        head_for(s_.aim_x, s_.aim_y);
        face_heading();
        p_.held_frame = o_.frame;
        animate();
        if (s_.idle != 0) o_.frame = p_.held_frame;
    }

    void face_pointer() { // $96BC6
        head_for(w_.camera.x + w_.pointer_x - 0x18, w_.camera.y + w_.pointer_y);
        face_heading();
        p_.held_frame = o_.frame;
        animate();
        if (s_.idle != 0) o_.frame = p_.held_frame;
    }

    bool at_waypoint() const { return whole(o_.x) == s_.waypoint_x && whole(o_.y) == s_.waypoint_y; }

    // $A0AAC: false (heading kept) when the target is the position itself.
    bool head_for(int x, int y) {
        const auto heading = g_.heading_toward(std::int16_t(x - whole(o_.x)), std::int16_t(y - whole(o_.y)));
        if (!heading) return false;
        s_.heading = *heading;
        return true;
    }

    int heading_facing() const { return (((s_.heading >> 5) - 1) ^ 0xf) & 0xf; }

    void face_heading() { // $96C08
        s_.facing = heading_facing();
        p_.turn = 0;
    }

    void turn_facing() { // $A0C12: a step towards the heading every second tick
        p_.turn = 0;
        if (--s_.facing_timer >= 0) return;
        s_.facing_timer = 1;
        const int target = heading_facing() & 0xe;
        p_.turn = tables_.turn(s_.facing - target) * 2;
    }

    void set_speed() { // $94BD6
        int speed = 16;
        if (o_.kind == 5) speed = std::min(12 + s_.skill, 26); // enemies by their skill
        else if (squad_.mode == 0) speed >>= 1;
        else if (squad_.mode != 1) speed = 24;
        s_.speed = speed;
        if (!s_.wading) {
            const int hidden = o_.hidden_lines;
            int now = hidden;
            if (hidden != s_.slow_target) {
                if (hidden < s_.slow_target) {
                    now = hidden + 1;
                    o_.hidden_lines = now;
                } else {
                    now = hidden - 1;
                    if (now != s_.slow_target) {
                        now = hidden - 2;
                        o_.hidden_lines = now;
                    }
                }
            }
            if (now == 0) return;
        }
        s_.speed = 6;
    }

    void save_position() { // $94FFA
        p_.moved_back = false;
        p_.old_x = o_.x;
        p_.old_y = o_.y;
    }

    void put_back() { // $95010
        p_.moved_back = true;
        o_.x = p_.old_x;
        o_.y = p_.old_y;
    }

    void step() { // $94CEA
        if (!snap_to_waypoint()) {
            terrain_speed();
            move();
        }
        keep_on_map();
        const int before = s_.terrain;
        react_to_terrain();
        if (before == 6) {
            if (s_.terrain == 4) o_.hidden_lines = 3;
            else if (s_.terrain == 5) o_.hidden_lines = 5;
        }
        wait_behind();
    }

    bool snap_to_waypoint() { // $94D40
        if (s_.speed <= 8) return false;
        if (g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y)) > 2) return false;
        set_whole(o_.x, s_.waypoint_x);
        set_whole(o_.y, s_.waypoint_y);
        p_.moved_back = true;
        return true;
    }

    void terrain_speed() { // $94C54
        auto& v = s_.speed;
        const auto half = [&] { v = int(unsigned(std::uint16_t(v)) >> 1); };
        const auto plus = [&](int shift) { v = int(std::uint16_t(v + int(unsigned(std::uint16_t(v)) >> shift))); };
        switch (s_.terrain) {
        case 13: {
            const int h = s_.heading;
            if (h < 0xd0) {
                if (h > 0x8f) half();
                else if (h > 0x6f) return;
                else if (h > 0x3f) plus(3);
                else plus(2);
            } else if (h < 0x130) {
                half();
                half();
            } else if (h < 0x171) {
                half();
            } else if (h < 0x191) {
                return;
            } else if (h < 0x1c1) {
                plus(3);
            } else {
                plus(2);
            }
            return;
        }
        case 1:
        case 2:
            half();
            half();
            return;
        case 7:
            if (((whole(o_.y) + whole(o_.x)) & 3) == 0) return;
            v = int(std::uint16_t(v - int(unsigned(std::uint16_t(v)) >> 2)));
            return;
        default:
            return;
        }
    }

    void move() { // $A0C9A
        if (s_.speed == 0) return;
        s_.heading &= 0x1fe;
        o_.x += (g_.sine(s_.heading) >> 2) * std::int16_t(s_.speed);
        if (o_.x < 0) o_.x = 0;
        o_.y += (g_.sine(s_.heading + 0x80) >> 2) * std::int16_t(s_.speed);
        if (o_.y < 0) o_.y = 0;
    }

    void keep_on_map() { // $94F72
        if (whole(o_.y) <= 13) {
            set_whole(o_.y, whole(p_.old_y));
            p_.moved_back = true;
        }
        if (whole(o_.y) >= t_.height() && (s_.state == 0 || s_.state >= 0x32)) {
            set_whole(o_.y, whole(p_.old_y));
            p_.moved_back = true;
        }
        const int x = whole(o_.x), old = whole(p_.old_x);
        if (x <= 13) {
            if (old <= 13) return;
        } else {
            if (x + 0x1a < t_.width()) return;
            if (old + 0x1a >= t_.width()) return;
        }
        set_whole(o_.x, old);
        p_.moved_back = true;
    }

    bool passable() const {
        const auto probe = t_.probe(whole(o_.x), whole(o_.y), 8, -3);
        return !probe.outside && !probe.blocking;
    }

    void react_to_terrain() { // $950C2
        const auto probe = t_.probe(whole(o_.x), whole(o_.y), 8, -3);
        if (probe.outside || probe.blocking) {
            blocked();
            return;
        }
        const int a = probe.attribute;
        s_.terrain = a;
        s_.slow_target = 0;
        s_.wading = false;
        // Enemies (and side 2) keep out of holes and squeezes; side 2 out of water.
        const bool keeps_out = o_.kind == 5 || o_.side == 2;
        if ((a == 10 || a == 9) && keeps_out) {
            blocked();
            return;
        }
        if (a == 10) {
            if (s_.state == 0 || s_.state >= 0x32) {
                s_.state = 3; // falls (handled with the next steps)
                return;
            }
        }
        if (a == 9) {
            ++s_.squeeze;
            return;
        }
        s_.squeeze = 0;
        if (a == 8) {
            if (s_.state != 0 || s_.speed == 0 || o_.hidden_lines != 0 || o_.height != 0) return;
            if ((w_.random() & 0x3f) == 0) s_.state = 0x32; // trips
            return;
        }
        if (a == 1 || a == 2) {
            o_.hidden_lines = 0;
            if (!s_.sinking) {
                if (o_.height == 0) {
                    s_.sinking = true;
                    o_.height = 1;
                }
            } else if (a == 1) {
                o_.height = 2;
            } else if (o_.height < 6) {
                o_.height += 2;
            }
            return;
        }
        if (s_.sinking) {
            o_.height -= 3;
            if (o_.height <= 0) {
                o_.height = 0;
                s_.sinking = false;
            }
        }
        switch (a) {
        case 4: s_.slow_target = 3; return;
        case 5: s_.slow_target = 6; return;
        case 6:
            if (o_.side == 2) {
                blocked();
                return;
            }
            s_.wading = true;
            s_.slow_target = 0;
            o_.hidden_lines = 0;
            return;
        case 11: s_.entering = 1; return;
        default: o_.hidden_lines = 0; return;
        }
    }

    void blocked() { // $95268
        s_.idle = 1;
        p_.moved_back = true;
        if (o_.side != 0) {
            enemy_blocked();
            return;
        }
        const int new_x = whole(o_.x);
        set_whole(o_.x, whole(p_.old_x)); // keep the new y?
        if (passable()) return;
        set_whole(o_.x, new_x);
        set_whole(o_.y, whole(p_.old_y)); // keep the new x?
        if (passable()) return;
        o_.frame = w_.saved_frame;
        put_back();
        s_.waypoint_x = whole(o_.x);
        s_.waypoint_y = whole(o_.y);
        s_.route_index = int(squad_.route.size()) * 4; // the end of the route
        s_.stopped = 0xff;
    }

    // $95276: an enemy turns away from what blocks it: along x when the old
    // x is open, else along y; then by up to 32 either way from the frame
    // counter, and walks on that way for 12-43 ticks.
    void enemy_blocked() {
        set_whole(o_.x, whole(p_.old_x));
        const auto probe = t_.probe(whole(o_.x), whole(o_.y), 8, -3);
        const bool closed = probe.outside || probe.blocking || probe.attribute == 9 || probe.attribute == 10 ||
                            (o_.side == 2 && probe.attribute == 6);
        const int h = std::int16_t(s_.heading);
        if (!closed) s_.heading = h > 0x80 && h <= 0x180 ? 0x100 : 0;
        else s_.heading = h > 0x100 ? 0x180 : 0x80;
        s_.heading = std::int16_t(s_.heading + ((((w_.frame_counter >> 2) & 0x3f) - 0x20) & 0x1fe));
        set_whole(o_.y, whole(p_.old_y));
        s_.wait = (w_.frame_counter & 0x1f) + 12;
    }

    void wait_behind() { // $94D9C
        if (o_.side != 0) return;
        p_.bumped = false;
        const auto& members = squad_.members;
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (members[i] != slot_) continue;
            if (i == 0) return;
            const auto& ahead = w_.objects.slots[std::size_t(members[i - 1])];
            const auto& ahead_walker = w_.walkers[std::size_t(members[i - 1])];
            const int x = whole(o_.x), y = whole(o_.y), ax = whole(ahead.x), ay = whole(ahead.y);
            if (ax <= std::int16_t(x + 8) && x <= std::int16_t(ax + 8) && std::int16_t(y - 2) <= std::int16_t(ay + 2) &&
                std::int16_t(ay - 2) <= std::int16_t(y + 2) && ahead_walker.carried == 0 &&
                s_.route_index == ahead_walker.route_index) {
                put_back();
                w_.keep_frame = true;
                s_.idle = 1;
                p_.bumped = true;
            }
            return;
        }
    }

    void take_next_waypoint() { // $ABE34
        const std::size_t index = std::size_t(s_.route_index / 4);
        if (index < squad_.route.size()) {
            s_.waypoint_x = squad_.route[index][0];
            s_.waypoint_y = squad_.route[index][1];
            s_.route_index += 4;
            s_.took_waypoint = true;
            return;
        }
        s_.took_waypoint = false; // squads joining at the route's end come later
    }

    void animate() { // $96358
        if (o_.side != 0 && s_.building >= 0 && at_waypoint()) { // goes into its building
            o_.graphic = 0xdc;
            o_.frame = 0;
            w_.walked = true;
            return;
        }
        const int mode = w_.last_mode;
        if (mode != 2 && (s_.aim_override || (s_.idle != 0 && !s_.wading))) w_.keep_frame = true;
        s_.facing = (s_.facing + p_.turn) & 0xe;
        const int f = s_.facing;
        if (w_.other_squad && o_.side != 0 && s_.terrain != 6) {
            // An enemy without a target stands (picture +$60).
            o_.graphic = tables_.picture(o_.side, 0x60 + f);
            o_.frame = 0;
            p_.held_frame = 0;
            return;
        }
        if (s_.animation_state == 2) {
            // The firing pose: the walking picture, still (fast squads: the
            // fast clock), for 8 ticks.
            if (mode != 0) {
                o_.graphic = tables_.picture(o_.side, f);
                o_.frame = mode == 2 ? w_.clock.value[2] : 0;
            }
            if (--s_.animation_timer != 0) return;
            s_.animation_state = 0;
        } else if (s_.animation_state == 1) {
            // Throwing: frames 0-2 of the throwing picture, 2 ticks each.
            if (o_.graphic != tables_.picture(o_.side, 0x50 + f)) {
                o_.graphic = tables_.picture(o_.side, 0x50 + f);
                s_.animation_step = 0;
                o_.frame = 0;
                return;
            }
            if (++s_.animation_step != 2) return;
            s_.animation_step = 0;
            p_.held_frame = ++o_.frame;
            if (o_.frame < 3) return;
            p_.held_frame = 0;
            o_.frame = 0;
            s_.animation_state = 0;
            s_.aim_override = false;
        }
        if (s_.animation_state == 3) {
            // State 3 (meaning open): its picture for 7 ticks.
            if (o_.graphic != tables_.picture(o_.side, 0x70 + f)) {
                o_.graphic = tables_.picture(o_.side, 0x70 + f);
                s_.animation_step = 0;
                o_.frame = 0;
                return;
            }
            if (++s_.animation_step != 7) return;
            s_.animation_step = 0;
            p_.held_frame = 0;
            o_.frame = 0;
            s_.animation_state = 0;
            s_.aim_override = false;
        }
        if (s_.wading) {
            o_.graphic = tables_.picture(o_.side, 0x40 + f);
            o_.frame = (s_.idle != 0 ? w_.clock.value[1] : w_.clock.value[0]) & 1;
        } else if (mode == 0) {
            o_.graphic = tables_.picture(o_.side, 0x30 + f);
            o_.frame = w_.clock.value[1];
        } else if (o_.graphic == tables_.picture(o_.side, 0x30 + f) && o_.frame != 0) {
            o_.frame = 0;
        } else {
            o_.graphic = tables_.picture(o_.side, f);
            o_.frame = s_.roster_weapon == 2 ? w_.clock.value[2] : w_.clock.value[0];
        }
        if (w_.keep_frame) {
            o_.frame = w_.saved_frame;
            w_.keep_frame = false;
        }
    }

    World& w_;
    const Terrain& t_;
    const GeometryTables& g_;
    const SoldierTables& tables_;
    const FireTables& fire_tables_;
    const ProgramTables& tables_all_;
    GameObject& o_;
    Walker& s_;
    int slot_;
    Squad no_squad_;
    Squad& squad_;
    Pass p_;
};

} // namespace

void AnimationClock::tick() { // $94B54
    const auto bounce = [&](int i) {
        value[std::size_t(i)] += direction[std::size_t(i)];
        if (value[std::size_t(i)] == 0 || value[std::size_t(i)] == 2) {
            direction[std::size_t(i)] = -direction[std::size_t(i)]; // eori #$FFFE turns 1 into -1 and back
        }
    };
    if (--slow_timer < 0) {
        slow_timer = 3;
        bounce(0);
        bounce(1);
    }
    if (--fast_timer < 0) {
        fast_timer = 1;
        bounce(2);
    }
}

Terrain::Terrain(const data::MissionMap& map, std::span<const std::uint8_t> base_hit, std::span<const std::uint8_t> sub_hit,
                 std::span<const std::uint8_t> base_bht, std::span<const std::uint8_t> sub_bht,
                 std::span<const std::uint8_t> program)
    : width_(map.width * 16), height_(map.height * 16), cells_(map.cells) {
    map_width_tiles_ = map.width;
    for (std::size_t i = 0; i + 1 < base_hit.size(); i += 2) attributes_.push_back(amiga::read_be16(base_hit, i));
    for (std::size_t i = 0; i + 1 < sub_hit.size(); i += 2) attributes_.push_back(amiga::read_be16(sub_hit, i));
    masks_.assign(base_bht.begin(), base_bht.end());
    masks_.insert(masks_.end(), sub_bht.begin(), sub_bht.end());
    for (std::size_t i = 0; i < 16; ++i) blocking_[i] = program[file_offset(0xa2340) + i];
}

Terrain::Probe Terrain::probe(int x, int y, int dx, int dy) const { // $A22CE
    const int py = std::int16_t(y + dy), px = std::int16_t(x + dx);
    if (py < 0 || px < 0) return {true, true, 0};
    const auto cell = std::size_t((py >> 4) * map_width_tiles_ + (px >> 4));
    const int tile = cell < cells_.size() ? cells_[cell] & 0x1ff : 0;
    int attribute = std::size_t(tile) < attributes_.size() ? attributes_[std::size_t(tile)] : 0;
    if (attribute & 0x8000) {
        const auto row = std::size_t(tile) * 8 + std::size_t((py >> 1) & 7);
        if (row < masks_.size() && (masks_[row] >> (7 - ((px >> 1) & 7)) & 1)) attribute >>= 4;
    }
    attribute &= 0xf;
    return {false, blocking_[std::size_t(attribute)] != 0, attribute};
}

bool update_soldier(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Soldier(world, terrain, tables, slot).update();
}

bool update_enemy(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Soldier(world, terrain, tables, slot).update_enemy();
}

void Terrain::set_swaps(std::span<const std::uint8_t> base_swp, std::span<const std::uint8_t> sub_swp) {
    swaps_.clear();
    for (std::size_t i = 0; i + 1 < base_swp.size(); i += 2) swaps_.push_back(std::int16_t(amiga::read_be16(base_swp, i)));
    for (std::size_t i = 0; i + 1 < sub_swp.size(); i += 2) swaps_.push_back(std::int16_t(amiga::read_be16(sub_swp, i)));
}

int Terrain::swap(int tile) const {
    return tile >= 0 && std::size_t(tile) < swaps_.size() ? swaps_[std::size_t(tile)] : -1;
}

bool Terrain::sight_blocked(int x0, int y0, int x1, int y1) const { // $A1260 → $A0EC0, $A0F22
    // The line's steps from tile to tile ($A0EC0).
    int x = int(unsigned(std::uint16_t(x0)) >> 4), y = int(unsigned(std::uint16_t(y0)) >> 4);
    const int ex = int(unsigned(std::uint16_t(x1)) >> 4), ey = int(unsigned(std::uint16_t(y1)) >> 4);
    int dx = ex - x, dy = ey - y;
    const int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = std::abs(dx);
    dy = std::abs(dy);
    int error = dy == 0 ? -1 : 0;
    std::vector<int> steps;
    const int start = y * map_width_tiles_ + x;
    while (x != ex || y != ey) {
        if (error >= 0) {
            y += sy;
            error -= dx;
            steps.push_back(sy * map_width_tiles_);
        } else {
            x += sx;
            error += dy;
            steps.push_back(sx);
        }
    }
    // The walk ($A0F22): two steps at a time; the first two places are not
    // looked at.
    int cell = start, skip = 2;
    std::size_t k = 0;
    while (true) {
        if (skip != 0) {
            --skip;
        } else if (cell >= 0 && std::size_t(cell) < cells_.size()) {
            const int tile = cells_[std::size_t(cell)] & 0x1ff; // (flagged cells: open)
            const int attribute = std::size_t(tile) < attributes_.size() ? attributes_[std::size_t(tile)] : 0;
            if (!(attribute & 0x8000) && blocking_[std::size_t(attribute & 0xf)] != 0) return true;
        }
        if (k >= steps.size()) return false;
        int step = steps[k++];
        if (k < steps.size()) {
            cell += step;
            step = steps[k++];
        }
        cell += step;
    }
}

} // namespace fodder::game
