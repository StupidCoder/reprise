#include "cannon_fodder_amiga/game/mission.hpp"
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
        // state is cleared). ((+$75 bit 1, open, also keeps them.)
        if (w.state != 0 && w.state < 0x32) {
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
    // (The original goes on with empty squads' merges and the selected
    // squad, $9DCC8…; not rebuilt yet.)
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

bool spawn_enemy(World& world, int from) { // $949F0
    if (world.outcome == World::Outcome::won || world.objects.enemies >= 10) return false;
    int slot = -1;
    for (int i = 29; i >= 0; --i) { // $9819A: the highest free slot below 30
        const auto& o = world.objects.slots[std::size_t(i)];
        if (!o.used || whole(o.x) < 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return false;
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
    return true;
}

} // namespace fodder::game
