#pragma once
#include "app/game_entry.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The games and what the user has imported. Imported content lives in
// <data>/games/<id>/: the extracted files plus a manifest ("release.txt")
// naming the release and the SHA-256 of every file. An import is written to
// <data>/staging/<id>/, read back and checked, then moved into place in one
// step, so a failed import never damages an earlier one.
namespace app {

class Library {
public:
    Library(std::filesystem::path data_directory, std::vector<GameEntry> games);

    const std::vector<GameEntry>& games() const { return games_; }

    // Installed and intact (every file matches the manifest).
    bool installed(std::size_t game) const { return installs_[game].has_value(); }
    // The recognised release, for an installed game.
    std::string release(std::size_t game) const;
    // The installed files with their sizes, for an installed game.
    std::vector<std::pair<std::string, std::uintmax_t>> installed_files(std::size_t game) const;
    std::filesystem::path content_directory(std::size_t game) const;

    struct Outcome {
        bool ok{};
        std::optional<std::size_t> game; // the game the image was installed for
        std::string message;             // for the user
    };

    // Imports an image. The preferred game (the one selected in the
    // launcher) is tried first, then every other importable game, so a
    // dropped image lands with the game it belongs to.
    Outcome import(const std::filesystem::path& image, std::optional<std::size_t> preferred);

    // Deletes a game's imported content.
    void remove(std::size_t game);

private:
    struct Install {
        std::string release;
        std::vector<std::pair<std::string, std::uintmax_t>> files;
    };

    std::optional<Install> check_install(const GameEntry& game) const;
    void install(const GameEntry& game, const ImportResult& result,
                 const std::filesystem::path& source);

    std::filesystem::path root_;
    std::vector<GameEntry> games_;
    std::vector<std::optional<Install>> installs_;
};

} // namespace app
