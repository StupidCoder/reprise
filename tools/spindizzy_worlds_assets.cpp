// Spindizzy Worlds research tool: imports the disk image into a content
// folder and exports the decoded data (games/spindizzy_worlds_amiga/game.md).
//
//   spindizzy_worlds_assets import <image.adf> <content folder>
//   spindizzy_worlds_assets export <content folder> <output folder>
//   spindizzy_worlds_assets gridcheck <content folder> <memory.bin>
//   spindizzy_worlds_assets viewcheck <content folder> <memory.bin> <out.png> [back]
//   spindizzy_worlds_assets physcheck <content folder> <trace.txt>
//   spindizzy_worlds_assets drive <content folder> <out folder> "<world name> <level>" BITS:FRAMES ...
//
// gridcheck (research): reads the world and level the original is in from
// a reference machine's chip-memory dump ($251C(a5), $251E(a5), A5 =
// $616E8), builds that level natively and compares every cell with the
// original's grid at A5+$E.
//
// viewcheck (research): draws the same level natively at the original's
// scroll position ($2570(a5) − 16, $2572(a5)): playfield, stars, objects
// and the explorer in depth order, the objects' run-time state (animation
// step, height, position, heading, shown/fired) taken from the original's
// object records; compares with the displayed buffer ($63740) above the
// panel (counting the shown columns 16-303) and writes original, native
// and difference side by side.
//
// physcheck (research): replays a reference machine's explorer trace
// (harness `sdw.py trace`: the grid at the first tick, then per tick the
// joystick byte and the explorer's variables before the tick) through the
// native movement rules from the first tick's state and compares every
// following tick; stops at the first difference. After a death it starts
// again from the original's next traced tick (after the respawn).
//
// drive: runs the shell's test drive headless with joystick bits per frame
// (1 up, 2 down, 4 left, 8 right, 10 space, 80 fire) and saves a picture
// after each BITS:FRAMES item.

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/data/preview.hpp"
#include "spindizzy_worlds_amiga/game/module.hpp"
#include "spindizzy_worlds_amiga/import/disk_import.hpp"
#include "spindizzy_worlds_amiga/play/explorer.hpp"
#include "spindizzy_worlds_amiga/view/objects.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"
#include "spindizzy_worlds_amiga/view/starfield.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include "spindizzy_worlds_amiga/world/object_kind.hpp"
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <map>
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
               const std::filesystem::path& out, bool back_buffer) {
    using namespace spindizzy;
    const auto word = [&](std::uint32_t at) { return int(m.at(at) << 8 | m.at(at + 1)); };
    const auto u32 = [&](std::uint32_t at) { return std::uint32_t(word(at)) << 16 | std::uint32_t(word(at + 2)); };
    const auto& world = assets.worlds.at(std::size_t(word(a5 + 0x251C)));
    const int level_number = word(a5 + 0x251E);
    world::Switches switches;
    switches.primary = m.at(a5 + 0x3616);
    switches.secondary = m.at(a5 + 0x3617);
    switches.binary = m.at(a5 + 0x3619);
    const auto level = world::build_level(world, level_number, assets.block_attributes);
    const auto map = view::TileMap::build(level.grid, assets.graphics);
    const int left = int(std::int16_t(word(a5 + 0x2570))) - 16, top = int(std::int16_t(word(a5 + 0x2572)));
    constexpr int rows = 150; // the playfield above the panel
    view::Canvas canvas(320, rows);
    view::draw_playfield(canvas, map, assets.graphics, left, top);

    // Stars ($15BD2) go on the background before the sprites; the running
    // generator's state is the dump's (used by seed 0 only).
    view::StarRandom running(u32(a5 + 0x2094));
    for (const auto& object : level.objects) {
        if (object.kind == int(world::ObjectKind::starfield)) {
            view::draw_stars(canvas, view::make_stars(assets.main, object, running));
        }
    }

    // The objects' run-time state from the records ($25CC(a5), $40 bytes
    // each). Records are made in level order into the first free slots, so
    // the n-th object among the kinds sharing a frame handler (+4, from the
    // table $148C2; creatures share $1467E) is the n-th such record.
    const auto handler_of = [&](int kind) { return assets.main.u32(assets.main.u32(0x148C2 + std::uint32_t(kind) * 4) + 4); };
    const auto state = [&](std::size_t index) {
        const auto& object = level.objects[index];
        int nth = 0;
        for (std::size_t j = 0; j < index; ++j) nth += handler_of(level.objects[j].kind) == handler_of(object.kind);
        view::ObjectState now;
        for (int i = 1; i < 31; ++i) {
            const std::uint32_t record = a5 + 0x25CC + std::uint32_t(i) * 0x40;
            if (u32(record + 4) != handler_of(object.kind) || nth-- > 0) continue;
            now.step = m.at(record + 0x1F);
            now.x = word(record + 8);
            now.y = word(record + 0xA);
            now.z = word(record + 0xC);
            now.heading = word(record + 0x28) & 3;
            now.shown = m.at(record + 0x20) != 0;
            break;
        }
        return now;
    };

    // The explorer, natively from its world position ($24C6) and spin step.
    const view::WorldPosition explorer{int(std::int16_t(word(a5 + 0x24C6))), int(std::int16_t(word(a5 + 0x24C8))),
                                       int(std::int16_t(word(a5 + 0x24CA)))};
    // Flashing: bchg #0,+$21 picked the +$280 set when the bit was 1, so
    // after the pass a clear bit means the alternate frame was drawn. The
    // special states ($24F5 bit 0 or 1 by slow mode, $24F2 > 0) take +$C80.
    const int counter = word(a5 + 0x25CC + 0x1E);
    const bool flashing = word(a5 + 0x2510) != 0;
    const bool flash_phase = flashing && !(m.at(a5 + 0x25CC + 0x21) & 1);
    const int state_bit = word(a5 + 0x24A6) != 0 ? 0 : 1;
    const bool special = ((m.at(a5 + 0x24F5) >> state_bit) & 1) || std::int16_t(word(a5 + 0x24F2)) > 0;
    // While arriving, the slot runs $12B04 with the arrival list.
    const bool arriving = u32(a5 + 0x25CC + 4) == 0x12B04;
    const std::uint32_t explorer_at =
        arriving ? view::explorer_frames + view::animation_offset(assets.main, view::explorer_arrival_list,
                                                                  m.at(a5 + 0x25CC + 0x1F))
                 : view::explorer_frame(counter, flash_phase, special);
    const view::ObjectSprite explorer_sprite{
        0, data::read_sprite_frame(assets.main, explorer_at, 1, 16),
        view::explorer_place(explorer), view::sort_value(explorer) + 0xC};
    view::draw_with_explorer(canvas, view::start_sprites(level, switches, assets.main, state), explorer_sprite, map,
                             assets.graphics, left, top);

    // The displayed buffer shows the last finished frame; a dump taken at
    // the end of the object drawing loop ($143DC, harness `sdw.py drawn`)
    // has the frame the records describe in the back buffer ($4(a5)).
    const std::uint32_t buffer = back_buffer ? u32(a5 + 4) : u32(0x63740);
    const auto& palette = assets.graphics.palettes[std::size_t(word(a5 + 0x207E) & 3)];
    platform::Image image(3 * 320, rows);
    // The display shows buffer columns 16-303 (288 pixels, the range the
    // stars use; the pillars cover the rest): differences are counted there,
    // the whole buffer is drawn.
    constexpr int shown_left = 16, shown_right = 304;
    int differ = 0;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < 320; ++x) {
            int original = 0;
            for (int p = 0; p < 4; ++p)
                original |= (m.at(buffer + std::uint32_t(p * 0x1F40 + y * 40 + x / 8)) >> (7 - x % 8) & 1) << p;
            const int native = canvas.at(x, y);
            image.put(x, y, data::to_color(palette[std::size_t(original)]));
            image.put(320 + x, y, data::to_color(palette[std::size_t(native)]));
            const bool shown = x >= shown_left && x < shown_right;
            image.put(640 + x, y, original == native ? platform::Color{40, 40, 40}
                                  : shown            ? platform::Color{255, 0, 0}
                                                     : platform::Color{90, 40, 40});
            differ += shown && original != native;
        }
    std::printf("world %s level %d at (%d, %d): %d of %d pixels differ\n", world.name.c_str(), level_number, left, top,
                differ, (shown_right - shown_left) * rows);
    platform::save_png(image, out);
    return 0;
}

// A traced tick: the joystick byte and the original's variables
// (A5+$24B8 … $251F, $2060 … $206F, $3616 … $3617).
struct TracedTick {
    std::uint8_t joystick{};
    std::vector<std::uint8_t> state;
    int word(int address) const {
        const int at = address >= 0x3616 ? 0x78 + address - 0x3616 : address >= 0x24B8 ? address - 0x24B8 : 0x68 + address - 0x2060;
        return std::int16_t(state.at(std::size_t(at)) << 8 | state.at(std::size_t(at) + 1));
    }
    std::int32_t longword(int address) const {
        return std::int32_t(std::uint32_t(std::uint16_t(word(address))) << 16 | std::uint16_t(word(address + 2)));
    }
};

std::vector<std::uint8_t> from_hex(const std::string& text) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) bytes.push_back(std::uint8_t(std::stoi(text.substr(i, 2), nullptr, 16)));
    return bytes;
}

spindizzy::play::Explorer traced_explorer(const TracedTick& t) {
    spindizzy::play::Explorer e;
    e.x = std::int16_t(t.word(0x24C6)); e.y = std::int16_t(t.word(0x24C8)); e.z = std::int16_t(t.word(0x24CA));
    e.respawn_x = std::int16_t(t.word(0x24CC)); e.respawn_y = std::int16_t(t.word(0x24CE)); e.respawn_z = std::int16_t(t.word(0x24D0));
    e.vx = t.longword(0x24D4); e.vy = t.longword(0x24D8); e.vz = t.longword(0x24DC);
    e.friction = std::int16_t(t.word(0x24E2));
    e.brake_lock = t.word(0x24EC);
    e.arrival_ticks = std::int16_t(t.word(0x24F2));
    e.timer_a = std::int16_t(t.word(0x24F4)); e.timer_b = std::int16_t(t.word(0x24F6));
    e.column_x = t.word(0x24FE) >> 8 & 31; e.column_y = t.word(0x24FE) >> 3 & 31;
    e.floor_base = std::int16_t(t.word(0x2504));
    e.wall_level = t.word(0x250A);
    e.floor_block = t.word(0x250E);
    e.contact = std::uint16_t(t.word(0x2510)); e.previous_contact = std::uint16_t(t.word(0x2512));
    e.hot_ticks = t.word(0x2514);
    e.on_surface = t.longword(0x2516) != 0x13222;
    return e;
}

int phys_check(const spindizzy::data::Assets& assets, const std::filesystem::path& path) {
    using namespace spindizzy;
    std::ifstream in(path);
    std::string line;
    world::LevelGrid grid;
    std::vector<TracedTick> ticks;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string first, hex;
        fields >> first >> hex;
        if (first == "grid") {
            const auto cells = from_hex(hex);
            for (int x = 0; x < world::level_size; ++x)
                for (int y = 0; y < world::level_size; ++y)
                    for (int z = 0; z < world::level_height; ++z)
                        grid.set(x, y, z, cells.at(std::size_t(x * 256 + y * 8 + z)));
            continue;
        }
        ticks.push_back({std::uint8_t(std::stoi(first, nullptr, 16)), from_hex(hex)});
    }
    if (ticks.empty()) throw std::runtime_error("empty trace");
    const auto surfaces = play::read_surfaces(assets.main);
    world::Switches switches;
    switches.primary = std::uint8_t(ticks[0].word(0x3616) >> 8);
    switches.secondary = std::uint8_t(ticks[0].word(0x3616));
    play::Ground ground{grid, surfaces, std::uint16_t(ticks[0].word(0x24B8)), switches};
    auto explorer = traced_explorer(ticks[0]);
    int deaths = 0, object_effects = 0;
    static const char* const kind_names[] = {"none", "lost", "switch", "ice", "flat", "slope", "steep", "corner ramp",
                                             "diagonal", "corner slope", "valley", "ridge", "hot", "bouncy",
                                             "conveyor", "deadly", "lift top"};
    std::map<std::string, int> touched; // ticks ending in contact, by surface kind
    int walls = 0;
    const auto report = [&] {
        std::printf("  in contact:");
        for (const auto& [name, count] : touched) std::printf(" %s %d,", name.c_str(), count);
        std::printf(" wall or corner pushes %d\n", walls);
    };
    for (std::size_t i = 0; i + 1 < ticks.size(); ++i) {
        play::TickEvents events;
        const auto before = explorer;
        play::move_explorer(explorer, ticks[i].joystick, ground, events);
        play::settle_explorer(explorer, events);
        if (explorer.contact != 0)
            ++touched[kind_names[std::size_t(surfaces.kind[std::size_t(explorer.floor_block & 0xFF)])]];
        const int fx = explorer.x & 0xFF, fy = explorer.y & 0xFF;
        if (((fx == 0x5A || fx == 0xA6 || fx == 0xA5) && (before.x & 0xFF) != fx) ||
            ((fy == 0x5A || fy == 0xA6 || fy == 0xA5) && (before.y & 0xFF) != fy))
            ++walls; // (approximate: a push leaves one of these inside parts)
        const auto want = traced_explorer(ticks[i + 1]);
        struct Field { const char* name; long native, original; bool objects_write = false; };
        const Field fields[] = {
            {"x", explorer.x, want.x}, {"y", explorer.y, want.y}, {"z", explorer.z, want.z},
            {"vx", explorer.vx, want.vx}, {"vy", explorer.vy, want.vy}, {"vz", explorer.vz, want.vz},
            {"contact", explorer.contact, want.contact}, {"previous", explorer.previous_contact, want.previous_contact},
            {"friction", explorer.friction, want.friction}, {"block", explorer.floor_block, want.floor_block},
            {"base", explorer.floor_base, want.floor_base, true}, {"on_surface", explorer.on_surface, want.on_surface},
            {"column_x", explorer.column_x, want.column_x}, {"column_y", explorer.column_y, want.column_y},
            {"wall_level", explorer.wall_level, want.wall_level}, {"hot", explorer.hot_ticks, want.hot_ticks},
            {"brake", explorer.brake_lock, want.brake_lock},
            {"respawn_x", explorer.respawn_x, want.respawn_x}, {"respawn_y", explorer.respawn_y, want.respawn_y},
            {"respawn_z", explorer.respawn_z, want.respawn_z},
            {"timer_a", explorer.timer_a, want.timer_a, true}, {"arrival", explorer.arrival_ticks, want.arrival_ticks, true},
        };
        bool same = true, objects_only = true;
        for (const auto& f : fields) {
            same = same && f.native == f.original;
            objects_only = objects_only && (f.objects_write || f.native == f.original);
        }
        const int switch_word = switches.primary << 8 | switches.secondary;
        same = same && switch_word == (ticks[i + 1].word(0x3616) & 0xFFFF);
        if (!events.deaths.empty()) {
            // The original's next traced tick follows the respawn: start
            // again from it.
            std::printf("tick %zu: death (clue %d)\n", i, events.deaths[0].hint);
            ++deaths;
            explorer = traced_explorer(ticks[i + 1]);
            continue;
        }
        if (!same && objects_only) {
            // Objects write these (an alien's touch adds flash ticks at
            // $156EC): not the movement's; start again from the original.
            std::printf("tick %zu: object effect on the explorer (", i);
            for (const auto& f : fields)
                if (f.native != f.original) std::printf(" %s", f.name);
            std::printf(" )\n");
            ++object_effects;
            explorer = traced_explorer(ticks[i + 1]);
            continue;
        }
        if (!same) {
            std::printf("tick %zu (input %02X) differs:\n", i, ticks[i].joystick);
            for (const auto& f : fields)
                std::printf("  %-10s native %8lX original %8lX%s\n", f.name, f.native & 0xFFFFFFFF, f.original & 0xFFFFFFFF,
                            f.native == f.original ? "" : "  <");
            std::printf("  switches   native %04X original %04X\n", switch_word, ticks[i + 1].word(0x3616) & 0xFFFF);
            report();
            return 1;
        }
    }
    std::printf("%zu ticks: all equal (%d deaths, %d object effects re-synchronised)\n", ticks.size() - 1, deaths,
                object_effects);
    report();
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
        if (command == "viewcheck" && (argc == 5 || argc == 6)) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return view_check(assets, platform::read_file(platform::utf8_path(argv[3])), platform::utf8_path(argv[4]),
                              argc == 6 && std::string(argv[5]) == "back");
        }
        if (command == "drive" && argc >= 5) {
            auto module = spindizzy::game::start(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            const auto choices = module->options().at(0).choices;
            for (std::size_t c = 0; c < choices.size(); ++c)
                if (choices[c] == argv[4]) module->set_option(0, int(c));
            for (int i = 5; i < argc; ++i) {
                const std::string item = argv[i];
                const int bits = std::stoi(item.substr(0, item.find(':')), nullptr, 16);
                const int frames = std::stoi(item.substr(item.find(':') + 1));
                app::GameInput input;
                input.up = bits & 1; input.down = bits & 2; input.left = bits & 4; input.right = bits & 8;
                input.space = bits & 0x10; input.fire = bits & 0x80;
                for (int f = 0; f < frames; ++f) module->tick(input);
                char name[32];
                std::snprintf(name, sizeof name, "drive%02d.png", i - 4);
                platform::save_png(module->picture(), out / name);
            }
            return 0;
        }
        if (command == "physcheck" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return phys_check(assets, platform::utf8_path(argv[3]));
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
                         "gridcheck <content> <memory.bin> | viewcheck <content> <memory.bin> <out.png> [back] | "
                         "physcheck <content> <trace.txt> | drive <content> <out> \"<world> <level>\" BITS:FRAMES ...\n");
    return 2;
}
