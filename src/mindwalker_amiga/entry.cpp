#include "mindwalker_amiga/entry.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/data/preview.hpp"
#include "mindwalker_amiga/game/module.hpp"
#include "mindwalker_amiga/import/disk_import.hpp"
#include <string>

namespace mindwalker {
namespace {

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    result.push_back({"Title picture (MindB)", ham_image(assets.title)});
    for (std::size_t i = 0; i < assets.portraits.size(); ++i) {
        result.push_back({"Portrait " + std::to_string(i + 1) + " (MindF" + std::to_string(i + 1) + ")",
                          portrait_image(assets.portraits[i])});
    }
    for (const auto& set : assets.figures) {
        result.push_back({set.name, frame_sheet(set.frames)});
    }
    result.push_back({"Shared icons", frame_sheet(assets.icons, {0x000, 0xFFF})});
    result.push_back({"Tunnel hands", frame_sheet({assets.hands[0], assets.hands[1]}, hands_palette(assets))});
    result.push_back({"Arena images", frame_sheet(assets.arena_images, assets.arena_palette)});
    result.push_back({"Landscape palette", palette_image(assets.landscape_palette)});
    result.push_back({"Tunnel palette", palette_image(assets.tunnel_palette)});
    result.push_back({"Arena palette", palette_image(assets.arena_palette)});
    result.push_back({"Psyche palette", palette_image(assets.psyche_palette)});
    for (const auto& sample : assets.samples) {
        // The game sets each sound's period when it plays it; the viewer
        // plays them at period 428 (8,287 Hz on a PAL Amiga).
        result.push_back({"Sound: " + sample.name + " (played at period 428)", sample_image(sample),
                          app::PreviewSound::signed8(sample.data, 3546895.0 / 428)});
    }
    return result;
}

} // namespace

app::GameEntry entry() {
    return {
        .id = "mindwalker_amiga",
        .title = "Mindwalker",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .start = game::start,
        .progress = "The title, the world menu and the new game, matching the original frame for frame, and the "
                    "landscape's opening view of the generated world. Not rebuilt yet: walking in the landscape, "
                    "the other stages, the high scores, saving and the pause box.",
    };
}

} // namespace mindwalker
