#include "tower_of_babel_amiga/game/tower_state.hpp"
#include "tower_of_babel_amiga/game/tower_map.hpp"

namespace babel::game {

// The original's layout puts the object map where floor 4 would be, so cell
// reads above floor 3 see object slots ($FF = none).
std::uint8_t TowerState::cell(CellIndex i) const {
    if (i < 0) return 0;
    if (i < 500) return cells_[std::size_t(i)];
    return std::uint8_t(object_at(i - 400));
}

void TowerState::set_cell(CellIndex i, std::uint8_t value) {
    if (i >= 0 && i < 500) cells_[std::size_t(i)] = value;
    else if (i >= 500) set_object_at(i - 400, std::int8_t(value));
}

int TowerState::object_at(CellIndex i) const {
    if (i < 100 || i >= 500) return -1;
    return objects_at_[std::size_t(i)];
}

void TowerState::set_object_at(CellIndex i, int slot) {
    if (i >= 100 && i < 500) objects_at_[std::size_t(i)] = std::int8_t(slot);
}

void TowerState::explode(int slot) {
    auto& o = objects[std::size_t(slot)];
    o.type = 0x1A;
    o.flags = 0;
    o.progress = 0;
    o.counter_1a = 0x20;
    if (o.target != 0) {
        set_object_at(o.target, -1);
        o.target = 0;
    }
}

void TowerState::check_complete() {
    if (klondikes == klondikes_needed && destroyed == destroy_needed && end_countdown == 0) {
        end_countdown = blink + 0x15;
        end_reason = 1;
    }
}

// Without the Grabber the Klondikes cannot all be collected; without all
// three spiders nothing can be done. (Called after any explosion near a
// mine or time bomb, whether a spider went or not.)
void TowerState::spider_lost() {
    if (objects[6].type == 0) return; // the Grabber still works
    if (klondikes < klondikes_needed) {
        if (end_countdown == 0) { end_countdown = blink + 0x15; end_reason = 4; }
    } else if (objects[4].type != 0 && objects[5].type != 0 && end_countdown == 0) {
        end_countdown = blink + 0x15;
        end_reason = 3;
    }
}

// A floor cell goes, unless it is a lift, something stands on it or a
// block below carries it. Its light (bit 2) passes to the next cell below
// that has anything, or to the ground.
void TowerState::remove_floor(CellIndex i) {
    using namespace cell;
    const std::uint8_t c = this->cell(i);
    if (!(c & solid) || (c & lift) || object_at(i) >= 0) return;
    if (i >= 200 && (this->cell(i - 100) & full_height)) return;
    set_cell(i, 0);
    if (!(c & lit)) return;
    for (CellIndex below = i - 100;; below -= 100) {
        if (below < 100) { set_cell(below, lit); return; } // the ground takes it as it is
        if (this->cell(below) != 0) { set_cell(below, std::uint8_t(this->cell(below) | lit)); return; }
    }
}

} // namespace babel::game

namespace babel::game {

TowerState TowerState::unpack(const data::Tower& tower, const data::Memory& program) {
    constexpr std::uint32_t kind_table = 0x13742; // 10 bytes a kind: type, shape (from $18200), heading, flags
    constexpr std::uint32_t shapes_base = 0x18200;
    TowerState t;
    // Cells: a border of 1s round each floor ($133A0); the ground row only
    // keeps the lit flag; the object map starts empty.
    const TowerMap map(tower, program);
    t.cells_.fill(0);
    t.objects_at_.fill(-1);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) t.set_cell(cell_index(-1, x, z), map.ground_lit(x, z) ? cell::lit : 0);
    for (int f = 0; f < 4; ++f) {
        for (int i = 0; i < 100; ++i) t.set_cell((f + 1) * 100 + i, 1);
        for (int z = 0; z < 8; ++z)
            for (int x = 0; x < 8; ++x) t.set_cell(cell_index(f, x, z), map.at(f, x, z));
    }
    // The cameras (slots 0-3): on each side of the tower facing it ($13692).
    // Their +$14 / +$16 are the panel's camera offsets, not cells.
    for (int n = 0; n < 4; ++n) {
        static constexpr int xs[4] = {0, -0xC40, 0, 0xC40}, zs[4] = {-0xC40, 0, 0xC40, 0};
        auto& c = t.objects[std::size_t(n)];
        c = {};
        c.type = 0;
        c.x = xs[n];
        c.y = 0x100;
        c.z = zs[n];
        c.heading = n;
        c.cell = 0;
        c.target = -0xC40;
        c.slot = n;
    }
    // The spiders' slots stay unused ($3F) unless the tower places them.
    for (int n = 4; n < 7; ++n) t.objects[std::size_t(n)].type = 0x3F;
    int next = 7;
    for (const auto& placed : tower.objects) {
        const int slot = placed.kind < 7 ? placed.kind : next++;
        if (slot >= objects_max) break;
        const std::uint32_t entry = kind_table + std::uint32_t(placed.kind) * 10;
        auto& o = t.objects[std::size_t(slot)];
        o = {};
        o.type = program.u16(entry);
        o.shape = shapes_base + program.u32(entry + 2);
        o.x = placed.x * 0x200 - 0x700;
        o.y = placed.floor * 0x200;
        o.z = placed.z * 0x200 - 0x700;
        o.heading = o.state = std::int16_t(program.u16(entry + 6));
        o.flags = program.u16(entry + 8);
        o.cell = cell_index(placed.floor, placed.x, placed.z);
        o.slot = slot;
        t.set_object_at(o.cell, slot);
    }
    if (next < objects_max) t.objects[std::size_t(next)].type = -1;

    t.klondikes_needed = tower.klondikes;
    t.destroy_needed = tower.objects_to_destroy;
    t.bomb_speed = tower.settings[0];
    t.mine_holes = !tower.flags[8];
    t.footsteps = tower.settings[10] == 0;
    // $13568: the time limit as four BCD digits (no limit: 0).
    if (tower.has_time_limit()) {
        const int s = tower.time_limit;
        t.time_left = (s / 1000) << 12 | (s % 1000 / 100) << 8 | (s % 100 / 10) << 4 | s % 10;
    }
    t.clock = -1;
    t.started = false;
    // $1362A: the first spider the tower has; without any, camera 0 unless
    // the tower forbids cameras (flag 4).
    t.active = tower.flags[4] ? 4 : 0;
    for (int spider = 0; spider < 3; ++spider)
        if (tower.spider_available(spider)) {
            t.active = 4 + spider;
            break;
        }
    for (auto& p : t.programs) p = Program{-1, -1, -1, 0, {}};
    t.panel = PanelLights{};
    return t;
}

TowerMap TowerState::map() const {
    TowerMap m;
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) {
            m.set_ground_lit(x, z, (cell(cell_index(-1, x, z)) & cell::lit) != 0);
            for (int f = 0; f < 4; ++f) m.set(f, x, z, cell(cell_index(f, x, z)));
        }
    return m;
}

} // namespace babel::game
