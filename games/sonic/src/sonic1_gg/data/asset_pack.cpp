#include "sonic1_gg/data/asset_pack.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/flow_assets.hpp"

// Writes every act, room and special stage plus the frontend and sound data.
// Pack folders: the pack root holds Green Hill 1 and the shared data; other
// acts live in act2, act3, bridge1-3, jungle, jungle2-3, labyrinth1-3,
// special1-8 and stage12..26 (Scrap Brain, Sky Base and their linked rooms).
namespace sonic1_gg {

void extract_asset_pack(const Rom& rom, PackWriter& out, ExtractProgress progress) {
    unsigned done = 0;
    auto save = [&](const Level& level, const std::string& folder) {
        save_level(level, out, folder);
        if (progress) {
            progress(++done, 34);
        }
    };
    save(extract_level(rom, 0), "");
    save(sonic1_gg::extract_level(rom, 1), "act2");
    save(sonic1_gg::extract_level(rom, 2), "act3");
    save(sonic1_gg::extract_level(rom, 6), "jungle");
    for (unsigned act = 7; act < 9; ++act) {
        save(sonic1_gg::extract_level(rom, act), "jungle" + std::to_string(act - 5));
    }
    for (unsigned act = 3; act < 6; ++act) {
        save(sonic1_gg::extract_level(rom, act), "bridge" + std::to_string(act - 2));
    }
    for (unsigned act = 9; act < 12; ++act) {
        save(sonic1_gg::extract_level(rom, act), "labyrinth" + std::to_string(act - 8));
    }
    for (unsigned act = 28; act < 36; ++act) {
        save(sonic1_gg::extract_level(rom, act), "special" + std::to_string(act - 27));
    }
    for (unsigned act : {12u, 13u, 14u, 15u, 16u, 17u, 18u, 20u, 21u, 22u, 23u, 24u, 25u, 26u}) {
        save(sonic1_gg::extract_level(rom, act), "stage" + std::to_string(act));
    }
    sonic1_gg::save_sound(sonic1_gg::extract_sound(rom), out);
    sonic1_gg::save_flow(sonic1_gg::extract_flow(rom), out);
}
} // namespace sonic1_gg
