// Generic stage packages: any course of the game, built from the verified copy of the ROM's file
// system when it is first played, in the same package format as the opening (the session loads
// both the same way).
//
// What a stage package holds (phase 0 of the full-game work, see .analysis/docs/stages.md):
//   * every placed object that has a model, drawn in its rest pose where the game placed it:
//     map parts, blocks, items and enemies alike (their behaviour is not reconstructed yet);
//   * the collision of every object that has a KCL, for the recovered player collider;
//   * the stage's sky, scene lights, lighting tables, depth-shadow parameters and camera tickets;
//   * the protagonist with the recovered player movement.
// The objects without a model (areas, switches, rails and other logic objects) and the models
// that are not found are listed in the package's report.json.
//
// Asset names follow the game's loader (recovered):
//   * InitSharedAssets.byml "SharedStage" in the stage's design archive names the stage whose
//     assets are used (0x253a50);
//   * the scene lights are ObjectData/<asset stage without "Stage">Scene (0x22cc08);
//   * the stage texture archive is ObjectData/<asset stage without "Stage">Textures, the cube
//     map ObjectData/CubeMap<asset stage>, else ObjectData/CubeMapDefault (0x253a50).
#pragma once
#include "Inputs.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace captain_toad {

// Stage package format version (scene.json "stage_schema"); older packages are rebuilt.
constexpr int kStageSchema = 66;   // 66: water reads the drawn view's half of a side-by-side copy

struct StageAssets {
    std::string stage;        // the course's stage name, e.g. "PullUpStage"
    std::string assetStage;   // SharedStage or the stage itself
    std::string base;         // assetStage without its trailing "Stage"
};
StageAssets stageAssets(Inputs& in, const std::string& stage);

// Builds the package of `stage` into `out` (which must not exist). `progress` as for the import.
void exportStage(Inputs& in, const std::string& stage, const std::filesystem::path& out,
                 const std::function<void(double, const std::string&)>& progress = {});

}   // namespace captain_toad
