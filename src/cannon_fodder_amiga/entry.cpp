#include "cannon_fodder_amiga/entry.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/module.hpp"
#include "cannon_fodder_amiga/import/disk_import.hpp"

namespace fodder {

app::GameEntry entry() {
    return {
        .id = "cannon_fodder_amiga",
        .title = "Cannon Fodder",
        .machine = app::Machine::amiga,
        .media = "three disk images (ADF), one after another or together",
        .import = import::import_disk,
        .previews = [](const std::filesystem::path& content) { return data::previews(content); },
        .start = [](const std::filesystem::path& content) { return game::start_game(content, 0); },
        .progress = "The first ten maps play with the mouse (left button walks, right fires, both throw a grenade or "
                    "fire a rocket; the panel's icons choose grenade or rocket), with the original's music and sound "
                    "effects. No front end, briefing, recruits or vehicles yet.",
    };
}

} // namespace fodder
