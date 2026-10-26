#include "bruce_lee_c64/entry.hpp"
#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/data/preview.hpp"
#include "bruce_lee_c64/game/module.hpp"
#include "bruce_lee_c64/import/disk_import.hpp"
#include <string>

namespace bruce_lee {
namespace {
std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    result.push_back({"Loading picture", loading_image(assets)});
    for (int room = 0; room < int(assets.rooms.size()); ++room) {
        result.push_back({"Room " + std::to_string(room + 1), room_image(assets, room)});
    }
    for (int set = 0; set < int(assets.charsets.size()); ++set) {
        result.push_back({"Play characters, set " + std::to_string(set + 1), charset_sheet(assets, set)});
    }
    result.push_back({"Text characters (C64 ROM glyphs)", text_sheet(assets)});
    return result;
}
} // namespace

app::GameEntry entry() {
    return {
        .id = "bruce_lee_c64",
        .title = "Bruce Lee",
        .machine = app::Machine::c64,
        .media = "the disk image (G64)",
        .import = import::import_disk,
        .previews = previews,
        .start = game::start,
        .progress = "Playable from the title to the end and the next round, with sound, the demo and both two-player modes; compared with the original in scripted runs.",
    };
}

} // namespace bruce_lee
