#pragma once
// Transactional, offline ROM import for the game library. The pack is built
// in a hidden staging folder beside the destination and published with one
// rename, so the library never sees a partial import; the staging folder is
// removed on every exit, including errors and cancellation.
#include "import/rom_image.hpp"
#include "import/rom_source.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

namespace picarat::import {

// Pack layout versions recorded in import.json; the app rejects other
// values. The first, third and fourth adventures' packs have their own
// numbering.
inline constexpr int kPackSchema = 18;
inline constexpr int kAdventure1PackSchema = 3;
inline constexpr int kAdventure3PackSchema = 2;
inline constexpr int kAdventure4PackSchema = 1;
inline constexpr int kAdventure5PackSchema = 1;
inline constexpr int kAdventure6PackSchema = 1;

struct ImportCancelled : std::runtime_error {
    ImportCancelled() : std::runtime_error("Import cancelled") {}
};

struct ImportResult {
    Game game = Game::Adventure2;
    std::string romSha256, gameCode;
    std::size_t assetCount = 0, puzzles = 0;
};

using ImportProgress = std::function<void(double fraction, const std::string& message)>;
// Where the pack of the identified game goes; called once the ROM is known.
using ImportDestination = std::function<std::filesystem::path(Game)>;

// Identifies `rom` (one of kKnownRoms) and imports it into the folder
// `destination` names for that game, which must not exist yet. Progress is
// reported from the calling thread; setting `cancel` stops at the next step.
ImportResult importRom(const std::filesystem::path& rom, const ImportDestination& destination,
                       const ImportProgress& progress = {},
                       const std::atomic<bool>* cancel = nullptr);
// The same for a ROM read through `source` (the app adapts the platform
// layer's reader, which also opens Android's content:// documents). DS ROMs
// are read into memory; 3DS cartridge images (up to 4 GiB) stream.
ImportResult importRom(RomSource& source, const ImportDestination& destination,
                       const ImportProgress& progress = {},
                       const std::atomic<bool>* cancel = nullptr);
// The same for a DS ROM already read into memory.
ImportResult importRom(Bytes romImage, const ImportDestination& destination,
                       const ImportProgress& progress = {},
                       const std::atomic<bool>* cancel = nullptr);

// Larger files cannot be Nintendo DS ROMs (the biggest cartridge holds 512 MiB).
// 3DS cartridge images are read through a RomSource and may be larger.
inline constexpr std::uintmax_t kMaximumRomSize = 512u << 20;

} // namespace picarat::import
