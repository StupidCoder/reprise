#pragma once
#include "app/game_module.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace sonic_advance_gba {

// Creates the Sonic Advance (Game Boy Advance) module from an installed asset
// pack. The game starts at power-on like the cartridge.
std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& pack,
                                               std::span<const std::uint8_t> state = {});

} // namespace sonic_advance_gba
