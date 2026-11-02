#pragma once
#include "app/game_module.hpp"
#include "collections/theme.hpp"
#include "platform/file_source.hpp"
#include "platform/image.hpp"
#include "ui/model_view.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// What the launcher knows about a game in the collection. Each game
// directory provides one entry; the launcher knows nothing else about it.
namespace app {

// A phase and its measured fraction (negative when work cannot be measured).
using ImportProgress = std::function<void(const std::string&, double)>;

enum class Machine { c64, amiga, game_gear, nintendo_ds, nintendo_3ds, ios, game_boy_advance, playstation_4 };

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
    std::vector<ImportedFile> files; // names may contain '/' for subfolders
    std::string release; // which release was recognised, for the user
    // Games on several disks: this image is disk `part` (1-based) of `parts`.
    // The library keeps each disk until all of them are imported, then
    // installs them together (same release; file names must not collide).
    int part{1};
    int parts{1};
};

// What an earlier, separate application of a game left on this computer (its own import and
// progress), to be taken over once (Library::adopt_legacy). Nothing of it is moved or changed.
struct LegacyCopy {
    // Files to install when the game is not installed yet (empty: none usable).
    std::optional<ImportResult> import;
    // Or, for large imports (GameEntry::import_into), a folder holding the game's files and
    // their release: copied file by file into Reprise, never moved or changed.
    std::optional<std::filesystem::path> folder;
    std::string folder_release;
    // (source, target in Reprise's data folder): copied when the target does not exist.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;
    // For the user: what could not be taken over, and why.
    std::vector<std::string> notes;
};

// A sound for the viewer to play when its preview is clicked: mono samples
// in -1..1 at their own rate (the viewer resamples).
struct PreviewSound {
    std::vector<float> samples;
    double rate{};

    // From the 8-bit signed samples Paula and most 8-bit sound chips play.
    static PreviewSound signed8(std::span<const std::int8_t> samples, double rate) {
        PreviewSound sound{{}, rate};
        sound.samples.reserve(samples.size());
        for (const auto s : samples) sound.samples.push_back(float(s) / 128.0f);
        return sound;
    }
};

// A picture of decoded game data, for the launcher's viewer. A preview can
// also carry a sound (the picture is then its waveform; clicking plays it)
// or a 3D model (drawn turning in place of the picture; drag to turn it).
struct Preview {
    std::string title;
    platform::Image image;
    std::optional<PreviewSound> sound{};
    std::shared_ptr<const ui::Model> model{};
};

// Supported enhancements, independent of the current option values. Quick saves
// are supplied by the framework registry, not duplicated in this list.
enum class GameFeature { widescreen, high_framerate, cheats, extra_modes, touch, tilt };

struct GameEntry {
    std::string id;    // folder and settings key, e.g. "fort_apocalypse_c64"
    std::string title; // the game's name (descriptive use only)
    Machine machine{};
    std::string media; // what to import, e.g. "the tape image (TAP)"

    // Decodes the user's image. Throws NotThisGame or std::runtime_error
    // with a message for the user when the image cannot be used.
    // Empty for games that are planned but not importable yet.
    std::function<ImportResult(std::span<const std::uint8_t> image)> import;
    // The largest file the importer accepts; larger files are not read for it. Disk and tape
    // images stay under the default; app archives (IPA) are larger.
    std::uintmax_t largest_image{64u << 20};
    // Importers of large images (cartridge images of hundreds of megabytes and more), used
    // instead of `import`: reads the image through `image` (random access, never read whole)
    // and writes the game's files into `folder` (made by the library, empty) instead of
    // returning them; returns the release. Throws NotThisGame or std::runtime_error as `import`
    // does. The library hashes the written files for the manifest; at start-up it checks such
    // an install by its files' sizes (hashing gigabytes at every start would take too long).
    std::function<std::string(platform::FileSource& image, const std::filesystem::path& folder,
                              const ImportProgress& progress)> import_into;

    // Pictures of the data decoded from an installed content folder; empty
    // when the game has no decoders yet. Throws std::runtime_error when the
    // content cannot be decoded.
    std::function<std::vector<Preview>(const std::filesystem::path& content)> previews;

    // Starts the game from an installed content folder; empty while nothing
    // of the game can be played yet.
    std::function<std::unique_ptr<GameModule>(const std::filesystem::path& content)> start;

    // Looks for an earlier application's copy of the game (`data`: Reprise's data folder);
    // empty for games without one or when there is nothing to take over.
    std::function<std::optional<LegacyCopy>(const std::filesystem::path& data)> legacy;

    // What can be played so far, for the launcher ("" = the whole game).
    std::string progress;
    // For the details page, when the defaults would say too much: what Continue restores (a
    // game whose quick save keeps a checkpoint, not the moment), and how a game without
    // Reprise quick saves keeps its progress (its own save slots). Empty: the defaults.
    std::string continue_note;
    std::string saves_note;

    // Editorial history of this platform release; never reconstruction evidence.
    int release_year{};
    std::string description;
    std::string history_title;
    std::string history;
    std::string history_sources; // Short credits; full references in docs/catalog-history.md.

    Collection collection{Collection::none};
    bool integration_pending{}; // Catalog preview only, no engine/importer wired yet.
    std::vector<GameFeature> features;
};

// Whether the game can be imported (by either kind of importer).
inline bool importable(const GameEntry& game) { return bool(game.import) || bool(game.import_into); }

} // namespace app
