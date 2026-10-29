#pragma once
// The fifth adventure's shop game (loop 6200, lt5CLoopMiniShop): goods
// arranged on a shelf so that customers find what they look for.
#include "story/a5_story.hpp"
#include <memory>
#include <vector>

namespace picarat::adventure5 {

constexpr int ShopLoopId = 6200;

std::unique_ptr<Loop> makeShopLoop(Story& story, int id, std::vector<xq::Value> values);

} // namespace picarat::adventure5
