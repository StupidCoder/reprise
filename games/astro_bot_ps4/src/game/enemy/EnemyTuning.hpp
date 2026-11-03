// An enemy kind's tuning: one block of data/multi_platformer/property/basic_enemies.xml
// (<EnemyStandard>, <EnemySpiky>, ...; bound by 0x2d5a90 with its <Enemy> 0x606bf0 and <Bind>
// 0x2d51c0 groups) plus <BasicEnemiesCommon>. Values are kept by path ("Bind/StepRadius") and
// read with the shipped defaults of the binders when a name is missing.
#pragma once

#include "math/Transform.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace beacon::game::enemy {

class EnemyTuning {
public:
    // Reads the block `kind` ("EnemyStandard") of basic_enemies.xml below the game folder `root`.
    static std::optional<EnemyTuning> load(const std::filesystem::path& root, const std::string& kind,
                                           std::string* error = nullptr);

    float number(const std::string& path, float fallback = 0) const;
    math::Vec3 vector(const std::string& path, const math::Vec3& fallback = {}) const;
    bool flag(const std::string& path, bool fallback = false) const;
    std::string text(const std::string& path) const;
    bool has(const std::string& path) const { return values_.count(path) != 0; }

private:
    std::map<std::string, std::string> values_;   // "Bind/StepRadius" -> "0.100000"; common: "Common/..."
};

}   // namespace beacon::game::enemy
