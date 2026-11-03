// The goal (class goal_balloon_base, vtable 0x1697b58, constructor 0x486e10, setup 0x487490,
// update 0x487dc0, sequence 0x488f30). Findings: .analysis/docs/objects.md "Goal".
//
// The platform (mup_co_goal_balloon_base): a base and a stage (joint_stage) the hero lands on.
// Landing on the stage (0x488330: the hero grounded with the goal's body as its support; edge
// detected each update) starts the pump (0x4883f0): the stage dips and springs back,
//   v = PumpSpeed at the landing; each update v += (-d PumpSpring - v PumpDamper) dt, d += v dt,
//   d kept in 0..1 (rising past 1 stops v; falling below 0 ends the pump), the stage lowered by
//   d PumpDepth (0x487dc0 -> joint override 0xbcb6b0).
#pragma once

#include "math/Transform.hpp"

#include <filesystem>
#include <string>

namespace beacon::game {

struct Level;
class LevelRuntime;

// <GoalBalloon> in multi_platformer.xml (the property set at 0x2ed2c68; offsets in comments).
struct GoalTuning {
    float pumpSpeed = 20, pumpSpring = 50, pumpDamper = 5, pumpDepth = 0.15f;   // +0x960 +0xa10 +0xac0 +0xb70
    float vrPlayerZPos = 1.8f;                                                    // +0x2220
};
GoalTuning loadGoalTuning(const std::filesystem::path& root);

struct GoalSetup {
    std::size_t object = 0;
    math::Mat4 world = math::identity();
    std::string definition = "mup_co_goal_balloon_base";
};
GoalSetup goalSetup(const Level& level, const LevelRuntime& runtime, std::size_t index);

class GoalBalloon {
public:
    GoalBalloon(GoalSetup setup, const GoalTuning& tuning) : setup_(std::move(setup)), tuning_(tuning) {}

    // One update; `heroOnStage`: the hero stands on the goal (0x488330).
    void update(float dt, bool heroOnStage);

    const GoalSetup& setup() const { return setup_; }
    // How far the stage is pushed down (m, along the stage joint's up axis).
    float stageDrop() const { return depth_ * tuning_.pumpDepth; }
    // The hero landed on the stage this update (the clip hero_on, 0x4883f0).
    bool heroLanded() const { return landed_; }

private:
    GoalSetup setup_;
    GoalTuning tuning_;
    bool pumping_ = false;   // +0x2648
    float depth_ = 0;        // +0x264c, 0..1
    float speed_ = 0;        // +0x2654
    bool heroOn_ = false;    // +0x2658 (the last update's)
    bool landed_ = false;
};

}   // namespace game
