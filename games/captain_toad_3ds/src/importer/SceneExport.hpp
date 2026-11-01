// Builds the viewer's scene package from the ROM: terrain and collision, sky, lights and camera,
// shadows, the player character, PICA material lighting, the headlamp/leaf effects, the opening
// actors, the player and opening data, the 2D layouts, the particle runtime data and the sound
// data. A port of the research exporters (tools/export_*.py); the package layout is unchanged.
#pragma once
#include "Inputs.hpp"
#include "Json.hpp"

#include <filesystem>
#include <functional>

namespace captain_toad {

using ProgressFn = std::function<void(double fraction, const std::string& step)>;

void exportPackage(Inputs& in, const std::filesystem::path& out, const ProgressFn& progress);

// The particle runtime data every package shares (the ROM PTCL, the generated runtime tables, the
// emitter textures and the particle vertex programs), written into `dir`; the returned file names
// carry `prefix` (their path relative to the package that refers to them).
struct ParticleCommon {
    json::Value ptcl, tables, textures, sets, shaders, sha;
};
ParticleCommon writeParticleCommon(Inputs& in, const std::filesystem::path& dir, const std::string& prefix);

}   // namespace captain_toad
