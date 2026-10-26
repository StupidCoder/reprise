#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
namespace platform {
// App-owned resources through SDL IO (including Android assets). Relative to
// the bundle / executable, never the user's imported-content directory.
std::vector<std::uint8_t> resource(std::string_view relative);
}
