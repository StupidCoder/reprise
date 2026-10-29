#include "tower_of_babel_amiga/game/rules.hpp"

// What each kind of object does on its own, frame by frame, after the moves
// ($BEE4 runs a handler per type from slot 4 until a negative type). Most
// of them only act while the tower runs (not before the start, nor while a
// Freezer holds it); Klondikes, Bonus Flags, Convertors, Freezers and
// particle clouds also animate when it is stopped.
namespace babel::game {
namespace {

using namespace cell;

// Effect sound records in the program.
constexpr int sound_blast = 0x2D2C + 0x806, sound_hop = 0x3068 + 0x806, sound_floor_gone = 0x300C + 0x806,
              sound_convertor = 0x31D8 + 0x806, sound_thaw = 0x2FB0 + 0x806,
              sound_step = 0x2E12 + 0x806, sound_step_lift = 0x2E40 + 0x806;

AnimationState animation_state(const TowerObject& o) {
    return {o.type, o.heading, o.state, o.progress, o.counter_1a, o.counter_1c, o.counter_1e};
}

constexpr std::uint32_t hopper_standing = 0x10200 + 0x4351A;

} // namespace

void Rules::show(TowerObject& o, const TowerObject& before, const TowerState& t) const {
    const AnimationClock clock{t.frame, t.running(), t.forcer_frames, t.forcer_direction};
    const int spider = int(&o - t.objects.data()) - 4;
    if (const auto shape = animation_.shape(animation_state(o), animation_state(before), clock, spider)) o.shape = *shape;
}

bool Rules::free_ahead(const TowerState& t, int slot, int direction) const {
    const CellIndex next = t.objects[std::size_t(slot)].cell + step_offset[std::size_t(direction & 3)];
    const std::uint8_t c = t.cell(next);
    return (c & solid) && !(c & full_height) && t.object_at(next) < 0;
}

void Rules::update(TowerState& t) const {
    for (int slot = 4; slot < TowerState::objects_max; ++slot) {
        auto& o = t.objects[std::size_t(slot)];
        if (o.type < 0) break;
        const TowerObject before = o;
        switch (o.type) {
        case 0x00: spider(t, slot); break;
        case 0x01: // Zapper: fires ahead every frame
            if (!t.running()) break;
            show(o, before, t);
            zap(t, o.heading, o.cell, 16, o.slot);
            break;
        case 0x02: // Pusher
            if (!t.running()) break;
            show(o, before, t);
            push(t, o.heading, o.cell, 16, o.slot);
            break;
        case 0x03: { // Rotating Zapper: fires each time it faces a compass direction
            if (!t.running()) break;
            const int turn = animation_.rotation(t.frame);
            o.counter_1e = turn;
            show(o, before, t);
            if ((turn & 7) == 0) zap(t, turn >> 3, o.cell, 16, o.slot);
            break;
        }
        case 0x04: // Rotating Pusher: pushes every 16 frames, a quarter turn on each time
            if (!t.running()) break;
            show(o, before, t);
            if ((t.frame & 15) == 0) push(t, (t.frame & 0xFFFF) >> 4, o.cell, 16, o.slot);
            break;
        case 0x05: // Worm: to and fro
            if (!t.running() || o.state >= 4) break;
            show(o, before, t);
            if (o.progress == 0 && !walk(t, slot, o.state)) o.state ^= 2;
            break;
        case 0x06: watcher(t, slot); break;
        case 0x07: hopper(t, slot); break;
        case 0x08: { // Proximity Mine: goes off when something next to it moves
            if (!t.running()) break;
            bool triggered = false;
            if (o.progress == 0) {
                for (int d = 3; d >= 0 && !triggered; --d) {
                    const int n = t.object_at(o.cell + step_offset[std::size_t(d)]);
                    if (n < 0) continue;
                    const auto& near = t.objects[std::size_t(n)];
                    triggered = near.progress != 0 && near.state < 8; // walking or pushed, not riding
                }
            }
            if (triggered) blast(t, slot, t.mine_holes);
            else show(o, before, t);
            break;
        }
        case 0x09: // Time Bomb: counts to 800
            if (!t.running()) break;
            o.counter_1c += t.bomb_speed + 1;
            if (o.counter_1c >= 0x320) blast(t, slot, false);
            else show(o, before, t);
            break;
        case 0x0D: lizard(t, slot); break;
        case 0x0E: // Klondike: once taken, 7 frames of vanishing
            if (o.counter_1a != 0) {
                show(o, before, t);
                if (--o.counter_1a == 0) {
                    o.type = 0x3F;
                    t.set_object_at(o.cell, -1);
                    o.cell = 0;
                }
            } else if (o.flags & 0x100) {
                o.flags &= ~0x100;
                o.counter_1a = 7;
            }
            break;
        case 0x10: // Convertor: hums every 32 frames
            if ((t.frame & 31) == 1) t.sounds.push_back(sound_convertor);
            show(o, before, t);
            break;
        case 0x13: // Freezer: after 64 of its 128 frames the tower runs again
            if (o.counter_1c == 0) break;
            if (--o.counter_1c == 0x40) {
                ++t.clock;
                t.sounds.push_back(sound_thaw);
            }
            show(o, before, t);
            break;
        case 0x14: wiper(t, slot); break;
        case 0x1A: // particle cloud: gone after 32 frames, taking the floor if it was a mine's
            if (--o.counter_1a != 0) break;
            {
                const CellIndex at = o.cell;
                o.type = 0x3F;
                t.set_object_at(at, -1);
                o.cell = 0;
                if (o.flags & 0x400) {
                    o.flags &= ~0x400;
                    t.remove_floor(at);
                }
            }
            break;
        case 0x0F: case 0x11: case 0x12: // Bonus Flag, Forcers: frames only
            show(o, before, t);
            break;
        default: // Blocks, Reflectors, Prisms, Exchangers: nothing
            break;
        }
    }
}

void Rules::spider(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    const TowerObject before = o;
    AnimationState now = animation_state(o);
    const int bit = 1 << ((slot - 4) & 3);
    if (o.progress != 0 && o.state >= 8 && o.state < 12 && (t.turn_flags & bit)) {
        t.turn_flags &= ~bit;
        now.ride_reversed = true;
    }
    const AnimationClock clock{t.frame, t.running(), t.forcer_frames, t.forcer_direction};
    if (const auto shape = animation_.shape(now, animation_state(before), clock, slot - 4)) o.shape = *shape;
    if (o.progress != 0 && o.state < 4 && (o.progress & 3) == 1 && t.footsteps)
        t.sounds.push_back((t.cell(o.cell) & lift) ? sound_step_lift : sound_step);
}

// The Watcher keeps a wall on its left: it turns left where it can, else
// goes on, else turns right, else round; a turn takes 4 frames (round 8).
void Rules::watcher(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    if (!t.running() || o.state >= 4) return;
    if (o.progress != 0) {
        show(o, o, t);
        return;
    }
    if (o.counter_1c != 0) { // turning
        --o.counter_1c;
        o.shape = animation_.watcher_turn(o.state, o.counter_1e < 0 ? -o.counter_1c : o.counter_1c);
        if (o.counter_1c != 0) return;
    } else if (free_ahead(t, slot, o.state - 1)) {
        o.state = (o.state - 1) & 3;
        o.counter_1c = 4;
        o.counter_1e = 0;
        return;
    }
    if (walk(t, slot, o.state)) return;
    const auto turn = [&](int direction, int frames, int sign) {
        if (!free_ahead(t, slot, direction)) return false;
        o.state = direction & 3;
        o.counter_1c = frames;
        o.counter_1e = sign;
        return true;
    };
    if (turn(o.state + 1, 4, -1) || turn(o.state + 2, 8, 0)) return;
    o.shape = animation_.watcher_idle(o.state, t.frame);
}

// The Hopper prefers to turn right, then straight on, left, back; boxed in,
// it hops on the spot.
void Rules::hopper(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    if (!t.running()) return;
    if (o.progress != 0) {
        show(o, o, t);
        if ((o.progress & 15) == 1) t.sounds.push_back(sound_hop);
        return;
    }
    o.shape = hopper_standing;
    o.state &= 3;
    const int ahead = o.state;
    for (const int turn : {1, 0, -1, -2})
        if (walk(t, slot, (ahead + turn) & 3)) return;
    o.state |= 0x10;
    o.progress = 16;
}

// The Lizard walks to and fro; where it turned, the floor goes once it has
// left that cell.
void Rules::lizard(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    if (!t.running() || o.state >= 4) return;
    show(o, o, t);
    if (o.progress != 0) return;
    if (o.counter_1c != 0 && t.object_at(o.counter_1c) < 0) {
        const CellIndex turned_at = o.counter_1c;
        o.counter_1c = 0;
        t.remove_floor(turned_at);
        t.sounds.push_back(sound_floor_gone);
    }
    if (!walk(t, slot, o.heading)) {
        o.heading ^= 2;
        o.counter_1c = o.cell;
    }
}

// Started by the Grabber (63 frames): at counts 48, 32, 16 and 0 it sweeps
// west, south, east, north, taking the floor up to the border or a block.
void Rules::wiper(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    if (!t.running() || o.counter_1c == 0) return;
    const TowerObject before = o;
    const int count = --o.counter_1c;
    if ((count & 0x30) == count) {
        const int step = step_offset[std::size_t((count >> 4) & 3)];
        for (CellIndex at = o.cell + step; !(t.cell(at) & 0x01) && !(t.cell(at) & full_height); at += step)
            t.remove_floor(at);
        t.sounds.push_back(sound_floor_gone);
    }
    show(o, before, t);
}

// A mine or time bomb goes off: it and everything next to it (except what
// is riding a lift) become particle clouds. Nothing destroyed this way
// counts towards the objective.
void Rules::blast(TowerState& t, int slot, bool hole) const {
    auto& o = t.objects[std::size_t(slot)];
    for (int d = 3; d >= 0; --d) {
        const int n = t.object_at(o.cell + step_offset[std::size_t(d)]);
        if (n < 0) continue;
        const auto& near = t.objects[std::size_t(n)];
        if (near.progress != 0 && near.state >= 8 && near.state < 12) continue;
        t.explode(n);
    }
    t.explode(slot);
    if (hole) o.flags = 0x400; // the cloud takes the floor with it when it goes
    t.spider_lost();
    t.sounds.push_back(sound_blast);
}

} // namespace babel::game
