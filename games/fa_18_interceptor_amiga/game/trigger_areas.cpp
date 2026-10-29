#include "fa_18_interceptor_amiga/game/trigger_areas.hpp"
#include <algorithm>

namespace interceptor::game {
namespace {

// An entry's words (as in world_objects.cpp).
constexpr std::uint32_t heads_for_word = 4, path_word = 6, entry_size = 10;

WorldObject* find(std::vector<WorldObject>& objects, int record) {
    const auto it = std::find_if(objects.begin(), objects.end(), [&](const WorldObject& o) { return o.record == record; });
    return it == objects.end() ? nullptr : &*it;
}

// A record's place as a point to head for (its +6, +8, +$C, +$E, +$10).
Waypoint place_of(const Aircraft& a) {
    return {std::int16_t(a.x >> 22), std::int16_t(a.z >> 22), std::int16_t((a.x & 0x3FFFFF) >> 8),
            std::int16_t((a.z & 0x3FFFFF) >> 8), a.y >> 8};
}

// Following the player or the area's point again, an aircraft on its way
// (behaviour 3 or 4) turns for home (5).
void homeward(Course& c) {
    if (c.behaviour == 3 || c.behaviour == 4) c.behaviour = 5;
}

} // namespace

void TriggerAreas::check(std::int16_t cell_x, std::int16_t cell_z, std::vector<WorldObject>& objects,
                         const Spawn& spawn) {
    // The walk's bit number is the register the spawner also leaves its
    // result in, so after an area is spawned the next areas are known by
    // other bits (game.md: the original's slip, kept).
    int index = 0;
    for (const auto& area : lists_.areas()) {
        const auto bit = std::uint8_t(1u << (index & 7));
        const bool in = area.contains(cell_x, cell_z);
        if (!(inside_ & bit)) {
            if (in) {
                inside_ |= bit;
                index = spawn(area, index);
            }
        } else if (in) {
            index = spawn(area, index);
        } else {
            inside_ &= std::uint8_t(~bit);
            leave(area, objects);
        }
        if (++index >= 8) break;
    }
}

// $11D1E6: the player left the area: its records that follow the player
// or have no record to follow head for their entry's point again.
void TriggerAreas::leave(const ObjectLists::TriggerArea& area, std::vector<WorldObject>& objects) const {
    const auto& m = lists_.program();
    for (int i = 0; i < area.count; ++i) {
        const std::uint32_t at = area.entries + std::uint32_t(i) * entry_size;
        const std::uint16_t heads_for = m.u16(at + heads_for_word);
        WorldObject* o = find(objects, heads_for & 0x7F);
        if (!o) continue;
        auto& c = o->course;
        auto& a = o->craft;
        if ((c.target_record & 0x80) && (c.target_record & 0x7F)) continue; // (following another record)
        const auto go_home = [&] {
            homeward(c);
            c.target = lists_.path(m.u16(at + path_word));
            c.target_record = 0x80;
            a.state &= ~0x0001;
        };
        if (!(heads_for & 0xFF00)) {
            go_home();
        } else if (heads_for & 0x8000) {
            if (!(heads_for & 0x7F00)) continue;
            c.target = lists_.path((heads_for & 0x7F00) >> 7);
            c.target_record = 0xFF;
            a.state &= ~0x0001;
            c.behaviour = 3;
        } else {
            const int r = heads_for >> 8;
            const WorldObject* other = find(objects, r);
            if (!other || !other->active()) {
                go_home();
                continue;
            }
            c.target = place_of(other->craft);
            c.target_record = std::uint8_t(r | 0x80);
            a.state &= ~0x0001;
            c.behaviour = 3;
        }
    }
}

void TriggerAreas::send_home(WorldObject& o) const {
    auto& a = o.craft;
    if ((a.type & 0xF0) != 0x10 || a.role == 8 || o.area <= 0) return; // (+$5D 0: message $1E, a stub)
    const auto& areas = lists_.areas();
    if (o.area > int(areas.size())) return;
    const auto& area = areas[std::size_t(o.area - 1)];
    if (area.contains(std::int16_t(a.x >> 22), std::int16_t(a.z >> 22))) return;
    const auto& m = lists_.program();
    for (int i = 0; i < area.count; ++i) {
        const std::uint32_t at = area.entries + std::uint32_t(i) * entry_size;
        if ((m.u16(at + heads_for_word) & 0x7F) != o.record) continue;
        homeward(o.course);
        a.state &= ~0x0001;
        o.course.target = lists_.path(m.u16(at + path_word));
        o.course.target_record = 0xFF;
        return;
    }
}

} // namespace interceptor::game
