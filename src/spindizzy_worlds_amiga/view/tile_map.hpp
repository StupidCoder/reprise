#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include <array>
#include <span>
#include <vector>

// The playfield as the original keeps it: a map of 16 × 8-pixel tiles,
// each holding up to four layers (front first), built from the level grid
// by $12372 → $1220A → $12328. Checked against the original's map
// ($200E(a5)) for Tutorial level 1: every layer equal.
//
// Projection: grid cell (x, y, z) puts its block's first piece row at
// column y − x + 31 and row x + y − z + 7; each block is two columns wide
// and four rows high (plus a base row for the lowest cell of a column).
namespace spindizzy::view {

constexpr int map_columns = 64;
constexpr int map_rows = 128;
constexpr int map_width = map_columns * data::tile_width;  // 1024 pixels
constexpr int map_height = map_rows * data::tile_height;    // 1024 pixels

struct TileLayer {
    int tile{};
    int mask{};  // 0 = opaque
    int depth{}; // the key's high byte: larger is nearer (for sorting sprites in later)
};

class TileMap {
public:
    TileMap();
    static TileMap build(const world::LevelGrid&, const data::Graphics&);

    // A cell's layers, nearest first.
    std::span<const TileLayer> layers(int column, int row) const;

private:
    struct Cell {
        std::array<TileLayer, 4> layers{};
        int count{};
        bool full{};
    };
    void add(int column, int row, const data::TilePiece&, int depth);
    void block(int column, int row, int depth, std::uint8_t type, const world::CellLook&, const data::Graphics&);

    std::vector<Cell> cells_;
};

} // namespace spindizzy::view
