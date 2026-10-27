#pragma once
#include "marble_madness_amiga/data/game_files.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The ground of a course: a height map over a grid of 8 x 8 cells.
//
// The game keeps one record per grid point ("vertex") holding the heights of
// the four cells that meet there, so neighbouring cells need not agree at a
// shared corner: that is how the map has vertical walls and drops. Heights
// are in the game's units (the integer part of a 16.16 world z; ground is
// around $4000, 0 means no ground at all).
//
// The Track file stores the map compressed. The course start ($10EA12 →
// $10E1EC) rasterises the course's slope records band by band (24 diagonal
// rows at a time, $10E4A0), applies the band's patches, encodes each vertex
// as a code ($10DC24: flat, two-level slope or a pattern from a table; codes
// $800-$FFF are animated vertices looked up in a table the course's scripts
// rewrite) and stores the code's index in a dictionary from the Track
// ($10DDCC, a binary search that keeps the nearest entry when a code is
// missing). The marble's routines decode the four vertices around it each
// time it changes cells ($10DE9C). This class does the same steps and keeps
// the codes per vertex; decoding happens on request, so animated vertices
// follow their table.
//
// Vertex coordinates are the slope records' cell coordinates. A world
// position (16.16) is in cell (x >> 19, y >> 19); that cell's corners are
// the vertices (cx, cy), (cx + 1, cy), (cx, cy + 1) and (cx + 1, cy + 1).
// Internally the game files vertices by diagonal row r = vx + vy - 21 and
// column (r >> 1) + 21 - vx (22 per even row, 21 per odd row).
namespace marble_madness::game {

// The heights a vertex gives the four cells around it, indexed by the
// cell's direction from the vertex.
struct Vertex {
    enum Quadrant { minus_x_plus_y = 0, plus_x_plus_y = 1, plus_x_minus_y = 2, minus_x_minus_y = 3 };
    std::array<std::int16_t, 4> height{};
};

class Terrain {
public:
    Terrain(const data::GameFiles&, data::Course);

    Vertex vertex(int vx, int vy) const;
    // The vertex's stored code (research: $800-$FFF are animated), or 0.
    std::uint16_t stored_code(int vx, int vy) const {
        const int row = vx + vy - 0x15, column = (row >> 1) + 0x15 - vx;
        return row < 0 || row >= rows_ || column < 0 || column >= columns ? 0 : code_at(row, column);
    }

    // $10E0A8: the higher of two of a vertex's corners, addressed by its
    // diagonal row and column (0: no ground, or past the course's last
    // row). Pairs ($10EEE4, $10EEDC): 0 corners 0 and 1, 1 corners 1 and 2,
    // 2 corners 0 and 3, 3 corners 2 and 3. The covers that hide a marble
    // behind walls compare these with its sprite.
    std::int16_t higher_corner(int row, int column, int pair) const;

    // The view's height offset for this course (Track descriptor +$10, plus
    // +$12 on Silly; $10EA12 keeps it in $1009D2).
    int view_offset() const { return view_offset_; }

    // Codes of animated vertices ($800-$FFF), rewritten by course scripts
    // (the table at $1007B0, indexed by code & $7FE).
    std::array<std::uint16_t, 0x400> animated{};

private:
    static constexpr int columns = 22;
    std::uint16_t code_at(int row, int column) const;

    const amiga::LoadedImage* program_{};
    const amiga::LoadedImage* track_{};
    std::uint32_t patterns_{};             // 4-byte vertex patterns, -1 ends
    int rows_{};                           // diagonal rows
    std::vector<std::int16_t> row_base_;   // reference height per diagonal row
    std::vector<std::uint16_t> codes_;     // per row and column, after the dictionary
    int view_offset_{};
};

} // namespace marble_madness::game
