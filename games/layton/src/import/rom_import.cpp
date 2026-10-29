#include "import/rom_import.hpp"
#include "import/a1_pack.hpp"
#include "import/a3_pack.hpp"
#include "import/a4_pack.hpp"
#include "import/a56_pack.hpp"
#include "import/asset_pack.hpp"
#include "import/a2_puzzle_library.hpp"
#include "import/rom_image.hpp"
#include "import/sha256.hpp"
#include <fstream>
#include <random>
#include <sstream>

namespace picarat::import {
namespace fs = std::filesystem;

namespace {
// Removes the staging folder however the import ends.
struct Staging {
    fs::path path;

    ~Staging() {
        std::error_code ignored;
        if (!path.empty()) {
            fs::remove_all(path, ignored);
        }
    }
};

fs::path makeStaging(const fs::path& parent) {
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
        std::ostringstream name;
        name << ".picarat-import-" << std::hex << random() << random();
        const fs::path path = parent / name.str();
        if (fs::create_directory(path)) {
            return path;
        }
    }
    throw std::runtime_error("Could not create a staging folder for the import.");
}

void writeFile(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
}

// Verifies what was written, adds import.json and publishes the staged pack
// with one rename.
template <class Progress>
void publish(const fs::path& pack, const fs::path& destination, std::ostringstream& json,
             ImportResult& manifest, const PackResult& result, Progress& progress) {
    manifest.assetCount = result.outputs.size();
    progress(0.96, "Verifying the imported game…");
    for (const auto& [name, digest] : result.outputs) {
        if (sha256(readFile(pack / name)) != digest) {
            throw std::runtime_error("An extracted asset failed verification.");
        }
    }
    json << ",\n  \"rom_sha256\": \"" << manifest.romSha256 << "\",\n  \"game_code\": \""
         << manifest.gameCode << "\",\n  \"asset_count\": " << manifest.assetCount
         << ",\n  \"puzzles\": " << manifest.puzzles << ",\n  \"provenance_sha256\": \""
         << sha256(readFile(pack / "provenance.json")) << "\"\n}";
    writeFile(pack / "import.json", json.str());
    progress(0.99, "Verifying the imported game…");
    // Renaming within the destination filesystem publishes a complete pack.
    // Never replace an existing import (including a concurrent import).
    if (fs::exists(fs::symlink_status(destination))) {
        throw std::runtime_error("A game import already exists.");
    }
    fs::rename(pack, destination);
}
} // namespace

ImportResult importRom(const fs::path& romPath, const ImportDestination& destinationFor,
                       const ImportProgress& progressCallback, const std::atomic<bool>* cancel) {
    FileRomSource source(romPath);
    return importRom(source, destinationFor, progressCallback, cancel);
}

ImportResult importRom(Bytes romImage, const ImportDestination& destinationFor,
                       const ImportProgress& progressCallback, const std::atomic<bool>* cancel) {
    auto progress = [&](double fraction, const std::string& message) {
        if (cancel && cancel->load()) {
            throw ImportCancelled();
        }
        if (progressCallback) {
            progressCallback(fraction, message);
        }
    };
    progress(0.01, "Checking the ROM…");
    // Reject unknown games/regions/revisions before creating any destination data.
    check(romImage.size() <= kMaximumRomSize, "The file is too large to be a Nintendo DS ROM.");
    const NdsRom rom(std::move(romImage));
    const Game game = rom.identity().game;
    const fs::path destination = destinationFor(game);
    if (fs::exists(fs::symlink_status(destination))) {
        throw std::runtime_error(std::string(rom.identity().title) + " is already imported.");
    }
    progress(0.04, "Reading game resources…");
    fs::create_directories(destination.parent_path());
    Staging staging{makeStaging(destination.parent_path())};
    const fs::path pack = staging.path / "Assets";
    auto stepProgress = [&](double fraction, const char* message) {
        progress(0.05 + 0.90 * fraction, message);
    };
    ImportResult manifest{game, rom.sha256(), rom.gameCode(), 0, 0};
    std::ostringstream json;
    PackResult result;
    if (game == Game::Adventure2) {
        result = buildNativePack(rom, pack, kPuzzleLibrary, stepProgress);
        manifest.puzzles = kPuzzleLibrary.size();
        json << "{\n  \"schema\": " << kPackSchema;
    } else if (game == Game::Adventure1) {
        adventure1::PackSummary summary;
        result = adventure1::buildPack(rom, pack, summary, stepProgress);
        std::string languages;
        for (const std::string& code : summary.languages) {
            languages += (languages.empty() ? "" : ",") + code;
        }
        json << "{\n  \"game\": \"adventure1\",\n  \"schema\": " << kAdventure1PackSchema
             << ",\n  \"languages\": \"" << languages
             << "\",\n  \"backgrounds\": " << summary.backgrounds
             << ",\n  \"sprites\": " << summary.sprites << ",\n  \"scripts\": " << summary.scripts
             << ",\n  \"texts\": " << summary.texts << ",\n  \"fonts\": " << summary.fonts
             << ",\n  \"sequences\": " << summary.sequences
             << ",\n  \"sound_effect_archives\": " << summary.soundEffectArchives
             << ",\n  \"streams\": " << summary.streams << ",\n  \"movies\": " << summary.movies;
    } else if (game == Game::Adventure4) {
        adventure4::PackSummary summary;
        result = adventure4::buildPack(rom, pack, summary, stepProgress);
        json << "{\n  \"game\": \"adventure4\",\n  \"schema\": " << kAdventure4PackSchema
             << ",\n  \"scripts\": " << summary.scripts
             << ",\n  \"data_lists\": " << summary.dataLists
             << ",\n  \"pictures\": " << summary.pictures
             << ",\n  \"palettes\": " << summary.palettes << ",\n  \"fonts\": " << summary.fonts
             << ",\n  \"sounds\": " << summary.sounds << ",\n  \"streams\": " << summary.streams
             << ",\n  \"movies\": " << summary.movies;
    } else {
        adventure3::PackSummary summary;
        result = adventure3::buildPack(rom, pack, summary, stepProgress);
        json << "{\n  \"game\": \"adventure3\",\n  \"schema\": " << kAdventure3PackSchema
             << ",\n  \"pictures\": " << summary.pictures << ",\n  \"scripts\": " << summary.scripts
             << ",\n  \"fonts\": " << summary.fonts << ",\n  \"sounds\": " << summary.sounds
             << ",\n  \"streams\": " << summary.streams << ",\n  \"movies\": " << summary.movies;
    }
    publish(pack, destination, json, manifest, result, progress);
    if (progressCallback) {
        progressCallback(1.0, game == Game::Adventure2 ? "Ready to play" : "Import complete");
    }
    return manifest;
}

ImportResult importRom(RomSource& source, const ImportDestination& destinationFor,
                       const ImportProgress& progressCallback, const std::atomic<bool>* cancel) {
    Bytes head(std::min<std::uint64_t>(source.size(), 0x200));
    source.read(0, head);
    const bool ctr = head.size() == 0x200 && std::equal(head.begin() + 0x100, head.begin() + 0x104,
                                                        std::string_view("NCSD").begin());
    if (!ctr) {
        check(source.size() <= kMaximumRomSize, "The file is too large to be a Nintendo DS ROM.");
        Bytes image(source.size());
        source.read(0, image);
        return importRom(std::move(image), destinationFor, progressCallback, cancel);
    }
    auto progress = [&](double fraction, const std::string& message) {
        if (cancel && cancel->load()) {
            throw ImportCancelled();
        }
        if (progressCallback) {
            progressCallback(fraction, message);
        }
    };
    progress(0.01, "Checking the ROM…");
    // The whole 3DS image is hashed before anything is written.
    CtrRom rom(source,
               [&](double fraction) { progress(0.01 + 0.3 * fraction, "Checking the ROM…"); });
    const Game game = rom.identity().game;
    const fs::path destination = destinationFor(game);
    if (fs::exists(fs::symlink_status(destination))) {
        throw std::runtime_error(std::string(rom.identity().title) + " is already imported.");
    }
    fs::create_directories(destination.parent_path());
    Staging staging{makeStaging(destination.parent_path())};
    const fs::path pack = staging.path / "Assets";
    ImportResult manifest{game, rom.sha256(), std::string(rom.identity().gameCode), 0, 0};
    adventure56::PackSummary summary;
    const PackResult result =
        adventure56::buildPack(rom, pack, summary, [&](double fraction, const char* message) {
            progress(0.32 + 0.63 * fraction, message);
        });
    std::ostringstream json;
    json << "{\n  \"game\": \"" << (game == Game::Adventure5 ? "adventure5" : "adventure6")
         << "\",\n  \"schema\": "
         << (game == Game::Adventure5 ? kAdventure5PackSchema : kAdventure6PackSchema)
         << ",\n  \"archives\": " << summary.archives << ",\n  \"packs\": " << summary.packs
         << ",\n  \"pictures\": " << summary.pictures << ",\n  \"scripts\": " << summary.scripts
         << ",\n  \"data_lists\": " << summary.dataLists << ",\n  \"others\": " << summary.others
         << ",\n  \"sound_files\": " << summary.streams << ",\n  \"movies\": " << summary.movies;
    publish(pack, destination, json, manifest, result, progress);
    if (progressCallback) {
        progressCallback(1.0, "Import complete");
    }
    return manifest;
}

} // namespace picarat::import
