#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/vehicles.hpp"
#include "amiga/adf.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
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

constexpr int bleeding_mark = 0x9876;

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
        if (s_.carried != 0) return ride(w_, tables_all_, slot_); // $9A82C
        if (s_.flags75 & 2) { // protected (a power-up): hits do not hold
            if (s_.state <= 0x32) s_.state = 0;
            s_.entering = 0;
            s_.hit_timer = 0;
        }
        if (const int handled = states(); handled >= 0) return handled != 0;
        if (s_.state != 0) return true;
        if (end_pose()) return true;
        board(w_, tables_all_, slot_); // $9A87E
        const bool other_squad = squad_.merged_into < 0 && o_.squad != w_.selected_squad;
        w_.last_mode = squad_.mode;
        if (other_squad) return squad_on_its_own();
        w_.other_squad = false;
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

    // $967B8: a squad that is not selected looks for enemies by itself (one
    // candidate a tick, as the enemies do), aims at one it sees within 210
    // (but now and then lets it go) and fires at its own pace; it walks its
    // route meanwhile.
    bool squad_on_its_own() {
        w_.other_squad = true;
        const int index = std::clamp(s_.target, 0, object_slots - 1);
        const auto& t = w_.objects.slots[std::size_t(index)];
        const auto next = [&] {
            s_.fire_delay = 0;
            if (++s_.target >= object_slots) s_.target = 0;
        };
        bool found = false;
        if (t.used && whole(t.x) >= 0 && t.kind == 5 && w_.walkers[std::size_t(index)].state == 0 && !s_.wading) {
            const int d = g_.distance(whole(t.x) - whole(o_.x), whole(t.y) - whole(o_.y));
            if (d < 0xd2) {
                found = d <= 0x28;
                if (!found) {
                    w_.target_distance = d;
                    found = !t_.sight_blocked(whole(o_.x), whole(o_.y), whole(t.x), whole(t.y)) || d < 0x40;
                }
            }
        }
        if (found && (w_.random() & 0x1f) == 5) found = false;
        if (!found) {
            next();
            return walk();
        }
        s_.aim_override = true;
        s_.aim_x = whole(t.x);
        s_.aim_y = whole(t.y) - 0xe;
        if (s_.fire_delay == 0) {
            s_.fire_delay = int(w_.random() & 0xf) + 1;
            if ((w_.random() & 0x3f) == 0x2a) s_.fire_delay = 1;
        }
        if (s_.fire_delay > 0 && --s_.fire_delay == 0) s_.fire_delay = -1;
        if (s_.fire_delay < 0) use_weapons(true, false, false);
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

    bool update_bazooka() { // $9B48E: a soldier with a rocket launcher, kneeling
        if (const int handled = states(); handled >= 0) return handled != 0;
        w_.other_squad = false;
        s_.idle = 0xff;
        seek_target();
        // $96230 with all weapons off: kind $24 fires rockets at a target
        // beyond 24.
        if (s_.fire_delay != 0) {
            bool fire = s_.fire_delay < 0;
            if (!fire && --s_.fire_delay == 0) {
                s_.fire_delay = -1;
                fire = true;
            }
            if (fire) use_weapons(false, false, w_.target_distance > 0x18);
        }
        turn_facing();
        w_.other_squad = false;
        w_.walked = false;
        s_.animation_state = 3;
        s_.aim_override = true;
        s_.animation_step = 0;
        o_.frame = 0;
        look_at_aim();
        o_.frame = 2;
        return true;
    }

    // $9A2A6 / $9A2DA / $9A31A / $9A290: civilians ($3D, $3E, the calling
    // $46, the standing $53). They walk home to the civilians' house when
    // there is one, wander otherwise; $3E throws spears once civilians have
    // been killed, $46 always may.
    bool update_civilian(const MissionTables& mission) {
        if (o_.kind == 0x46) o_.side = 1;
        if (s_.state != 0) return civilian_dies();
        o_.side = 2;
        o_.graphic = 0xd0;
        if (o_.kind == 0x53) return true;
        if (o_.kind == 0x46) {
            if (w_.landscape == 3) call_out(0x26);
            if (w_.landscape == 4) call_out(0x1f);
        }
        const int home = walk_home();
        if (home > 0) return true;
        if (home < 0) civilian_wander(mission);
        s_.speed = o_.kind == 0x3d ? 6 : o_.kind == 0x3e ? 10 : 0xc;
        civilian_step();
        if (o_.kind == 0x46 || (o_.kind == 0x3e && w_.civilians_angry)) throw_at_leader();
        return true;
    }

    // $9A506 / $9A5A8 / $9A5D2: a civilian's step towards the waypoint (a
    // wobble except kind $44), the terrain, the walking picture.
    void civilian_step() {
        save_position();
        if (s_.wait != 0) {
            --s_.wait;
        } else if (!head_for(s_.waypoint_x, s_.waypoint_y)) {
            p_.moved_back = true;
        }
        if (o_.kind != 0x44) {
            const auto r = w_.random();
            int wobble = int(r & 0xe);
            if (r & 0x8000) wobble = -wobble;
            s_.heading = (s_.heading + wobble) & 0x1fe;
        }
        if (!snap_to_waypoint()) {
            terrain_speed();
            move();
        }
        keep_on_map();
        react_to_terrain();
        if (s_.terrain == 6) put_back();
        else if (s_.terrain == 4) o_.hidden_lines = 3;
        else if (s_.terrain == 5) o_.hidden_lines = 5;
        if (o_.kind == 0x48 || o_.kind == 0x6a) return; // (hostages have their own picture)
        // $9A5A8 / $9A5D2: a step every 4 ticks, facing the heading.
        if (!p_.moved_back) {
            s_.fall_count = (s_.fall_count + 1) & 3;
            if (s_.fall_count == 0) o_.frame = (o_.frame + 1) & 1;
        }
        o_.frame = ((((s_.heading >> 5) - 1) ^ 0xf) & 0xe) | (o_.frame & 1);
    }

    // $87906: now and then (1 tick in 128) a call, louder in the moors and
    // the desert.
    void call_out(int effect) {
        if (w_.end_count != 0) return;
        if ((w_.random() & 0x7f) != 0xe) return;
        request_sound(w_, effect, (w_.landscape == 3 || w_.landscape == 4) ? 0x7f : 0, slot_);
    }

    // $9A380: to the civilians' house ($5A) while there is one: within 3 it
    // goes in (objective 8 met); within 6 the door opens; blocked from view
    // it now and then steps aside. -1 no house, 0 on the way, 1 home.
    int walk_home() {
        const int house = w_.civilian_house;
        if (house < 0) return -1;
        if (s_.stopped != 0 && --s_.stopped != 0) return 0;
        const auto& h = w_.objects.slots[std::size_t(house)];
        s_.waypoint_x = whole(h.x);
        s_.waypoint_y = whole(h.y);
        const int d = g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y), 0x20, 1000);
        if (d <= 3) {
            free_object(w_, slot_);
            w_.civilian_home = true;
            return 1;
        }
        if (d < 6) {
            w_.walkers[std::size_t(house)].fall_count = 10;
            return 0;
        }
        if (!t_.sight_blocked(whole(o_.x), whole(o_.y), s_.waypoint_x, s_.waypoint_y)) return 0;
        if ((w_.random() & 0x3f) != 0) return 0;
        s_.waypoint_x = whole(o_.x) - 0x10;
        s_.waypoint_y = whole(o_.y) - 0x20;
        s_.stopped = 0x1e;
        return 0;
    }

    // $9A490: on screen, one tick in 64, a spear at the leader.
    void throw_at_leader() {
        if (!o_.on_screen || (w_.random() & 0x3f) != 9 || w_.leader_slot < 0) return;
        const auto& l = w_.objects.slots[std::size_t(w_.leader_slot)];
        s_.aim_x = whole(l.x);
        s_.aim_y = whole(l.y);
        throw_spear();
    }

    // $9938A: a spear (kind $47), like a bullet: 8 ticks (more for skilled
    // throwers, at most 16), speed 24 then 50 + skill, aimed 7 right of the
    // aim point, off by up to 7.
    void throw_spear() {
        if (w_.end_count != 0 || s_.aim_x == 0) return;
        const int slot = allocate_slot(w_);
        if (slot < 0 || w_.bullets_alive == 12) return;
        ++w_.bullets_alive;
        auto& b = w_.objects.slots[std::size_t(slot)];
        auto& bw = w_.walkers[std::size_t(slot)];
        auto& bs = w_.bullets[std::size_t(slot)];
        b.x = o_.x;
        b.y = o_.y;
        b.graphic = 0xd7;
        b.frame = 0;
        bs.lifetime = std::min((s_.skill >> 3) + 8, 16);
        b.kind = 0x47;
        b.height = o_.height + 6;
        bw.height_fraction = s_.height_fraction;
        b.hidden_lines = o_.hidden_lines;
        b.side = o_.side;
        b.layer = 0;
        bs.cruise_speed = 0x32 + s_.skill;
        bw.speed = 0x18;
        bw.aim_x = s_.aim_x + 7;
        bw.aim_y = s_.aim_y;
        bs.owner = slot_;
        bs.muzzle_ticks = 2;
        bs.offset_x = 0;
        bs.offset_y = 2;
        bs.first_heading = -1;
        const auto r = w_.random();
        int spread = int(r & 7);
        if (r & 0x80) spread = -spread;
        bs.spread = spread;
        bs.spread_done = false;
        if (w_.landscape == 3) request_sound(w_, (w_.random() & 1) ? 0x10 : 0x11, 0, slot_);
        bs.age = 0;
    }

    // $9A16C (kind $44) / $9A04C ($5B): a civilian standing on the spot,
    // moving its arms, calling now and then; $5B blows up when the leader
    // comes within 20.
    bool update_bystander() {
        if (bystander_hit()) return true;
        o_.side = 2;
        o_.graphic = 0xd5;
        if (s_.aim_x == 0) {
            s_.aim_x = -1;
            s_.animation_state = int(w_.random() & 0xf);
        }
        if (w_.tick_counter & 1) s_.animation_state = (s_.animation_state + 1) & 0xff;
        static constexpr int frames[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 1}; // $9A1FA
        o_.frame = frames[s_.animation_state & 15];
        if (w_.landscape == 3) {
            const int r = int(w_.random() & 3);
            if (r != 3) call_out(0x23 + r);
        }
        if (w_.landscape == 2) call_out(0x1e);
        if (o_.kind == 0x5b && o_.graphic != 0xe1 && s_.state == 0 && w_.leader_slot >= 0) {
            const auto& l = w_.objects.slots[std::size_t(w_.leader_slot)];
            if (g_.distance(whole(o_.x) - whole(l.x), whole(o_.y) - whole(l.y), 0x20, 1000) <= 0x14) s_.state = 9;
        }
        return true;
    }

    // $9A092: shot, it flashes for 20 ticks (bleeding at first) and is gone;
    // a $5B's fuse (state 9) runs its frames, then it explodes.
    bool bystander_hit() {
        if (s_.state == 0) return false;
        if (s_.state == 9) {
            if (o_.graphic != 0xe1) {
                o_.graphic = 0xe1;
                o_.frame = 0;
                s_.fall_count = 0;
                return true;
            }
            s_.fall_count += 2;
            static constexpr int fuse[] = {1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 2, 3, -1}; // $9A148
            const int at = std::clamp(s_.fall_count / 2, 0, int(std::size(fuse)) - 1);
            if (fuse[at] < 0) {
                o_.kind = 0xc;
                s_.waypoint_x = std::int16_t(0x8005);
                s_.waypoint_y = std::int16_t(0xfffd);
                return true;
            }
            o_.frame = fuse[at];
            return true;
        }
        if (s_.fall_count == 0) {
            s_.fall_count = 0x14;
            o_.frame = 0;
        }
        if (--s_.fall_count == 0) {
            free_object(w_, slot_);
            return true;
        }
        if (s_.fall_count < 10) {
            o_.frame = 0;
            o_.graphic = o_.graphic == 0xd5 ? 0x7c : 0xd5;
            return true;
        }
        spawn_blood(w_, slot_);
        return true;
    }

    // $98F12 (kinds $48 and $6A): a hostage. It walks to the nearest of: the
    // rescue tent ($49; there it is saved), a recruit (it follows him and
    // gets into his vehicle when it stands near), an enemy (it stays and
    // cowers; the enemy guards it). Kind $48 heads for a tent within 128
    // every 200 ticks; it cannot be hurt.
    bool update_hostage(const MissionTables& mission) {
        if (s_.carried != 0) return hostage_riding();
        o_.side = 2;
        o_.graphic = 0xd9;
        const std::int32_t start_x = o_.x, start_y = o_.y;
        hostage_choose();
        if (!o_.used || o_.removed) return true;
        civilian_wander(mission);
        s_.speed = 0xc;
        civilian_step();
        s_.state = 0;
        // $9933E / $9936E: the walking picture, 8 directions of 3.
        int step = s_.fire_delay;
        s_.fall_count = (s_.fall_count + 1) & 1;
        if (s_.fall_count == 0) step += 2;
        s_.fire_delay = step & 6;
        static constexpr int steps[4] = {0, 1, 2, 1}; // $99366
        o_.frame = steps[s_.fire_delay >> 1];
        const int facing = (((((s_.heading + 0x20) & 0x1c0) >> 5) - 1) ^ 0xf) & 0xe;
        o_.frame += facing + (facing >> 1);
        // $990C4
        if (w_.hostage_held) {
            o_.graphic = 0xd8;
            o_.frame = 1;
            put_back();
        } else if (o_.x == start_x && o_.y == start_y) {
            o_.graphic = 0xda;
            if ((w_.tick_counter & 0xf) == 0) {
                static constexpr int idle[4] = {0, 1, 2, 1}; // $99122
                s_.idle_frame = idle[(w_.random() & 6) >> 1];
            }
            o_.frame = s_.idle_frame;
        }
        if (o_.kind == 0x6a && slot_ + 1 < object_slots) {
            auto& piece = w_.objects.slots[std::size_t(slot_ + 1)];
            if (s_.carried != 0) {
                piece.kind = 4;
                piece.graphic = 0x7c;
                piece.frame = 0;
            } else {
                piece.kind = 0x29;
                set_whole(piece.x, whole(o_.x) + 6);
                set_whole(piece.y, whole(o_.y) - 0xb + o_.hidden_lines);
                piece.height = o_.height;
            }
        }
        if (o_.x == p_.old_x && o_.y == p_.old_y) o_.graphic = 0xd8;
        return true;
    }

    // $9912A: the hostage's choice for the tick (one candidate a tick).
    void hostage_choose() {
        w_.hostage_held = false;
        const bool leader_kind = o_.kind == 0x6a;
        if (!leader_kind && w_.rescue_tent >= 0) {
            if (s_.hits != 0) {
                --s_.hits;
            } else {
                s_.hits = 0xc8;
                const auto& t = w_.objects.slots[std::size_t(w_.rescue_tent)];
                if (g_.distance(whole(t.x) + 0xa - whole(o_.x), whole(t.y) - 5 - whole(o_.y), 0x80, 1000) < 0x7f) {
                    s_.target = w_.rescue_tent;
                }
            }
        }
        const std::vector<int> kinds = leader_kind ? std::vector<int>{0, 0x49} : std::vector<int>{0x49, 5, 0};
        const int found = scan(kinds);
        if (found < 0) {
            if ((w_.random() & (leader_kind ? 7 : 0x3f)) == 0 && ++s_.target >= object_slots) s_.target = 0;
            return;
        }
        const auto& t = w_.objects.slots[std::size_t(found)];
        if (t.kind == 0x49) {
            s_.waypoint_x = whole(t.x) + 0xa;
            s_.waypoint_y = whole(t.y) - 5;
            if (g_.distance(s_.waypoint_x - whole(o_.x), s_.waypoint_y - whole(o_.y), 0x10, 1000) < 3) {
                --w_.objects.kind_6a_48;
                if (leader_kind) free_object(w_, slot_ + 1);
                free_object(w_, slot_);
            }
            return;
        }
        if (t.kind == 5) {
            w_.walkers[std::size_t(found)].building = slot_; // the enemy guards it
            s_.waypoint_x = whole(o_.x);
            s_.waypoint_y = whole(o_.y);
            w_.hostage_held = true;
            return;
        }
        if (leader_kind) {
            w_.hostage_held = true;
            o_.graphic = 0xd8;
        }
        s_.waypoint_x = whole(t.x) + 4;
        s_.waypoint_y = whole(t.y) - 6;
        const auto& tw = w_.walkers[std::size_t(found)];
        if (tw.carried != 0 && tw.vehicle >= 0) {
            const auto& v = w_.objects.slots[std::size_t(tw.vehicle)];
            const auto& vw = w_.walkers[std::size_t(tw.vehicle)];
            if (vw.speed <= 2 && v.height <= 3 &&
                g_.distance(whole(v.x) - whole(o_.x), whole(v.y) - whole(o_.y), 0x10, 1000) <= (leader_kind ? 0xf : 0xa)) {
                s_.carried = 0xff;
                return;
            }
        }
        if ((w_.random() & (leader_kind ? 7 : 0x3f)) == 0 && ++s_.target >= object_slots) s_.target = 0;
    }

    // $98E64 for the hostage (see vehicles.cpp): one candidate a tick.
    int scan(const std::vector<int>& kinds) {
        const int index = std::clamp(s_.target, 0, object_slots - 1);
        const auto& t = w_.objects.slots[std::size_t(index)];
        const auto next = [&] {
            if (++s_.target >= object_slots) s_.target = 0;
            return -1;
        };
        if (!t.used || whole(t.x) < 0) return next();
        if (std::find(kinds.begin(), kinds.end(), t.kind) == kinds.end()) return next();
        const int d = g_.distance(whole(t.x) - whole(o_.x), whole(t.y) - whole(o_.y));
        if (d >= 0xd2) return next();
        if (d > 0x28 && t_.sight_blocked(whole(o_.x), whole(o_.y), whole(t.x), whole(t.y)) && d >= 0x40) return next();
        return index;
    }

    // $98FE2: riding with its recruit; when he gets out, it does too (on
    // open ground next to him) with a scream.
    bool hostage_riding() {
        s_.state = 0;
        const int r = std::clamp(s_.target, 0, object_slots - 1);
        const auto& t = w_.objects.slots[std::size_t(r)];
        if (w_.walkers[std::size_t(r)].carried != 0) {
            o_.graphic = 0x7c;
            o_.frame = 0;
            o_.x = t.x;
            o_.y = t.y;
            return true;
        }
        set_whole(o_.x, whole(t.x) + 8);
        set_whole(o_.y, whole(t.y) + 0xa);
        const int y0 = whole(o_.y);
        while (t_.probe(whole(o_.x), whole(o_.y), 8, -3).blocking) {
            if (whole(o_.y) - 1 >= 0) {
                set_whole(o_.y, whole(o_.y) - 1);
                continue;
            }
            set_whole(o_.y, y0);
            if (whole(o_.x) - 1 < 0) {
                set_whole(o_.x, 0);
                s_.state = 5;
                break;
            }
            set_whole(o_.x, whole(o_.x) - 1);
        }
        if (whole(o_.y) >= t_.height()) set_whole(o_.y, t_.height() - 1);
        s_.carried = 0;
        // $99066
        if (o_.kind == 0x6a) free_object(w_, slot_ + 1);
        o_.graphic = 0xdb;
        o_.frame = 0;
        request_sound(w_, tables_.cry(int(w_.random())), 0x14, slot_);
        return true;
    }

private:
    // $9A4C6 / $99FE4: now and then a new place to walk to, on open ground
    // somewhere on the map (4 tiles in from the top and left).
    void civilian_wander(const MissionTables& mission) {
        if (s_.stopped != 0 && --s_.stopped != 0) return;
        if (o_.kind == 0x48 || o_.kind == 0x6a) {
            s_.waypoint_x = whole(o_.x);
            s_.waypoint_y = whole(o_.y);
        } else {
            const int tx = int(w_.random() & 0x7f) + 4;
            if (tx >= t_.width_tiles()) return;
            const int ty = int(w_.random() & 0x3f) + 4;
            if (ty >= t_.height_tiles()) return;
            const int x = tx * 16 + int(w_.random() & 0xf), y = ty * 16 + int(w_.random() & 0xf);
            const int a = t_.tile_attribute(x, y);
            const bool avoid = (a & 0x8000) ? (mission.civilian_avoids(a & 15) || mission.civilian_avoids((a >> 4) & 15))
                                            : mission.civilian_avoids(a & 15);
            if (avoid) return;
            s_.waypoint_x = x;
            s_.waypoint_y = y;
        }
        s_.stopped = int(w_.random() & 0x3f) + 20;
    }

    // $9A426: a civilian killed: the others turn hostile; on a "protect"
    // or "get civilian home" mission it is lost.
    bool civilian_dies() {
        if (o_.graphic == 0xd6) {
            if (++o_.frame >= 9) free_object(w_, slot_);
            return true;
        }
        w_.civilians_angry = true;
        if (w_.objectives[3] || w_.objectives[7]) fail_objective(w_); // ($81DD2)
        o_.graphic = 0xd6;
        o_.frame = 0;
        request_sound(w_, tables_.cry(int(w_.random())), 0x14, slot_);
        return true;
    }

    // $95BB2: the states before walking: 1 handled, 0 not rebuilt yet, -1
    // the normal state (go on).
    int states() {
        if (s_.state == 3) return drop(); // into a hole
        if (s_.state == 6) return lie(true);
        if (s_.state == 2) return lie(false);
        if (whole(o_.y) >= t_.height()) set_whole(o_.y, t_.height());
        if (s_.squeeze != 0) return drop(); // over a cliff
        if (s_.entering == 1) return drown();
        if (s_.state == 0xa) return writhe();
        if (s_.state == 0x32) return trip(false);
        if (s_.state == 0x33) return slide();
        if (s_.state == 0x34) return trip(true);
        if (s_.state == 0x5a || s_.state == 0x5b) { // into a vehicle with a puff ($95A28)
            const int r = board_by_puff(w_, slot_);
            if (r != 2) return r;
            s_.speed = 0x30;
            step();
            return 1;
        }
        if (s_.state == 0) return -1;
        return s_.hit_timer != 0 ? start_falling() : fall();
    }

    // $965FC: once the phase is won (or an objective failed) the recruits
    // stop: they salute, jump about (with a shadow) or, failed, jump or
    // throw up their arms; in water they stand deeper. True: handled.
    bool end_pose() {
        if (!w_.failed_objective && w_.outcome != World::Outcome::won) return false;
        if (t_.probe(whole(o_.x), whole(o_.y), 8, -3).attribute == 6) {
            o_.hidden_lines = o_.height < 8 ? std::min(8 - o_.height, 8) : 0;
        }
        const auto salute = [&] { // $966B2
            static constexpr int frames[7] = {0, 1, 2, 3, 2, 1, 2}; // $96722
            o_.frame = frames[std::clamp(s_.fall_count, 0, 6)];
            s_.speed ^= 0x8000;
            if (s_.speed & 0x8000) return true;
            if (++s_.fall_count >= 7) s_.fall_count = 3;
            return true;
        };
        const auto jump = [&] { // $966F6
            o_.frame += s_.fall_count;
            if (o_.frame == 0 || o_.frame == 5) s_.fall_count = -s_.fall_count;
            o_.height = std::max(o_.frame - 1, 0) * 8;
            return true;
        };
        const auto arms = [&] { // $966E0
            s_.speed ^= 0x8000;
            if (s_.speed & 0x8000) return true;
            if (o_.frame < 5) ++o_.frame;
            return true;
        };
        if (o_.graphic == 0x41) return jump();
        if (o_.graphic == 0x9f) return salute();
        if (o_.graphic == 0x38) return arms();
        o_.frame = 0;
        s_.speed = 0;
        if (!w_.failed_objective && ((w_.random() & 1) == 0 || o_.hidden_lines != 0)) {
            o_.graphic = 0x9f;
            s_.fall_count = 0;
            return salute();
        }
        if ((w_.random() & 1) == 0) {
            o_.graphic = 0x41;
            o_.frame = int(w_.random() & 3);
            s_.fall_count = 1;
            cast_shadow(w_, slot_);
            return jump();
        }
        o_.graphic = 0x38;
        return arms();
    }

    // $960E8: a hole (state 3) swallows the soldier: deeper by a growing
    // step until 14 lines are hidden, then he is dead. Over a cliff
    // (terrain 9, the squeeze count) he falls faster and faster, his
    // waypoint and route sliding down with him while the ground below is a
    // cliff or a hole; landing within 12 ticks he walks on, later he dies.
    int drop() {
        if (s_.squeeze == 1) {
            s_.state_timer = 1;
            s_.waypoint_y += 2;
        }
        const int step = s_.state_timer;
        s_.state_timer = std::int16_t(s_.state_timer + 1);
        int ground = 0;
        if (s_.state == 3) {
            o_.hidden_lines += step;
            if (o_.hidden_lines < 14) return 1;
            o_.hidden_lines = 14;
            s_.state_timer = 20;
            request_sound(w_, tables_.cry(int(w_.random())), 0xa, slot_);
        } else {
            set_whole(o_.y, whole(o_.y) + step);
            if (whole(o_.y) >= t_.height()) s_.state = 2;
            p_.old_y = o_.y;
            // (The original passes x as the row and y as the column here.)
            const int below = t_.probe_raw(s_.waypoint_x + 8, s_.waypoint_y - 3);
            if (below == 9 || below == 10) {
                s_.waypoint_y += s_.state_timer;
                const std::size_t index = std::size_t(s_.route_index / 4);
                if (index < squad_.route.size()) squad_.route[index][1] = s_.waypoint_y;
            }
            ground = t_.probe(whole(o_.x), whole(o_.y), 8, -3).attribute; // (what $950C2 reads)
            react_to_terrain();
            if (s_.state_timer > 5) { // the falling picture
                s_.facing = (s_.facing + p_.turn) & 0xe;
                o_.graphic = tables_.picture(o_.side, 0x80 + s_.facing);
                s_.facing_timer = s_.heading;
                o_.frame = 0;
            }
            if (ground == 9) return 1;
        }
        if (s_.state_timer < 12) { // landed in time
            s_.facing_timer = 0;
            s_.squeeze = 0;
            return s_.state == 0 ? -1 : 1;
        }
        s_.state = 6;
        s_.heading = s_.facing_timer;
        s_.state_timer = 0;
        s_.pause = 1;
        return 1;
    }

    // $95F48: deep water (terrain 11): the soldier goes under, a line every
    // 10 ticks from 5 to 13, and is dead.
    int drown() {
        if (o_.hidden_lines < 5) {
            o_.hidden_lines = 5;
            request_sound(w_, 0xe, 0xa, slot_);
            o_.graphic = o_.side != 0 ? 0x7a : 0x38;
            o_.frame = 0;
            s_.state_timer = 10;
            return 1;
        }
        if (--s_.state_timer == 0) {
            if (++o_.hidden_lines >= 13) {
                s_.state = 6;
                s_.heading = s_.facing_timer;
                s_.state_timer = 0;
                s_.pause = 1;
                return 1;
            }
            s_.state_timer = 10;
        }
        if (!(s_.state_timer & 2) && o_.frame < 5) ++o_.frame;
        return 1;
    }

    // $95ABA (state $32) / $959D4 ($34): a trip; not in grass or water
    // (terrain 4-6). The soldier slides on (state $33), turned a little.
    int trip(bool sharp) {
        if (s_.terrain >= 4 && s_.terrain <= 6) {
            s_.state = 0;
            return -1;
        }
        s_.state = 0x33;
        if (sharp) {
            s_.speed = 0x2e;
            s_.heading = s_.heading + int(w_.random() & 0x1f);
        } else {
            static constexpr std::array<int, 4> turns{-0x22, -0x38, 0x38, 0x22}; // $95AB2
            s_.speed = 0x24;
            s_.heading = (s_.heading + turns[std::size_t((w_.random() & 6) >> 1)]) & 0x1fe;
        }
        o_.graphic = o_.side != 0 ? 0xa7 : 0xa4;
        o_.frame = ((((s_.heading >> 5) - 1) ^ 0xf) & 0xe) >> 1;
        return 1;
    }

    // $95B1C: sliding, slower by 5 a tick (at once in grass or water); then
    // he sits up (picture +$60) and gets on, and the squad's leader is chosen
    // again.
    int slide() {
        save_position();
        w_.saved_frame = o_.frame;
        move();
        keep_on_map();
        react_to_terrain();
        if (s_.state == 0x33) {
            bool stop = s_.terrain >= 4 && s_.terrain <= 6;
            if (!stop) {
                s_.speed -= 5;
                if (s_.speed > 0) return 1;
                s_.speed = 0;
            }
            const int facing = (((s_.heading >> 5) - 1) ^ 0xf) & 0xe;
            const int sitting = tables_.picture(o_.side, 0x60 + facing);
            if (o_.graphic != sitting) {
                o_.graphic = sitting;
                o_.frame = 0;
                return 1;
            }
            s_.state = 0;
        }
        s_.idle = 0;
        choose_leader(w_);
        return 1;
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
                w_.target_distance = distance;
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
        // Once the mission is lost every falling body bleeds on (the mark
        // $9876 in the route index).
        if (w_.outcome == World::Outcome::lost) s_.route_index = bleeding_mark;
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
        s_.facing_timer = s_.heading;
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
        if ((s_.route_index & 0xffff) == bleeding_mark) { // $95DA8: blood every tick, a cry every 8
            ++s_.waypoint_y;
            spawn_blood(w_, slot_);
            if ((s_.waypoint_y & 7) == 0) request_sound(w_, tables_.cry(int(w_.random())), 0x14, slot_);
        }
        if (s_.spin >= 0) {
            s_.facing_timer = s_.spin;
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
        s_.heading = s_.facing_timer;
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
        // $ABE6E: at the route's end a soldier of a squad told to join
        // another one goes over to it (with the squad's ammunition), and
        // that squad is selected.
        if (!s_.took_waypoint) return;
        s_.took_waypoint = false;
        const int from = o_.squad;
        if (from < 0 || from > 2) return;
        const int to = w_.squads[std::size_t(from)].merged_into;
        if (to < 0 || to > 2 || w_.squads[std::size_t(from)].members.size() > 8) return;
        if (s_.roster_index >= 0 && s_.roster_index < 8) w_.marked[std::size_t(s_.roster_index)] = false;
        s_.route_index = 4 * int(w_.squads[std::size_t(to)].route.size());
        o_.squad = to;
        rebuild_squads(w_);
        w_.selected_squad = to;
        auto& a = w_.squads[std::size_t(from)];
        auto& b = w_.squads[std::size_t(to)];
        b.grenades += a.grenades;
        a.grenades = 0;
        b.rockets += a.rockets;
        a.rockets = 0;
        choose_leader(w_);
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

bool update_bazooka(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Soldier(world, terrain, tables, slot).update_bazooka();
}

bool update_bystander(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Soldier(world, terrain, tables, slot).update_bystander();
}

bool update_hostage(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Soldier(world, terrain, tables, slot).update_hostage(tables.mission);
}

bool update_civilian(World& world, const Terrain& terrain, const ProgramTables& tables, int slot) {
    return Soldier(world, terrain, tables, slot).update_civilian(tables.mission);
}

void Terrain::set_swaps(std::span<const std::uint8_t> base_swp, std::span<const std::uint8_t> sub_swp) {
    swaps_.clear();
    for (std::size_t i = 0; i + 1 < base_swp.size(); i += 2) swaps_.push_back(std::int16_t(amiga::read_be16(base_swp, i)));
    for (std::size_t i = 0; i + 1 < sub_swp.size(); i += 2) swaps_.push_back(std::int16_t(amiga::read_be16(sub_swp, i)));
}

int Terrain::probe_raw(int row, int column) const { // $A22C8
    const auto r = unsigned(std::uint16_t(row)), c = unsigned(std::uint16_t(column));
    const auto cell = std::size_t((r >> 4) * unsigned(map_width_tiles_) + (c >> 4));
    const int tile = cell < cells_.size() ? cells_[cell] & 0x1ff : 0;
    int attribute = std::size_t(tile) < attributes_.size() ? attributes_[std::size_t(tile)] : 0;
    if (attribute & 0x8000) {
        const auto mask_row = std::size_t(tile) * 8 + ((c >> 1) & 7);
        if (mask_row < masks_.size() && (masks_[mask_row] >> (7 - ((r >> 1) & 7)) & 1)) attribute >>= 4;
    }
    return attribute & 0xf;
}

int Terrain::tile_attribute(int x, int y) const {
    const int cell = (int(unsigned(std::uint16_t(y)) >> 4)) * map_width_tiles_ + (int(unsigned(std::uint16_t(x)) >> 4));
    if (cell < 0 || std::size_t(cell) >= cells_.size()) return 0;
    const auto tile = std::size_t(cells_[std::size_t(cell)] & 0x1ff);
    return tile < attributes_.size() ? attributes_[tile] : 0;
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
