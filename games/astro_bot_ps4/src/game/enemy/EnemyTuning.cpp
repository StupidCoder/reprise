#include "game/enemy/EnemyTuning.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

#include <cstdio>
#include <cstdlib>

namespace beacon::game::enemy {

namespace {

void collect(const data::XmlNode& n, const std::string& prefix, std::map<std::string, std::string>& out) {
    for (const auto& c : n.children) {
        const std::string path = prefix.empty() ? c.name : prefix + "/" + c.name;
        if (c.children.empty()) {
            out[path] = c.text;
        } else {
            collect(c, path, out);
        }
    }
}

}   // namespace

std::optional<EnemyTuning> EnemyTuning::load(const std::filesystem::path& root, const std::string& kind,
                                             std::string* error) {
    const auto path = root / "data" / "multi_platformer" / "property" / "basic_enemies.xml";
    data::Bytes bytes;
    if (!data::loadFile(path, bytes)) {
        if (error) {
            *error = path.string() + ": cannot read";
        }
        return std::nullopt;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), error);
    if (!xml) {
        return std::nullopt;
    }
    const data::XmlNode* block = xml->child(kind);
    if (!block) {
        if (error) {
            *error = "basic_enemies.xml: no <" + kind + ">";
        }
        return std::nullopt;
    }
    EnemyTuning t;
    collect(*block, "", t.values_);
    if (const data::XmlNode* common = xml->child("BasicEnemiesCommon")) {
        collect(*common, "Common", t.values_);
    }
    return t;
}

float EnemyTuning::number(const std::string& path, float fallback) const {
    auto it = values_.find(path);
    return it == values_.end() || it->second.empty() ? fallback : std::strtof(it->second.c_str(), nullptr);
}

math::Vec3 EnemyTuning::vector(const std::string& path, const math::Vec3& fallback) const {
    auto it = values_.find(path);
    math::Vec3 v = fallback;
    if (it != values_.end()) {
        std::sscanf(it->second.c_str(), "%f %f %f", &v.x, &v.y, &v.z);
    }
    return v;
}

bool EnemyTuning::flag(const std::string& path, bool fallback) const {
    auto it = values_.find(path);
    if (it == values_.end()) {
        return fallback;
    }
    return it->second == "true" || it->second == "1";
}

std::string EnemyTuning::text(const std::string& path) const {
    auto it = values_.find(path);
    return it == values_.end() ? std::string() : it->second;
}

}   // namespace beacon::game::enemy
