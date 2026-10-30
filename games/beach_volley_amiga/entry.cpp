#include "beach_volley_amiga/entry.hpp"
#include "beach_volley_amiga/data/assets.hpp"
#include "beach_volley_amiga/data/preview.hpp"
#include "beach_volley_amiga/front/intro.hpp"
#include "beach_volley_amiga/game/module.hpp"
#include "beach_volley_amiga/import/disk_import.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include <string>

namespace beach_volley {
namespace {

std::vector<app::Preview> previews(const std::filesystem::path& content) {
    const auto assets = data::load_assets(content);
    std::vector<app::Preview> result;
    for (const auto& court : assets.courts) {
        const auto n = std::to_string(court.number);
        result.push_back({"Court " + n + " (terrain" + n + ", 576 x 232)", data::to_image(court.picture, court.palette)});
    }
    for (const auto& court : assets.courts) {
        result.push_back({"Court " + std::to_string(court.number) + ": palette", data::palette_image(court.palette)});
    }
    // The intro's pictures in the palettes it shows them in.
    const auto files = data::load_intro_files(content);
    const auto art = front::decode_intro(files);
    result.push_back({"Intro: Ocean logo (304 x 83)", data::to_image(art.ocean, art.gold_blue)});
    result.push_back({"Intro: trophy (trophe, 224 x 399)", data::to_image(art.trophy, art.gold_blue)});
    result.push_back({"Intro: \"PRESENT\"", data::to_image(art.present, art.gold_blue)});
    result.push_back({"Intro: small Ocean logo", data::to_image(art.small_ocean, art.gold_red)});
    result.push_back({"Intro: \"Beach\"", data::to_image(art.beach, art.gold_white)});
    result.push_back({"Intro: \"Volley\"", data::to_image(art.volley, art.gold_white)});
    result.push_back({"Intro: credits", data::to_image(art.credits, art.gold_blue)});
    for (std::size_t i = 0; i < art.pages.size(); ++i)
        result.push_back({"Intro: comic page " + std::to_string(i + 1), data::to_image(art.pages[i], art.comic)});
    result.push_back({"Intro: \"F1 1PLAYER\"", data::to_image(art.choices[0], art.comic)});
    result.push_back({"Intro: \"F2 2PLAYERS\"", data::to_image(art.choices[1], art.comic)});
    // The comic's voices and the music's 15 samples (each up to the next
    // one's start), at the rate of the note C-2 (period $1AC).
    constexpr double c2_rate = 3546895.0 / 0x1AC;
    for (std::size_t i = 0; i < art.voices.size(); ++i) {
        const auto& v = art.voices[i];
        const auto bytes = std::span(files.block).subspan(v.offset, v.length * 2u);
        app::Preview voice{"Intro: voice " + std::to_string(i + 1), platform::Image(1, 1)};
        voice.sound = app::PreviewSound::signed8(
            std::span(reinterpret_cast<const std::int8_t*>(bytes.data()), bytes.size()), 3546895.0 / v.period);
        voice.image = data::waveform_image(*voice.sound);
        result.push_back(std::move(voice));
    }
    const auto tables = sound::read_player_tables(files.code, front::intro_sample_table, front::intro_period_table,
                                                  files.instruments.size());
    for (std::size_t n = 0; n < 15; ++n) {
        const std::size_t from = tables.sample_starts[n];
        const std::size_t to = n + 1 < 15 ? tables.sample_starts[n + 1] : files.instruments.size();
        const auto bytes = std::span(files.instruments).subspan(from, to - from);
        app::Preview sample{"Music: sample " + std::to_string(n + 1), platform::Image(1, 1)};
        sample.sound = app::PreviewSound::signed8(
            std::span(reinterpret_cast<const std::int8_t*>(bytes.data()), bytes.size()), c2_rate);
        sample.image = data::waveform_image(*sample.sound);
        result.push_back(std::move(sample));
    }
    return result;
}

} // namespace

// Editorial text (year, description, history) is kept with the other
// catalog notes in app/catalog.cpp; sources in docs/catalog-history.md.
app::GameEntry entry() {
    return {
        .id = "beach_volley_amiga",
        .title = "Beach Volley",
        .machine = app::Machine::amiga,
        .media = "the disk image (ADF)",
        .import = import::import_disk,
        .previews = previews,
        .start = game::start,
        .progress = "The intro, the menu and the one-player tour: each venue's card, its match with music and effects, the winner's picture, through all eight venues; the ending and two players are not rebuilt yet.",
    };
}

} // namespace beach_volley
