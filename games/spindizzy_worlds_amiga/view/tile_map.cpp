#include "spindizzy_worlds_amiga/view/tile_map.hpp"

namespace spindizzy::view {

TileMap::TileMap() : cells_(std::size_t(map_columns * map_rows)) {}

std::span<const TileLayer> TileMap::layers(int column, int row) const {
    if (column < 0 || column >= map_columns || row < 0 || row >= map_rows) return {};
    const auto& cell = cells_[std::size_t(row * map_columns + column)];
    return {cell.layers.data(), std::size_t(cell.count)};
}

// $12328: one piece into a cell. A full cell (an opaque piece, or four
// layers) takes nothing more; a piece whose mask the cell already has is
// left out (it would add nothing behind the same shape).
void TileMap::add(int column, int row, const data::TilePiece& piece, int depth) {
    if (piece.tile < 0) return;
    if (column < 0 || column >= map_columns) return; // the original's map never reaches past these
    auto& cell = cells_[std::size_t((row & (map_rows - 1)) * map_columns + column)]; // rows wrap at 128 ($121F4)
    if (cell.full) return;
    for (int i = 0; i < cell.count; ++i) {
        if (cell.layers[std::size_t(i)].mask == piece.mask) return;
    }
    cell.layers[std::size_t(cell.count++)] = {piece.tile, piece.mask, depth & 0xFF};
    if (piece.mask == 0 || cell.count == 4) cell.full = true;
}

// $1220A: a grid cell's pieces.
void TileMap::block(int column, int row, int depth, std::uint8_t type, const world::CellLook& look,
                    const data::Graphics& graphics) {
    if (look.in_front) ++depth;
    const auto row_of = [&](const auto& pieces, std::size_t first, int at, int key) {
        add(column, at, pieces[first], key);
        add(column + 1, at, pieces[first + 1], key);
    };
    if ((type & 0x3F) == 2) {
        // A column cell under a block: one row of the variant pieces of
        // type (type & $C0) + 2, chosen by its distance below the block.
        const auto& pieces = graphics.pieces[(type & 0xC0) + 2];
        row_of(pieces, std::size_t(look.depth) * 2, row + 3, depth);
    } else if (look.lowered) {
        const auto& pieces = graphics.pieces[type];
        for (int r = 0; r < 4; ++r) row_of(pieces, std::size_t(r) * 2, row + 1 + r, depth);
        return; // no base row
    } else {
        const auto& pieces = graphics.pieces[type];
        row_of(pieces, 0, row, depth + 1); // the top row sorts one step nearer
        for (int r = 1; r < 4; ++r) row_of(pieces, std::size_t(r) * 2, row + r, depth);
    }
    if (look.base) {
        // The base row: pieces of type (type & $C0), chosen by the distance.
        const auto& pieces = graphics.pieces[type & 0xC0];
        row_of(pieces, std::size_t(look.depth) * 2, row + 4, depth);
    }
}

TileMap TileMap::build(const world::LevelGrid& grid, const data::Graphics& graphics) {
    TileMap map;
    // $12372: front to back, so nearer pieces claim the layers first. The
    // depth key starts at $C1 and drops 2 per step in x or y, 1 per step in z.
    for (int x = world::level_size - 1; x >= 0; --x) {
        for (int y = world::level_size - 1; y >= 0; --y) {
            for (int z = world::level_height - 1; z >= 0; --z) {
                const auto type = grid.at(x, y, z);
                if (type == 0) continue;
                const int depth = 0xC1 - 2 * (62 - x - y) - (7 - z);
                map.block(y - x + 31, x + y - z + 7, depth, type, grid.look(x, y, z), graphics);
            }
        }
    }
    return map;
}

} // namespace spindizzy::view
