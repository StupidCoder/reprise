#include "mindwalker_amiga/arena/arena_map.hpp"
#include <algorithm>

namespace mindwalker::arena {
namespace {

using Random = std::mt19937;

int bits(Random& random, int mask) { return int(random() >> 16) & mask; }

void run(ArenaMap& map, int x0, int y0, int x1, int y1) {
    for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) map.set(x, y, Cell::open);
}

} // namespace

ArenaMap ArenaMap::generate(std::uint32_t seed, const std::array<int, 8>& room_creatures) {
    Random random(seed);
    ArenaMap map;
    map.cells.fill(Cell::wall);
    // $401630 / $401656: a random cell of the room whose corner is (x, y),
    // two to five cells in from the room's left and one to four down.
    const auto cell_x = [&](int x) { return bits(random, 3) + x + 2; };
    const auto cell_y = [&](int y) { return bits(random, 3) + y + 1; };
    map.difficulty = bits(random, 15);
    const int passes = 4 - (map.difficulty & 3);
    for (int pass = 0; pass < passes; ++pass)
        for (int x = 0; x < width - 1; x += room_width)
            for (int y = 0; y < height - 1; y += room_height) {
                const int from_x = cell_x(x), from_y = cell_y(y);
                const int other = bits(random, 7);
                if (bits(random, 0x40) == 0) {
                    run(map, from_x, from_y, cell_x(other * room_width), from_y); // along the map row
                } else {
                    run(map, from_x, from_y, from_x, cell_y(other * room_height)); // down the map column
                }
            }
    if (bits(random, 0x40) == 0) {
        run(map, 0, exit_y, width - 1, exit_y);
    } else {
        run(map, exit_x, 0, exit_x, height - 1);
    }
    run(map, exit_x - 1, exit_y - 1, exit_x + 1, exit_y + 1);
    map.set(exit_x, exit_y, Cell::exit);
    // $401480: the target, up to 3,000 tries for an open cell; room row 4
    // counts as 3 and room column 4 as 5, so it is never in line with home.
    int tx = 0, ty = 0;
    for (int tries = 0; tries < 3000; ++tries) {
        int row = bits(random, 7), column = bits(random, 7);
        if (row == 4) row = 3;
        if (column == 4) column = 5;
        tx = cell_x(column * room_width);
        ty = cell_y(row * room_height);
        if (map.at(tx, ty) == Cell::open) break;
    }
    map.set(tx, ty, Cell::target);
    map.target_x = tx;
    map.target_y = ty;
    // $4015A0: each room's creatures; the target's room six, home none.
    for (auto& column : map.creatures)
        for (auto& count : column) count = room_creatures[std::size_t(bits(random, 7))];
    map.creatures[std::size_t(tx / room_width)][std::size_t(ty / room_height)] = 6;
    map.creatures[home_room][home_room] = 0;
    return map;
}

void ArenaMap::wall_in_home() {
    for (int y = exit_y - 1; y <= exit_y + 1; ++y)
        for (int x = exit_x - 1; x <= exit_x + 1; ++x) set(x, y, Cell::wall);
    set(exit_x, exit_y, Cell::exit);
}

} // namespace mindwalker::arena
