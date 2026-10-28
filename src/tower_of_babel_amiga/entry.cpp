#include "tower_of_babel_amiga/entry.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/data/model.hpp"
#include "tower_of_babel_amiga/data/shape.hpp"
#include "tower_of_babel_amiga/data/preview.hpp"
#include "tower_of_babel_amiga/import/disk_import.hpp"
#include <string>

namespace babel {
namespace {

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    result.push_back({"Title screen", title_image(assets.title)});
    // 3D shapes of the designer's object kinds, in Standard tower A's colours.
    const auto& reference = assets.groups.at(1).towers.at(0);
    for (const auto& entry : kind_shapes(assets.program)) {
        app::Preview preview{"Shape: " + object_kind_name(assets.program, entry.kind) + " (kind " +
                                 std::to_string(entry.kind) + ")",
                             platform::Image(1, 1)};
        try {
            preview.model = shape_model(decode_shape(assets.main, entry.address), assets.program, reference);
        } catch (const std::exception&) {
            continue; // kinds whose table entry names no shape
        }
        result.push_back(std::move(preview));
    }
    for (const auto& group : assets.groups) {
        for (std::size_t i = 0; i < group.towers.size(); ++i) {
            const auto& tower = group.towers[i];
            std::string title = group.name + " " + char('A' + int(i % 9)) + ": " +
                                (tower.name.empty() ? "(no name)" : tower.name);
            if (!tower.author.empty()) title += " (" + tower.author + ")";
            result.push_back({title, tower_image(tower, assets.program)});
            if (auto view = start_view_image(tower, assets.program)) result.push_back({title + ", start view", *view});
        }
    }
    return result;
}

} // namespace

app::GameEntry entry() {
    return {
        .id = "tower_of_babel_amiga",
        .title = "Tower of Babel",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .progress = "Disk import (the packed program unpacked, every data range checked) and decoded data: the "
                    "title screen and all 153 towers (117 standard, 9 tutorial, 18 demonstration and the empty "
                    "custom group) with their maps, objects and colours. Nothing playable yet.",
    };
}

} // namespace babel
