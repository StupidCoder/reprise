#include "marble_madness_amiga/entry.hpp"
#include "marble_madness_amiga/data/assets.hpp"
#include "marble_madness_amiga/data/preview.hpp"
#include "marble_madness_amiga/game/module.hpp"
#include "marble_madness_amiga/import/disk_import.hpp"
#include <string>

namespace marble_madness {
namespace {

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    result.push_back({"Title picture (c/splash)", splash_image(assets)});
    for (int c = 0; c < course_count; ++c) {
        const auto course = Course(c);
        const auto& palette = assets.maps[std::size_t(c)].palette;
        const std::string name = course_name(course);
        result.push_back({name + " course", course_image(assets, course)});
        result.push_back({name + " tiles", tile_sheet(assets, course)});
        result.push_back({name + " objects", library_sheet(assets.course_images[std::size_t(c)], palette)});
        result.push_back({name + " wall covers", library_sheet(assets.wall_covers[std::size_t(c)], palette, 800)});
    }
    const auto& palette = assets.maps[0].palette;
    result.push_back({"Marble and score pop-ups", library_sheet(assets.marble, palette)});
    result.push_back({"Slinky", library_sheet(assets.slinky, palette)});
    result.push_back({"Acid pools", library_sheet(assets.acid, palette)});
    result.push_back({"Bird", library_sheet(assets.bird, palette)});
    return result;
}

} // namespace

app::GameEntry entry() {
    return {
        .id = "marble_madness_amiga",
        .title = "Marble Madness",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .start = game::start,
        .progress = "All six courses and the one-player game around them (options, course introductions, bonuses, "
                    "the end), matching the original update for update. Not rebuilt yet: two players racing at "
                    "once and mouse control.",
    };
}

} // namespace marble_madness
