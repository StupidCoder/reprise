// Spindizzy Worlds research tool: imports the disk image into a content
// folder and exports the decoded data (games/spindizzy_worlds_amiga/game.md).
//
//   spindizzy_worlds_assets import <image.adf> <content folder>
//   spindizzy_worlds_assets export <content folder> <output folder>
//   spindizzy_worlds_assets gridcheck <content folder> <memory.bin>
//   spindizzy_worlds_assets viewcheck <content folder> <memory.bin> <out.png> [back]
//   spindizzy_worlds_assets physcheck <content folder> <trace.txt>
//   spindizzy_worlds_assets drive <content folder> <out folder> BITS:FRAMES ...
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
#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/play/surfaces.hpp"
#include "spindizzy_worlds_amiga/view/objects.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"
#include "spindizzy_worlds_amiga/view/starfield.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include "spindizzy_worlds_amiga/world/object_kind.hpp"
#include <cctype>
#include <cstdio>
#include <cmath>
#include <memory>
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
    e.ice_ticks = t.word(0x2514);
    e.on_surface = t.longword(0x2516) != 0x13222;
    return e;
}


// playcheck (research): a whole-game trace (harness `sdw.py playtrace`:
// A5+0..$3800 at a level start, then per rules tick the joystick byte and
// the bytes that changed) replayed through the native rules from the
// level start's state: the explorer, fuel, jewels, switches, the level
// and every object slot, compared tick by tick across level changes.
struct Ram {
    std::vector<std::uint8_t> bytes;
    int u8(int a) const { return bytes.at(std::size_t(a)); }
    int u16(int a) const { return u8(a) << 8 | u8(a + 1); }
    std::int16_t s16(int a) const { return std::int16_t(u16(a)); }
    std::int32_t s32(int a) const { return std::int32_t(std::uint32_t(u16(a)) << 16 | std::uint32_t(u16(a + 2))); }
};

spindizzy::play::Progress progress_from(const Ram& m) {
    spindizzy::play::Progress p;
    p.world = m.u16(0x251C); p.level = m.u16(0x251E);
    p.fuel = m.s32(0x2064); p.fuel_change = m.s32(0x206C); p.drain = m.s32(0x2068);
    p.reserve = m.s32(0x2074); p.reserve_at_start = m.s32(0x2078); p.jewel_fuel = m.s16(0x2070);
    p.costs = {m.s16(0x205C), m.s16(0x205E), m.s16(0x2060), m.s16(0x2062)};
    p.difficulty = m.u16(0x207C);
    for (int i = 0; i < 16; ++i) p.level_jewels[std::size_t(i)] = std::uint32_t(m.s32(0x2524 + 4 * i));
    p.jewels = std::uint32_t(m.s32(0x2566));
    p.jewels_found = m.u8(0x2564); p.jewels_taken = m.u8(0x2565);
    p.switches.primary = std::uint8_t(m.u8(0x3616)); p.switches.secondary = std::uint8_t(m.u8(0x3617));
    p.switches.binary = std::uint8_t(m.u8(0x3619)); p.switches.conditions_enabled = m.u16(0x361A) != 0;
    p.timer = std::uint16_t(m.u16(0x360E)); p.timer_expired = m.u16(0x2492) != 0;
    p.clues_left = std::uint32_t(m.s32(0x3610));
    p.rotation = m.u8(0x248B); p.arrival_place = std::uint16_t(m.u16(0x24C0));
    p.random = std::uint32_t(m.s32(0x2094));
    p.clue_word = std::uint16_t(m.u16(0x36A2)); p.clue_counter = m.u16(0x36A0);
    TracedTick t;
    t.state.assign(0x80, 0);
    for (int i = 0; i < 0x68; ++i) t.state[std::size_t(i)] = std::uint8_t(m.u8(0x24B8 + i));
    p.explorer = traced_explorer(t);
    return p;
}

int play_check(const spindizzy::data::Assets& assets, const std::filesystem::path& path) {
    using namespace spindizzy;
    std::ifstream in(path);
    std::string line;
    Ram m;
    std::vector<std::pair<int, std::string>> ticks; // joystick, spans
    while (std::getline(in, line)) {
        if (line.rfind("start ", 0) == 0) m.bytes = from_hex(line.substr(6));
        else if (line == "end") break;
        else ticks.push_back({std::stoi(line.substr(0, 2), nullptr, 16), line.size() > 3 ? line.substr(3) : ""});
    }
    // A record without any change is a second stop of the harness with no
    // tick between (seen once in 13,000): the tick after the record before
    // it ran with this record's input.
    for (std::size_t i = 1; i < ticks.size(); ++i)
        if (ticks[i].second.empty()) {
            ticks[i - 1].first = ticks[i].first;
            ticks.erase(ticks.begin() + std::ptrdiff_t(i--));
        }
    const auto surfaces = play::read_surfaces(assets.main);
    auto progress = progress_from(m);
    auto lp = std::make_unique<play::LevelPlay>(assets, surfaces, progress);
    auto apply = [&](const std::string& spans) {
        std::istringstream words(spans);
        std::string span;
        while (words >> span) {
            if (span[0] == 'K') continue; // a key, applied after the tick
            const auto colon = span.find(':');
            const int at = std::stoi(span.substr(0, colon), nullptr, 16);
            const auto bytes = from_hex(span.substr(colon + 1));
            for (std::size_t i = 0; i < bytes.size(); ++i) m.bytes.at(std::size_t(at) + i) = bytes[i];
        }
    };
    int compared = 0, levels = 1;
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        apply(ticks[i].second); // the state before tick i
        // Compare.
        struct Field { std::string name; long native, original; };
        std::vector<Field> fields;
        const auto& e = progress.explorer;
        const int slot1 = 0x258C + 0x40;
        const std::uint32_t code = std::uint32_t(m.s32(slot1));
        const char* kind = code == 0x12B7C ? "playing" : code == 0x12AD2 ? "arriving" : code == 0x12DA2 ? "lost" : code == 0x12CE8 ? "dying" : "?";
        const char* phases[] = {"playing", "arriving", "lost", "dying"};
        const bool explorer_moves = code == 0x12B7C;
        fields.push_back({std::string("phase ") + phases[int(lp->phase())] + "/" + kind,
                          long(int(lp->phase())), long(code == 0x12B7C ? 0 : code == 0x12AD2 ? 1 : code == 0x12DA2 ? 2 : code == 0x12CE8 ? 3 : 9)});
        fields.push_back({"level", progress.level, m.u16(0x251E)});
        if (explorer_moves || lp->phase() == play::ExplorerPhase::playing) {
            fields.push_back({"x", e.x, m.s16(0x24C6)});
            fields.push_back({"y", e.y, m.s16(0x24C8)});
            fields.push_back({"z", e.z, m.s16(0x24CA)});
            fields.push_back({"vx", e.vx, m.s32(0x24D4)});
            fields.push_back({"vy", e.vy, m.s32(0x24D8)});
            fields.push_back({"vz", e.vz, m.s32(0x24DC)});
            fields.push_back({"contact", e.contact, m.u16(0x2510)});
        }
        fields.push_back({"fuel", progress.fuel, m.s32(0x2064)});
        fields.push_back({"fuel+", progress.fuel_change, m.s32(0x206C)});
        fields.push_back({"reserve", progress.reserve, m.s32(0x2074)});
        fields.push_back({"jewels", long(progress.jewels), long(std::uint32_t(m.s32(0x2566)))});
        fields.push_back({"taken", progress.jewels_taken, m.u8(0x2565)});
        fields.push_back({"found", progress.jewels_found, m.u8(0x2564)});
        fields.push_back({"switch", progress.switches.primary << 8 | progress.switches.secondary, m.u16(0x3616)});
        fields.push_back({"binary", progress.switches.binary, m.u8(0x3619)});
        fields.push_back({"timer", progress.timer, m.u16(0x360E)});
        fields.push_back({"random", long(progress.random), long(std::uint32_t(m.s32(0x2094)))});
        fields.push_back({"rotation", progress.rotation & 3, m.u8(0x248B) & 3});
        fields.push_back({"clue word", progress.clue_word, m.u16(0x36A2)});
        {
            int differ = 0;
            for (int x = 0; x < 32; ++x)
                for (int y = 0; y < 32; ++y)
                    for (int z = 0; z < 8; ++z) differ += lp->level().grid.at(x, y, z) != m.u8(0xE + x * 256 + y * 8 + z);
            fields.push_back({"grid cells differing", differ, 0});
        }
        std::vector<bool> used(32);
        for (const auto& o : lp->objects().objects()) {
            if (!o.alive) continue;
            used[std::size_t(o.slot)] = true;
            if (o.kind == world::ObjectKind::starfield || o.kind == world::ObjectKind::clue) continue; // fields reused
            const int r = 0x258C + o.slot * 0x40;
            const std::string n = "slot" + std::to_string(o.slot) + " " + std::string(world::object_kind_name(int(o.kind)));
            fields.push_back({n + " made", 1, m.s32(r) != 0});
            fields.push_back({n + " x", o.x, m.s16(r + 8)});
            fields.push_back({n + " y", o.y, m.s16(r + 0xA)});
            fields.push_back({n + " z", o.z, m.s16(r + 0xC)});
            if (o.kind == world::ObjectKind::lift || o.kind == world::ObjectKind::heads) {
                fields.push_back({n + " +2e", o.word2e, m.u16(r + 0x2E)});
                fields.push_back({n + " +30", o.word30, m.u16(r + 0x30)});
            }
            if (o.kind == world::ObjectKind::jewel || o.kind == world::ObjectKind::boink || o.kind == world::ObjectKind::pac || o.kind == world::ObjectKind::pyramid || (o.kind == world::ObjectKind::exit_point && o.byte20)) {
                fields.push_back({n + " count", o.count, m.u8(r + 0x1E)});
                fields.push_back({n + " step", o.step, m.u8(r + 0x1F)});
            }
            if (o.kind == world::ObjectKind::exit_point || o.kind == world::ObjectKind::spark_generator)
                fields.push_back({n + " +20", o.byte20 != 0, m.u8(r + 0x20) != 0});
            if (o.kind == world::ObjectKind::spark_generator && !o.byte20) {
                fields.push_back({n + " count", o.count, m.u8(r + 0x1E)});
                fields.push_back({n + " step", o.step, m.u8(r + 0x1F)});
            }
            if (o.kind == world::ObjectKind::pac || o.kind == world::ObjectKind::pyramid) {
                fields.push_back({n + " heading", o.heading, m.u16(r + 0x28)});
                fields.push_back({n + " target", long(std::uint32_t(std::uint16_t(o.target_x)) << 16 | std::uint16_t(o.target_y)), long(std::uint32_t(m.s32(r + (o.kind == world::ObjectKind::pyramid ? 0x32 : 0x36))))});
                fields.push_back({n + " step", long(std::uint32_t(std::uint16_t(o.step_x)) << 16 | std::uint16_t(o.step_y)), long(std::uint32_t(m.s32(r + 0x2C)))});
                if (o.kind == world::ObjectKind::pac) fields.push_back({n + " pause", o.pause, m.u8(r + 0x33)});
            }
        }
        for (int s = 2; s < 32; ++s)
            if (!used[std::size_t(s)] && m.s32(0x258C + s * 0x40) != 0) {
                // a slot the native side does not run (inert kinds, starfields)
                const std::uint32_t c = std::uint32_t(m.s32(0x258C + s * 0x40));
                // inert kinds and kind −9's notice (gone after its first frame)
                if (c != 0x14EF4 && c != 0x13358 && c != 0x13862) fields.push_back({"slot" + std::to_string(s) + " made (code " + std::to_string(c) + ")", 0, 1});
            }
        bool same = true;
        for (const auto& f : fields) same = same && f.native == f.original;
        if (!same) {
            std::printf("tick %zu (input %02X, level %d) differs:\n", i, ticks[i].first, progress.level);
            for (const auto& f : fields)
                std::printf("  %-28s native %10lX original %10lX%s\n", f.name.c_str(), f.native, f.original, f.native == f.original ? "" : "  <");
            return 1;
        }
        ++compared;
        // Tick i, then the frame's work.
        lp->tick(std::uint8_t(ticks[i].first));
        (void)lp->take_clue();
        lp->frame(true);
        // A key pressed at this record is read at the next frame's start.
        for (std::size_t k = ticks[i].second.find('K'); k != std::string::npos && k < 4; k = std::string::npos) {
            const int key = std::stoi(ticks[i].second.substr(k + 1, 2), nullptr, 16);
            if (key == 'r') lp->turn(1);
            if (key == 't') lp->turn(-1);
        }
        if (lp->ended()) {
            if (progress.game_over || progress.level == 0) break;
            lp = std::make_unique<play::LevelPlay>(assets, surfaces, progress);
            ++levels;
        }
    }
    std::printf("%d ticks equal over %d level%s\n", compared, levels, levels == 1 ? "" : "s");
    return 0;
}


// route (research): steers the native game through a script and writes
// the joystick byte of every rules tick (for `sdw.py playtrace` on the
// reference machine). Script items:
//   go X Y [SPEED]  roll to the centre of cell (X, Y) and settle there
//   push BITS N     hold joystick bits for N ticks
//   wait N          N ticks without input
//   level L         ticks without input until level L is reached
//   rest            ticks without input until the explorer is still
// Starts at the world's level 1 like a new game's first world.
int route(const spindizzy::data::Assets& assets, int world, const std::string& script_path, const std::filesystem::path& out,
          int first_level = 1) {
    using namespace spindizzy;
    const auto surfaces = play::read_surfaces(assets.main);
    play::Progress progress;
    progress.start_world(assets.main, world, 0);
    progress.level = first_level; // the harness's SDW_LEVEL
    auto lp = std::make_unique<play::LevelPlay>(assets, surfaces, progress);
    std::ofstream inputs(out);
    long tick = 0;
    bool done = false;
    auto step = [&](std::uint8_t bits) {
        if (done) return;
        lp->tick(bits);
        if (auto clue = lp->take_clue()) std::printf("  tick %ld: clue %d\n", tick, clue->number);
        lp->frame(true);
        inputs << std::hex << int(bits) << std::dec << ":1 ";
        ++tick;
        if (lp->ended()) {
            if (progress.game_over) { std::printf("  tick %ld: game over\n", tick); done = true; return; }
            if (progress.level == 0) { std::printf("  tick %ld: world done\n", tick); done = true; return; }
            std::printf("  tick %ld: level %d\n", tick, progress.level);
            lp = std::make_unique<play::LevelPlay>(assets, surfaces, progress);
        }
    };
    auto report = [&](const char* what) {
        const auto& e = progress.explorer;
        std::printf("%-22s tick %5ld level %2d at %2d,%2d z %4X (%04X %04X) v %6.2f %6.2f fuel %3X jewels %08X taken %d\n", what, tick,
                    progress.level, e.x >> 8, e.y >> 8, e.z & 0xFFFF, e.x & 0xFFFF, e.y & 0xFFFF, e.vx / 65536.0, e.vy / 65536.0,
                    progress.fuel >> 16, progress.jewels, progress.jewels_taken);
    };
    std::ifstream script(script_path);
    std::string word;
    while (!done && script >> word) {
        if (word == "waitz") { // waitz SLOT Z: until that object's height is at most Z (hex)
            int slot; std::string z; script >> slot >> z;
            const bool above = z[0] == '>';
            const int limit = std::stoi(above ? z.substr(1) : z, nullptr, 16);
            for (int i = 0; i < 3000 && !done; ++i) {
                bool ok = false;
                for (const auto& o : lp->objects().objects())
                    if (o.slot == slot && o.alive && (above ? o.z >= limit : o.z <= limit)) ok = true;
                if (ok) break;
                step(0);
            }
            report(("waitz " + std::to_string(slot)).c_str());
            continue;
        }
        if (word == "go" || word == "fgo") {
            const bool fire = word == "fgo";
            int cx, cy;
            double speed = 6;
            script >> cx >> cy;
            if (script.peek() == ' ') { std::string maybe; std::streampos at = script.tellg(); script >> maybe;
                if (!maybe.empty() && (std::isdigit(maybe[0]) || maybe[0] == '.')) speed = std::stod(maybe); else script.seekg(at); }
            const double tx = cx * 256 + 128, ty = cy * 256 + 128;
            int settled = 0, guard = 0;
            const int start_level = progress.level;
            while (!done && guard++ < 800 && progress.level == start_level) {
                const auto& e = progress.explorer;
                const double vx = e.vx / 65536.0, vy = e.vy / 65536.0;
                auto want = [&](double d) { return std::max(-speed, std::min(speed, d / 12.0)); };
                std::uint8_t bits = 0;
                const double ex = want(tx - e.x) - vx, ey = want(ty - e.y) - vy;
                if (ex > 0.5) bits |= play::stick::down; else if (ex < -0.5) bits |= play::stick::up;
                if (ey > 0.5) bits |= play::stick::right; else if (ey < -0.5) bits |= play::stick::left;
                if (fire && bits) bits |= play::stick::fire;
                step(bits);
                const bool near = std::abs(tx - e.x) < 40 && std::abs(ty - e.y) < 40 && std::abs(vx) < 0.6 && std::abs(vy) < 0.6;
                settled = near ? settled + 1 : 0;
                if (settled > 6) break;
            }
            report(("go " + std::to_string(cx) + "," + std::to_string(cy)).c_str());
        } else if (word == "push") {
            std::string bits; int n; script >> bits >> n;
            for (int i = 0; i < n; ++i) step(std::uint8_t(std::stoi(bits, nullptr, 16)));
            report(("push " + bits).c_str());
        } else if (word == "wait") {
            int n; script >> n;
            for (int i = 0; i < n; ++i) step(0);
            report("wait");
        } else if (word == "level") {
            int l; script >> l;
            for (int i = 0; i < 5000 && !done && progress.level != l; ++i) step(0);
            report(("level " + std::to_string(l)).c_str());
        } else if (word == "rest") {
            for (int i = 0; i < 500 && !done; ++i) {
                step(0);
                const auto& e = progress.explorer;
                if (std::abs(e.vx) < 0x1000 && std::abs(e.vy) < 0x1000 && (e.contact & 0x8000)) break;
            }
            report("rest");
        }
    }
    std::printf("%ld ticks\n", tick);
    return 0;
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
                                             "diagonal", "corner slope", "valley", "ridge", "ice", "bouncy",
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
            {"wall_level", explorer.wall_level, want.wall_level}, {"ice", explorer.ice_ticks, want.ice_ticks},
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
        if (command == "map" && argc == 6) {
            // A route-planning map (research): per column the top block's
            // height (brightness), surface kind (letter) and the objects.
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto& world = assets.worlds.at(std::size_t(std::stoi(argv[3])));
            const int id = std::stoi(argv[4]);
            const auto level = spindizzy::world::build_level(world, id, assets.block_attributes);
            const auto surfaces = spindizzy::play::read_surfaces(assets.main);
            // Two characters per cell: the top's level (or '.') and its kind.
            std::printf("     y:");
            for (int y = 0; y < 32; ++y) std::printf("%2d", y % 100);
            std::printf("\n");
            for (int x = 0; x < 32; ++x) {
                std::printf("x %2d   ", x);
                for (int y = 0; y < 32; ++y) {
                    int z = 7;
                    while (z >= 0 && (level.grid.at(x, y, z) == 0 || level.grid.at(x, y, z) == 0x22 || level.grid.at(x, y, z) == 0x23)) --z;
                    char a = '.', b = ' ';
                    if (z >= 0) {
                        const auto kind = surfaces.kind[level.grid.at(x, y, z)];
                        a = char('0' + z);
                        b = ".LSiFsSrdcvRhbCDT"[int(kind)]; // none lost switch slippery flat slope steep ramp diag cslope valley ridge ice bouncy conveyor deadly lifttop
                        if (kind == spindizzy::play::Surface::flat) {
                            const int raise = surfaces.raise[level.grid.at(x, y, z)];
                            b = raise == 0 ? ' ' : raise < 0x80 ? '-' : raise < 0x100 ? '^' : '#'; // raised flats ($40, $80-$C0, more)
                        }
                    }
                    for (const auto& o : level.objects) if (o.x == x && o.y == y && o.kind != 14) b = "?JLEBWbTsPpHgCSMP"[o.kind];
                    std::printf("%c%c", a, b);
                }
                std::printf("\n");
            }
            return 0;
        }
        if (command == "objects" && argc == 5) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto& world = assets.worlds.at(std::size_t(std::stoi(argv[3])));
            const int id = std::stoi(argv[4]);
            const auto header = world.header(id);
            std::printf("header %04X %04X %04X %04X\n", header.first, header.second, header.flags, header.fourth);
            const auto level = spindizzy::world::build_level(world, id, assets.block_attributes);
            for (const auto& object : level.objects) {
                std::printf("%2d,%2d,%d %-16s", object.x, object.y, object.z,
                            std::string(spindizzy::world::object_kind_name(object.kind)).c_str());
                for (auto byte : object.parameters) std::printf(" %02X", byte);
                std::printf("\n");
            }
            for (auto condition : level.conditions) std::printf("condition %04X\n", condition);
            return 0;
        }
        if (command == "viewcheck" && (argc == 5 || argc == 6)) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return view_check(assets, platform::read_file(platform::utf8_path(argv[3])), platform::utf8_path(argv[4]),
                              argc == 6 && std::string(argv[5]) == "back");
        }
        if (command == "drivefile" && argc == 5) {
            // drivefile <content> <out folder> <file>: BITS:FRAMES items from
            // a file; "!" saves a picture (shotNN.png), the last frame too.
            auto module = spindizzy::game::start(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            std::ifstream items(argv[4]);
            std::string item;
            int shots = 0;
            auto shot = [&] {
                char name[32];
                std::snprintf(name, sizeof name, "shot%02d.png", ++shots);
                platform::save_png(module->picture(), out / name);
            };
            while (items >> item) {
                if (item == "!") { shot(); continue; }
                const int bits = std::stoi(item.substr(0, item.find(':')), nullptr, 16);
                const int frames = std::stoi(item.substr(item.find(':') + 1));
                app::GameInput input;
                input.up = bits & 1; input.down = bits & 2; input.left = bits & 4; input.right = bits & 8;
                input.space = bits & 0x10; input.fire = bits & 0x80;
                for (int f = 0; f < frames; ++f) module->tick(input);
            }
            shot();
            return 0;
        }
        if (command == "drive" && argc >= 4) {
            auto module = spindizzy::game::start(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (int i = 4; i < argc; ++i) {
                const std::string item = argv[i];
                const int bits = std::stoi(item.substr(0, item.find(':')), nullptr, 16);
                const int frames = std::stoi(item.substr(item.find(':') + 1));
                app::GameInput input;
                input.up = bits & 1; input.down = bits & 2; input.left = bits & 4; input.right = bits & 8;
                input.space = bits & 0x10; input.fire = bits & 0x80;
                for (int f = 0; f < frames; ++f) module->tick(input);
                char name[32];
                std::snprintf(name, sizeof name, "drive%02d.png", i - 3);
                platform::save_png(module->picture(), out / name);
            }
            return 0;
        }
        if (command == "route" && (argc == 6 || argc == 7)) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return route(assets, std::stoi(argv[3]), argv[4], platform::utf8_path(argv[5]), argc == 7 ? std::stoi(argv[6]) : 1);
        }
        if (command == "playcheck" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return play_check(assets, platform::utf8_path(argv[3]));
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
                         "physcheck <content> <trace.txt> | drive <content> <out> BITS:FRAMES ... | objects <content> <world> <level>\n");
    return 2;
}
