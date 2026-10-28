#include "fa_18_interceptor_amiga/entry.hpp"
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/preview.hpp"
#include "fa_18_interceptor_amiga/import/disk_import.hpp"
#include <cstdio>
#include <string>

namespace interceptor {
namespace {

// The main menu as $101D30 sets it up: palette 0 ($103C1C with $119FCC),
// lines 12 rows apart ($1E0 bytes into $13AA2E) and blocks 6, 100-109.
const std::vector<int> main_menu = {6, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109};

std::string first_words(const data::TextBlock& block) {
    for (const auto& line : block.lines) {
        std::string text = line.text;
        const auto start = text.find_first_not_of(' ');
        if (start == std::string::npos) continue;
        text = text.substr(start);
        return text.size() > 40 ? text.substr(0, 40) + "…" : text;
    }
    return {};
}

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    using namespace data;
    const auto assets = load_assets(content);
    std::vector<app::Preview> result;
    for (const auto& picture : assets.pictures) result.push_back({picture.title, picture_image(picture.picture)});
    result.push_back({"Main menu (screen texts, palette 0)", text_screen(assets, main_menu, 0, 12)});
    for (const auto& block : assets.texts) {
        bool in_menu = false;
        for (int n : main_menu) in_menu = in_menu || n == block.number;
        const auto title = first_words(block);
        if (in_menu || title.empty()) continue; // blank blocks are filled in at run time (e.g. the call sign, $10370A)
        result.push_back({"Screen text " + std::to_string(block.number) + ": " + title,
                          text_screen(assets, {block.number}, 0)});
    }
    result.push_back({"Cockpit messages", message_sheet(assets)});
    result.push_back({"Fonts (cockpit, menus)", font_sheet(assets)});
    result.push_back({"Palettes", palette_sheet(assets)});
    for (const auto& sound : assets.sounds) {
        char title[160];
        std::snprintf(title, sizeof title, "Sound %s: %zu bytes, slot %d, period %d (%d Hz)", sound.title.c_str(),
                      sound.samples.size(), sound.slot, sound.period, int(3546895.0 / sound.period + 0.5));
        result.push_back({title, waveform_image(sound)});
    }
    return result;
}

} // namespace

app::GameEntry entry() {
    return {
        .id = "fa_18_interceptor_amiga",
        .title = "F/A-18 Interceptor",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .start = {},
        .progress = "Disk import (program and every data file checked) and decoded data: the title and "
                    "instrument pictures, the menu and briefing screen texts, cockpit messages, both fonts, "
                    "palettes and the sampled sounds. Nothing playable yet.",
        .release_year = 1988,
        .description = "Take to the skies in a modern jet fighter. Learn the aircraft, practise your flying and tackle combat missions where a steady hand matters as much as a quick reaction.",
        .history_title = "An Amiga flight simulator",
        .history = "Electronic Arts published the Amiga release in 1988. Intellisoft developed it, with design credited to Bob Dinnerman and Moses Ma, programming to Dinnerman, and music to Dave Warhol.",
        .history_sources = "MobyGames: Amiga release; Lemon Amiga: development credits.",
    };
}

} // namespace interceptor
