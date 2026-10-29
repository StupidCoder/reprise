#pragma once
// The fifth adventure's rabbit (loops 6000..6004): the protagonist's rabbit in its
// room (6000, lt5CLoopMiniRabbit), talking and training with it (6001
// Commu), the show puzzles (6002 Nazo), the show (6003 Show) and the
// trick notebook (6004 Note). 6005 is a script loop (mini_rabbit_select,
// a5_story.cpp).
#include "story/a5_story.hpp"
#include <memory>
#include <vector>

namespace picarat::adventure5 {

constexpr int RabbitLoopId = 6000;
constexpr int RabbitCommuLoopId = 6001;
constexpr int RabbitNazoLoopId = 6002;
constexpr int RabbitShowLoopId = 6003;
constexpr int RabbitNoteLoopId = 6004;

inline bool isRabbitLoop(int id) { return id >= RabbitLoopId && id <= RabbitNoteLoopId; }

std::unique_ptr<Loop> makeRabbitLoop(Story& story, int id, std::vector<xq::Value> values);

} // namespace picarat::adventure5
