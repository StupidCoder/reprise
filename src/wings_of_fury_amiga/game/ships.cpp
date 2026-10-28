#include "wings_of_fury_amiga/game/ships.hpp"
#include "wings_of_fury_amiga/game/flight.hpp"
#include <algorithm>

namespace wings::game {
namespace {

constexpr std::uint32_t a4 = 0x11AF52;
constexpr int rows = 16;

std::string string_at(const amiga::LoadedImage& wings, std::uint32_t at) {
    std::string s;
    for (char c; (c = char(wings.u8(at))) != 0; ++at) s += c;
    return s;
}

} // namespace

ShipTables read_ship_tables(const amiga::LoadedImage& wings) {
    ShipTables t;
    // The scan's set-up ($102E8E.. $102FC8), $103252's libraries and gun
    // tables, and $10252C's plane lists, per kind.
    struct Source {
        int marker, slot, guns, deck, value, hits, sink_timer, first_from, span, overlay;
        std::uint32_t gun_table, counts, plane_template;
    };
    static constexpr Source sources[4] = {
        {0xE4, 0, 8, 0x1B, 0x9C4, 1, 0x14, -0x20, 0xA0, 0xD0, 0x59FC, 0x7ACE, 0x7A56},   // destroyer
        {0x10D, 1, 0xE, 0x1B, 0x1194, 2, -1, -0x20, 0xC0, 0xF8, 0x59EC, 0x7AB0, 0x7A32}, // battleship
        {0xCC, 2, 4, 0x1C, 0x3E8, 1, 0x14, -0x10, 0x20, 0xB8, 0x59B2, 0x7A92, 0x7A06},   // cruise ship
        {0xF2, 3, 0xF, 0x15, 0x1770, 3, 0x14, -0x20, 0x9C, 0, 0x59D0, 0x7A74, 0x79EA},   // carrier
    };
    for (std::size_t i = 0; i < t.kinds.size(); ++i) {
        const auto& s = sources[i];
        auto& k = t.kinds[i];
        k = {s.marker, s.slot, s.deck, s.value, s.hits, s.sink_timer, s.first_from, s.span, s.overlay, {}, {}, {}, {}};
        for (int g = 0; g < s.guns; ++g) {
            k.gun_x.push_back(wings.s16(a4 - s.gun_table + std::uint32_t(g * 2)));
            k.gun_lift.push_back(s.slot == 2 && (g == 1 || g == 2) ? 5 : 0); // ($103312)
        }
        int most = 0;
        for (int r = 0; r < rows; ++r) {
            const int count = wings.u8(a4 - s.counts + std::uint32_t(r * 2));
            k.plane_counts.push_back(count);
            k.plane_counts.push_back(wings.u8(a4 - s.counts + std::uint32_t(r * 2 + 1)));
            most = std::max(most, count);
        }
        for (int w = 0; w < 2 + most * 4; ++w) k.plane_template.push_back(wings.s16(a4 - s.plane_template + std::uint32_t(w * 2)));
    }
    for (std::uint32_t i = 0; i < 4; ++i) t.names[i] = string_at(wings, wings.u32(a4 - 0x7456 + i * 4));
    t.sunk = string_at(wings, a4 - 0x7526);
    return t;
}

void find_ships(FlightWorld& w, const data::MissionMap& map, const FlightTables& t, int row) {
    for (int slot = 0; slot < 4; ++slot) {
        auto& s = w.ships[std::size_t(slot)];
        const int sink_timer = s.sink_timer;
        s = ShipState{};
        s.sink_timer = sink_timer; // (the battleship's is not set by the scan)
    }
    for (std::size_t c = 0; c < map.words.size(); ++c) {
        const auto word = map.words[c];
        if ((word & 0x8000) == 0) continue;
        const int index = word >> 2 & 0x1FF;
        for (const auto& k : t.ships.kinds) {
            if (k.marker != index) continue;
            auto& s = w.ships[std::size_t(k.slot)];
            if (s.present) break; // (the first of each kind)
            s.present = true;
            s.afloat = true;
            s.deck = k.deck;
            s.value = k.value;
            s.ready = k.hits;
            if (k.sink_timer >= 0) s.sink_timer = k.sink_timer;
            s.first = int(c) * 2 + k.first_from;
            s.last = s.first + k.span;
            s.overlay = k.overlay;
            ++w.islands.ships_left;
            // $10350E: the guns.
            for (std::size_t g = 0; g < k.gun_x.size(); ++g) {
                ShipGun gun;
                gun.x = k.gun_x[g] + s.first * 4;
                gun.lift = k.gun_lift[g];
                s.guns.push_back(gun);
            }
            // $10252C: the parked planes for this rank and mission.
            const std::size_t at = std::size_t(row) * 2;
            const int count = at < k.plane_counts.size() ? k.plane_counts[at] : 0;
            s.planes.count = count;
            s.planes.limit = at + 1 < k.plane_counts.size() ? k.plane_counts[at + 1] : 0;
            const int x = s.first * 4;
            s.planes.low = k.plane_template[0] + x;
            s.planes.high = k.plane_template[1] + x;
            for (int p = 0; p < count; ++p) {
                const std::size_t q = 2 + std::size_t(p) * 4;
                s.planes.planes.push_back({k.plane_template[q], k.plane_template[q + 1] + x, k.plane_template[q + 2],
                                           k.plane_template[q + 3]});
            }
            break;
        }
    }
}

} // namespace wings::game
