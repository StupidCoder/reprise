#pragma once
#include "app/game_module.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace triple_trouble_gg {

// Creates the Triple Trouble (Game Gear) module from an installed asset
// pack. With a non-empty `state` (a quick save), play resumes where it was
// saved; otherwise the game starts at power-on like the cartridge.
std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& pack,
                                               std::span<const std::uint8_t> state = {});

// Starts directly at a level selector start point (index into levels).
std::unique_ptr<shell::GameModule> make_level_module(const std::filesystem::path& pack,
                                                     unsigned level);

} // namespace triple_trouble_gg
