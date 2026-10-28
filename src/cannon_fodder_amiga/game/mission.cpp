#include "cannon_fodder_amiga/game/mission.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include <algorithm>

namespace fodder::game {
namespace {

int whole(std::int32_t v) { return std::int16_t(std::uint32_t(v) >> 16); }
void set_whole(std::int32_t& v, int w) {
    v = std::int32_t((std::uint32_t(std::uint16_t(w)) << 16) | (std::uint32_t(v) & 0xffff));
}

} // namespace

void start_mission(World& world, const MissionTables& tables, int phase) { // $9E0EC, $A4C4E
    world.phase = phase;
    world.objectives = {};
    for (const int code : tables.objectives(phase)) {
        if (code >= 1 && code <= 8) world.objectives[std::size_t(code - 1)] = true;
    }
    const auto range = tables.skill_range(phase);
    world.skill_min = range[0];
    world.skill_max = range[1];
    world.skill_next = (range[0] + range[1]) >> 1;
    world.skill_average = world.skill_next;
    world.skill_step = 1;
    for (int i = 0; i < 30; ++i) { // the placed enemies (kind 5 and $24)
        auto& o = world.objects.slots[std::size_t(i)];
        if (o.used && (o.kind == 5 || o.kind == 0x24)) world.walkers[std::size_t(i)].skill = next_enemy_skill(world);
    }
}

int next_enemy_skill(World& world) { // $9E150
    const int skill = world.skill_next;
    int step = world.skill_step, next = skill + step;
    if (step >= 0) {
        if (next >= world.skill_max) {
            next = world.skill_max;
            step = -step;
        }
    } else if (next < 0 || next <= world.skill_min) {
        next = world.skill_min;
        step = -step;
    }
    world.skill_step = step;
    world.skill_next = next;
    return skill;
}

void rebuild_squads(World& world) { // $9DC18
    for (auto& squad : world.squads) squad.members.clear();
    world.recruit_lists.fill(-1);
    int alive = 0;
    for (const int slot : world.roster_slots) {
        if (slot < 0) continue;
        auto& w = world.walkers[std::size_t(slot)];
        // Dying recruits leave the lists, except in a vehicle (where the
        // state is cleared) or protected by a power-up (+$75 bit 1).
        if (w.state != 0 && w.state < 0x32 && !(w.flags75 & 2)) {
            if (w.carried == 0) continue;
            w.state = 0;
        }
        const int squad = std::clamp(world.objects.slots[std::size_t(slot)].squad, 0, 2);
        auto& members = world.squads[std::size_t(squad)].members;
        if (members.size() < 8) {
            world.recruit_lists[std::size_t(squad * 9) + members.size()] = slot;
            members.push_back(slot);
        }
        ++alive;
    }
    if (alive == 0) world.outcome = World::Outcome::lost;
    // $9DCC8: a squad without soldiers joins nobody.
    for (auto& squad : world.squads) {
        if (squad.members.empty()) squad.merged_into = -1;
    }
}

void check_objectives(World& world) { // $9DD64
    int buildings = 0, enemies = 0;
    for (int i = 0; i < object_slots; ++i) {
        const auto& o = world.objects.slots[std::size_t(i)];
        if (!o.used || whole(o.x) < 0) continue;
        const int state = world.walkers[std::size_t(i)].state;
        switch (o.kind) {
        case 0x6c: case 0x6d: case 0x6e: case 0x58: case 0x64: // factory, computer and the like
            if (state != 7) ++buildings;
            break;
        case 0x19: case 0x14: // huts
            if (state != 5 && state != 7) ++buildings;
            break;
        case 5: case 0x24: case 0x28: case 0x2a: case 0x2b: case 0x2c: case 0x45: case 0x50:
        case 0x51: case 0x52: case 0x54: case 0x55: case 0x69: // enemies ($9DE20)
            ++enemies;
            break;
        default:
            break;
        }
    }
    world.buildings_left = buildings;
    const auto& want = world.objectives;
    if (want[1] && buildings != 0) return;               // destroy enemy buildings
    if (want[0] && enemies != 0) return;                 // kill all enemy
    if ((want[4] || want[2]) && world.objects.kind_6a_48 != 0) return; // kidnap the leader, rescue hostages
    if (want[7] && !world.civilian_home) return;         // get civilian home
    world.outcome = World::Outcome::won;
}

namespace {

void set_waypoint(Squad& squad, int index, int x, int y, bool last) {
    if (index < 0) return;
    if (int(squad.route.size()) <= index) squad.route.resize(std::size_t(index) + 1);
    squad.route[std::size_t(index)] = {x, y};
    if (last) squad.route.resize(std::size_t(index) + 1); // the original's -1 after it
}

} // namespace

void order_walk(World& world, int x, int y) { // $9EB36
    const int s = world.selected_squad;
    if (s < 0 || s > 2 || world.leader_slot < 0) return;
    if (world.walkers[std::size_t(world.leader_slot)].carried != 0) return;
    auto& squad = world.squads[std::size_t(s)];
    for (const int slot : world.roster_slots) {
        if (slot < 0 || world.objects.slots[std::size_t(slot)].squad != s) continue;
        auto& o = world.objects.slots[std::size_t(slot)];
        world.walkers[std::size_t(slot)].wait = 0;
        o.x &= ~0xffff; // the fractions cleared
        o.y &= ~0xffff;
    }
    // $A1136
    if (world.route_window[std::size_t(s)] != 0) { // a quick second click: one more waypoint
        set_waypoint(squad, squad.route_end, x, y, true);
        if (squad.route_end < 29) ++squad.route_end;
        world.route_window[std::size_t(s)] = 8;
    } else {
        const int members = int(squad.members.size());
        int at = members; // room for the soldiers' places before the target
        if (world.last_click[0] != 0) set_waypoint(squad, at++, world.last_click[0], world.last_click[1], false);
        set_waypoint(squad, at, x, y, true);
        world.last_click = {x, y};
        if (members == 0) return;
        squad.route_end = at + 1;
        for (const int slot : squad.members) {
            const auto& o = world.objects.slots[std::size_t(slot)];
            auto& w = world.walkers[std::size_t(slot)];
            if (w.entering != 0 || w.carried != 0) continue;
            w.stopped = 0;
            w.pause = 0;
            w.idle = 0;
            w.route_index = (at + 1) * 4;
            --at;
            set_waypoint(squad, at, std::int16_t(std::uint32_t(o.x) >> 16), std::int16_t(std::uint32_t(o.y) >> 16), false);
            w.waypoint_x = x;
            w.waypoint_y = y;
            x = std::int16_t(std::uint32_t(o.x) >> 16);
            y = std::int16_t(std::uint32_t(o.y) >> 16);
        }
        world.route_window[std::size_t(s)] = 8;
    }
    for (auto& other : world.squads) {
        if (other.merged_into == s) other.merged_into = -1;
    }
}

// $9F0E8: a click on one of the recruits: with no squad selected his is
// selected; one of another squad (the two together at most 8) is joined
// by the selected squad: that squad stops and the selected one walks to
// him. True when a recruit was clicked.
bool click_recruit(World& world, int x, int y) {
    for (const int slot : world.roster_slots) {
        if (slot < 0) continue;
        const auto& t = world.objects.slots[std::size_t(slot)];
        const int tx = whole(t.x) + 4, ty = whole(t.y) - t.height - 0xd;
        if (x < tx || x > tx + 6 || y < ty || y > ty + 0xd) continue;
        const int his = t.squad;
        if (his < 0 || his > 2) return true;
        if (world.selected_squad < 0) {
            world.selected_squad = his;
            choose_leader(world);
            return true;
        }
        if (his == world.selected_squad) return true;
        auto& mine = world.squads[std::size_t(world.selected_squad)];
        auto& theirs = world.squads[std::size_t(his)];
        if (mine.members.size() + theirs.members.size() > 8) return true;
        mine.merged_into = his;
        theirs.route.clear(); // $ABFB2: they stop
        for (const int m : theirs.members) {
            auto& w = world.walkers[std::size_t(m)];
            const auto& o = world.objects.slots[std::size_t(m)];
            w.route_index = 0;
            w.waypoint_x = whole(o.x);
            w.waypoint_y = whole(o.y);
        }
        return true;
    }
    return false;
}

void choose_leader(World& world) { // $9F9C6
    int chosen = -1;
    if (world.selected_squad >= 0 && world.selected_squad < 3) {
        const auto& members = world.squads[std::size_t(world.selected_squad)].members;
        for (std::size_t k = 0; k < members.size(); ++k) {
            const int slot = members[k];
            if (world.objects.slots[std::size_t(slot)].kind != 0) continue;
            auto& w = world.walkers[std::size_t(slot)];
            const bool well = (w.flags75 & 2) || w.state == 0 || w.state >= 0x32;
            if (!(well && w.entering == 0)) {
                if (w.carried == 0) continue; // dying or going in somewhere
                w.state = 0;
                w.entering = 0;
            }
            if (w.carried != 0 && k + 1 < members.size()) continue; // rather one on foot
            chosen = slot;
            break;
        }
        if (chosen < 0) world.selected_squad = -1;
    }
    world.leader_slot = chosen; // (not while driving a vehicle, $82798)
}

void tick_route_windows(World& world) { // $A1248
    for (auto& w : world.route_window) {
        if (w != 0) --w;
    }
}

int spawn_enemy(World& world, int from) { // $949F0
    if (world.outcome == World::Outcome::won || world.objects.enemies >= 10) return -1;
    const int slot = free_slot_below_30(world);
    if (slot < 0) return -1;
    const auto& hut = world.objects.slots[std::size_t(from)];
    auto& o = world.objects.slots[std::size_t(slot)];
    auto& w = world.walkers[std::size_t(slot)];
    o = GameObject{};
    w = Walker{};
    world.bullets[std::size_t(slot)] = BulletState{};
    world.attachments[std::size_t(slot)] = Attachment{};
    o.used = true;
    o.kind = 5;
    set_whole(o.x, whole(hut.x) - 6);
    set_whole(o.y, whole(hut.y) + 4);
    o.graphic = 0x7c;
    o.side = 1;
    // It first follows the squad's leader for 120-375 ticks, walking out
    // downwards (heading $1C0 +- 30) for 8-23 ticks.
    w.target = -(int(world.random() & 0xff) + 0x78);
    const auto r = world.random();
    int turn = int(r & 0x1e);
    if (r & 0x8000) turn = -turn;
    w.heading = 0x1c0 + turn;
    w.wait = int(r & 0xf) + 8;
    w.skill = next_enemy_skill(world);
    world.spawns = (world.spawns + 1) & 0xf;
    if (world.spawns == 0 && world.skill_max < 30) ++world.skill_max;
    ++world.objects.enemies;
    return slot;
}

namespace {

// $9487C: an end text below the view's centre, drawn last, no squad.
void place_end_text(World& world, int slot, int graphic, int kind, int dx, int dy) {
    auto& o = world.objects.slots[std::size_t(slot)];
    o = GameObject{};
    world.walkers[std::size_t(slot)] = Walker{};
    o.used = true;
    set_whole(o.x, world.snapshot.x + 0x54 + dx);
    set_whole(o.y, world.snapshot.y + 0xeb + dy);
    o.graphic = graphic;
    o.kind = kind;
    o.squad = -1;
    o.layer = 1;
}

} // namespace

bool run_mission_end(World& world, bool last_phase_of_mission, bool recruits_left, EndText* started) {
    if (started) *started = EndText::none;
    if (world.outcome == World::Outcome::playing) return false;
    if (world.end_count < 0) return true;
    if (world.end_count == 0) {
        world.end_count = world.failed_objective ? 50 : 100;
        free_object(world, 40);
        EndText text;
        if (world.outcome == World::Outcome::won && !world.failed_objective) {
            text = last_phase_of_mission ? EndText::mission_complete : EndText::phase_complete;
            if (last_phase_of_mission) place_end_text(world, 41, 0xa2, 0x1d, 0x12, -0x14); // $9480A
            else place_end_text(world, 41, 0xa1, 0x1e, 0x1b, -0x14);                       // $9483A
            place_end_text(world, 42, 0xa0, 0x1c, 0, 0);                                     // $9486A
        } else if (world.failed_objective || recruits_left) {                                // $94956
            text = EndText::try_again;
            place_end_text(world, 41, 0xca, 0x3a, 0x12, -0x14);
            place_end_text(world, 42, 0xcb, 0x3b, 0, 0);
        } else {                                                                             // $947C4
            text = EndText::game_over;
            place_end_text(world, 41, 0xc1, 0x22, 0, 0);
        }
        if (started) *started = text;
    }
    if (--world.end_count == 0) world.end_count = -1;
    return false;
}

void update_end_text(World& world, int slot) {
    auto& o = world.objects.slots[std::size_t(slot)];
    int column = 0, line = 0;
    switch (o.kind) {
    case 0x22: column = 0x6b; line = 0x87; break; // GAME OVER
    case 0x1c: column = 0x57; line = 0x87; break; // COMPLETE
    case 0x1d: column = 0x66; line = 0x73; break; // MISSION
    case 0x1e: column = 0x6f; line = 0x73; break; // PHASE
    case 0x3a: column = 0x81; line = 0x73; break; // TRY
    default:   column = 0x67; line = 0x85; break; // $3B: AGAIN
    }
    set_whole(o.x, world.snapshot.x + column);
    set_whole(o.y, whole(o.y) - 0x20);
    if (whole(o.y) <= world.snapshot.y + line) set_whole(o.y, world.snapshot.y + line);
}

} // namespace fodder::game
