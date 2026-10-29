#pragma once
// The fifth adventure's secrets (loops 5000..5260): script loops on the
// scrt_* scripts (a5_story.cpp's table), except the character profiles
// (5210, lt5CLoopScrtChara), which keep a table of the characters and their
// poses for the script.
#include "story/a5_story.hpp"
#include <memory>
#include <vector>

namespace picarat::adventure5 {

constexpr int SecretProfilesLoopId = 5210;

std::unique_ptr<Loop> makeSecretProfilesLoop(Story& story, int id, std::vector<xq::Value> values);

} // namespace picarat::adventure5
