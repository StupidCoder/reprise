#pragma once
#include "sonic1_gg/data/level_data.hpp"
#include <functional>
#include <string>

namespace sonic1_gg {
using ExtractProgress = std::function<void(unsigned, unsigned)>;
// Reusable extraction operation. The caller owns transactional installation.
void extract_asset_pack(const Rom&, const std::filesystem::path&, ExtractProgress = {});
} // namespace sonic1_gg
