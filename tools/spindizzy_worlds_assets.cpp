// Spindizzy Worlds research tool: imports the disk image into a content
// folder and exports the decoded data (.analysis/spindizzy_worlds_amiga/formats.md).
//
//   spindizzy_worlds_assets import <image.adf> <content folder>
//   spindizzy_worlds_assets export <content folder> <output folder>
//   spindizzy_worlds_assets gridcheck <content folder> <memory.bin>
//   spindizzy_worlds_assets viewcheck <content folder> <memory.bin> <out.png>
//
// gridcheck (research): reads the world and level the original is in from
// a reference machine's chip-memory dump ($251C(a5), $251E(a5), A5 =
// $616E8), builds that level natively and compares every cell with the
// original's grid at A5+$E.
//
// viewcheck (research): draws the same level's playfield natively at the
// original's scroll position ($2570(a5) − 16, $2572(a5)) and compares it
// with the displayed buffer ($63740) above the panel; writes original,
// native and difference side by side. Sprites (the explorer, objects,
// stars) are drawn as far as they are rebuilt: the explorer natively,
// objects from the original's records; the rest shows as differences.

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/data/preview.hpp"
#include "spindizzy_worlds_amiga/import/disk_import.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include "spindizzy_worlds_amiga/world/object_kind.hpp"
#include <map>
#include <cstdio>
#include <exception>
#include <string>

namespace {

constexpr std::uint32_t a5 = 0x616E8;

int grid_check(const spindizzy::data::Assets& assets, const std::vector<std::uint8_t>& m) {
    using namespace spindizzy;
    const auto word = [&](std::uint32_t offset) { return int(std::int16_t(m.at(a5 + offset) << 8 | m.at(a5 + offset + 1))); };
    const int world_number = word(0x251C), level_number = word(0x251E);
    const auto& world = assets.worlds.at(std::size_t(world_number));
    std::printf("world %d (%s), level %d\n", world_number, world.name.c_str(), level_number);
    world::Switches switches;
    switches.conditions_enabled = word(0x361A) != 0;
    switches.jewels_left = (word(0x2566) | word(0x2568)) != 0;
    switches.primary = m.at(a5 + 0x3616);
    switches.secondary = m.at(a5 + 0x3617);
    switches.binary = m.at(a5 + 0x3619); // low byte of the word $3618
    const auto level = world::build_level(world, level_number, assets.block_attributes, switches);
    int differ = 0, occupied = 0;
    for (int x = 0; x < world::level_size; ++x)
        for (int y = 0; y < world::level_size; ++y)
            for (int z = 0; z < world::level_height; ++z) {
                const int original = m.at(a5 + 0xE + std::uint32_t(x * 256 + y * 8 + z));
                const int native = level.grid.at(x, y, z);
                occupied += original != 0;
                if (original != native && ++differ <= 12)
                    std::printf("  x %2d y %2d z %d: original %02X native %02X\n", x, y, z, original, native);
            }
    std::printf("cells: %d differ (%d occupied in the original); %zu objects, %zu conditions\n", differ, occupied,
                level.objects.size(), level.conditions.size());
    return 0;
}

int view_check(const spindizzy::data::Assets& assets, const std::vector<std::uint8_t>& m,
               const std::filesystem::path& out) {
    using namespace spindizzy;
    const auto word = [&](std::uint32_t at) { return int(m.at(at) << 8 | m.at(at + 1)); };
    const auto& world = assets.worlds.at(std::size_t(word(a5 + 0x251C)));
    const int level_number = word(a5 + 0x251E);
    const auto level = world::build_level(world, level_number, assets.block_attributes);
    const auto map = view::TileMap::build(level.grid, assets.graphics);
    const int left = int(std::int16_t(word(a5 + 0x2570))) - 16, top = int(std::int16_t(word(a5 + 0x2572)));
    constexpr int rows = 150; // the playfield above the panel
    view::Canvas canvas(320, rows);
    view::draw_playfield(canvas, map, assets.graphics, left, top);
    // The objects, from the original's records ($25CC(a5), $40 bytes each):
    // their places (+$12) and keys (+$16) as their code left them, drawn
    // with the record's base frame (+$1A); row counts by handler (research
    // table: the kinds' own code picks frames and sizes).
    const auto u32 = [&](std::uint32_t at) { return std::uint32_t(word(at)) << 16 | std::uint32_t(word(at + 2)); };
    for (int i = 1; i < 31; ++i) {
        const std::uint32_t record = a5 + 0x25CC + std::uint32_t(i) * 0x40;
        const auto handler = u32(record + 4), frame_at = u32(record + 0x1A);
        int rows = 0;
        if (handler == 0x14BCA) rows = 16;
        if (handler == 0x15128 || handler == 0x16112) rows = 13;
        if (rows == 0 || !assets.main.contains(frame_at, std::uint32_t(rows) * 20)) continue;
        const view::SpritePlace place{int(std::int16_t(word(record + 0x12))), int(std::int16_t(word(record + 0x14))),
                                      std::uint16_t(word(record + 0x16))};
        view::draw_sprite(canvas, data::read_sprite_frame(assets.main, frame_at, 2, rows), place, map,
                          assets.graphics, left, top);
    }
    // The explorer, natively from its world position ($24C6) and spin step.
    const view::WorldPosition explorer{int(std::int16_t(word(a5 + 0x24C6))), int(std::int16_t(word(a5 + 0x24C8))),
                                       int(std::int16_t(word(a5 + 0x24CA)))};
    const int step = word(a5 + 0x25CC + 0x1E) & 0xC;
    const auto frame = data::read_sprite_frame(
        assets.main, view::explorer_frames + std::uint32_t(step) * 0x28, 1, 16);
    view::draw_sprite(canvas, frame, view::explorer_place(explorer), map, assets.graphics, left, top);
    const std::uint32_t buffer = std::uint32_t(word(0x63740)) << 16 | std::uint32_t(word(0x63742));
    const auto& palette = assets.graphics.palettes[std::size_t(word(a5 + 0x207E) & 3)];
    platform::Image image(3 * 320, rows);
    int differ = 0;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < 320; ++x) {
            int original = 0;
            for (int p = 0; p < 4; ++p)
                original |= (m.at(buffer + std::uint32_t(p * 0x1F40 + y * 40 + x / 8)) >> (7 - x % 8) & 1) << p;
            const int native = canvas.at(x, y);
            image.put(x, y, data::to_color(palette[std::size_t(original)]));
            image.put(320 + x, y, data::to_color(palette[std::size_t(native)]));
            image.put(640 + x, y, original == native ? platform::Color{40, 40, 40} : platform::Color{255, 0, 0});
            differ += original != native;
        }
    std::printf("world %s level %d at (%d, %d): %d of %d pixels differ\n", world.name.c_str(), level_number, left, top,
                differ, 320 * rows);
    platform::save_png(image, out);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    using namespace spindizzy::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = spindizzy::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (const auto& file : result.files) platform::write_file(out / file.name, file.bytes);
            std::printf("%s: %zu files\n", result.release.c_str(), result.files.size());
            return 0;
        }
        if (command == "export" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            platform::save_png(title_image(assets.title), out / "title.png");
            for (const auto& world : assets.worlds) {
                std::printf("world %2d %-12s setting %04X  %2zu levels  %2zu macros  ", world.number, world.name.c_str(),
                            world.setting, world.levels.size(), world.macros.size());
                std::printf("\n");
                for (const auto& [id, record] : world.levels) {
                    const auto level = spindizzy::world::build_level(world, id, assets.block_attributes);
                    std::map<int, int> kinds;
                    for (const auto& object : level.objects) ++kinds[object.kind];
                    std::printf("    level %2d start %2d,%2d:", id, world.header(id).start_x(), world.header(id).start_y());
                    for (const auto& [kind, count] : kinds) {
                        std::printf(" %s×%d", std::string(spindizzy::world::object_kind_name(kind)).c_str(), count);
                    }
                    std::printf("\n");
                }
                char name[32];
                std::snprintf(name, sizeof name, "world%02d.png", world.number);
                platform::save_png(world_image(world, assets.block_attributes), out / name);
                for (const auto& [id, record] : world.levels) {
                    std::snprintf(name, sizeof name, "world%02d_level%02d.png", world.number, id);
                    platform::save_png(level_image(world, id, assets), out / name);
                }
            }
            return 0;
        }
        if (command == "viewcheck" && argc == 5) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return view_check(assets, platform::read_file(platform::utf8_path(argv[3])), platform::utf8_path(argv[4]));
        }
        if (command == "gridcheck" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return grid_check(assets, platform::read_file(platform::utf8_path(argv[3])));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: spindizzy_worlds_assets import <image.adf> <content> | export <content> <out> | "
                         "gridcheck <content> <memory.bin> | viewcheck <content> <memory.bin> <out.png>\n");
    return 2;
}
