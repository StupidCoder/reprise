#pragma once
#include "app/game_module.hpp"
#include "gamegear/music_player.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

// The games the shell can run, by catalog entry. Everything the shell needs
// to know about one game module goes through here: its asset pack, the ROM
// importer, starting the game or one of its levels, and its level list.
namespace shell {

// A level selector start point: the module's level number, the world it
// belongs to (consecutive entries of one world share a row), its label and
// what it starts (empty: an act from its entrance).
struct LevelChoice {
    int id;
    std::string world;
    std::string label;
    std::string hint{};
};

using ImportProgress = std::function<void(unsigned, unsigned)>;

struct Title {
    const char* name;      // shown on title cards, e.g. "Sonic 1"
    const char* platform;  // the original hardware, e.g. "Game Gear"
    const char* save_name; // quick save file name
    const char* supported; // the supported ROM, for messages
    std::function<std::filesystem::path(const std::filesystem::path& root)> pack;
    std::function<bool(const std::filesystem::path& root)> installed;
    std::function<void(const std::filesystem::path& pack)> validate;
    std::function<bool(const std::filesystem::path& image)> recognizes;
    std::function<void(const std::filesystem::path& image, const std::filesystem::path& root,
                       ImportProgress)>
        import;
    std::function<std::unique_ptr<GameModule>(const std::filesystem::path& pack,
                                              std::span<const std::uint8_t> state)>
        make;
    std::function<std::unique_ptr<GameModule>(const std::filesystem::path& pack, int level)>
        make_level;
    std::vector<LevelChoice> levels;
    // The sound test's player on the game's asset pack, or empty when the
    // library offers no sound test for this game.
    std::function<std::unique_ptr<gg::MusicPlayer>(const std::filesystem::path& pack)> music;
    // The enhancements this game offers, in menu order. Settings are shared
    // between games; the ones a game does not list stay off for it.
    std::vector<Enhancement> enhancements;
};

// The title behind a catalog entry, or nullptr if it is not implemented.
const Title* title_for(int catalog_index);

} // namespace shell
