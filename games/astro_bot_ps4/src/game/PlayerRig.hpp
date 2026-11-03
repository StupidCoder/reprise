// The VR player's rig: where the player's head sits in the level. In the default move mode
// ("MoveModeZ", 0xa61950) the rig follows the hero along world Z only: the hero may roam in a
// free area between `near` and `far` metres ahead of the rig; leaving it moves the rig so that a
// SmoothParam share of the error closes per update, limited by MaxSpeed/MaxAccel (forward, -Z)
// or MaxSpeedBack/MaxAccelBack (back, only with CanGoBack). The area bends with the hero's
// sideways offset x: far = FreeAreaFar + x^2 * FreeAreaCurveParamFar,
// near = FreeAreaNear - x^2 * FreeAreaCurveParamNear. Tuning: <VrPlayer><MoveModeZ>.
#pragma once

#include "math/Transform.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace beacon::game {

struct PlayerRigTuning {
    float freeAreaFar = 1.1f, freeAreaNear = 0.4f;
    float freeAreaCurveParamFar = 0.02f, freeAreaCurveParamNear = 0.4f;
    float smoothParam = 0.15f;
    float maxAccel = 10, maxSpeed = 40;
    bool canGoBack = false;
    float maxAccelBack = 3, maxSpeedBack = 0.3f;
};

std::optional<PlayerRigTuning> loadPlayerRigTuning(const std::filesystem::path& root, std::string* error = nullptr);

class PlayerRig {
public:
    explicit PlayerRig(const PlayerRigTuning& t) : t_(t) {}

    // `origin`: the level's vr_player_locator position.
    void reset(const math::Vec3& origin);
    void update(float dt, const math::Vec3& hero);
    const math::Vec3& position() const { return pos_; }
    const PlayerRigTuning& tuning() const { return t_; }
    void setTuning(const PlayerRigTuning& t) { t_ = t; }

private:
    PlayerRigTuning t_;
    math::Vec3 pos_;
    float speed_ = 0;   // current Z speed (m/s)
};

}   // namespace game
