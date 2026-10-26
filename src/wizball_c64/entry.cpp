#include "wizball_c64/entry.hpp"
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/data/preview.hpp"
#include "wizball_c64/import/disk_import.hpp"
#include <string>

namespace wizball {
namespace {
std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    for (int level = 0; level < Assets::level_count; ++level) {
        const auto name = "Level place " + std::to_string(level);
        result.push_back({name + ", grey", level_image(assets, level, false, 3)});
        result.push_back({name + ", painted", level_image(assets, level, true, 3)});
    }
    for (int place = Assets::level_count; place < int(assets.places.size()); ++place) {
        result.push_back({"Place " + std::to_string(place) + ", map columns 90-95", place_image(assets, place)});
    }
    result.push_back({"Tiles (level place 0 characters)", tile_sheet(assets, 0)});
    result.push_back({"Playfield characters (level place 0)", play_charset_sheet(assets, 0)});
    result.push_back({"Front-end characters", front_charset_sheet(assets)});
    result.push_back({"Sprites", sprite_sheet(assets)});
    return result;
}
} // namespace

app::GameEntry entry() {
    return {
        .id = "wizball_c64",
        .title = "Wizball",
        .machine = app::Machine::c64,
        .media = "the disk image (G64)",
        .import = import::import_disk,
        .previews = previews,
        .progress = "Import and the decoded levels, characters and sprites; the game itself is not rebuilt yet.",
    };
}

} // namespace wizball
