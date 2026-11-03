// A level's lighting set-up: the directional light (art_light_directional), the ambient light
// (art_light_ambient), the sky and its image-based lighting (art_sky_ibl) and the scene
// parameters (art_spx: data/multi_platformer/spx/<SceneParamName>.spx over spx/default.xml).
// Findings: .analysis/docs/rendering.md.
#pragma once

#include "game/LevelDocument.hpp"
#include "math/Vector.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace beacon::game {

// Scene parameters by flattened name ("DirShadowFar", "Pipeline_Bloom_Scale", ...), each a list of
// floats (Bool / Enum / Float / Vector4 alike).
class SceneParams {
public:
    bool load(const std::filesystem::path& root, const std::string& name);
    float number(const std::string& key, float fallback = 0) const;
    math::Vec3 vec3(const std::string& key, const math::Vec3& fallback = {}) const;

private:
    std::map<std::string, std::vector<float>> values_;
};

struct SceneLighting {
    bool hasSun = false;
    math::Vec3 sunDirection{0, 1, 0};   // towards the light (unit)
    math::Mat4 sunRotation = math::identity();   // the light's rotation (its transform without translation)
    math::Vec3 sunColor{1, 1, 1};       // Color x Intensity
    math::Vec3 ambient;                 // Color x Intensity
    // The sky: its model (drawn around the view) and the cube its image-based lighting samples.
    std::string skyModel;
    std::string iblTexture;
    bool enableSky = true, enableIbl = true;
    float iblIntensity = 1;
    math::Vec3 iblColor{1, 1, 1};
    math::Vec3 iblCentre;
    float iblRadius = 100;
    SceneParams params;
};

SceneLighting loadSceneLighting(const Level& level, const std::filesystem::path& root);

}   // namespace game
