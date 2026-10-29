#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// One quick save per game, named by the game (Title::save_name).
namespace shell {
std::filesystem::path quick_save_path(const std::filesystem::path& root, const char* name);
bool has_quick_save(const std::filesystem::path& root, const char* name);
void write_quick_save(const std::filesystem::path& root, const char* name,
                      std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> read_quick_save(const std::filesystem::path& root, const char* name);
} // namespace shell
