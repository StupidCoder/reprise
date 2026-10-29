#pragma once
// The headless self-test (picarat --self-test OUT --assets PACK).
#include <filesystem>

namespace picarat::shell {

// Runs the opening and the first puzzles with scripted stylus input and
// writes captures and a report to `output`. Returns the process exit code.
int selfTest(const std::filesystem::path& output, const std::filesystem::path& assets);

} // namespace picarat::shell
