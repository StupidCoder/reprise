#include "rock_n_roll_amiga/entry.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"
#include "rock_n_roll_amiga/game/module.hpp"
#include "rock_n_roll_amiga/import/disk_import.hpp"
#include <string>

namespace rocknroll {
namespace {

std::string two_digits(int n) {
    return (n < 10 ? "0" : "") + std::to_string(n);
}

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    for (const auto& picture : assets.pictures) {
        result.push_back({"Picture: " + picture.name, picture_image(picture.picture)});
    }
    for (const auto& level : assets.levels) {
        result.push_back({"Level " + two_digits(level.number) + ": " + level.name + " (" + std::to_string(level.columns) +
                              " x " + std::to_string(level.rows) + ", world " + std::to_string(level.world) + ")",
                          level_image(level, assets.world(level.world), 8)});
    }
    for (const auto& world : assets.worlds) {
        if (world.number > 7) continue; // the title screen's set
        result.push_back({"World " + std::to_string(world.number) + ": graphics bank (tiles, animations, program graphics)",
                          bank_image(world.bank, world.tiles.palette)});
        result.push_back({"World " + std::to_string(world.number) + ": palette", palette_image(world.tiles.palette)});
    }
    return result;
}

} // namespace

app::GameEntry entry() {
    return {
        .id = "rock_n_roll_amiga",
        .title = "Rock 'n Roll",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .start = game::start,
        .progress = "Level play with the mouse: rolling, railings, ice, arrows, slopes, falls with the original's "
                    "continue/restart prompt, pickups, locks, shops, teleports, bridges, switches, crumbling floor and "
                    "the exit to the next level, matched to the original update for update. Not yet: front end, panel "
                    "numbers, items, music, some cells.",
    };
}

} // namespace rocknroll
