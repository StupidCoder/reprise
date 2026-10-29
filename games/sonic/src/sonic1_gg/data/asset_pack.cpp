#include "sonic1_gg/data/asset_pack.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/flow_assets.hpp"

// Writes every act, room and special stage plus the frontend and sound data.
// Pack folders: the pack root holds Green Hill 1 and the shared data; other
// acts live in act2, act3, bridge1-3, jungle, jungle2-3, labyrinth1-3,
// special1-8 and stage12..26 (Scrap Brain, Sky Base and their linked rooms).
namespace sonic1_gg {

void extract_asset_pack(const Rom& rom, const std::filesystem::path& directory,
                        ExtractProgress progress) {
    unsigned done = 0;
    auto save = [&](const Level& level, const std::filesystem::path& path) {
        save_level(level, path);
        if (progress) {
            progress(++done, 34);
        }
    };
    auto level = extract_level(rom, 0);
    save(level, directory);
    save(sonic1_gg::extract_level(rom, 1), std::filesystem::path(directory) / "act2");
    save(sonic1_gg::extract_level(rom, 2), std::filesystem::path(directory) / "act3");
    save(sonic1_gg::extract_level(rom, 6), std::filesystem::path(directory) / "jungle");
    for (unsigned act = 7; act < 9; ++act) {
        save(sonic1_gg::extract_level(rom, act),
             std::filesystem::path(directory) / ("jungle" + std::to_string(act - 5)));
    }
    for (unsigned act = 3; act < 6; ++act) {
        save(sonic1_gg::extract_level(rom, act),
             std::filesystem::path(directory) / ("bridge" + std::to_string(act - 2)));
    }
    for (unsigned act = 9; act < 12; ++act) {
        save(sonic1_gg::extract_level(rom, act),
             std::filesystem::path(directory) / ("labyrinth" + std::to_string(act - 8)));
    }
    for (unsigned act = 28; act < 36; ++act) {
        save(sonic1_gg::extract_level(rom, act),
             std::filesystem::path(directory) / ("special" + std::to_string(act - 27)));
    }
    for (unsigned act : {12u, 13u, 14u, 15u, 16u, 17u, 18u, 20u, 21u, 22u, 23u, 24u, 25u, 26u}) {
        save(sonic1_gg::extract_level(rom, act),
             std::filesystem::path(directory) / ("stage" + std::to_string(act)));
    }
    sonic1_gg::save_sound(sonic1_gg::extract_sound(rom), directory);
    sonic1_gg::save_flow(sonic1_gg::extract_flow(rom), directory);
}
} // namespace sonic1_gg
