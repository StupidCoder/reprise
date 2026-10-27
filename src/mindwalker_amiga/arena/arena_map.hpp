#pragma once
#include <array>
#include <cstdint>
#include <random>

// The arena's maze (program C, h8 $401108): a 64 x 48 map of cells, read
// as 8 x 8 rooms of 8 x 6 cells; one room fills the playfield, one cell a
// 32 x 32 tile. Walls everywhere at first, then corridors drawn as straight
// runs from a random cell of every room to a random cell of another room in
// the same map row or column, two to four times over; one run crosses the
// whole map through the home room, whose middle is cleared around the exit.
// The target lies in a random open cell of a room off the home room's row
// and column. Each room starts with a number of creatures (`$401C56` by a
// draw), the target's room with six, the home room with none.
namespace mindwalker::arena {

enum class Cell : std::uint8_t { open = 0, wall = 1, exit = 2, target = 3 }; // the map's pens

struct ArenaMap {
    static constexpr int width = 64, height = 48;
    static constexpr int room_width = 8, room_height = 6, rooms = 8;
    static constexpr int exit_x = 0x24, exit_y = 0x1B; // in the home room (4, 4)
    static constexpr int home_room = 4;

    std::array<Cell, width * height> cells{};
    std::array<std::array<int, rooms>, rooms> creatures{}; // [room x][room y] ($4046B8)
    int target_x{}, target_y{};
    int difficulty{};                                      // $401CB8: 0-15 (bits 2-3 pick the spawning odds)

    Cell at(int x, int y) const { return cells[std::size_t(y * width + x)]; }
    void set(int x, int y, Cell c) { cells[std::size_t(y * width + x)] = c; }

    // A new maze from the world's seed (shared +$4C), through a standard
    // generator (the original's draws are not reproduced); `room_creatures`
    // C's table of creatures per room ($401C56, picked by a draw & 7).
    static ArenaMap generate(std::uint32_t seed, const std::array<int, 8>& room_creatures);
    // $401CEE..: the target taken: the home room's middle walled in around
    // the exit (the way back has to cross walls).
    void wall_in_home();
};

} // namespace mindwalker::arena
