#pragma once
#include "amiga/iff.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Marble Madness's data files, decoded from an installed content folder
// (.analysis/marble_madness_amiga/formats.md has the layouts and the code
// that reads each one).
namespace marble_madness::data {

using amiga::Rgb4;
using Palette = std::array<Rgb4, 16>;

// The six courses in the program's order (the name tables at $1034FC on).
enum class Course { practice, beginner, intermediate, aerial, silly, ultimate };
constexpr int course_count = 6;
const char* course_name(Course);

// An 8 x 8 tile, one colour index (0-15) per pixel, row by row.
using Tile = std::array<std::uint8_t, 64>;

// A course's look: its tile set, the tile map and the playfield palette
// (<course>.mlb, loaded by $1080A8, drawn by $109AF8/$109BA8).
struct CourseMap {
    static constexpr int columns = 36; // 288 pixels; the tile map's rows are 72 bytes
    int rows{};                        // rows the scroll can reach (header word)
    std::vector<std::uint16_t> cells;  // tile numbers, row by row; may hold more rows than `rows`
    std::vector<Tile> tiles;
    Palette palette{};

    int stored_rows() const { return int(cells.size()) / columns; }
    std::uint16_t cell(int row, int column) const { return cells[std::size_t(row * columns + column)]; }
};

// One cell of an image library (.ilb/.vlb, loaded by $108224): a frame of an
// object. Type 1 cells are hardware-sprite images (2 planes, 16 pixels per
// plane word, each row's plane words together); type 0 cells are scenery
// drawn into the playfield (4 planes, one plane after the other), whose
// silhouette the loader ORs into a mask for hiding the marble behind them.
struct ImageCell {
    int type{};
    int flags{};
    int width{};  // pixels (16 per plane word)
    int height{}; // rows (for sprites without the stored guard row)
    int planes{};
    std::vector<std::uint8_t> pixels; // colour index per pixel (0 = clear)
    std::uint32_t link{};             // descriptor +$10 (an offset into the library, 0 if none)
};

struct ImageLibrary {
    std::vector<ImageCell> cells;
};

struct Assets {
    std::array<CourseMap, course_count> maps;
    std::array<ImageLibrary, course_count> course_images; // bank 0: obstacles (Silly.ilb for Ultimate too)
    std::array<ImageLibrary, course_count> wall_covers;   // bank 5: <course>obsc.vlb
    ImageLibrary marble;                                  // bank 1: marbdat.vlb
    ImageLibrary slinky, acid, bird;                      // banks 2-4: slink, ooze, birdink .vlb
    amiga::IlbmPicture splash;
};

// Reads every decoded file of the content folder; throws std::runtime_error
// when one is missing or does not decode.
Assets load_assets(const std::filesystem::path& content);

CourseMap decode_course_map(std::span<const std::uint8_t> packed);
ImageLibrary decode_image_library(std::span<const std::uint8_t> packed);

} // namespace marble_madness::data
