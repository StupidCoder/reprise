#pragma once
#include "app/game_module.hpp"
#include "platform/image.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// What the launcher knows about a game in the collection. Each game
// directory provides one entry; the launcher knows nothing else about it.
namespace app {

enum class Machine { c64, amiga };

// A file the importer extracted from the user's image, to be installed in
// the game's content folder.
struct ImportedFile {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

// Thrown by an importer when the image belongs to another game (or is not
// a game image at all). Any other error means the image is this game's but
// cannot be used (damaged, or an unsupported release), and is shown as is.
struct NotThisGame : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ImportResult {
    std::vector<ImportedFile> files;
    std::string release; // which release was recognised, for the user
};

// A picture of decoded game data, for the launcher's viewer.
struct Preview {
    std::string title;
    platform::Image image;
};

struct GameEntry {
    std::string id;    // folder and settings key, e.g. "fort_apocalypse_c64"
    std::string title; // the game's name (descriptive use only)
    Machine machine{};
    std::string media; // what to import, e.g. "the tape image (TAP)"

    // Decodes the user's image. Throws NotThisGame or std::runtime_error
    // with a message for the user when the image cannot be used.
    // Empty for games that are planned but not importable yet.
    std::function<ImportResult(std::span<const std::uint8_t> image)> import;

    // Pictures of the data decoded from an installed content folder; empty
    // when the game has no decoders yet. Throws std::runtime_error when the
    // content cannot be decoded.
    std::function<std::vector<Preview>(const std::filesystem::path& content)> previews;

    // Starts the game from an installed content folder; empty while nothing
    // of the game can be played yet.
    std::function<std::unique_ptr<GameModule>(const std::filesystem::path& content)> start;

    // What can be played so far, for the launcher ("" = the whole game).
    std::string progress;

    // Editorial history of this platform release; never reconstruction evidence.
    int release_year{};
    std::string description;
    std::string history_title;
    std::string history;
    std::string history_sources; // Short credits; full references in docs/catalog-history.md.

};

} // namespace app
