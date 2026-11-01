// BCH animation export (H3D-style curve groups): skeletal transform curves, baked matrix
// segments, visibility tracks and material animations, as package JSON.
#pragma once
#include "Bch.hpp"

#include <string>
#include <vector>

namespace captain_toad {

// Character clip (tools/character_animation.py): Hermite curves (96-bit and quantized 64-bit keys).
json::Value characterAnimation(const Bch& b, const std::string& name);
// Visibility tracks of the named clips, keyed by track name.
json::Value visibility(const Bch& b, const std::vector<std::string>& names);
// Actor clip (tools/actor_export.py): transform curves and MtxTransform matrices.
json::Value actorAnimation(const Bch& b, const std::string& name);
// One curve at `cp` in the generic layout.
json::Value curve(const Bch& b, size_t cp);
// Material animation (table 9).
json::Value materialAnimation(const Bch& b, const std::string& name);
// Bone flag words of model `index`.
std::vector<uint32_t> boneFlags(const Bch& b, size_t index);

}   // namespace captain_toad
