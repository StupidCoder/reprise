#include "wings_of_fury_amiga/data/assets.hpp"
#include "amiga/adf.hpp"
#include "platform/files.hpp"
#include "wings_of_fury_amiga/data/packed.hpp"
#include <array>
#include <stdexcept>

namespace wings::data {
namespace {

// `wings` laid out from $100000, hunks 8 bytes apart (as in the research
// notes, .analysis/wings_of_fury_amiga/work/bases.txt).
constexpr std::uint32_t wings_base = 0x100000;

constexpr std::array picture_files = {"wingstitle", "broderbund",  "creditscreen", "selectrank", "rank.iff",
                                      "iff-dash",   "nightdash",   "hiscore.iff",  "hiscoreslab"};
// Palettes stored as a bare CMAP chunk (whose length field is not filled in:
// the game reads 32 colours).
constexpr std::array bare_palette_files = {"wingspalette", "night.p", "nightocean.p"};
// Palettes stored as tiny (sometimes packed) ILBM pictures.
constexpr std::array picture_palette_files = {"palette", "ocean.p", "ocean.palette"};
constexpr std::array library_files = {"world.shp",      "hellcat.shp",    "japplane.shp", "torpedo.shp",
                                      "8thscale.shp",   "battleship.shp", "cruiseship.shp", "destroyer.shp",
                                      "japcarrier.shp", "dash.shp",       "nightdash.shp", "selectrank.shp"};
constexpr std::array sample_files = {"boom",       "screech",       "scream",  "splash",
                                     "machinegun", "metal.clang.1", "grind.1", "engine"};

// The texts in `wings`: rank names (pointer list $115864, seven entries) and
// the story the title sequence scrolls ($107E80 starts at the pointer at
// $1158E0 and shows 0x35 zero-terminated lines).
constexpr std::uint32_t rank_name_pointers = 0x115864;
constexpr int rank_count = 7;
constexpr std::uint32_t story_pointer = 0x1158E0;
constexpr int story_lines = 0x35;
constexpr std::uint32_t us_flag_steps = 0x114BA1; // -$63B1(A4), read by $103D10

// The scenery table's parts in order ($10535A copies 184, 24, 27, 13 and 25
// entries; the loaders pair each library with its name list).
struct SceneryPart {
    const char* library;
    std::uint32_t names;
    int count;
};
constexpr std::array scenery_parts = {
    SceneryPart{"world.shp", 0x113E38, 184},     SceneryPart{"cruiseship.shp", 0x113C24, 24},
    SceneryPart{"destroyer.shp", 0x113BB4, 27},  SceneryPart{"japcarrier.shp", 0x113B7C, 13},
    SceneryPart{"battleship.shp", 0x113B14, 25},
};

// highscore: ten 36-byte entries (u32 score, u16 rank, 30-byte name).
constexpr std::size_t score_entry_size = 36;

std::runtime_error damaged(const std::string& file) {
    return std::runtime_error("The installed Wings of Fury data is damaged (" + file +
                              "); please import the disk again.");
}

std::vector<std::uint8_t> read(const std::filesystem::path& content, const std::string& name) {
    try {
        return platform::read_file(content / name);
    } catch (const std::exception&) {
        throw damaged(name);
    }
}

std::string string_at(const amiga::LoadedImage& image, std::uint32_t address) {
    std::string text;
    for (std::uint8_t c; (c = image.u8(address)) != 0; ++address) {
        text += char(c);
    }
    return text;
}

Palette bare_palette(std::span<const std::uint8_t> file, const std::string& name) {
    constexpr int colours = 32;
    if (file.size() < 8 + colours * 3 || file[0] != 'C' || file[1] != 'M' || file[2] != 'A' || file[3] != 'P') {
        throw damaged(name);
    }
    Palette palette;
    for (int i = 0; i < colours; ++i) {
        const auto* rgb = &file[std::size_t(8 + i * 3)];
        palette.push_back(Rgb4((rgb[0] >> 4) << 8 | (rgb[1] >> 4) << 4 | rgb[2] >> 4));
    }
    return palette;
}

} // namespace

std::string_view preview_palette_for(std::string_view library) {
    if (library == "nightdash.shp") return "nightdash";
    if (library == "dash.shp") return "iff-dash";
    if (library == "selectrank.shp") return "selectrank";
    return "wingspalette";
}

const Shape* scenery_shape(const Assets& assets, int index) {
    if (index < 0 || std::size_t(index) >= assets.scenery.size()) {
        return nullptr;
    }
    const auto& entry = assets.scenery[std::size_t(index)];
    for (const auto& library : assets.libraries) {
        if (library.name == entry.library) {
            return library.library.find(entry.shape);
        }
    }
    return nullptr;
}

Assets load_assets(const std::filesystem::path& content) {
    Assets assets;
    for (const std::string name : picture_files) {
        try {
            assets.pictures.push_back({name, amiga::read_ilbm(unpack_file(read(content, name)))});
        } catch (const std::runtime_error&) {
            throw damaged(name);
        }
    }
    for (const std::string name : bare_palette_files) {
        assets.palettes.push_back({name, bare_palette(read(content, name), name)});
    }
    for (const std::string name : picture_palette_files) {
        try {
            assets.palettes.push_back({name, amiga::read_ilbm(unpack_file(read(content, name))).palette});
        } catch (const std::runtime_error&) {
            throw damaged(name);
        }
    }
    for (const std::string name : library_files) {
        try {
            assets.libraries.push_back({name, read_shape_library(read(content, name))});
        } catch (const std::runtime_error&) {
            throw damaged(name);
        }
    }
    for (char letter = 'a'; letter <= 'o'; ++letter) {
        const std::string name = std::string(1, letter) + ".map";
        const auto file = read(content, name);
        if (file.size() < 8 || amiga::read_be32(file, 0) != file.size()) {
            throw damaged(name);
        }
        MissionMap map{name, amiga::read_be32(file, 4), {}};
        for (std::size_t at = 8; at + 1 < file.size(); at += 2) {
            map.words.push_back(amiga::read_be16(file, at));
        }
        assets.maps.push_back(std::move(map));
    }
    for (const std::string name : sample_files) {
        const auto file = read(content, name);
        assets.samples.push_back({name, {file.begin(), file.end()}});
    }
    try {
        const amiga::LoadedImage wings(amiga::read_load_file(read(content, "wings")), wings_base);
        for (int r = 0; r < rank_count; ++r) {
            assets.rank_names.push_back(string_at(wings, wings.u32(rank_name_pointers + std::uint32_t(r * 4))));
        }
        for (const auto& part : scenery_parts) {
            for (int i = 0; i < part.count; ++i) {
                std::string shape;
                for (std::uint32_t c = 0; c < 4; ++c) {
                    shape += char(wings.u8(part.names + std::uint32_t(i * 4) + c));
                }
                assets.scenery.push_back({part.library, std::move(shape)});
            }
        }
        for (std::size_t i = 0; i < assets.us_flag_steps.size(); ++i) {
            assets.us_flag_steps[i] = wings.u8(us_flag_steps + std::uint32_t(i));
        }
        std::uint32_t line = wings.u32(story_pointer);
        for (int i = 0; i < story_lines; ++i) {
            assets.story.push_back(string_at(wings, line));
            line += std::uint32_t(assets.story.back().size() + 1);
        }
    } catch (const std::runtime_error&) {
        throw damaged("wings");
    }
    const auto scores = read(content, "highscore");
    for (std::size_t at = 0; at + score_entry_size <= scores.size(); at += score_entry_size) {
        HighScore score{amiga::read_be32(scores, at), amiga::read_be16(scores, at + 4), {}};
        for (std::size_t i = at + 6; i < at + score_entry_size && scores[i] != 0; ++i) {
            score.name += char(scores[i]);
        }
        assets.high_scores.push_back(std::move(score));
    }
    return assets;
}

} // namespace wings::data
