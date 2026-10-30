#include "stunt_car_racer_amiga/entry.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/preview.hpp"
#include "stunt_car_racer_amiga/import/disk_import.hpp"

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
        .progress = "Import, the loading picture and the eight circuits as 3D models; nothing of the game can be played yet.",
    };
}

} // namespace stuntcar
