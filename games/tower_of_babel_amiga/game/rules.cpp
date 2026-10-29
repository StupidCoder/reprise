#include "tower_of_babel_amiga/game/rules.hpp"
#include <utility>

namespace babel::game {
namespace {

using namespace cell;

// (dx, dz) per frame for a walk in `direction`: 16 units along it.
std::pair<int, int> stride(int direction, int length) {
    int dx = 0, dz = length;
    for (int i = 0; i < direction; ++i) { // the original turns (0, 16) like its headings
        dx = -dx;
        std::swap(dx, dz);
    }
    return {dx, dz};
}

} // namespace

Rules::Rules(const data::Memory& program) : program_(program), animation_(program) {}

bool Rules::walk(TowerState& t, int slot, int direction) const {
    auto& o = t.objects[std::size_t(slot)];
    if (o.progress != 0) return false;
    if (direction >= 8) { // a lift ride from the cell it stands on
        const CellIndex here = o.cell;
        const std::uint8_t c = t.cell(here);
        if (!(c & solid) || !(c & lift)) return false; // (no floor: the original stops with error 8)
        if (direction < 10) { // up
            if (c & lift_open) return false;
            const CellIndex above = here + 100;
            o.target = above;
            t.set_cell(above, std::uint8_t(t.cell(above) | solid));
            t.set_object_at(above, slot);
        } else { // down
            if (!(c & lift_open)) return false;
            const CellIndex below = here - 100;
            if (below < 0) return false; // (the original stops with error 8)
            o.target = below;
            t.set_cell(here, std::uint8_t(c & ~opaque));
            t.set_cell(below, std::uint8_t(t.cell(below) & ~full_height));
            t.set_object_at(below, slot);
        }
        ++t.lift_moves;
    } else {
        if (direction >= 4) return false;
        const CellIndex next = o.cell + step_offset[std::size_t(direction)];
        const std::uint8_t c = t.cell(next);
        if (!(c & solid) || (c & full_height) || t.object_at(next) >= 0) return false;
    }
    o.state = direction;
    o.progress = move_frames;
    return true;
}

bool Rules::reverse(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    if (o.state >= 8) { // a lift ride turns back: the cells swap, the animation notes it
        std::swap(o.cell, o.target);
        t.turn_flags |= 1 << ((o.slot - 4) & 3);
    } else if (o.state >= 4) {
        return false;
    } else if (o.progress == 16 || o.progress == 10 || o.progress == 22) {
        return false; // not on the frames where cells change hands
    }
    o.state ^= 2;
    o.progress = move_frames - o.progress;
    return true;
}

bool Rules::back(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    if (o.progress != 0) return reverse(t, slot);
    const std::uint8_t c = t.cell(o.cell);
    if (c & lift) return walk(t, slot, (c & lift_open) ? 10 : 8);
    if (t.backwards_allowed) return walk(t, slot, o.heading ^ 2);
    return false;
}

void Rules::turn_left(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    o.heading = (o.heading - 1) & 3;
}

void Rules::turn_right(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    o.heading = (o.heading + 1) & 3;
}

namespace {

// Effect sound records in the program.
constexpr int sound_zap_hit = 0x2CFE + 0x806, sound_push = 0x2E6E + 0x806, sound_grab = 0x2EF8 + 0x806,
              sound_exchange = 0x2E9C + 0x806, sound_freeze = 0x2F54 + 0x806;

} // namespace

// The beams. Both run cell by cell from the spider in a direction, at most
// 16 cells, stopped by the tower's border (bit 0) and blocks (bit 3); they
// pass empty cells, the spider that fired (`shooter`, until a reflection
// turns the beam back), and particle clouds. A convertor (flag bit 4) in
// line with the beam turns a zap into a push and a push into a zap, which
// carries on from there with the cells it has left.
bool Rules::zap(TowerState& t, int direction, CellIndex at, int range, int shooter) const {
    int step = step_offset[std::size_t(direction &= 3)];
    for (;;) {
        if (--range < 0) return false;
        at += step;
        const std::uint8_t c = t.cell(at);
        if ((c & 0x01) || (c & full_height)) return false;
        const int n = t.object_at(at);
        if (n < 0) continue;
        auto& o = t.objects[std::size_t(n)];
        if (o.slot == shooter || o.type == 0x1A) continue;
        if (o.type == 0x3F) return false;
        if (o.progress != 0 && o.state >= 8 && o.state < 12) return false; // riding a lift
        if (o.cell != at) continue; // only moving into this cell
        const int low = o.flags & 0xFF, high = o.flags >> 8 & 0xFF;
        if (low & 0x01) { // destroyed
            const int type = o.type;
            // A Freezer at work: the tower runs again. (Also in its last 64
            // frames, when it already does: the original's freeze counter
            // then goes one too high, and the next Freezer stops nothing.)
            if (type == 0x13 && o.counter_1c != 0) {
                o.counter_1c = 0;
                ++t.clock;
            }
            t.explode(n);
            if (type == 0) {
                t.spider_lost();
            } else if (t.destroyed < t.destroy_needed) {
                ++t.destroyed;
                t.check_complete();
            }
            t.sounds.push_back(sound_zap_hit);
            return true;
        }
        if (low & 0x02) { // reflected: back the way it came, and it may hit the shooter now
            direction ^= 2;
            step = -step;
            shooter = -1;
            continue;
        }
        if (low & 0x20) { // a prism turns the beam a quarter, from two sides only
            if (o.heading == direction) direction -= 1;
            else if (((o.heading + 1) & 3) == direction) direction += 1;
            else return false;
            shooter = o.slot;
            direction &= 3;
            step = step_offset[std::size_t(direction)];
            continue;
        }
        if (low & 0x10) { // a convertor in line: the beam pushes from here
            if ((o.heading ^ direction) & 1) return false;
            return push(t, direction, at, range, shooter);
        }
        if (high & 0x08) { // reverses its move (not on its first, last or middle frames)
            const int p = o.progress;
            if (p == 0 || p == 2 || p == 30 || p == 16) return false;
            o.state ^= 2;
            o.progress = move_frames - p;
            return true;
        }
        return false;
    }
}

bool Rules::push(TowerState& t, int direction, CellIndex at, int range, int shooter) const {
    const int step = step_offset[std::size_t(direction &= 3)];
    for (;;) {
        if (--range < 0) return false;
        at += step;
        const std::uint8_t c = t.cell(at);
        if ((c & 0x01) || (c & full_height)) return false;
        const int n = t.object_at(at);
        if (n < 0) continue;
        auto& o = t.objects[std::size_t(n)];
        if (o.slot == shooter || o.type == 0x1A) continue;
        if (o.type == 0x3F) return false;
        const int low = o.flags & 0xFF;
        if (low & 0x04) { // pushable: one cell on, if that cell is free floor
            const CellIndex next = at + step;
            const std::uint8_t d = t.cell(next);
            if (!(d & solid) || (d & 0x01) || (d & full_height) || t.object_at(next) >= 0 || o.progress != 0) return false;
            o.progress = 16;
            o.state = direction + 4;
            o.target = o.cell + step;
            t.set_object_at(o.target, n);
            t.sounds.push_back(sound_push);
            return true;
        }
        if (low & 0x10) { // a convertor in line: the beam zaps from here
            if ((o.heading ^ direction) & 1) return false;
            return zap(t, direction, at, range, shooter);
        }
        return false;
    }
}

// $B90E: the Grabber's beam has no range limit and no shooter to skip.
bool Rules::grab(TowerState& t, int slot) const {
    auto& me = t.objects[std::size_t(slot)];
    const int step = step_offset[std::size_t(me.heading & 3)];
    for (CellIndex at = me.cell + step;; at += step) {
        const std::uint8_t c = t.cell(at);
        if ((c & 0x01) || (c & full_height)) return false;
        const int n = t.object_at(at);
        if (n < 0) continue;
        auto& o = t.objects[std::size_t(n)];
        if (o.type == 0x1A) continue;
        if (o.type == 0x3F) return false;
        const int low = o.flags & 0xFF;
        if (low & 0x08) { // a Klondike: taken (its animation then removes it)
            o.flags |= 0x100;
            if (t.klondikes < t.klondikes_needed) {
                ++t.klondikes;
                t.check_complete();
            }
            t.sounds.push_back(sound_grab);
            return true;
        }
        if (low & 0x40) return o.type == 0x11 ? raise_lifts(t) : lower_lifts(t); // the forcers
        if (!(low & 0x80)) return false;
        if (o.type == 0x13) { // Freezer: the tower stops for 128 frames
            if (o.counter_1c != 0) return false;
            o.counter_1c = 0x80;
            if (--t.clock < 0) t.sounds.push_back(sound_freeze);
            return true;
        }
        if (o.type == 0x14) { // Wiper: starts its 63 frames
            if (o.progress != 0 || o.counter_1c != 0) return false;
            o.counter_1c = 0x3F;
            return true;
        }
        if (o.type != 0x15) return false;
        // Exchanger: the Grabber takes its place (and turns round), it goes.
        if (me.target != 0) { t.set_object_at(me.target, -1); me.target = 0; }
        if (o.target != 0) { t.set_object_at(o.target, -1); o.target = 0; }
        const int mine = t.object_at(me.cell), theirs = t.object_at(o.cell);
        t.set_object_at(me.cell, theirs);
        t.set_object_at(o.cell, mine);
        std::swap(me.cell, o.cell);
        std::swap(me.x, o.x);
        std::swap(me.y, o.y);
        std::swap(me.z, o.z);
        me.heading ^= 2;
        me.progress = 0;
        t.explode(n);
        if (t.destroyed < t.destroy_needed) {
            ++t.destroyed;
            t.check_complete();
        }
        t.sounds.push_back(sound_exchange);
        return true;
    }
}

// $BAAA: Forcer (Up): every lift down on floors 0-2 that is free starts up
// (at most 20), carried by the platform objects 25-27 (one per floor).
bool Rules::raise_lifts(TowerState& t) const {
    if (t.forcer_frames != 0) return false;
    t.objects[25].y = 0; t.objects[26].y = 0x200; t.objects[27].y = 0x400;
    t.forcer_cells.clear();
    for (int floor = 0; floor < 3 && t.forcer_cells.size() < 20; ++floor) {
        for (int k = 0; k < 100 && t.forcer_cells.size() < 20; ++k) {
            const CellIndex c = (floor + 1) * 100 + k;
            const std::uint8_t v = t.cell(c);
            if (!(v & lift) || (v & lift_open) || (v & full_height) || t.object_at(c) >= 0) continue;
            t.set_object_at(c, 25 + floor);
            t.set_object_at(c + 100, 25 + floor);
            t.set_cell(c + 100, std::uint8_t(t.cell(c + 100) | solid));
            t.forcer_cells.push_back(c);
        }
    }
    t.forcer_frames = 0x20;
    t.forcer_direction = 1;
    return true;
}

// $BB42: Forcer (Down): every lift up on floors 3-1 that is free starts down.
bool Rules::lower_lifts(TowerState& t) const {
    if (t.forcer_frames != 0) return false;
    t.objects[25].y = 0x200; t.objects[26].y = 0x400; t.objects[27].y = 0x600;
    t.forcer_cells.clear();
    for (int floor = 3; floor > 0 && t.forcer_cells.size() < 20; --floor) {
        for (int k = 99; k >= 0 && t.forcer_cells.size() < 20; --k) {
            const CellIndex c = (floor + 1) * 100 + k;
            const std::uint8_t v = t.cell(c);
            if (!(v & lift) || !(v & lift_open) || !(t.cell(c - 100) & full_height) || t.object_at(c) >= 0) continue;
            t.set_object_at(c, 25 + floor - 1);
            t.set_object_at(c - 100, 25 + floor - 1);
            t.set_cell(c, std::uint8_t(v & ~opaque));
            t.set_cell(c - 100, std::uint8_t(t.cell(c - 100) & ~full_height));
            t.forcer_cells.push_back(c);
        }
    }
    t.forcer_frames = 0x20;
    t.forcer_direction = -1;
    return true;
}

// $B66E: what fire does depends on the spider.
bool Rules::fire(TowerState& t, int slot) const {
    const auto& me = t.objects[std::size_t(slot)];
    if (slot == 4) return zap(t, me.heading, me.cell, 16, me.slot);
    if (slot == 5) return push(t, me.heading, me.cell, 16, me.slot);
    return grab(t, slot);
}

void Rules::decide(TowerState& t, int slot) const {
    auto& p = t.programs[std::size_t(slot)];
    if (p.mode != 1) return; // only a running program ($CB6C tests unsigned < 2, not 0)
    const bool shown = slot == t.active;
    const int position = ++p.position;
    if (position < 0 || position >= 8) { // past the end: the program stops
        p.mode = -1;
        if (shown) {
            t.panel.program = t.panel.move = t.panel.turn = -1;
            t.panel.program_record = slot;
        }
        return;
    }
    switch (p.commands[std::size_t(position)]) {
    case 0: // an empty step
        p.command = -1;
        if (shown) t.panel.move = t.panel.turn = -1;
        break;
    case 1:
        if (!walk(t, slot, t.objects[std::size_t(slot)].heading)) break;
        p.command = 0;
        if (shown) { t.panel.move = 0; t.panel.program_record = slot; }
        break;
    case 2:
        if (!back(t, slot)) break;
        p.command = 1;
        if (shown) { t.panel.move = 0; t.panel.program_record = slot; }
        break;
    case 3:
        turn_left(t, slot);
        if (shown) { t.panel.turn = 0; t.panel.program_record = slot; }
        break;
    case 4:
        turn_right(t, slot);
        if (shown) { t.panel.turn = 1; t.panel.program_record = slot; }
        break;
    case 5:
        if (!fire(t, slot)) break;
        if (shown) { t.panel.turn = 2; t.panel.program_record = slot; }
        break;
    default:
        break;
    }
}

void Rules::advance(TowerState& t, int slot) const {
    auto& o = t.objects[std::size_t(slot)];
    const int s = o.state;
    if (s < 4) { // walking
        const auto [dx, dz] = stride(s, 16);
        o.x = std::int16_t(o.x + dx);
        o.z = std::int16_t(o.z + dz);
        if (--o.progress == 0) return;
        if (o.progress == 16) { std::swap(o.cell, o.target); return; }
        const int claim = claim_point(o.type);
        if (o.progress == claim) { // the old cell is free again
            t.set_object_at(o.target, -1);
            o.target = 0;
            return;
        }
        if (o.progress != move_frames - claim) return;
        const CellIndex next = o.cell + step_offset[std::size_t(s)];
        if (t.cell(next) != 0 && t.object_at(next) < 0) { // claim the cell ahead
            t.set_object_at(next, slot);
            o.target = next;
        } else { // taken or gone: back to where it came from
            o.state = s ^ 2;
            o.progress = move_frames - o.progress;
        }
    } else if (s < 8) { // pushed: steps from the table
        const auto [dx, dz] = stride(s - 4, pushed_step(o.progress));
        o.x = std::int16_t(o.x + dx);
        o.z = std::int16_t(o.z + dz);
        if (--o.progress == 0) {
            t.set_object_at(o.target, -1);
            o.target = 0;
        } else if (o.progress == 8) {
            std::swap(o.cell, o.target);
        }
    } else if (s < 12) { // riding a lift
        const int dy = lift_step(o.progress);
        o.y = std::int16_t(o.y + ((s & 2) ? -dy : dy));
        if (--o.progress != 0) return;
        const CellIndex from = o.cell, to = o.target;
        if (s < 10) {
            t.set_cell(from, std::uint8_t(t.cell(from) | full_height));
            t.set_cell(to, std::uint8_t(t.cell(to) | opaque));
        } else {
            t.set_cell(from, std::uint8_t(t.cell(from) & ~solid));
        }
        t.set_object_at(from, -1);
        o.cell = to;
        o.target = 0;
        --t.lift_moves;
    } else {
        --o.progress;
    }
}

void Rules::step_forcers(TowerState& t) const {
    if (t.forcer_frames == 0) return;
    const int dy = lift_step(t.forcer_frames);
    for (int slot = 25; slot < 28; ++slot) { // the three objects the forcers carry ($18FA)
        auto& o = t.objects[std::size_t(slot)];
        o.y = std::int16_t(t.forcer_direction >= 0 ? o.y + dy : o.y - dy);
    }
    if (--t.forcer_frames != 0) return;
    for (const CellIndex c : t.forcer_cells) {
        if (t.forcer_direction >= 0) {
            t.set_cell(c, std::uint8_t(t.cell(c) | full_height));
            t.set_cell(c + 100, std::uint8_t(t.cell(c + 100) | opaque));
            t.set_object_at(c, -1);
            t.set_object_at(c + 100, -1);
        } else {
            t.set_cell(c, std::uint8_t(t.cell(c) & ~solid));
            t.set_object_at(c, -1);
            t.set_object_at(c - 100, -1);
        }
    }
    t.forcer_cells.clear();
}

void Rules::step(TowerState& t) const {
    for (int slot = 4; slot < 7; ++slot) { // the spiders: decide when standing, then move
        auto& o = t.objects[std::size_t(slot)];
        if (o.type >= 0x1A) continue;
        if (o.progress == 0) decide(t, slot);
        if (o.progress != 0) advance(t, slot);
    }
    if (!t.running()) return;
    for (int slot = 7; slot < TowerState::objects_max; ++slot) { // the others only move here
        const auto& o = t.objects[std::size_t(slot)];
        if ((o.type >> 8 & 0xFF) >= 0x80) break; // the list ends at a negative type (its high byte)
        if (o.progress != 0) advance(t, slot);
    }
    step_forcers(t);
}

} // namespace babel::game
