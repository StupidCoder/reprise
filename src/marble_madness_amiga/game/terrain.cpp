#include "marble_madness_amiga/game/terrain.hpp"
#include <algorithm>
#include <stdexcept>

namespace marble_madness::game {
namespace {

// Program tables (addresses in program.hunks' research layout).
constexpr std::uint32_t profile_of_drop = 0x102472;   // word per height drop 0-$60: profile, or -1
constexpr std::uint32_t slope_directions = 0x102534;  // 8 x (dx, dy) words
constexpr std::uint32_t drop_of_profile = 0x101F0C;   // 32 words; $1000 = down to nothing

constexpr int band_rows = 24;
constexpr int band_columns = 22;

// Corner heights of the band buffer ($100CE2), 4 per vertex, as the
// rasteriser writes them.
using Band = std::vector<std::array<std::int16_t, 4>>;

// One slope record (8 bytes): a rectangle of cells with a base height and a
// height profile running in one of eight directions.
struct SlopeRecord {
    int x0, y0, x_size, y_size; // x signed, y unsigned
    std::int16_t base;
    int profile;                // 0-31
    int direction;              // 0-7
    bool flip;                  // negate the profile
};

SlopeRecord read_record(const amiga::LoadedImage& track, std::uint32_t at) {
    return {.x0 = track.s8(at),
            .y0 = track.u8(at + 1),
            .x_size = track.s8(at + 2),
            .y_size = track.u8(at + 3),
            .base = track.s16(at + 4),
            .profile = track.s8(at + 6) & 0x1F,
            .direction = track.u8(at + 7) & 7,
            .flip = (track.u8(at + 7) & 8) != 0};
}

// $10E4A0: writes one record's heights into the band starting at diagonal
// row `first_row`. The profile's bytes are used one per visited vertex
// (also those outside the band) and restart after the $80 byte.
void rasterise(const amiga::LoadedImage& program, const amiga::LoadedImage& track, std::uint32_t profiles,
               const SlopeRecord& r, int first_row, int rows_in_band, Band& band) {
    const std::uint32_t profile = track.u32(profiles + std::uint32_t(r.profile) * 4);
    const int step_x = program.s16(slope_directions + std::uint32_t(r.direction) * 4);
    const int step_y = program.s16(slope_directions + std::uint32_t(r.direction) * 4 + 2);
    const int x_last = r.x0 + r.x_size - 1, y_last = r.y0 + r.y_size - 1;
    const int x_from = step_x < 0 ? x_last : r.x0, x_to = step_x < 0 ? r.x0 - 1 : r.x0 + r.x_size;
    const int y_from = step_y < 0 ? y_last : r.y0, y_to = step_y < 0 ? r.y0 - 1 : r.y0 + r.y_size;
    // The game's band coordinates (param 2 and 3 of $10E4A0).
    const int row_origin = first_row + 0x15, column_origin = (first_row >> 1) + 0x15;
    std::uint32_t cursor = profile;

    auto visit = [&](int x, int y) {
        const auto row = std::int16_t(x + y - row_origin);
        if (row >= 0 && row < rows_in_band) {
            const auto column = std::int16_t((row >> 1) + (column_origin - x));
            if (column >= 0 && column < band_columns - (row & 1)) {
                const int step = track.s8(cursor);
                const auto height = std::int16_t(r.base + (r.flip ? -step : step));
                auto& c = band[std::size_t(row * band_columns + column)];
                // Which corners of the vertex belong to the rectangle.
                if (x == r.x0) {
                    if (y == r.y0) {
                        c[2] = height;
                    } else if (y == y_last) {
                        c[3] = height;
                    } else {
                        c[2] = c[3] = height;
                    }
                } else if (x == x_last) {
                    if (y == r.y0) {
                        c[1] = height;
                    } else if (y == y_last) {
                        c[0] = height;
                    } else {
                        c[0] = c[1] = height;
                    }
                } else if (y == r.y0) {
                    c[1] = c[2] = height;
                } else if (y == y_last) {
                    c[0] = c[3] = height;
                } else {
                    c = {height, height, height, height};
                }
            }
        }
        ++cursor;
        if (track.s8(cursor) == -0x80) {
            cursor = profile;
        }
    };
    // Directions 0-3 walk x inside y; 4-7 walk y inside x.
    if (r.direction < 4) {
        for (int y = y_from; y != y_to; y += step_y) {
            for (int x = x_from; x != x_to; x += step_x) {
                visit(x, y);
            }
        }
    } else {
        for (int x = x_from; x != x_to; x += step_x) {
            for (int y = y_from; y != y_to; y += step_y) {
                visit(x, y);
            }
        }
    }
}

// $10DC24 for one vertex: its code relative to the row's reference height.
std::uint16_t encode(const amiga::LoadedImage& program, const amiga::LoadedImage& track, std::uint32_t patterns,
                     const std::array<std::int16_t, 4>& c, std::int16_t base) {
    const std::int16_t c0 = c[0], c1 = c[1], c2 = c[2], c3 = c[3];
    // A raw corner 0 below $1000 (only patches write those) is the code.
    if (c0 != 0 && c0 < 0x1000) {
        return std::uint16_t(c0);
    }
    const auto near = [&](std::int16_t h) { return h == 0 || (h >= base - 0x40 && h < base + 0x40); };
    bool pattern = !(near(c0) && near(c1) && near(c2) && near(c3));
    if (!pattern) {
        if (c1 == c0 && c2 == c1 && c3 == c2) {
            return c0 == 0 ? 0 : std::uint16_t(c0 - base + 0xF040);
        }
        // Two levels: corner 0's and the first other one.
        const std::int16_t other = c1 != c0 ? c1 : c2 != c0 ? c2 : c3;
        if ((c2 != c0 && c2 != other) || (c3 != c0 && c3 != other)) {
            pattern = true;
        } else {
            const std::int16_t high = c0 > other ? c0 : other;
            const std::int16_t low = c0 > other ? other : c0;
            const int drop = low == 0 ? 0x1000 : high - low;
            int profile = -1;
            if (drop == 0x1000) {
                profile = 0x1F;
            } else if (drop <= 0x60) {
                profile = program.s16(profile_of_drop + std::uint32_t(drop) * 2);
            }
            if (profile >= 0) {
                auto code = std::uint16_t(high - base + 0x40 + (profile << 7));
                code = std::uint16_t(code + (c1 == high ? 0x1000 : 0) + (c2 == high ? 0x2000 : 0) +
                                     (c3 == high ? 0x4000 : 0) + (c0 == high ? 0x8000 : 0));
                return code;
            }
            pattern = true;
        }
    }
    // A pattern of four bytes (heights + $80 - reference, 0 = nothing),
    // stored as corners 1, 2, 3, 0. The search never compares the table's
    // first entry; a missing pattern yields the end marker's offset.
    const auto rel = [&](std::int16_t h) { return std::uint16_t(h == 0 ? 0 : std::int16_t(h + 0x80 - base)); };
    const std::uint32_t key = std::uint32_t(std::uint16_t((rel(c1) << 8) + rel(c2))) << 16 |
                              std::uint16_t((rel(c3) << 8) + rel(c0));
    std::uint32_t at = patterns;
    while (true) {
        const bool end = track.u32(at) == 0xFFFFFFFF;
        at += 4;
        if (end || track.u32(at) == key) {
            break;
        }
    }
    return std::uint16_t(at - patterns);
}

// $10DDCC: the dictionary entry for a code (binary search over the sorted
// words, steps of 512 entries halving down to 1; the search ends where it is
// when the steps run out, so a missing code takes a neighbour's entry).
std::uint16_t lookup(const amiga::LoadedImage& track, std::uint32_t first, std::uint32_t last, std::uint16_t code) {
    std::uint32_t at = first;
    for (std::uint32_t step = 0x400; step != 1; step >>= 1) {
        const std::uint16_t entry = track.u16(at);
        if (code == entry) {
            break;
        }
        at = code < entry ? at - step : at + step;
        if (std::int64_t(at) > std::int64_t(last)) {
            at = last;
        } else if (std::int64_t(at) < std::int64_t(first)) {
            at = first;
        }
    }
    return track.u16(at);
}

} // namespace

Terrain::Terrain(const data::GameFiles& files, data::Course course)
    : program_(&files.program()), track_(&files.track(course)) {
    const auto& track = *track_;
    const auto& program = *program_;
    const std::uint32_t descriptor = track.u32(files.track_header(course));
    patterns_ = track.u32(descriptor);
    rows_ = track.s16(descriptor + 0x18);
    view_offset_ = track.s16(descriptor + 0x10);
    if (course == data::Course::silly) {
        view_offset_ += track.s16(descriptor + 0x12);
    }
    // Reference heights: (count, height) word pairs until a count of 0 ($10EA6C).
    for (std::uint32_t at = track.u32(descriptor + 0xC); track.s16(at) != 0; at += 4) {
        row_base_.insert(row_base_.end(), std::size_t(track.s16(at)), track.s16(at + 2));
    }
    row_base_.resize(std::size_t(std::max(rows_, 0)) + band_rows, 0);
    codes_.assign(row_base_.size() * columns, 0);

    const int records = track.s16(descriptor + 0x1A);
    const std::uint32_t record_table = track.u32(descriptor + 0x1C);
    const std::uint32_t profiles = track.u32(descriptor + 0x20);
    const std::uint32_t dictionary = track.u32(descriptor + 0x26);
    const std::uint32_t dictionary_last = dictionary + std::uint32_t(track.s16(descriptor + 0x24)) * 2 - 2;

    // $10E1EC: per band, a mask word for every 16 records (bit i: record i
    // reaches this band), then (band row << 8 | column, height) patches of
    // corner 0, then $FFFE (another band follows) or $FFFF.
    std::uint32_t stream = track.u32(descriptor + 8);
    Band band(band_rows * band_columns);
    for (int first_row = 0;; first_row += band_rows) {
        const int rows_in_band = std::min(band_rows, rows_ - first_row);
        band.assign(band.size(), {});
        std::uint16_t mask = 0;
        for (int i = 0; i < records; ++i) {
            if ((i & 15) == 0) {
                mask = track.u16(stream);
                stream += 2;
            }
            if (mask >> (i & 15) & 1) {
                rasterise(program, track, profiles, read_record(track, record_table + std::uint32_t(i) * 8),
                          first_row, rows_in_band, band);
            }
        }
        std::uint16_t marker;
        while (((marker = track.u16(stream)) & 0xFFFE) != 0xFFFE) {
            const std::size_t cell = std::size_t((marker >> 8) * band_columns + (marker & 0xFF));
            if (cell < band.size()) {
                band[cell][0] = track.s16(stream + 2);
            }
            stream += 4;
        }
        stream += 2;
        for (int r = 0; r < rows_in_band; ++r) {
            const int row = first_row + r;
            // Odd rows have 21 vertices; the 22nd slot gets dictionary entry 0.
            const int width = band_columns - (r & 1);
            if (width < band_columns) {
                codes_[std::size_t(row * columns + width)] = track.u16(dictionary);
            }
            for (int col = 0; col < width; ++col) {
                const auto code = encode(program, track, patterns_, band[std::size_t(r * band_columns + col)],
                                         row_base_[std::size_t(row)]);
                codes_[std::size_t(row * columns + col)] = lookup(track, dictionary, dictionary_last, code);
            }
        }
        if (marker == 0xFFFF) {
            break;
        }
        if (first_row > 0x4000) {
            throw std::runtime_error("A course's height map does not end; please import the disk again.");
        }
    }
}

std::uint16_t Terrain::code_at(int row, int column) const {
    return codes_[std::size_t(row * columns + column)];
}

std::int16_t Terrain::higher_corner(int row, int column, int pair) const {
    static constexpr std::array<std::array<int, 2>, 4> corners{{{0, 1}, {1, 2}, {0, 3}, {2, 3}}};
    if (row >= rows_ || row < 0 || column < 0 || column >= columns) {
        return 0;
    }
    const auto [a, b] = corners[std::size_t(pair & 3)];
    const std::int16_t base = row_base_[std::size_t(row)];
    std::uint16_t code = code_at(row, column);
    while ((code & 0xF000) == 0) {
        if ((code & 0xFFF) == 0) {
            return 0;
        }
        if ((code & 0x800) == 0) {
            // A pattern: the higher stored byte (0 = nothing).
            const std::uint32_t at = patterns_ + std::uint32_t(std::int16_t(code));
            const int high = std::max(track_->u8(at + std::uint32_t(a)), track_->u8(at + std::uint32_t(b)));
            return high == 0 ? std::int16_t(0) : std::int16_t(base + high - 0x80);
        }
        code = animated[(code & 0x7FE) / 2];
    }
    // Flat or two levels: the high level if either corner has it.
    const auto high = std::int16_t(base + (code & 0x7F) - 0x40);
    const std::uint16_t bits = std::uint16_t((0x1000 << a) | (0x1000 << b));
    if ((code & bits) != 0) {
        return high;
    }
    const std::int16_t drop = program_->s16(drop_of_profile + std::uint32_t((code >> 7) & 0x1F) * 2);
    return drop == 0x1000 ? std::int16_t(0) : std::int16_t(high - drop);
}

// $10DE9C for one vertex.
Vertex Terrain::vertex(int vx, int vy) const {
    Vertex v;
    const int row = vx + vy - 0x15;
    if (row < 0 || row >= rows_) {
        return v;
    }
    const int column = (row >> 1) + 0x15 - vx;
    if (column < 0 || column >= columns) {
        return v; // not reached inside a course (the game would read a neighbour's bits)
    }
    const std::int16_t base = row_base_[std::size_t(row)];
    std::uint16_t code = code_at(row, column);
    while (code < 0x1000) {
        if (code < 0x800) {
            if (code == 0) {
                return v;
            }
            // A pattern: heights relative to the row's reference + $80.
            const std::uint32_t at = patterns_ + std::uint32_t(std::int16_t(code));
            for (int k = 0; k < 4; ++k) {
                const auto b = track_->u8(at + std::uint32_t(k));
                v.height[std::size_t(k)] = b == 0 ? 0 : std::int16_t(base - 0x80 + b);
            }
            return v;
        }
        code = animated[(code & 0x7FE) / 2];
    }
    const auto high = std::int16_t(base + (code & 0x7F) - 0x40);
    if (code >= 0xF000) {
        v.height = {high, high, high, high};
        return v;
    }
    const std::int16_t drop = program_->s16(drop_of_profile + std::uint32_t((code >> 7) & 0x1F) * 2);
    const auto low = drop == 0x1000 ? std::int16_t(0) : std::int16_t(high - drop);
    v.height = {(code & 0x1000) ? high : low, (code & 0x2000) ? high : low, (code & 0x4000) ? high : low,
                (code & 0x8000) ? high : low};
    return v;
}

} // namespace marble_madness::game
