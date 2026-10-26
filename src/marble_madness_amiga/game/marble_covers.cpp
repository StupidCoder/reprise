// Hiding the marble behind walls ($11F486, $11EC42).
//
// The course is drawn once into the playfield; the marble is a hardware
// sprite in front of it. Where a wall or a ledge stands between the marble
// and the viewer, the game cuts the wall's shape out of the sprite: every
// update it picks, per 8-pixel screen column the sprite touches, at most
// one "cover" cell from the course's wall-cover library (<course>obsc.vlb,
// bank 5) and a height, and the sprite builder clears the cover's pixels
// from the sprite ($111708). When every column is covered completely, the
// sprite is not shown at all.
//
// Two sources decide the covers of a column:
// - The Track's cover lists (header +$C: a list per column of 5-word
//   entries {last row, rows, kind, height, cell}, -1 ends), placed by hand
//   for walls the general rule gets wrong;
// - otherwise a bit map per column (descriptor +$2E: one bit per diagonal
//   row that has a wall face in this column): the first such row at or in
//   front of the marble is looked up in the height map, and the ground's
//   height there against the sprite's top and bottom decides between no
//   cover, a full one (cell 0) or a slanted edge (cells 1-$21).
#include "marble_madness_amiga/game/race.hpp"

namespace marble_madness::game {
namespace {

// Program tables.
constexpr std::uint32_t row_offsets = 0x11F4F9;  // 3 x 5 bytes: rows in front of the marble per column
constexpr std::uint32_t bits_from = 0x11F508;    // 8 bytes: masks of the bits from a row on
constexpr std::uint32_t edge_up_cells = 0x11F510; // words by height difference: covers rising right
constexpr std::uint32_t edge_down_cells = 0x11F532; // words: covers falling right
constexpr std::uint32_t edge_heights = 0x11F554; // words: their height offsets

constexpr int view_columns = 0x24; // 288 pixels in 8-pixel columns

std::int16_t s16(std::int32_t v) { return std::int16_t(v); }

} // namespace

// $11F486: a marble is shown while its sprite is on screen horizontally and
// not covered completely.
void Race::check_visible(Marble& m) {
    m.sprite_flag = 0;
    const std::int16_t screen_x = s16(m.screen[0] >> 16);
    for (auto& c : m.covers) {
        c.cell = -1;
    }
    if (screen_x > -0x10 && screen_x < 0x100) {
        m.sprite_flag = 1;
        build_covers(m);
    }
}

// $11EC42 for a marble (the same routine serves the creatures with other
// sizes; those are not rebuilt yet). Its global "no covers" switch
// ($11F4F8) is never set in this version.
void Race::build_covers(Marble& m) {
    const auto& program = files_->program();
    const auto& track = files_->track(course_);
    const bool player = is_player(m);
    std::size_t count = 0;
    auto add = [&](int cell, int column, int y) {
        if (count < m.covers.size()) {
            m.covers[count++] = {0x50000 + cell, s16(column << 3), s16(y)};
        }
    };

    if (player) {
        // Taken, appearing or past the goal: no covers. Held by a region
        // (other than code 10): hidden.
        if (m.mode == Marble::respawning || m.mode == Marble::finishing || m.mode == Marble::captured) {
            return;
        }
        if (m.region_code != 0 && m.region_timer != 0xFF && m.region_code != 10 && m.mode != Marble::appearing &&
            m.mode != Marble::settling) {
            m.sprite_flag = 0;
            return;
        }
    }
    // The sprite's columns and lines: 3 x 14, or 5 x 32 for the larger
    // animation frames. Screen heights count upwards.
    int columns = 3;
    int height = 0xE;
    if (player && (m.mode == Marble::landing || m.mode == Marble::breaking || m.mode == Marble::bumped ||
                   m.mode == Marble::appearing)) {
        columns = 5;
        height = 0x20;
    }
    const std::int16_t screen_x = s16(m.screen[0] >> 16);
    const std::int16_t top = s16(m.screen[0]);
    const std::int16_t bottom = s16(top + height);

    // The marble's diagonal row, from its cell and from its exact position
    // (they differ in the half of a cell nearer the viewer).
    const std::int16_t row = s16((m.x >> 19) + (m.y >> 19) - 0x15);
    const std::int16_t exact_row = s16(((m.x + m.y) >> 19) - 0x15);
    const std::int16_t centre = s16(screen_x >> 3);
    int first = centre - 2;
    std::uint32_t offsets = row_offsets;
    if (row != exact_row) {
        offsets += 10;
    } else if (((centre ^ row) & 1) != 0) {
        offsets += 5;
    }
    if (columns == 3) {
        ++offsets;
        first = centre - 1;
    }
    if (first < 0) {
        columns += first;
        offsets = std::uint32_t(std::int32_t(offsets) - first);
        first = 0;
    } else if (first + columns > view_columns) {
        columns = view_columns - first;
    }

    // Course heights against screen heights: the view's offset from the
    // course's first view ($1009D2 - descriptor +$10).
    const std::int16_t view_shift = s16(view_ - first_view_);
    const std::uint32_t lists = track.u32(files_->track_header(course_) + 0xC);
    // Ultimate switches between bit-map sets ($1006A4, set by its course
    // code $11E848, not rebuilt yet); the other courses use the first.
    const std::uint32_t bit_maps = descriptor_ + 0x2E;
    unsigned covered = 0; // bit per column: covered completely

    for (int column = first; column < first + columns; ++column) {
        int r = row + program.s8(offsets++);
        if (r < 0) {
            r = 0;
        }
        bool full = false;
        bool done = false;
        // The hand-placed covers of this column (entries in row order).
        const std::uint32_t list = lists != 0 ? track.u32(lists + std::uint32_t(column) * 4) : 0;
        for (std::uint32_t at = list; at != 0; at += 10) {
            const std::int16_t last = track.s16(at);
            if (r <= last) {
                if (last - track.s16(at + 2) <= r) {
                    const std::int16_t kind = track.s16(at + 4);
                    const std::int16_t y = s16(track.s16(at + 6) + view_shift);
                    const std::int16_t cell = track.s16(at + 8);
                    done = true;
                    if (kind >= 0 && kind < 8) {
                        // A wall in front of the marble unless it sits higher.
                        if (y >= top - kind) {
                            full = true;
                        } else {
                            add(cell, column, y);
                        }
                    } else if (kind == 8) {
                        if (y < bottom) {
                            add(cell, column, y);
                        }
                    } else if (kind == -2) {
                        // A floor the marble is under.
                        if (int(std::uint16_t(m.z >> 16)) < cell) {
                            full = true;
                        }
                    } else if (kind < 0) {
                        // Covers unless the marble is on the entry's row.
                        if (r != last || exact_row != last) {
                            add(cell, column, y);
                        }
                    } else if (y <= bottom) {
                        add(cell, column, y);
                    } else {
                        // Higher than the marble: look further from row `kind`.
                        r = kind & 0xFF;
                        done = false;
                    }
                }
                break;
            }
            if (track.s16(at + 10) == -1) {
                break;
            }
        }

        if (!done && !full) {
            // The first wall face at or after row r in this column.
            const std::uint32_t map = track.u32(bit_maps + std::uint32_t(column) * 4);
            const std::uint32_t start = map + std::uint32_t(r >> 3);
            std::uint32_t at = start;
            unsigned bits = program.u8(bits_from + std::uint32_t(r & 7)) & track.u8(at);
            r &= ~7;
            if (bits == 0) {
                do {
                    ++at;
                } while (track.u8(at) == 0);
                bits = track.u8(at);
                r += int(at - start) * 8;
            }
            for (unsigned bit = 1; (bits & bit) == 0; bit <<= 1) {
                ++r;
            }
            // The ground's screen height at the face's two vertices.
            const std::int16_t row_y = s16((r << 2) - view_ - 0x16);
            int parity = (column ^ r) & 1;
            const std::int16_t near = s16(terrain_.higher_corner(r, ((column + parity) >> 1) + 2, parity) - row_y);
            if (near > bottom + 0x10) {
                full = true;
            } else if (near >= top - 0x10) {
                parity ^= 1;
                const std::int16_t far =
                    s16(terrain_.higher_corner(r + 1, ((column + parity) >> 1) + 2, parity + 2) - row_y - 4);
                if (far > top || near > top) {
                    if (far >= bottom && near >= bottom) {
                        full = true;
                    } else {
                        // A slanted edge: the cell by the difference of its
                        // left and right heights.
                        std::int16_t left = near, right = far;
                        if (parity == 0) {
                            std::swap(left, right);
                        }
                        if (left > right) {
                            const auto d = std::uint32_t(left - right) * 2;
                            add(program.s16(edge_down_cells + d), column, program.s16(edge_heights + d) + right);
                        } else {
                            const auto d = std::uint32_t(right - left) * 2;
                            add(program.s16(edge_up_cells + d), column, program.s16(edge_heights + d) + left);
                        }
                    }
                }
            }
        }
        if (full) {
            covered |= 1u << (column - first);
            add(0, column, bottom - 0x2F);
        }
    }

    // Hidden when every column is covered (or, for a large frame, the
    // middle three).
    if (int(covered) == (1 << columns) - 1 || (player && columns == 5 && (covered & 0xE) == 0xE)) {
        m.sprite_flag = 0;
    }
}

} // namespace marble_madness::game
