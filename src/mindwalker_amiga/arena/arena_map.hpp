#pragma once
#include "mindwalker_amiga/world/random.hpp"
#include <array>
#include <cstdint>

// The arena's maze (program C, h8 $401108): a 64 x 48 map of cells, read
// as 8 x 8 rooms of 8 x 6 cells; one room fills the playfield, one cell a
// 32 x 32 tile. Walls everywhere at first, then corridors drawn as straight
// runs from a random cell of every room to a random cell of another room in
// the same map row or column, one to four times over; one run crosses the
// whole map through the home room, whose middle is cleared around the exit.
// The target lies in a random open cell of a room off the home room's row
// and column. Each room starts with a number of creatures (`$401C56` by a
// draw), the target's room with six, the home room with none. All of it
// follows from the world's seed (shared +$4C) through the game's own
// generators, so a world's arena is always the same.
namespace mindwalker::arena {

enum class Cell : std::uint8_t { open = 0, wall = 1, exit = 2, target = 3 }; // the map's pens

// The maze's generators, which play goes on drawing from: a creature's
// cell when it starts in a room (columns and rows) and the lightning's
// crooked lines (bolt).
struct MazeRandom {
    world::ShiftRegister seed;    // $401CBC: the world's seed
    world::ShiftRegister columns; // $401CC4: cells across, run directions
    world::ShiftRegister targets; // $401CC8: which room a run goes to; the target's room
    world::ShiftRegister rows;    // $401CCC: cells down; the rooms' creatures
    world::Mixer bolt;            // $4046A0

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(seed, columns, targets, rows, bolt); }
};

// C's tables the maze reads.
struct MazeTables {
    std::array<std::uint16_t, 3> keys{};    // $401C50: mixed into the three generators' seeds
    std::array<int, 8> room_creatures{};    // $401C56: creatures per room, by a draw & 7

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(keys, room_creatures); }
};

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

    // h8: the maze of the world's seed; `random` is left as play goes on
    // with it.
    static ArenaMap generate(std::uint32_t seed, const MazeTables&, MazeRandom& random);
    // $401D24: the target taken: the home room's middle walled in around
    // the exit (the way back has to cross walls).
    void wall_in_home();

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(cells, creatures, target_x, target_y, difficulty); }
};

// $401630 / $401656: a random cell of the room whose corner is (x, y),
// two to five cells in from its left, one to four down.
inline int random_column(MazeRandom& r, int x) { return (r.columns.next() & 3) + x + 2; }
inline int random_row(MazeRandom& r, int y) { return (r.rows.next() & 3) + y + 1; }

} // namespace mindwalker::arena
