#include "app/titles.hpp"
#include "app/shell_ui.hpp"
#include "platform/sha256.hpp"
#include "sonic1_gg/catalog.hpp"
#include "sonic1_gg/data/content_store.hpp"
#include "sonic1_gg/module.hpp"
#include "sonic1_gg/music_player.hpp"
#include "sonic2_gg/catalog.hpp"
#include "sonic2_gg/data/content_store.hpp"
#include "sonic2_gg/data/rom.hpp"
#include "sonic2_gg/module.hpp"
#include "sonic2_gg/music_player.hpp"
#include "sonic_advance_gba/data/content_store.hpp"
#include "sonic_advance_gba/module.hpp"
#include "triple_trouble_gg/catalog.hpp"
#include "triple_trouble_gg/data/content_store.hpp"
#include "triple_trouble_gg/data/rom.hpp"
#include "triple_trouble_gg/module.hpp"
#include "triple_trouble_gg/music_player.hpp"
#include <fstream>
#include <iterator>
#include <vector>

namespace shell {
namespace fs = std::filesystem;

namespace {

// Whether `image` is `size` bytes with the given SHA-256 (the 512 KiB
// cartridges share a size).
bool is_image(const fs::path& image, std::uintmax_t size, const char* sha256) {
    std::error_code ec;
    if (fs::file_size(image, ec) != size) {
        return false;
    }
    std::ifstream f(image, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f),
                                          std::istreambuf_iterator<char>()};
    return platform::sha256(bytes) == sha256;
}

Title make_sonic1() {
    Title t{
        .name = "Sonic 1",
        .platform = "Game Gear",
        .save_name = "sonic-gg.quick",
        .supported = "Supported: Sonic 1 Game Gear Japan/USA, 256 KiB, CRC32 3E31CB8C.",
        .pack = [](const fs::path& root) { return sonic1_gg::pack_path(root); },
        .installed = [](const fs::path& root) { return sonic1_gg::pack_installed(root); },
        .validate = [](const fs::path& pack) { sonic1_gg::validate_pack(pack); },
        .recognizes =
            [](const fs::path& image) {
                std::error_code ec;
                return fs::file_size(image, ec) == 262144;
            },
        .import = [](const fs::path& image, const fs::path& root,
                     ImportProgress progress) { sonic1_gg::import_rom(image, root, progress); },
        .make =
            [](const fs::path& pack, std::span<const std::uint8_t> state) {
                return sonic1_gg::make_module(pack, state);
            },
        .make_level = [](const fs::path& pack,
                         int level) { return sonic1_gg::make_level_module(pack, unsigned(level)); },
        .levels = {},
        .music = [](const fs::path& pack) -> std::unique_ptr<gg::MusicPlayer> {
            return std::make_unique<sonic1_gg::MusicPlayer>(pack);
        },
        .enhancements = {Enhancement::shield_transparency, Enhancement::widescreen,
                         Enhancement::water, Enhancement::rings, Enhancement::lcd_screen}};
    for (const auto& l : sonic1_gg::levels) {
        const bool special = l.id >= 28 && l.id < sonic1_gg::ending_start;
        std::string hint =
            special ? "Plays the special stage with 3 lives, then goes on to Green Hill Act 2."
            : l.id == sonic1_gg::ending_start
                ? "The finale after the escape and the ending, without the emeralds."
            : l.id == sonic1_gg::good_ending_start
                ? "The finale after the escape and the ending, with all six emeralds."
                : "";
        t.levels.push_back({int(l.id), special ? "Special stages" : l.world,
                            special ? std::string("Stage ") + l.label : l.label, hint});
    }
    return t;
}

Title make_sonic2() {
    Title t{
        .name = "Sonic 2",
        .platform = "Game Gear",
        .save_name = "sonic2-gg.quick",
        .supported = "Supported: Sonic 2 Game Gear (World), 512 KiB, CRC32 95A18EC7.",
        .pack = [](const fs::path& root) { return sonic2_gg::pack_path(root); },
        .installed = [](const fs::path& root) { return sonic2_gg::pack_installed(root); },
        .validate = [](const fs::path& pack) { sonic2_gg::validate_pack(pack); },
        .recognizes =
            [](const fs::path& image) {
                return is_image(image, 524288, sonic2_gg::cartridge.sha256);
            },
        .import = [](const fs::path& image, const fs::path& root,
                     ImportProgress progress) { sonic2_gg::import_rom(image, root, progress); },
        .make =
            [](const fs::path& pack, std::span<const std::uint8_t> state) {
                return sonic2_gg::make_module(pack, state);
            },
        .make_level = [](const fs::path& pack,
                         int level) { return sonic2_gg::make_level_module(pack, unsigned(level)); },
        .levels = {},
        .music = [](const fs::path& pack) -> std::unique_ptr<gg::MusicPlayer> {
            return sonic2_gg::make_music_player(pack);
        },
        .enhancements = {Enhancement::widescreen, Enhancement::water, Enhancement::lcd_screen}};
    for (std::size_t i = 0; i < sonic2_gg::levels.size(); ++i) {
        t.levels.push_back({int(i), sonic2_gg::levels[i].world, sonic2_gg::levels[i].label});
    }
    t.levels.push_back({int(sonic2_gg::ending_start), "Ending", "Standard",
                        "The ending and credits, without all six emeralds."});
    t.levels.push_back({int(sonic2_gg::good_ending_start), "Ending", "All emeralds",
                        "The ending and credits, with all six emeralds."});
    return t;
}

Title make_triple_trouble() {
    Title t{
        .name = "Triple Trouble",
        .platform = "Game Gear",
        .save_name = "triple-trouble-gg.quick",
        .supported = "Supported: Sonic Triple Trouble Game Gear (USA, Europe, Brazil), 512 KiB, "
                     "CRC32 D23A2A93.",
        .pack = [](const fs::path& root) { return triple_trouble_gg::pack_path(root); },
        .installed = [](const fs::path& root) { return triple_trouble_gg::pack_installed(root); },
        .validate = [](const fs::path& pack) { triple_trouble_gg::validate_pack(pack); },
        .recognizes =
            [](const fs::path& image) {
                return is_image(image, 524288, triple_trouble_gg::cartridge.sha256);
            },
        .import =
            [](const fs::path& image, const fs::path& root, ImportProgress progress) {
                triple_trouble_gg::import_rom(image, root, progress);
            },
        .make =
            [](const fs::path& pack, std::span<const std::uint8_t> state) {
                return triple_trouble_gg::make_module(pack, state);
            },
        .make_level =
            [](const fs::path& pack, int level) {
                return triple_trouble_gg::make_level_module(pack, unsigned(level));
            },
        .levels = {},
        .music = [](const fs::path& pack) -> std::unique_ptr<gg::MusicPlayer> {
            return triple_trouble_gg::make_music_player(pack);
        },
        .enhancements = {Enhancement::widescreen, Enhancement::lcd_screen}};
    for (std::size_t i = 0; i < triple_trouble_gg::levels.size(); ++i) {
        t.levels.push_back(
            {int(i), triple_trouble_gg::levels[i].world, triple_trouble_gg::levels[i].label});
    }
    for (unsigned i = 0; i < triple_trouble_gg::special_stage_count; ++i) {
        t.levels.push_back({int(triple_trouble_gg::special_stage_start + i), "Special stages",
                            "Stage " + std::to_string(i + 1),
                            "Plays the special stage with 3 lives, then Great Turquoise Act 1."});
    }
    t.levels.push_back({int(triple_trouble_gg::ending_start), "Ending", "Standard",
                        "The rival's short scene, as without all six emeralds."});
    t.levels.push_back({int(triple_trouble_gg::good_ending_start), "Ending", "All emeralds",
                        "The ending and credits, with all six emeralds."});
    return t;
}

// Sonic Advance runs from power-on only: no level selector, sound test,
// quick saves or enhancements yet.
Title make_sonic_advance() {
    return Title{
        .name = "Sonic Advance",
        .platform = "Game Boy Advance",
        .save_name = "sonic-advance-gba.quick",
        .supported = "Supported: Sonic Advance Game Boy Advance (USA), 8 MiB, CRC32 63F70FD8.",
        .pack = [](const fs::path& root) { return sonic_advance_gba::pack_path(root); },
        .installed = [](const fs::path& root) { return sonic_advance_gba::pack_installed(root); },
        .validate = [](const fs::path& pack) { sonic_advance_gba::validate_pack(pack); },
        .recognizes =
            [](const fs::path& image) { return sonic_advance_gba::is_supported_rom(image); },
        .import =
            [](const fs::path& image, const fs::path& root, ImportProgress progress) {
                sonic_advance_gba::import_rom(image, root, progress);
            },
        .make =
            [](const fs::path& pack, std::span<const std::uint8_t> state) {
                return sonic_advance_gba::make_module(pack, state);
            },
        .make_level = {},
        .levels = {},
        .music = {},
        .enhancements = {}};
}

} // namespace

const Title* title_for(int catalog_index) {
    static const Title sonic1 = make_sonic1();
    static const Title sonic2 = make_sonic2();
    static const Title triple_trouble = make_triple_trouble();
    static const Title sonic_advance = make_sonic_advance();
    switch (catalog_index) {
    case 0:
        return &sonic1;
    case 1:
        return &sonic2;
    case 2:
        return &triple_trouble;
    case 9:
        return &sonic_advance;
    default:
        return nullptr;
    }
}

} // namespace shell
