#pragma once
// The fifth adventure's robot game (loop 6100, lt5CLoopMiniRobot): a wind-up
// robot steered across a board of blocks, panels, keys and enemies, a turn
// at a time.
#include "story/a5_story.hpp"
#include <memory>
#include <vector>

namespace picarat::adventure5 {

constexpr int RobotLoopId = 6100;

std::unique_ptr<Loop> makeRobotLoop(Story& story, int id, std::vector<xq::Value> values);

} // namespace picarat::adventure5
