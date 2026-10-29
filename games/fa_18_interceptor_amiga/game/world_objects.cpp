#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include <algorithm>

namespace interceptor::game {
namespace {

constexpr std::uint32_t lists = 0x11DFB2;
constexpr std::uint32_t paths = 0x11DDC0;
constexpr std::uint32_t type_records = 0x115F4C; // 20 bytes each
constexpr std::uint32_t trigger_areas = 0x11DF00; // longs to the areas

// An entry's words.
namespace entry {
constexpr std::uint32_t type_record = 0, type = 2, heads_for = 4, path = 6, flags = 8, size = 10;
} // namespace entry
// Its flags word.
namespace flags {
constexpr std::uint16_t touchable = 0x8000, role_8 = 0x4000, bit_0 = 0x2000, scattered = 0x1000, mode = 0x0F00,
                        halve_scatter = 0x0080, level = 0x007F;
} // namespace flags

std::int32_t world(std::int16_t cell, std::int16_t fine) { return (std::int32_t(cell) << 22) + (std::int32_t(fine) << 8); }

} // namespace

ObjectLists::ObjectLists(const amiga::LoadedImage& program) : program_(program) {
    for (std::uint32_t at = trigger_areas; program_.s16(at) >= 0 && areas_.size() < 8; at += 4) {
        const std::uint32_t a = program_.u32(at);
        areas_.push_back({program_.s16(a), program_.s16(a + 2), program_.s16(a + 4), program_.s16(a + 6), a + 10,
                          program_.s16(a + 8)});
    }
}

int ObjectLists::spawn_area(const TriggerArea& area, int index, std::vector<WorldObject>& objects, int& aircraft,
                            int aircraft_limit, const ObjectChoice& choice, Spawned& out,
                            const FlightTables& tables) const {
    if (choice.mode <= 2 || choice.mode == 0x7D) return index;
    // The spawner keeps its result in the walk's register, which also
    // gives the next entry's area number (+$5D − 1).
    int result = -1;
    for (int i = 0; i < area.count; ++i) {
        const int area_number = i == 0 ? index : result;
        result = spawn_entry(area.entries + std::uint32_t(i) * entry::size, area_number, objects, aircraft,
                             aircraft_limit, choice, out, tables)
                     ? 0
                     : -1;
    }
    return result;
}

std::optional<int> ObjectLists::entry_place(std::uint32_t from, int record) const {
    for (int guard = 0; guard < 64 && from && program_.s16(from) >= 0; ++guard, from += entry::size) {
        if ((program_.u16(from + entry::heads_for) & 0xFF) == record) return program_.u16(from + entry::path);
    }
    return std::nullopt;
}

Waypoint ObjectLists::path(int offset) const {
    const std::uint32_t at = paths + std::uint32_t(program_.s16(paths + std::uint32_t(offset)));
    return {program_.s16(at), program_.s16(at + 2), program_.s16(at + 4), program_.s16(at + 6), program_.s16(at + 8)};
}

ObjectLists::Spawned ObjectLists::spawn(const ObjectChoice& choice, const FlightTables& tables) const {
    Spawned out;
    const int list_index = choice.mode == 0x7E || choice.mode == 0x7F ? 10 : choice.mode;
    if (list_index < 1 || list_index > 10) return out;
    // The variant: flown again, the one before; else the random's low
    // bits (0 and 1 both the first).
    int variant = 0;
    if (choice.again) {
        variant = *choice.again;
    } else if (choice.variant_random & 3) {
        variant = (choice.variant_random & 3) - 1;
    }
    out.variant = variant;
    out.offset_x = choice.offset_x;
    out.offset_z = choice.offset_z;
    const std::uint32_t head = lists + std::uint32_t((list_index - 1) * 12 + variant * 4);
    std::uint32_t list = lists + std::uint32_t(program_.s16(head));
    out.aircraft_limit = program_.s16(head + 2);
    // The stage's entries ($11CFBE: the demo's lists have one).
    const int stage = list_index == 10 ? 0 : choice.stage;
    list += std::uint32_t(program_.s16(list + std::uint32_t(2 * stage)));
    out.first_entry = list;
    int aircraft = 0;
    for (std::uint32_t at = list; program_.s16(at) >= 0; at += entry::size) {
        // The first entry always; the others up to the mission's level.
        if (at != list && (program_.u16(at + entry::flags) & flags::level) > choice.level) continue;
        // ($11D2E2: the area number −2, so +$5D −1.)
        WorldObject* spawned = spawn_entry(at, -2, out.objects, aircraft, out.aircraft_limit, choice, out, tables);
        if (!spawned) continue;
        // $11CFE4: turned towards where it heads for.
        auto& o = *spawned;
        const std::uint16_t heads_for = program_.u16(at + entry::heads_for);
        std::int32_t to_x = 0, to_z = 0;
        if (heads_for & 0x8000) {
            if (!(heads_for & 0x7F00)) continue;
            o.course.target = path((heads_for & 0x7F00) >> 7);
            o.course.target_record = 0xFF;
            to_x = world(o.course.target.cell_x, o.course.target.fine_x);
            to_z = world(o.course.target.cell_z, o.course.target.fine_z);
        } else {
            const int record = heads_for >> 8;
            const WorldObject* other = nullptr;
            for (const auto& p : out.objects) {
                if (p.record == record && p.active()) other = &p;
            }
            if (!other) continue;
            o.course.target = {std::int16_t(other->craft.x >> 22), std::int16_t(other->craft.z >> 22),
                        std::int16_t((other->craft.x & 0x3FFFFF) >> 8), std::int16_t((other->craft.z & 0x3FFFFF) >> 8),
                        other->craft.y >> 8};
            o.course.target_record = std::uint8_t(record | 0x80);
            to_x = other->craft.x;
            to_z = other->craft.z;
        }
        const auto direction = direction_of(tables, to_x - o.craft.x, 0, to_z - o.craft.z);
        o.craft.attitude = {0, direction.heading, 0};
        o.craft.body = body_matrix(tables, o.craft.attitude);
        o.craft.view = view_matrix(tables, o.craft.attitude);
    }
    out.aircraft = aircraft;
    return out;
}

WorldObject* ObjectLists::spawn_entry(std::uint32_t at, int area, std::vector<WorldObject>& objects, int& aircraft,
                                      int aircraft_limit, const ObjectChoice& choice, Spawned& out,
                                      const FlightTables& tables) const {
    const auto& m = program_;
    const std::uint16_t type_offset = m.u16(at + entry::type_record);
    const std::uint8_t type = std::uint8_t(m.u16(at + entry::type));
    const std::uint16_t heads_for = m.u16(at + entry::heads_for);
    const std::uint16_t f = m.u16(at + entry::flags);
    // Only in its mode; not into a record in use; not past the mission's
    // number of aircraft.
    if ((f & flags::mode) && int((f & flags::mode) >> 8) != choice.mode) return nullptr;
    const int record = heads_for & 0x7F;
    const auto last = std::find_if(objects.begin(), objects.end(), [&](const WorldObject& o) { return o.record == record; });
    if (last != objects.end() && last->active()) return nullptr;
    if (aircraft_limit <= aircraft) return nullptr;
    // A record that held a type $15 (the cruise missile) is not used again
    // while its cell x is not 0.
    if (last != objects.end() && last->craft.type == 0x15 && (last->craft.x >> 22) != 0) return nullptr;

    WorldObject o;
    o.record = record;
    o.shape_type = type_offset / 20;
    // The model's scale nibble (its first level's point block, +6).
    const std::uint32_t base = m.u32(type_records + type_offset + 4);
    std::uint16_t model = m.u16(base);
    if (!(model & 0x8000)) model = (model & 0x4000) ? m.u16(base + 2) : m.u16(base + 4);
    o.scale = m.u8(base + 6 + (model & 0xFFF)) & 15;
    auto& a = o.craft;
    a = Aircraft{};
    a.player = false;
    a.type = type;
    o.group = std::uint8_t(record);
    o.course.path = std::uint8_t((heads_for & 0x7F00) >> 8);
    Waypoint place = path(m.u16(at + entry::path));
    if (f & flags::touchable) {
        a.state |= 0x0008;
    } else if ((type & 0xF0) == 0x10) {
        ++aircraft;
    }
    if (f & flags::role_8) a.role = 8;
    if (f & flags::bit_0) a.state |= 0x0001;
    if (f & flags::scattered) {
        // Moved by up to three cells each way: by the random's bits, kept
        // for flying the mission again; halved for some.
        std::int16_t dx = 0, dz = 0;
        if (choice.again) {
            dx = choice.offset_x;
            dz = choice.offset_z;
        } else {
            const int r = choice.variant_random;
            dx = std::int16_t((r >> 1) & 3);
            if (r & 0x10) dx = std::int16_t(-dx);
            dz = std::int16_t((r >> 2) & 3);
            if (r & 0x20) dz = std::int16_t(-dz);
            out.offset_x = dx;
            out.offset_z = dz;
        }
        if (f & flags::halve_scatter) {
            dx = std::int16_t(dx >> 1);
            dz = std::int16_t(dz >> 1);
        }
        place.cell_x = std::int16_t(place.cell_x + dx);
        place.cell_z = std::int16_t(place.cell_z + dz);
    }
    o.area = std::int8_t(area + 1);
    o.course.behaviour = 3;
    if ((type & 0xF0) != 0x20) a.state |= 0x1080;
    // Where it heads for: a path's place, or (by record) its own place
    // until the other record's is taken over.
    if (heads_for & 0x8000) {
        if (heads_for & 0x7F00) o.course.target = path((heads_for & 0x7F00) >> 7);
        o.course.target_record = 0xFF;
    } else {
        o.course.target = place;
        o.course.target_record = std::uint8_t((heads_for >> 8) | 0x80);
    }
    a.x = world(place.cell_x, place.fine_x);
    a.z = world(place.cell_z, place.fine_z);
    a.y = place.height << 8;
    if (place.height != 0) {
        // In the air: the gear up, at speed.
        a.gear = 0xE0;
        a.speed = a.airspeed = 0x2000;
    }
    a.state = std::uint16_t((a.state | 0x0140) & 0x7FFF);
    a.fuel = 0x61A800;
    o.weapons = 0x3D;
    o.pilot = 0x10;
    a.attitude = {};
    a.body = body_matrix(tables, a.attitude);
    a.view = view_matrix(tables, a.attitude);
    // The records stay in record order.
    if (last != objects.end()) {
        *last = o;
        return &*last;
    }
    const auto at_record = std::find_if(objects.begin(), objects.end(), [&](const WorldObject& p) { return p.record > record; });
    return &*objects.insert(at_record, o);
}

} // namespace interceptor::game
