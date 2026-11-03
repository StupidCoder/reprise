#include "game/SceneLighting.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace beacon::game {

namespace {

std::vector<float> parseNumbers(const std::string& text) {
    std::vector<float> out;
    std::istringstream in(text);
    float v;
    while (in >> v) {
        out.push_back(v);
    }
    return out;
}

// Colour attributes are gamma-encoded; lighting runs on linear values: the engine's
// directional light constants (run roof_cloud_fs, lighting buffer [20..22]) hold
// (1, 0.800585, 0.531049) for the authored (1, 0.9038, 0.75), i.e. c^2.2.
float srgbToLinear(float c) {
    return std::pow(std::max(c, 0.0f), 2.2f);
}

std::optional<data::XmlNode> readXml(const std::filesystem::path& path) {
    data::Bytes bytes;
    if (!data::loadFile(path, bytes)) {
        return std::nullopt;
    }
    return data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// A level's .spx nests its overrides (<Pipeline><Bloom><Scale>); the definition names them flat
// (Pipeline_Bloom_Scale).
void flatten(const data::XmlNode& n, const std::string& prefix, std::map<std::string, std::vector<float>>& out) {
    for (const auto& c : n.children) {
        const std::string key = prefix.empty() ? c.name : prefix + "_" + c.name;
        if (c.children.empty()) {
            out[key] = parseNumbers(c.text);
        } else {
            flatten(c, key, out);
        }
    }
}

}   // namespace

bool SceneParams::load(const std::filesystem::path& root, const std::string& name) {
    values_.clear();
    const auto dir = root / "data" / "multi_platformer" / "spx";
    if (auto def = readXml(dir / "default.xml")) {
        if (const data::XmlNode* attrs = def->child("Attributes")) {
            attrs->forEach("Attribute", [&](const data::XmlNode& a) {
                values_[a.childText("Name", "")] = parseNumbers(a.childText("Value", ""));
            });
        }
    }
    if (name.empty()) {
        return true;
    }
    auto spx = readXml(dir / (name + ".spx"));
    if (!spx) {
        return false;
    }
    flatten(*spx, "", values_);
    return true;
}

float SceneParams::number(const std::string& key, float fallback) const {
    auto it = values_.find(key);
    return it != values_.end() && !it->second.empty() ? it->second[0] : fallback;
}

math::Vec3 SceneParams::vec3(const std::string& key, const math::Vec3& fallback) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.size() < 3) {
        return fallback;
    }
    return {it->second[0], it->second[1], it->second[2]};
}

SceneLighting loadSceneLighting(const Level& level, const std::filesystem::path& root) {
    SceneLighting s;
    std::string paramName;
    for (const LevelObject& o : level.objects) {
        if (o.asTemplate) {
            continue;
        }
        // The directional light's Color is gamma 2.2 encoded (see srgbToLinear; first seen as a
        // fit on the HDR frame of run roof_light3). Ambient and IBL colours are treated the same
        // way **[guess]** (white and black in ROOFTOPS either way).
        auto color = [&](math::Vec3 fallback) {
            if (auto t = o.text("Color"); !t.empty()) {
                auto v = parseNumbers(t);
                if (v.size() >= 3) {
                    return math::Vec3{srgbToLinear(v[0]), srgbToLinear(v[1]), srgbToLinear(v[2])};
                }
            }
            return fallback;
        };
        if (o.className == "art_light_directional" && !s.hasSun) {
            // A directional light shines along its local -z (Maya); the shader takes the
            // direction towards the light, the rotation's z axis.
            const math::Mat4 m = o.transform.matrix();
            s.sunDirection = math::normalize(math::Vec3{m[8], m[9], m[10]}, {0, 1, 0});
            s.sunRotation = m;
            s.sunRotation[12] = s.sunRotation[13] = s.sunRotation[14] = 0;
            s.sunColor = color({1, 1, 1}) * o.number("Intensity").value_or(1.0f);
            s.hasSun = true;
        } else if (o.className == "art_light_ambient") {
            s.ambient = color({0, 0, 0}) * o.number("Intensity").value_or(0.0f);
        } else if (o.className == "art_sky_ibl") {
            s.skyModel = o.odx;
            // Fallback name for the cube: the model's name with "tex_" after "mup_bg_" and "_ibl_01"
            // (right for ROOFTOPS only). The cube itself is the sky model's image-based light
            // (data::ModelLight, applied by app/LevelScene).
            std::string tex = o.odx;
            if (tex.rfind("mup_bg_", 0) == 0) {
                tex = "mup_bg_tex_" + tex.substr(7);
            }
            s.iblTexture = tex + "_ibl_01";
            s.enableSky = o.flag("EnableSky").value_or(true);
            s.enableIbl = o.flag("EnableIbl").value_or(true);
            // The sky probe the shaders read has intensity and colour 1 (run roof_cloud_fs, probe
            // record words 4..11) although IblIntensity is 0.87: where IblIntensity applies is
            // not found **[open]**; kept here, not applied to the lighting.
            s.iblIntensity = o.number("IblIntensity").value_or(1.0f);
            if (auto t = o.text("IblColor"); !t.empty()) {
                auto v = parseNumbers(t);
                if (v.size() >= 3) {
                    s.iblColor = {srgbToLinear(v[0]), srgbToLinear(v[1]), srgbToLinear(v[2])};
                }
            }
            s.iblCentre = {o.transform.translate[0], o.transform.translate[1], o.transform.translate[2]};
            s.iblRadius = o.number("IblSphereRadius").value_or(100.0f);
        } else if (o.className == "art_spx" && paramName.empty()) {
            paramName = o.text("SceneParamName");
        }
    }
    s.params.load(root, paramName);
    return s;
}

}   // namespace game
