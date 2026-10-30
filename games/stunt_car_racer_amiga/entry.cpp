#include "stunt_car_racer_amiga/entry.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/preview.hpp"
#include "stunt_car_racer_amiga/import/disk_import.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"

namespace stuntcar {
namespace {

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    const auto assets = data::load_assets(content);
    std::vector<app::Preview> result{{"Loading picture", data::to_image(assets.loading_picture)}};
    const auto circuits = data::load_circuits(assets.program);
    for (const auto& track : circuits.tracks) {
        auto model = data::circuit_model(circuits, track);
        app::Preview preview{"Circuit: " + track.name, data::circuit_image(*model)};
        preview.model = std::move(model);
        result.push_back(std::move(preview));
    }
    // The view from the car where it is lowered onto each circuit.
    const auto scenery = data::load_scenery(assets.program);
    const auto palette = render::race_palette(assets.program);
    for (const auto& track : circuits.tracks) {
        const auto view = render::draw_race_view(circuits, scenery, track, track.car_start_segment,
                                                 render::start_camera(circuits, track));
        result.push_back({"Race view: " + track.name, render::to_image(view, palette)});
    }
    return result;
}

} // namespace

// Year, description and history come from app/catalog.cpp (docs/catalog-history.md).
app::GameEntry entry() {
    return {
        .id = "stunt_car_racer_amiga",
        .title = "Stunt Car Racer",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .progress = "Import, the loading picture, the eight circuits as 3D models and the race view from each start; nothing of the game can be played yet.",
    };
}

} // namespace stuntcar
