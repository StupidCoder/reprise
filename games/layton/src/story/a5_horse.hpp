#pragma once
// The fifth adventure's horse ride (loop 6300, lt5CLoopMiniHorse): the
// story's chase (events 60 and 200200) and the bag's riding game share it.
#include "story/a5_story.hpp"
#include <memory>
#include <vector>

namespace picarat::adventure5 {

constexpr int HorseLoopId = 6300;

std::unique_ptr<Loop> makeHorseLoop(Story& story, int id, std::vector<xq::Value> values);

} // namespace picarat::adventure5
