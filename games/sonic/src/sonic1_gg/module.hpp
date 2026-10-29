#pragma once
#include "app/game_module.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace sonic1_gg {

// Creates the Sonic 1 (Game Gear) module from an installed asset pack. With a
// non-empty `state` (a quick save), play resumes exactly where it was saved;
// otherwise the game starts at the SEGA logo like the cartridge.
std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& pack,
                                               std::span<const std::uint8_t> state = {});

// Starts directly at one of the level selector's start points (see catalog.hpp).
std::unique_ptr<shell::GameModule> make_level_module(const std::filesystem::path& pack,
                                                     unsigned act);

} // namespace sonic1_gg
