#include "marble_madness_amiga/data/assets.hpp"
#include "amiga/adf.hpp"
#include "amiga/byte_run1.hpp"
#include "platform/files.hpp"
#include <stdexcept>

namespace marble_madness::data {
namespace {

using amiga::read_be16;
using amiga::read_be32;

// The program's file names per course ($1034FC, $103556, $11F576), as
// installed.
constexpr std::array<const char*, course_count> map_files = {"practy.mlb", "beginr.mlb", "interm.mlb",
                                                             "aerial.mlb", "silly.mlb",  "ultima.mlb"};
constexpr std::array<const char*, course_count> image_files = {"practy.ilb", "beginr.ilb", "interm.ilb",
                                                               "aerial.ilb", "silly.ilb",  "silly.ilb"};
constexpr std::array<const char*, course_count> cover_files = {"prcobsc.vlb", "begobsc.vlb", "intobsc.vlb",
                                                               "aerobsc.vlb", "silobsc.vlb", "ultobsc.vlb"};

// Larger than any buffer the program reserves for one of these files
// ($108440: at most $A410 for a map, $F618 for a library).
constexpr std::size_t unpack_limit = 0x20000;

[[noreturn]] void bad(const std::string& what) {
    throw std::runtime_error("The installed Marble Madness data is damaged (" + what + "); please import the disk again.");
}

// Plane bits to colour indices for one row of `words` plane words.
void put_row(std::span<std::uint8_t> out, std::span<const std::uint8_t> plane_row, int plane) {
    for (std::size_t x = 0; x < out.size(); ++x) {
        if (plane_row[x / 8] >> (7 - x % 8) & 1) {
            out[x] |= std::uint8_t(1 << plane);
        }
    }
}

} // namespace

const char* course_name(Course course) {
    static constexpr std::array<const char*, course_count> names = {"Practice", "Beginner", "Intermediate",
                                                                    "Aerial",   "Silly",    "Ultimate"};
    return names[std::size_t(course)];
}

// The unpacked .mlb (header read by $1080A8):
//   +$00 word   rows the scroll reaches
//   +$02 long x4 offsets of tile planes 0-3 (relocated by the loader)
//   +$12 long   offset of the tile map
//   +$16        16 colour words ($0RGB)
// Tile n's row r in plane p is the byte at plane_p + (n / 2) * 16 + n % 2 + 2r
// ($109BA8), so the planes are 8 bytes per tile; the tile map is a word per
// cell, 36 per row ($109AF8 steps $48 bytes a row), up to the end.
CourseMap decode_course_map(std::span<const std::uint8_t> packed) {
    const auto bytes = amiga::unpack_byte_run1(packed, unpack_limit);
    if (bytes.size() < 0x36) {
        bad("tile map header");
    }
    CourseMap map;
    map.rows = read_be16(bytes, 0);
    std::array<std::size_t, 4> planes{};
    for (std::size_t p = 0; p < 4; ++p) {
        planes[p] = read_be32(bytes, 2 + p * 4);
    }
    const std::size_t cells_at = read_be32(bytes, 0x12);
    const std::size_t plane_size = planes[1] - planes[0];
    if (planes[0] < 0x36 || plane_size == 0 || plane_size % 8 != 0 || planes[3] + plane_size > cells_at ||
        cells_at > bytes.size()) {
        bad("tile map layout");
    }
    for (std::size_t c = 0; c < 16; ++c) {
        map.palette[c] = read_be16(bytes, 0x16 + c * 2);
    }
    map.tiles.resize(plane_size / 8);
    for (std::size_t n = 0; n < map.tiles.size(); ++n) {
        for (std::size_t p = 0; p < 4; ++p) {
            const std::size_t base = planes[p] + (n / 2) * 16 + n % 2;
            for (std::size_t r = 0; r < 8; ++r) {
                put_row(std::span(map.tiles[n]).subspan(r * 8, 8), std::span(bytes).subspan(base + 2 * r, 1), int(p));
            }
        }
    }
    for (std::size_t at = cells_at; at + 2 <= bytes.size(); at += 2) {
        map.cells.push_back(read_be16(bytes, at));
    }
    map.cells.resize(map.cells.size() / CourseMap::columns * CourseMap::columns);
    if (map.stored_rows() < map.rows) {
        bad("tile map rows");
    }
    for (const auto cell : map.cells) {
        if (cell >= map.tiles.size()) {
            bad("tile number");
        }
    }
    return map;
}

// The unpacked library (walked by $108224): a word with the number of
// cells, then a 20-byte descriptor per cell:
//   +0 byte type, +1 byte flags, +2 width in plane words, +4 rows,
//   +6 bytes per plane (width * 2 * rows), +8 offset of the pixels,
//   +$C mask (type 0, made at load), +$10 another offset (or 0).
// Sprites (type 1) store one guard row more than they show; the loader
// drops it when $1084C8 is set, which it is in this program.
ImageLibrary decode_image_library(std::span<const std::uint8_t> packed) {
    const auto bytes = amiga::unpack_byte_run1(packed, unpack_limit);
    if (bytes.size() < 2) {
        bad("image library header");
    }
    const std::size_t count = read_be16(bytes, 0);
    if (2 + count * 20 > bytes.size()) {
        bad("image library size");
    }
    ImageLibrary library;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 2 + i * 20;
        ImageCell cell;
        cell.type = bytes[at];
        cell.flags = bytes[at + 1];
        const int words = read_be16(bytes, at + 2);
        int rows = read_be16(bytes, at + 4);
        const std::size_t plane_size = read_be16(bytes, at + 6);
        const std::size_t pixels_at = read_be32(bytes, at + 8);
        cell.link = read_be32(bytes, at + 16);
        cell.width = words * 16;
        cell.planes = cell.type == 1 ? 2 : 4;
        const std::size_t row_bytes = std::size_t(words) * 2;
        if (plane_size != row_bytes * std::size_t(rows) || pixels_at + plane_size * std::size_t(cell.planes) > bytes.size()) {
            bad("image cell " + std::to_string(i));
        }
        if (cell.type == 1 && rows > 0) {
            --rows;
        }
        cell.height = rows;
        cell.pixels.assign(std::size_t(cell.width) * std::size_t(rows), 0);
        for (int y = 0; y < rows; ++y) {
            auto out = std::span(cell.pixels).subspan(std::size_t(y) * std::size_t(cell.width), std::size_t(cell.width));
            for (int p = 0; p < cell.planes; ++p) {
                const std::size_t row_at =
                    cell.type == 1 ? pixels_at + (std::size_t(y) * std::size_t(cell.planes) + std::size_t(p)) * row_bytes
                                   : pixels_at + std::size_t(p) * plane_size + std::size_t(y) * row_bytes;
                put_row(out, std::span(bytes).subspan(row_at, row_bytes), p);
            }
        }
        library.cells.push_back(std::move(cell));
    }
    return library;
}

Assets load_assets(const std::filesystem::path& content) {
    const auto read = [&](const char* name) {
        try {
            return platform::read_file(content / name);
        } catch (const std::exception&) {
            bad(std::string(name) + " is missing");
        }
    };
    Assets assets;
    for (std::size_t c = 0; c < course_count; ++c) {
        assets.maps[c] = decode_course_map(read(map_files[c]));
        assets.course_images[c] = decode_image_library(read(image_files[c]));
        assets.wall_covers[c] = decode_image_library(read(cover_files[c]));
    }
    assets.marble = decode_image_library(read("marbdat.vlb"));
    assets.slinky = decode_image_library(read("slink.vlb"));
    assets.acid = decode_image_library(read("ooze.vlb"));
    assets.bird = decode_image_library(read("birdink.vlb"));
    assets.splash = amiga::read_ilbm(read("splash.iff"));
    return assets;
}

} // namespace marble_madness::data
