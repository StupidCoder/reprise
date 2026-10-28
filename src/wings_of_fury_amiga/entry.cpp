#include "wings_of_fury_amiga/entry.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/data/preview.hpp"
#include "wings_of_fury_amiga/game/module.hpp"
#include "wings_of_fury_amiga/import/disk_import.hpp"
#include <string>

namespace wings {
namespace {

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    for (const auto& picture : assets.pictures) {
        result.push_back({"Picture: " + picture.name, picture_image(picture.picture)});
    }
    for (const auto& library : assets.libraries) {
        const auto palette_name = preview_palette_for(library.name);
        result.push_back({"Shapes: " + library.name + " (" + std::to_string(library.library.shapes.size()) +
                              ", colours of " + std::string(palette_name) + ")",
                          library_sheet(library.library, find_palette(assets, palette_name))});
    }
    for (const auto& map : assets.maps) {
        result.push_back({"Map: " + map.name + " (scenery)", map_image(assets, map, find_palette(assets, "wingspalette"))});
    }
    for (const auto& palette : assets.palettes) {
        result.push_back({"Palette: " + palette.name, palette_image(palette.colours)});
    }
    for (const auto& sample : assets.samples) {
        result.push_back({"Sound: " + sample.name, sample_image(sample)});
    }
    return result;
}

} // namespace

app::GameEntry entry() {
    return {
        .id = "wings_of_fury_amiga",
        .title = "Wings of Fury",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .start = game::start,
        .progress = "The whole game: the story, rank selection, the briefings and all fifteen missions - islands "
                    "with huts, dugouts, pillboxes and soldiers, airfields, destroyers, battleships, cruise ships "
                    "and the Japanese carrier, torpedo bombers and fighters, night missions - with the fireworks "
                    "of a promotion, game over and the hall of fame, the music and sound effects, and the saved "
                    "games (RETURN FROM R&R; Ctrl-G on deck). The original's keys: Ctrl-R ends the game, Ctrl-V "
                    "its version, Ctrl-F swaps up and down, Ctrl-C clears the high scores, and its debug mode "
                    "(type colin). Options: VHS look and cheats (infinite lives, fuel and ammo, bullet proof, "
                    "enemy duds); quick save.",
    };
}

} // namespace wings
