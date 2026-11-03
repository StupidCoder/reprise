#include "game/DynamicGrass.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"
#include "math/Vector.hpp"

#include <cmath>
#include <limits>

namespace beacon::game {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kEpsilon = 1.1920929e-07f;   // 0x11c2c60
constexpr std::uint32_t kListInterval = 10;  // the hero list is refreshed every 10th update (0x3ee130)
}   // namespace

DynamicGrassTuning loadDynamicGrassTuning(const std::filesystem::path& root) {
    DynamicGrassTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "property" / "dynamic_grass.xml", bytes)) {
        return t;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!xml) {
        return t;
    }
    const data::XmlNode* node = xml->child("DynamicGrass");
    if (!node) {
        node = &*xml;
    }
    auto read = [&](const char* name, float& value) {
        if (auto text = node->childText(name)) {
            data::parseFloatList(*text, &value, 1);
        }
    };
    read("GrassDistanceThreshold", t.distanceThreshold);
    read("GrassBotDistance", t.botDistance);
    read("GrassForwardBlendSpeed", t.forwardBlendSpeed);
    read("GrassBackwardBlendSpeed", t.backwardBlendSpeed);
    read("GrassSoundDistance", t.soundDistance);
    return t;
}

DynamicGrass::DynamicGrass(std::vector<int> blades, float minAngle, float maxAngle, int index)
    : blades_(std::move(blades)), state_(blades_.size()), minAngle_(minAngle * kPi / 180),
      maxAngle_(maxAngle * kPi / 180), index_(index) {}

bool DynamicGrass::update(float dt, const DynamicGrassTuning& t, const math::Mat4& world, float scale,
                          std::span<const math::Vec3> heroes, std::span<const math::Vec3> bladePos) {
    using math::Vec3;
    // The hero list (0x3ee610, every 10th update, staggered by the tuft's index): heroes within
    // GrassDistanceThreshold + GrassBotDistance of the tuft.
    if (counter_ % kListInterval == std::uint32_t(index_) % kListInterval) {
        listed_.clear();
        const Vec3 at{world[12], world[13], world[14]};
        const float reach = t.distanceThreshold + t.botDistance;
        for (std::size_t h = 0; h < heroes.size(); ++h) {
            const Vec3 d = heroes[h] - at;
            if (std::sqrt(dot(d, d)) < reach) {
                listed_.push_back(int(h));
            }
        }
        listedAny_ = !listed_.empty();
    }
    ++counter_;
    bool bent = false;
    if (listedAny_) {
        // The bend (0x3f0370, called with r = GrassBotDistance / scale), in the model's space.
        const float r = t.botDistance / scale;
        const math::Mat4 toModel = math::inverseAffine(world);
        std::vector<Vec3> near;
        for (int h : listed_) {
            if (std::size_t(h) < heroes.size()) {
                near.push_back(math::transformPoint(toModel, heroes[std::size_t(h)]));
            }
        }
        for (std::size_t i = 0; i < state_.size() && i < bladePos.size(); ++i) {
            Blade& b = state_[i];
            float best = std::numeric_limits<float>::max();
            Vec3 from;
            for (const Vec3& n : near) {
                const Vec3 d = bladePos[i] - n;
                const float dd = dot(d, d);
                if (dd < best) {
                    best = dd;
                    from = n;
                }
            }
            const float d = std::sqrt(best);
            if (d < r && d > kEpsilon) {
                // Tip the blade away from the hero: about up x (blade - hero, horizontal), by an
                // angle from MaxAngle (touching) down to MinAngle (at r).
                const Vec3 away{bladePos[i].x - from.x, 0, bladePos[i].z - from.z};
                b.axis = math::normalize(math::cross({0, 1, 0}, away), b.axis);
                b.angle = maxAngle_ - d * (maxAngle_ - minAngle_) / r;
                b.weight = std::min(1.0f, b.weight + dt * t.forwardBlendSpeed);
            } else {
                b.weight = std::max(0.0f, b.weight - dt * t.backwardBlendSpeed);
            }
        }
    }
    for (const Blade& b : state_) {
        bent = bent || b.weight > kEpsilon;
    }
    return bent;
}

math::Quat DynamicGrass::bend(std::size_t i) const {
    const Blade& b = state_[i];
    const float half = 0.5f * b.angle * b.weight;
    const float s = std::sin(half);
    return {b.axis.x * s, b.axis.y * s, b.axis.z * s, std::cos(half)};
}

}   // namespace game
