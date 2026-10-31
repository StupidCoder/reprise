#pragma once
#include "app/game_entry.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The games and what the user has imported. Imported content lives in
// <data>/games/<id>/: the extracted files plus a manifest ("release.txt")
// naming the release and the SHA-256 of every file. An import is written to
// <data>/staging/<id>/, read back and checked, then moved into place in one
// step, so a failed import never damages an earlier one.
//
// A game on several disks is imported one disk at a time: each disk is kept
// in <data>/parts/<id>/<n>/ (with its own manifest) until every disk of the
// same release is there; then all of them are installed together.
namespace app {

class Library {
public:
    enum class StartupValidation { full, defer_large_imports };
    Library(std::filesystem::path data_directory, std::vector<GameEntry> games,
            StartupValidation validation = StartupValidation::full);
    // Interactive startup can read large imports' recorded inventories without
    // statting every asset. Complete that check before Start/Continue (once per
    // session). Imports and migration always validate immediately.
    bool verify(std::size_t game);

    const std::filesystem::path& data_directory() const { return root_; }

    const std::vector<GameEntry>& games() const { return games_; }

    // Available import. Deferred large inventories are validated by verify() before play.
    bool installed(std::size_t game) const { return installs_[game].has_value(); }
    // The recognised release, for an installed game.
    std::string release(std::size_t game) const;
    // The installed files with their sizes, for an installed game.
    std::vector<std::pair<std::string, std::uintmax_t>> installed_files(std::size_t game) const;
    std::filesystem::path content_directory(std::size_t game) const;
    // For a game on several disks that is not installed yet: which disks have
    // been imported so far ("disk 1 of 3"), or "" when none.
    std::string imported_parts(std::size_t game) const;

    struct Outcome {
        bool ok{};
        std::optional<std::size_t> game; // the game the image was installed for
        std::string message;             // for the user
        bool waiting{};                  // a disk was kept; more disks are needed
    };

    // Imports an image. The preferred game (the one selected in the
    // launcher) is tried first, then every other importable game, so a
    // dropped image lands with the game it belongs to.
    Outcome import(const std::filesystem::path& image, std::optional<std::size_t> preferred,
                   const ImportProgress& progress = {});

    // Deletes a game's imported content.
    void remove(std::size_t game);

    // Takes over what earlier applications of the games left (GameEntry::legacy), once per
    // game: installs their import when the game is not installed, copies their progress where
    // Reprise has none. Originals are only read; nothing in Reprise is overwritten. Returns
    // messages for the user (taken over, or why not). A game whose takeover failed is tried
    // again next time.
    std::vector<std::string> adopt_legacy();

private:
    struct Install {
        std::string release;
        bool needs_verification{};
        std::vector<std::pair<std::string, std::uintmax_t>> files;
    };

    std::optional<Install> check_install(const GameEntry& game, bool defer_large = false) const;
    std::optional<Install> check_folder(const std::filesystem::path& folder,
                                        const std::filesystem::path& cache = {}, bool defer_large = false) const;
    void install(const GameEntry& game, const ImportResult& result,
                 const std::filesystem::path& source);
    // Installs files `fill` writes into an empty staging folder (returning their release):
    // large imports and folder takeovers. The manifest's checksums are of the written files.
    void install_folder(const GameEntry& game, const std::filesystem::path& source,
                        const std::function<std::string(const std::filesystem::path&)>& fill,
                        const ImportProgress& progress = {});
    void write_verified(const std::filesystem::path& target, const ImportResult& result,
                        const std::filesystem::path& source);
    // Keeps one disk of a game on several disks; installs the game when all
    // disks are present. Returns the outcome for the user.
    Outcome import_part(std::size_t index, const ImportResult& result, const std::filesystem::path& source);
    std::filesystem::path parts_directory(const GameEntry& game) const;

    std::filesystem::path root_;
    std::vector<GameEntry> games_;
    std::vector<std::optional<Install>> installs_;
};

} // namespace app
