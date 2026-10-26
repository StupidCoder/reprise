// Exports Wizball's decoded data from an installed content folder: PNG
// pictures for people, and raw dumps laid out like the memory the game
// reads, for comparison with the original in an emulator
// (.analysis/wizball_c64).
//
// Usage: wizball_assets <content folder> <output folder>

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/data/preview.hpp"
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace wizball::data;
    if (argc != 3) {
        std::fprintf(stderr, "usage: wizball_assets <content folder> <output folder>\n");
        return 2;
    }
    try {
        const auto assets = load_assets(platform::utf8_path(argv[1]));
        const auto out = platform::utf8_path(argv[2]);
        std::filesystem::create_directories(out);
        for (int level = 0; level < Assets::level_count; ++level) {
            const auto name = "level" + std::to_string(level);
            platform::save_png(level_image(assets, level, false), out / (name + "-grey.png"));
            platform::save_png(level_image(assets, level, true), out / (name + "-painted.png"));
            platform::save_png(play_charset_sheet(assets, level), out / (name + "-charset.png"));
            // The playfield charset as at $F800-$FFF7 in that place.
            std::vector<std::uint8_t> charset;
            for (int c = 0; c < 0xA0; ++c) charset.insert(charset.end(), assets.play_charset[std::size_t(c)].begin(), assets.play_charset[std::size_t(c)].end());
            for (const auto& g : assets.places[std::size_t(level)].characters) charset.insert(charset.end(), g.begin(), g.end());
            platform::write_file(out / (name + "-charset.bin"), charset);
        }
        for (int p = 0; p < int(assets.places.size()); ++p) {
            const auto& place = assets.places[std::size_t(p)];
            std::printf("place %2d: map row %2d, column %2d, characters $%04X, painted $%02X $%02X $%02X\n", p,
                        place.first_row, place.first_column, place.characters_address, place.painted.shared1,
                        place.painted.shared2, place.painted.character);
            if (p >= Assets::level_count) {
                platform::save_png(place_image(assets, p), out / ("place" + std::to_string(p) + ".png"));
            }
        }
        platform::save_png(tile_sheet(assets, 0), out / "tiles.png");
        platform::save_png(front_charset_sheet(assets), out / "front-charset.png");
        platform::save_png(sprite_sheet(assets), out / "sprites.png");
        std::vector<std::uint8_t> front; // $F000-$F7FF with both flags clear
        for (const auto& g : assets.front_charset) front.insert(front.end(), g.begin(), g.end());
        platform::write_file(out / "front-charset.bin", front);
        std::printf("%zu tiles, %zu sprites. Exported to %s\n", assets.tiles.size(), assets.sprites.size(),
                    out.string().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wizball_assets: %s\n", e.what());
        return 1;
    }
}
