#include "fort_apocalypse_c64/entry.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/preview.hpp"
#include "fort_apocalypse_c64/game/module.hpp"
#include "fort_apocalypse_c64/import/tape_import.hpp"
#include <string>

namespace fort_apocalypse {
namespace {
std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    result.push_back({"HUD (top screen rows)", hud_image(assets, 0)});
    for (int level = 0; level < 2; ++level) {
        const auto n = std::to_string(level + 1);
        result.push_back({"Level " + n + " terrain", terrain_image(assets, level)});
        result.push_back({"Level " + n + " radar picture", radar_image(assets, level)});
    }
    result.push_back({"Helicopter poses and rotor frames", helicopter_sheet(assets, 7)});
    result.push_back({"Playfield characters", charset_sheet(assets.playfield_charset,
                                                            play_colours(assets, 0))});
    result.push_back({"HUD characters", charset_sheet(assets.hud_charset, play_colours(assets, 0))});
    return result;
}
} // namespace

app::GameEntry entry() {
    return {
        .id = "fort_apocalypse_c64",
        .title = "Fort Apocalypse",
        .machine = app::Machine::c64,
        .media = "the tape image (TAP)",
        .import = import::import_tape,
        .previews = previews,
        .start = game::start,
        .progress = "Title and options screens rebuilt; the game itself comes next.",
    };
}

} // namespace fort_apocalypse
