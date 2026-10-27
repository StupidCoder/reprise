#pragma once
#include "amiga/iff.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Cannon Fodder's data files, decoded from an installed content folder
// (disk1/, disk2/, disk3/, see import/disk_import.hpp). Formats are described
// in .analysis/cannon_fodder_amiga/formats.md; everything here was read from
// the files themselves (sizes, rendered pictures), the game program's use of
// them is still to be traced.
namespace fodder::data {

using Palette = std::vector<amiga::Rgb4>;

// The installed files of the three disks. A name is looked up on the given
// disk first and then on the others (disks 2 and 3 share files; the recovered
// disk 2 lacks one that disk 3 holds).
class Content {
public:
    explicit Content(std::filesystem::path folder) : folder_(std::move(folder)) {}

    std::optional<std::filesystem::path> find(std::string_view name, int disk) const;
    std::vector<std::uint8_t> read(std::string_view name, int disk) const; // throws when missing
    // Names of the installed files on one disk, sorted.
    std::vector<std::string> names(int disk) const;

private:
    std::filesystem::path folder_;
};

// A full-screen picture: the RAW screens (title, presentations, the photo
// pages of the intro, "insert disk 2") and the IFF ILBM pictures.
//
// RAW layout: 2^planes colour words ($0RGB), then each bitplane in turn,
// 320 pixels (40 bytes) per line. Height = (size - colours) / (40 x planes):
// 257 lines for the screens, 256 for FONT.RAW.
amiga::IlbmPicture read_raw_screen(std::span<const std::uint8_t> file);

// A colour file: .pal (16 colours) or .pl8 (8 colours), $0RGB words.
Palette read_palette(std::span<const std::uint8_t> file);

// A tile set (.blk): 16 x 16 tiles of 4 bitplanes, 128 bytes each, every
// plane's 16 rows (one word each) in turn. Base sets hold 240 tiles, the
// "sub" sets 159.
struct Tile {
    std::array<std::uint8_t, 256> pixels{}; // colour index, row by row
};
std::vector<Tile> read_tile_set(std::span<const std::uint8_t> file);

// A mission map (mapm<N>.map):
//   $00 base tile set name (16 bytes), $10 sub tile set name (16 bytes),
//   $20-$4F not used here (leftovers of the editor, hypothesis),
//   $50 "cfed", $54 width, $56 height (in tiles), $58-$5F unknown,
//   $60 width x height words, row by row.
// Cell bits 0-8 select the tile: 0-239 base set, 240 and up the sub set
// (checked by rendering every map). Bits 13-15 are flags whose meaning is
// still open (seen: $2000, $4000, $6000, $8000, $A000).
struct MissionMap {
    std::string base_set, sub_set; // "junbase.blk", "junsub0.blk"
    int width{}, height{};
    std::vector<std::uint16_t> cells;

    int tile(int x, int y) const { return cells[std::size_t(y * width + x)] & 0x1ff; }
};
MissionMap read_map(std::span<const std::uint8_t> file);

// The sprite placements of a map (mapm<N>.spt): 10-byte records, read raw
// until their fields are traced (hypothesis: u16 ?, u16 ?, x, y, type).
struct Placement {
    std::array<std::uint16_t, 5> words{};
};
std::vector<Placement> read_placements(std::span<const std::uint8_t> file);

// Map number -> its disk: missions 1-36 are on disk 2, 37-72 on disk 3.
int map_disk(int number);

} // namespace fodder::data
