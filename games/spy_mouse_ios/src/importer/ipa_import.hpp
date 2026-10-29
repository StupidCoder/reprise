// Importing the game from the user's own IPA (the iPhone app archive, a ZIP file).
//
// DoubleOCheese ships without game data. The importer copies the files the reconstruction reads
// from the archive's app folder (Payload/<name>.app): the game executable, whose file keys open
// the data files, and the res/ folder with maps, images, sounds and tables. Nothing is converted:
// the game runs from its own files. The copy is checked before it replaces an earlier import:
// every file against the archive's CRC-32, then the executable's keys against two data files
// whose integrity the original itself checks (data::GameFiles::open), and the level table.
#pragma once

#include <atomic>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace doc::importer {

// Folder of the imported app inside the data folder (holds SpyMouse and res/).
std::filesystem::path gameDirectory(const std::filesystem::path& dataDirectory);

// Whether a complete import is present.
bool gameReady(const std::filesystem::path& dataDirectory);

// `progress(fraction, step)` is called from the importing thread; setting `cancel` stops the
// import without changing an earlier one. Returns false with `error` set on failure.
bool importIpa(const std::filesystem::path& ipa, const std::filesystem::path& dataDirectory,
               const std::function<void(double, const std::string&)>& progress, const std::atomic<bool>* cancel,
               std::string& error);

// The files the game needs (bundle-relative names: "SpyMouse", "res/..."), from an IPA held in
// memory, with the same checks as importIpa (CRC-32 of every file, the keys against the data
// files, the level table). Nothing is written.
struct ImportedFiles {
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
};
bool readIpa(std::span<const std::uint8_t> ipa, ImportedFiles& out, std::string& error);
// Whether the bytes are a ZIP archive holding an app folder (Payload/<name>.app/).
bool isAppArchive(std::span<const std::uint8_t> bytes);

}  // namespace doc::importer
