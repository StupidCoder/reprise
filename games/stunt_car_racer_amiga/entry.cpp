#include "stunt_car_racer_amiga/entry.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/preview.hpp"
#include "stunt_car_racer_amiga/game/module.hpp"
#include "stunt_car_racer_amiga/import/disk_import.hpp"
#include "stunt_car_racer_amiga/render/race_screen.hpp"
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
    // The race screen where the car is lowered onto each circuit.
    const auto scenery = data::load_scenery(assets.program);
    const auto art = data::load_cockpit_art(assets.program);
    for (const auto& track : circuits.tracks)
        result.push_back({"Race screen: " + track.name, render::start_screen(assets.program, circuits, scenery, art, track)});
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
        .start = game::start,
        .progress = "A whole practice race on Little Ramp with the game's own car physics: the crane, laps and lap times, going over the edge and the crane's rescue, damage and wrecking, dust and smoke. A race against Jumpin' Jack is in the options; the rest of the league is not rebuilt yet.",
    };
}

} // namespace stuntcar
