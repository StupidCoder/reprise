// Exports Fort Apocalypse's decoded graphics data from an installed content
// folder: PNG pictures for people, and raw dumps laid out like the memory
// the game builds, for comparison with the game's own code running in an
// emulator (.analysis/fort_apocalypse_c64/tools).
//
// Usage: fort_apocalypse_assets <content folder> <output folder>

#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/preview.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {
using namespace fort_apocalypse::data;

// Character sets as at $5000-$5FFF: the HUD set at $5000, the playfield
// set at $5800 (each slot has room for 256 characters; the game uses 128).
std::vector<std::uint8_t> charset_dump(const Assets& a) {
    std::vector<std::uint8_t> out(0x1000);
    std::size_t at = 0;
    for (const auto* set : {&a.hud_charset, &a.playfield_charset}) {
        for (std::size_t c = 0; c < set->size(); ++c) {
            std::ranges::copy((*set)[c], out.begin() + std::ptrdiff_t(at + c * 8));
        }
        at += 0x800;
    }
    return out;
}

// Sprite blocks as at $4000-$43FF: shape n in block n + 1, each of its 18
// rows as [left][right][0].
std::vector<std::uint8_t> sprite_dump(const Assets& a) {
    std::vector<std::uint8_t> out(0x400);
    for (std::size_t n = 0; n < a.helicopter.size(); ++n) {
        for (std::size_t row = 0; row < SpriteShape::height; ++row) {
            out[(n + 1) * 64 + row * 3] = std::uint8_t(a.helicopter[n].rows[row] >> 8);
            out[(n + 1) * 64 + row * 3 + 1] = std::uint8_t(a.helicopter[n].rows[row]);
        }
    }
    return out;
}

// Terrain rows of 256 cells, the radar's 40 lines of 40 bytes, then the
// terrain and radar spills as value, count (16-bit little-endian) each.
std::vector<std::uint8_t> level_dump(const Level& level) {
    std::vector<std::uint8_t> out;
    for (const auto& row : level.terrain.cells) {
        out.insert(out.end(), row.begin(), row.end());
    }
    for (const auto& line : level.radar.bytes) {
        out.insert(out.end(), line.begin(), line.end());
    }
    for (const auto& spill : {level.terrain_spill, level.radar_spill}) {
        out.insert(out.end(), {spill.value, std::uint8_t(spill.count), std::uint8_t(spill.count >> 8)});
    }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: fort_apocalypse_assets <content folder> <output folder>\n");
        return 2;
    }
    try {
        const auto assets = load_assets(platform::utf8_path(argv[1]));
        const auto out = platform::utf8_path(argv[2]);
        std::filesystem::create_directories(out);
        platform::write_file(out / "charsets.bin", charset_dump(assets));
        platform::write_file(out / "sprites.bin", sprite_dump(assets));
        std::vector<std::uint8_t> hud;
        for (const auto& row : assets.hud.cells) {
            hud.insert(hud.end(), row.begin(), row.end());
        }
        platform::write_file(out / "hud.bin", hud);
        platform::write_file(out / "bullets.bin", assets.bullet_dots);

        platform::save_png(charset_sheet(assets.hud_charset, play_colours(assets, 0)),
                           out / "charset-hud.png");
        platform::save_png(charset_sheet(assets.playfield_charset, play_colours(assets, 0)),
                           out / "charset-playfield.png");
        platform::save_png(helicopter_sheet(assets, 7), out / "helicopter.png");
        platform::save_png(hud_image(assets, 0), out / "hud.png");
        for (int n = 0; n < int(assets.levels.size()); ++n) {
            const auto suffix = std::to_string(n);
            platform::write_file(out / ("level" + suffix + ".bin"),
                                 level_dump(assets.levels[std::size_t(n)]));
            platform::save_png(terrain_image(assets, n), out / ("terrain-level" + suffix + ".png"));
            platform::save_png(radar_image(assets, n), out / ("radar-level" + suffix + ".png"));
        }
        std::printf("Exported to %s\n", out.string().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fort_apocalypse_assets: %s\n", e.what());
        return 1;
    }
}
