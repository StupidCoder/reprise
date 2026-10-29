#include "spindizzy_worlds_amiga/entry.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/data/preview.hpp"
#include "spindizzy_worlds_amiga/game/module.hpp"
#include "spindizzy_worlds_amiga/import/disk_import.hpp"
#include <string>

namespace spindizzy {
namespace {

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    result.push_back({"Title picture", title_image(assets.title)});
    for (const auto& world : assets.worlds) {
        if (world.levels.empty()) continue; // world files 29 and 30 are empty
        const std::string name = "World " + std::to_string(world.number) + ": " + world.name;
        result.push_back({name + " (" + std::to_string(world.levels.size()) + " levels, seen from above)",
                          world_image(world, assets.block_attributes)});
        for (const auto& [id, record] : world.levels) {
            result.push_back({name + ", level " + std::to_string(id), level_image(world, id, assets)});
        }
    }
    return result;
}

} // namespace

// Editorial sources: docs/catalog-history.md. Artwork is interpretive.
app::GameEntry entry() {
    return {
        .id = "spindizzy_worlds_amiga",
        .title = "Spindizzy Worlds",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .start = game::start,
        .progress = "The whole game from the title to both endings: the attract pages, the menu, both clusters "
                    "and their turning maps, every level of the 30 worlds with all their objects, clues, the panel and "
                    "the fuel gauge, sound effects and music, the world's break-up, statistics and saves. Replayed "
                    "tick for tick against the original over 13 recorded runs (the whole Tutorial among them). "
                    "Options: widescreen up to 21:9, smooth motion and the VHS look.",
        .release_year = 1990,
        .description = "Guide a spinning explorer through a world of precarious platforms and geometric puzzles. Balance speed with precision as you negotiate narrow paths, ramps and the drops beyond their edges.",
        .history_title = "A world of geometric puzzles",
        .history = "The Amiga edition arrived in 1990, published by Electric Dreams. Its credits name Paul Shirley as designer and programmer, Debbie Sorrell for graphics, and Martin Walker for music.",
        .history_sources = "MobyGames: Amiga release; Lemon Amiga: development credits.",
    };
}

} // namespace spindizzy
