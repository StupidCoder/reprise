#include "game/PlayerRig.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::game {

std::optional<PlayerRigTuning> loadPlayerRigTuning(const std::filesystem::path& root, std::string* error) {
    const auto path = root / "data" / "multi_platformer" / "multi_platformer.xml";
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
    PlayerRigTuning t;
    const data::XmlNode* vr = xml->child("VrPlayer");
    const data::XmlNode* z = vr ? vr->child("MoveModeZ") : nullptr;
    if (!z) {
        return t;
    }
    auto read = [&](const char* name, float& v) {
        if (auto s = z->childText(name)) {
            data::parseFloatList(*s, &v, 1);
        }
    };
    read("FreeAreaFar", t.freeAreaFar);
    read("FreeAreaNear", t.freeAreaNear);
    read("FreeAreaCurveParamFar", t.freeAreaCurveParamFar);
    read("FreeAreaCurveParamNear", t.freeAreaCurveParamNear);
    read("SmoothParam", t.smoothParam);
    read("MaxAccel", t.maxAccel);
    read("MaxSpeed", t.maxSpeed);
    read("MaxAccelBack", t.maxAccelBack);
    read("MaxSpeedBack", t.maxSpeedBack);
    if (auto s = z->childText("CanGoBack")) {
        t.canGoBack = data::parseBool(*s).value_or(false);
    }
    return t;
}

void PlayerRig::reset(const math::Vec3& origin) {
    pos_ = origin;
    speed_ = 0;
}

void PlayerRig::update(float dt, const math::Vec3& hero) {
    if (dt <= 0) {
        return;
    }
    const float x = hero.x - pos_.x;   // sideways offset (hypothesis: relative to the rig)
    const float far = t_.freeAreaFar + x * x * t_.freeAreaCurveParamFar;
    const float near = t_.freeAreaNear - x * x * t_.freeAreaCurveParamNear;
    float hz = hero.z;
    if (!t_.canGoBack) {
        hz = std::min(hz, pos_.z - near);   // never pulled backwards
    }
    float v = 0;
    if (hz < pos_.z - far || pos_.z - near < hz) {
        const float edge = hz < pos_.z - far ? far : near;
        v = t_.smoothParam * ((hz - pos_.z) + edge) / dt;
        const bool back = v > 0;
        const float maxSpeed = back ? t_.maxSpeedBack : t_.maxSpeed;
        const float maxAccel = back ? t_.maxAccelBack : t_.maxAccel;
        v = std::clamp(v, -maxSpeed, maxSpeed);
        const float accel = (v - speed_) / dt;
        if (std::fabs(accel) > maxAccel) {
            v = speed_ + std::copysign(maxAccel, accel) * dt;
        }
        speed_ = v;
    }
    pos_.z += v * dt;
}

}   // namespace game
