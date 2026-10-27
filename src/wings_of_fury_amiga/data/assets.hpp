#pragma once
#include "amiga/hunk.hpp"
#include "amiga/iff.hpp"
#include "wings_of_fury_amiga/data/shapes.hpp"
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

// A mission map as `wings` $102B1A loads it: a u32 file size, a u32 the game
// turns into the map's length (length x 4 - 8 into its globals) and then
// 16-bit words to the end of the file. The words' fields are only partly read
// so far (bits 2-10 an index, $114/$115 mark special places; formats.md).
struct MissionMap {
    std::string name;
    std::uint32_t length{};
    std::vector<std::uint16_t> words;
};

struct HighScore {
    std::uint32_t score{};
    int rank{};
    std::string name;
};

struct Assets {
    std::vector<NamedPicture> pictures;
    std::vector<NamedPalette> palettes;
    std::vector<NamedLibrary> libraries;
    std::vector<MissionMap> maps;  // a.map ... o.map
    std::vector<Sample> samples;
    std::vector<std::string> rank_names;  // `wings` $115864: seven pointers
    std::vector<std::string> story;       // the scrolling story, line by line
    std::vector<HighScore> high_scores;
};

// The palette a library is previewed in (the game sets its colours per
// screen; this is the screen each library is drawn on, as far as known).
std::string_view preview_palette_for(std::string_view library);

// Reads every decoded file of the content folder; throws std::runtime_error
// when one is missing or does not decode.
Assets load_assets(const std::filesystem::path& content);

} // namespace wings::data
