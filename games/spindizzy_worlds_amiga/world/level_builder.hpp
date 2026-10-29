#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <vector>

// Building a level's playfield from its command list (the original's
// $125DE and the routines it dispatches to). A level is a box of 32 × 32
// cells, 8 cells high; each cell holds a block type byte. Command lists
// place single blocks, fill boxes and slopes, call macros (rotated and
// moved, optionally only when a switch condition holds) and place objects.
//
// Formats and evidence: games/spindizzy_worlds_amiga/game.md
// ("Level commands"). Checked against the original's grid (A5+$E) in a
// memory dump of Tutorial level 1: equal except for one column a lift
// object had already changed.
namespace spindizzy::world {

constexpr int level_size = 32;   // cells along x and y
constexpr int level_height = 8;  // cells along z

// How a cell is drawn, decided while building ($12472 keeps it in a second
// grid at $2576(a5), read by the tile-map builder $1220A).
struct CellLook {
    bool in_front{};  // bit 5 (block attribute bit 2): drawn one depth step nearer
    bool lowered{};   // bit 6 (attribute bits 0 and 1): its pieces start one row lower
    bool base{};      // bit 7: the lowest cell of a block's column, gets a base row
    int depth{};      // bits 0-1: a column cell's distance below its block (piece variant)
    template <class A> void persist(A& ar) {
        std::uint8_t d = std::uint8_t(depth);
        ar(in_front, lowered, base, d);
        depth = d;
    }
};

class LevelGrid {
public:
    std::uint8_t at(int x, int y, int z) const { return cells_[index(x, y, z)]; }
    const CellLook& look(int x, int y, int z) const { return looks_[index(x, y, z)]; }
    void set(int x, int y, int z, std::uint8_t type, CellLook look = {}) {
        cells_[index(x, y, z)] = type;
        looks_[index(x, y, z)] = look;
    }
    CellLook& look(int x, int y, int z) { return looks_[index(x, y, z)]; }
    // A type changed at run time (secret passages): the cell keeps its look.
    void set_type(int x, int y, int z, std::uint8_t type) { cells_[index(x, y, z)] = type; }
    // The highest occupied cell of a column, or -1.
    int top(int x, int y) const;
    template <class A> void persist(A& ar) { ar(cells_, looks_); }

private:
    static std::size_t index(int x, int y, int z) { // the original's x*256 + y*8 + z
        return std::size_t((x * level_size + y) * level_height + z);
    }
    std::array<std::uint8_t, level_size * level_size * level_height> cells_{};
    std::array<CellLook, level_size * level_size * level_height> looks_{};
};

// An object command ($1278C): where, which kind, and its own parameter
// bytes (read by the object's code, not traced yet).
struct PlacedObject {
    int x{}, y{}, z{};
    int kind{};
    std::vector<std::uint8_t> parameters;
    template <class A> void persist(A& ar) { ar(x, y, z, kind, parameters); }
};

// The switch state conditional macros test ($16266). The defaults are the
// values seen at the start of Tutorial level 1 (hypothesis: a level's
// starting state).
struct Switches {
    bool conditions_enabled{true};   // $361A(a5) non-zero
    bool jewels_left{true};          // $2566(a5) non-zero
    std::uint8_t primary{0xFF};      // $3616(a5): the pushed button's value, $FF none
    std::uint8_t secondary{0xFF};    // $3617(a5)
    std::uint8_t binary{0};          // $3618(a5): binary switch bits

    // Whether a condition word holds (the editor calls its fields "test
    // jewels", "jew NOTgone", "use push", "push switch", "NOT pushed", "use
    // binary" and "Val / Mask").
    bool holds(std::uint16_t condition) const;
    template <class A> void persist(A& ar) { ar(conditions_enabled, jewels_left, primary, secondary, binary); }
};

struct Level {
    LevelGrid grid;
    std::vector<PlacedObject> objects;
    std::vector<std::uint16_t> conditions; // the condition words met while building, in order ($162D8)
    template <class A> void persist(A& ar) { ar(grid, objects, conditions); }
};

// `rotation`: the level's quarter turns ($248B; the r and t keys).
Level build_level(const data::World& world, int level, const std::array<std::uint8_t, 256>& block_attributes,
                  const Switches& switches = {}, int rotation = 0);

} // namespace spindizzy::world
