// ROM import: verifies the user's game image, builds the opening's package and keeps a verified
// copy of the game's file system in the user data folder, from which the other stages are built
// when they are first played. The application bundle itself never contains game data.
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include "RomImage.hpp"

namespace captain_toad {

// Package format version; packages from other versions are imported again.
constexpr int kPackageSchema = 5;   // 5: water reads the drawn view's half of a side-by-side copy

// The imported opening package inside a data directory.
std::filesystem::path packageDirectory(const std::filesystem::path& dataDirectory);
// The verified copy of the ROM's files (exefs/code.bin and romfs/PATH) inside a data directory.
std::filesystem::path romDirectory(const std::filesystem::path& dataDirectory);
// True when a complete package of the current schema and the ROM copy are present.
bool packageReady(const std::filesystem::path& dataDirectory);

// Writes only to an empty Library-owned staging directory; Library owns final installation.
void importInto(RomImage& image, const std::filesystem::path& folder,
                const std::function<void(double, const std::string&)>& progress = {});

}   // namespace captain_toad
