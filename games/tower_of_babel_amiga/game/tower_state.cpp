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
