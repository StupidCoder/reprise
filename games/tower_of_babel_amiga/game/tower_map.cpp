#include "tower_of_babel_amiga/game/tower_map.hpp"
#include <stdexcept>

namespace babel::game {

TowerMap::TowerMap(const data::Tower& tower, std::span<const std::uint8_t> colour_patterns) {
    for (int f = 0; f < floors; ++f)
        for (int z = 0; z < size; ++z)
            for (int x = 0; x < size; ++x)
                cells_[std::size_t(f)][std::size_t(z)][std::size_t(x)] = tower.cells[std::size_t(f)][std::size_t(z)][std::size_t(x)];

    // $CD90: one bit per occupied cell from the pattern (4 floors x 8 rows,
    // a byte per row, column x = bit x).
    const std::size_t pattern = std::size_t(tower.settings[2]) * 32; // $E36
    if (colour_patterns.size() < pattern + 32) throw std::runtime_error("The colour pattern table is cut short.");
    for (int f = 0; f < floors; ++f) {
        for (int z = 0; z < size; ++z) {
            const std::uint8_t bits = colour_patterns[pattern + std::size_t(f * size + z)];
            for (int x = 0; x < size; ++x) {
                auto& c = cells_[std::size_t(f)][std::size_t(z)][std::size_t(x)];
                if (c == 0) continue;
                c = std::uint8_t((c & ~cell::second) | ((bits >> x & 1) ? cell::second : 0));
            }
        }
    }

    // $CCA8: light falls down each column from the top floor; any occupied
    // cell is lit while nothing above it blocks, and blocks what lies below
    // unless it is an open lift cell. The ground keeps what reaches it.
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            bool light = true;
            for (int f = floors - 1; f >= 0; --f) {
                auto& c = cells_[std::size_t(f)][std::size_t(z)][std::size_t(x)];
                if (c == 0) continue;
                if (light) c |= cell::lit;
                if (!((c & cell::lift) && (c & cell::lift_open))) light = false;
            }
            ground_[std::size_t(z)][std::size_t(x)] = light;
        }
    }
}

TowerMap::TowerMap(const data::Tower& tower, const data::Memory& program)
    : TowerMap(tower, program.bytes(0xCCF0, 8 * 32)) {}

std::uint8_t TowerMap::at(int floor, int x, int z) const {
    if (floor < 0 || floor >= floors || x < 0 || x >= size || z < 0 || z >= size) return 0;
    return cells_[std::size_t(floor)][std::size_t(z)][std::size_t(x)];
}

} // namespace babel::game
