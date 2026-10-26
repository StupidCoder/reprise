#include "bruce_lee_c64/game/terrain.hpp"
#include <array>

namespace bruce_lee::game {
namespace {

// A 4-bit pattern (two pixel pairs) of mask AND glyph is recorded by value;
// patterns 16-22 are where seven of them move when the cell's colour has
// any of its low three bits set (the colour of pixel pair 11 then differs),
// see Patterns::swap.
constexpr int pattern_slots = 23;
constexpr std::array<std::array<int, 2>, 7> swapped_patterns{
    {{3, 16}, {7, 17}, {11, 18}, {12, 19}, {13, 20}, {14, 21}, {15, 22}}};

// The four groups ($5465-$5598) per column: left, middle, right. Group 0's
// middle column lists the left column's pattern 17 a second time instead of
// its own (a slip in the original: the middle column's 17 never counts).
struct Group {
    std::array<std::array<bool, pattern_slots>, 3> members{};
};
constexpr std::array<std::array<int, 9>, 4> group_patterns{{
    {1, 4, 5, 6, 7, 17, 9, 13, 20},
    {2, 6, 8, 9, 10, 11, 14, 18, 21},
    {16, 17, 18, 19, 20, 21, 22, -1, -1},
    {3, 7, 11, 12, 13, 14, 15, -1, -1},
}};

struct Patterns {
    std::array<std::array<bool, pattern_slots>, 3> seen{};
    std::array<bool, 3> swapped{};

    void swap(int column) {
        for (const auto& [a, b] : swapped_patterns) {
            std::swap(seen[std::size_t(column)][std::size_t(a)], seen[std::size_t(column)][std::size_t(b)]);
        }
    }
    void set_swapped(int column, bool on) {
        if (swapped[std::size_t(column)] != on) {
            swapped[std::size_t(column)] = on;
            swap(column);
        }
    }
    void record(int column, std::uint8_t overlap) {
        if (overlap) {
            seen[std::size_t(column)][overlap >> 4] = true;
            seen[std::size_t(column)][overlap & 15] = true;
        }
    }
    bool any(int group) const {
        for (int column = 0; column < 3; ++column) {
            for (int pattern : group_patterns[std::size_t(group)]) {
                if (pattern < 0) {
                    continue;
                }
                // The slip: group 0, middle column, pattern 17 reads the left column.
                const int from = group == 0 && column == 1 && pattern == 17 ? 0 : column;
                if (seen[std::size_t(from)][std::size_t(pattern)]) {
                    return true;
                }
            }
        }
        return false;
    }
    // $543A: undo the colour swaps, then the four flags.
    std::uint8_t flags() {
        for (int column = 0; column < 3; ++column) {
            set_swapped(column, false);
        }
        std::uint8_t result = 0;
        for (int group = 3; group >= 0; --group) {
            result = std::uint8_t(result << 1 | (any(group) ? 1 : 0));
        }
        return result;
    }
};

} // namespace

TerrainContact probe_terrain(const Playfield& field, const data::Charset& charset, const data::CollisionMask& mask,
                             int x, int y) {
    // $5228: the cell and the offset within it (x in pixel pairs, y in lines).
    const int column_x = (x - 12) & 0xFF, row_y = (y - 0x1D) & 0xFF;
    const int pair_offset = column_x & 3, line_offset = row_y & 7;
    const int column = column_x >> 2, char_row = row_y >> 3;

    // $52B6: the mask placed in a buffer of four columns of 32 rows, laid out
    // one column after the other: `line_offset` empty rows first (so each
    // column's last rows run into the next column's first), shifted right by
    // the pair offset; three pairs are done as one column right, one pair
    // left. Bits shifted past the third column are lost.
    std::array<std::uint8_t, 128 + 40> buffer{};
    int at = line_offset;
    if (pair_offset == 3) {
        at += 32;
    }
    for (std::uint8_t b : mask) {
        buffer[std::size_t(at++)] = b;
    }
    auto column_byte = [&](int c, int row) -> std::uint8_t& { return buffer[std::size_t(32 * c + row)]; };
    if (pair_offset == 1 || pair_offset == 2) {
        for (int bit = 0; bit < 2 * pair_offset; ++bit) {
            for (int row = 0; row < 32; ++row) {
                const int c0 = column_byte(0, row), c1 = column_byte(1, row), c2 = column_byte(2, row);
                column_byte(0, row) = std::uint8_t(c0 >> 1);
                column_byte(1, row) = std::uint8_t(c1 >> 1 | (c0 & 1) << 7);
                column_byte(2, row) = std::uint8_t(c2 >> 1 | (c1 & 1) << 7);
            }
        }
    } else if (pair_offset == 3) {
        for (int bit = 0; bit < 2; ++bit) {
            for (int row = 0; row < 32; ++row) {
                const int c0 = column_byte(0, row), c1 = column_byte(1, row), c2 = column_byte(2, row),
                          c3 = column_byte(3, row);
                column_byte(3, row) = std::uint8_t(c3 << 1);
                column_byte(2, row) = std::uint8_t(c2 << 1 | c3 >> 7);
                column_byte(1, row) = std::uint8_t(c1 << 1 | c2 >> 7);
                column_byte(0, row) = std::uint8_t(c0 << 1 | c1 >> 7);
            }
        }
    }

    // $5271-$52AA, $5387: four character rows from screen row char_row + 1,
    // three columns; mask row by mask row the overlaps with the glyphs are
    // recorded. At mask row 15 + offset the flags so far are the body's; at
    // row 21 + offset the feet's, and the test ends.
    Patterns patterns;
    TerrainContact contact;
    const int body_row = line_offset + 15, feet_row = line_offset + 21;
    int mask_row = 0;
    for (int k = 0; k < 4; ++k) {
        const int screen_row = char_row + k + 1;
        std::array<const data::Glyph*, 3> glyphs{};
        for (int c = 0; c < 3; ++c) {
            const int col = column + c;
            if (c == 2 && col >= 40) { // $535F: no colour check, character 0
                glyphs[2] = &charset[0];
                continue;
            }
            const auto& cell = field.at(col, screen_row);
            patterns.set_swapped(c, (cell.colour & 7) != 0); // $55E0, $5652, $56C4
            glyphs[std::size_t(c)] = &charset[cell.code];
        }
        for (int line = 0; line < 8; ++line, ++mask_row) {
            for (int c = 0; c < 3; ++c) {
                patterns.record(c, std::uint8_t((*glyphs[std::size_t(c)])[std::size_t(line)] & column_byte(c, mask_row)));
            }
            if (mask_row + 1 == body_row) {
                // $53ED: evaluate without losing the swap state.
                const auto saved = patterns.swapped;
                contact.body = patterns.flags();
                for (int c = 0; c < 3; ++c) {
                    patterns.set_swapped(c, saved[std::size_t(c)]);
                }
            }
            if (mask_row + 1 == feet_row) {
                contact.feet = patterns.flags();
                return contact;
            }
        }
    }
    return contact;
}

} // namespace bruce_lee::game
