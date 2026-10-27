#include "mindwalker_amiga/arena/arena_map.hpp"
#include <algorithm>

namespace mindwalker::arena {
namespace {

void run(ArenaMap& map, int x0, int y0, int x1, int y1) { // a straight Draw in pen 0
    for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) map.set(x, y, Cell::open);
}

} // namespace

ArenaMap ArenaMap::generate(std::uint32_t seed, const MazeTables& tables, MazeRandom& r) {
    ArenaMap map;
    map.cells.fill(Cell::wall);
    // h8: three generators from the seed's draws and C's keys (the low word
    // sign-extended into the high one, as the C code's casts do).
    r.seed.state = seed;
    world::ShiftRegister* const three[3] = {&r.columns, &r.targets, &r.rows};
    for (std::size_t k = 0; k < 3; ++k) {
        const auto low = std::int16_t(tables.keys[k] ^ r.seed.next());
        three[k]->state = std::uint32_t(((std::int32_t(low) ^ 0x59DD) << 16) | std::int32_t(low));
    }
    {
        const auto high = r.columns.next();
        r.bolt = world::Mixer::seeded(high, r.rows.next());
    }
    const int pick = r.seed.next() >> 2;
    map.difficulty = pick & 0xF;
    for (int pass = 0; pass < 4 - (pick & 3); ++pass)
        for (int x = 0; x < width - 1; x += room_width)
            for (int y = 0; y < height - 1; y += room_height) {
                const int from_x = random_column(r, x), from_y = random_row(r, y);
                const int other = r.targets.next() & 7;
                if ((r.columns.next() & 0x40) == 0) {
                    run(map, from_x, from_y, random_column(r, other * room_width), from_y); // along the map row
                } else {
                    run(map, from_x, from_y, from_x, random_row(r, other * room_height)); // down the map column
                }
            }
    if ((r.columns.next() & 0x40) == 0) {
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
        int row = r.targets.next() >> 4 & 7;
        if (row == 4) row = 3;
        const int a = r.targets.next();
        int column = ((r.rows.next() >> 3) ^ a) & 7;
        if (column == 4) column = 5;
        ty = random_row(r, row * room_height);
        tx = random_column(r, column * room_width);
        if (map.at(tx, ty) == Cell::open) break;
    }
    map.set(tx, ty, Cell::target);
    map.target_x = tx;
    map.target_y = ty;
    // $4015A0: each room's creatures; home none, the target's room six.
    for (auto& column : map.creatures)
        for (auto& count : column) count = tables.room_creatures[r.rows.next() & 7];
    map.creatures[home_room][home_room] = 0;
    map.creatures[std::size_t(tx / room_width)][std::size_t(ty / room_height)] = 6;
    return map;
}

void ArenaMap::wall_in_home() {
    for (int y = exit_y - 1; y <= exit_y + 1; ++y)
        for (int x = exit_x - 1; x <= exit_x + 1; ++x) set(x, y, Cell::wall);
    set(exit_x, exit_y, Cell::exit);
}

} // namespace mindwalker::arena
