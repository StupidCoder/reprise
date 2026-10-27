#pragma once
#include "amiga/hunk.hpp"
#include "amiga/iff.hpp"
#include "wings_of_fury_amiga/data/shapes.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Wings of Fury's pictures, palettes, shape libraries, maps, sound samples and
// texts, decoded from an installed content folder (.analysis/
// wings_of_fury_amiga/formats.md has the layouts and the code that reads each
// one).
namespace wings::data {

using Rgb4 = amiga::Rgb4; // $0RGB, four bits per gun
using Palette = std::vector<Rgb4>;

struct NamedPicture {
    std::string name;
    amiga::IlbmPicture picture;
};

struct NamedPalette {
    std::string name;
    Palette colours;
};

struct NamedLibrary {
    std::string name;
    ShapeLibrary library;
};

// 8-bit signed samples, played as they are stored.
struct Sample {
    std::string name;
    std::vector<std::int8_t> data;
};

// A mission map as `wings` $102B1A loads it: a u32 file size, a u32 that
// places the carrier (x = value x 4 - 8, the player's start; seen in the
// reference machine for a.map) and then one 16-bit word per 8-pixel column
// of the world, read by the scenery drawer $1037BC:
//   bit 15       draw the column's shape here (a shape spans several columns;
//                only one of them carries the bit)
//   bits 11-13   x 4: the shape's drop below the scenery line ($97)
//   bits 2-10    index into the scenery table (SceneryEntry)
//   bits 0-1     the surface under the column ($104EE8; 2 on islands, 1 on
//                the carrier, where shapes follow the ship's bob, $104EAC)
// Index $114/$115 columns mark places the game fills from a per-rank list
// ($102C84); 5, $22, $9F and $113 get extra handling ($103B1C). Both open.
struct MissionMap {
    struct Column {
        bool draw{};
        int drop{};
        int index{};
        int surface{};
    };

    std::string name;
    std::uint32_t carrier_value{};
    std::vector<std::uint16_t> words;

    int carrier_x() const { return int(carrier_value) * 4 - 8; }
    Column column(std::size_t i) const {
        const int w = words[i];
        return {(w & 0x8000) != 0, (w >> 9) & 0x1C, (w >> 2) & 0x1FF, w & 3};
    }
};

// One entry of the scenery table $10535A builds: indices 0-183 name shapes of
// world.shp (name list $113E38), then the cruiser's (24, $113C24), the
// destroyer's (27, $113BB4), the Japanese carrier's (13, $113B7C) and the
// battleship's (25, $113B14). Names the library lacks ("barf", "bumb", the
// "lpn*" set) give no shape; their columns draw nothing.
struct SceneryEntry {
    std::string library;
    std::string shape;
};

struct HighScore {
    std::uint32_t score{};
    int rank{};
    std::string name;
};

struct Assets {
    amiga::LoadedImage program; // `wings` from $100000: tables are read from it
    std::vector<NamedPicture> pictures;
    std::vector<NamedPalette> palettes;
    std::vector<NamedLibrary> libraries;
    std::vector<MissionMap> maps;  // a.map ... o.map
    std::vector<SceneryEntry> scenery;
    std::array<int, 4> us_flag_steps{}; // `wings` $114BA1: the tower flag's frames per step
    std::vector<std::string> hellcat_names; // `wings` $113C88: hellcat.shp in the game's order
    std::vector<std::string> weapon_names;  // `wings` $1142F4: torpedo.shp in the game's order (-$40EE)
    std::array<int, 4> gun_flash_steps{};   // `wings` $114B5C
    std::vector<Sample> samples;
    std::vector<std::string> rank_names;  // `wings` $115864: seven pointers
    std::vector<std::string> story;       // the scrolling story, line by line
    std::vector<std::uint8_t> ticker_font; // `newarmyfont` as stored (game/ticker.hpp reads it)
    std::vector<HighScore> high_scores;
};

// The palette a library is previewed in (the game sets its colours per
// screen; this is the screen each library is drawn on, as far as known).
std::string_view preview_palette_for(std::string_view library);

// The shape a scenery index names, or nullptr. The 1/8-scale view uses the
// table $10535A builds alongside (-$407C): the same names looked up in
// 8thscale.shp.
const Shape* scenery_shape(const Assets&, int index);
const Shape* eighth_scale_shape(const Assets&, int index);

// The Hellcat library entry with this index in the game's list, or nullptr.
const Shape* hellcat_shape(const Assets&, int index);
// The weapons' pictures (torpedo.shp) by the game's index: bombs $40.., rockets
// $4C.. (burning) and $74.. (not yet lit), torpedoes $88/$89.
const Shape* weapon_shape(const Assets&, int index);

// Reads every decoded file of the content folder; throws std::runtime_error
// when one is missing or does not decode.
Assets load_assets(const std::filesystem::path& content);

} // namespace wings::data
