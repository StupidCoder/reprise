// Cannon Fodder research tool: imports the three disk images into a content
// folder and exports the decoded data (.analysis/cannon_fodder_amiga/
// formats.md).
//
//   cannon_fodder_assets import <disk1.adf> <disk2.adf> <disk3.adf> <content folder>
//   cannon_fodder_assets export <content folder> <output folder>
//   cannon_fodder_assets view <content folder> <map> <camera x> <camera y> <out.png> [indices.raw]
//   cannon_fodder_assets lookahead <content folder> <leader x> <leader y> <pointer map x> <pointer map y>
//   cannon_fodder_assets camreplay <content folder> <trace.txt> [--holds]
//   cannon_fodder_assets spawn <content folder> <map> [table.bin]
//   cannon_fodder_assets objects <content folder> <map> <grab prefix> [diff.png]
//   cannon_fodder_assets panel <content folder> <out.png> [sprites.bin]
//   cannon_fodder_assets replay <content folder> <map> <trace>
//
// replay checks the soldiers and bullets tick by tick against a recording of
// the original (harness/ticktrace2.py): each tick starts from the original's
// state before the object update; the native updates run in slot order, and
// every slot they handled is compared field by field with the original's
// state after the tick.
//
// panel composes the panel for the start of mission 1 (one squad, recruits
// 0 and 1 at rank 0, no ammunition) and, given the original's sprite memory
// from $394 (harness), compares all 48 x 224 pixels.
//
// objects draws the map and the objects of a grabbed object table
// (harness/objgrab.py: prefix.objects, prefix.buffer, prefix.txt) natively
// and compares the visible play area with the buffer the original drew.
//
// spawn prints the native mission set-up (game/objects.hpp) and, given a
// dump of the original's object table right after its spawner
// (harness/atspawn.py), compares every field the spawner writes.
//
// camreplay runs the native camera step from a recording of the original
// (.analysis/cannon_fodder_amiga/harness/camtrace.py, converted by
// tools/camtrace2txt.py): the first frame's state, then each frame's inputs
// (follow flags and steps, target point, hold counters, tick and map size);
// it compares position, velocity and chase speed with the next frame. With
// --holds the reversal holds come from the native tile drawer tick instead
// (snapshot at the start of each second tick).
//
// The export writes every launcher preview as a PNG (maps at full size). The
// view is the play view's map background (game/map_view.hpp) as a PNG and,
// optionally, as raw colour indices for comparison with the original's
// display (harness/display.py).

#include "amiga/adf.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/game/object_view.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <exception>
#include <string>

namespace {

std::string file_name(std::string title) {
    for (auto& c : title) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') c = '_';
    }
    return title;
}

int run(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "import" && argc == 6) {
        const std::filesystem::path out = argv[5];
        for (int i = 2; i < 5; ++i) {
            const auto result = fodder::import::import_disk(platform::read_file(argv[i]));
            std::printf("%s: disk %d of %d, %s, %zu files\n", argv[i], result.part, result.parts,
                        result.release.c_str(), result.files.size());
            for (const auto& file : result.files) {
                std::filesystem::create_directories((out / file.name).parent_path());
                platform::write_file(out / file.name, file.bytes);
            }
        }
        return 0;
    }
    if (command == "export" && argc == 4) {
        const std::filesystem::path out = argv[3];
        std::filesystem::create_directories(out);
        int n = 0;
        for (const auto& preview : fodder::data::previews(argv[2], true)) {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "%03d-", n++);
            platform::save_png(preview.image, out / (prefix + file_name(preview.title) + ".png"));
            std::printf("%s%s (%dx%d)\n", prefix, preview.title.c_str(), preview.image.width, preview.image.height);
        }
        return 0;
    }
    if (command == "view" && (argc == 7 || argc == 8)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const int disk = data::map_disk(number);
        const auto map = data::read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
        const game::MapPicture picture(map, data::read_tile_set(content.read(map.base_set, disk)),
                                       data::read_tile_set(content.read(map.sub_set, disk)));
        auto palette_name = map.base_set;
        palette_name.replace(palette_name.size() - 3, 3, "pal");
        const game::Camera camera{std::stoi(argv[4]), std::stoi(argv[5])};
        platform::save_png(game::view_image(picture, camera, data::read_palette(content.read(palette_name, disk))),
                           argv[6]);
        if (argc == 8) {
            platform::write_file(argv[7], game::view_indices(picture, camera));
        }
        return 0;
    }
    if (command == "lookahead" && argc == 7) {
        using namespace fodder;
        const game::GeometryTables tables(data::Content(argv[2]).read("fodderc", 1));
        const game::Point leader{std::stoi(argv[3]), std::stoi(argv[4])};
        const game::Point pointer{std::stoi(argv[5]), std::stoi(argv[6])};
        const auto target = game::look_ahead_target(tables, leader, pointer, false);
        const int dx = pointer.x - leader.x - 24, dy = pointer.y - leader.y;
        std::printf("distance %d direction %d target ", tables.distance(dx, dy), tables.direction(dx, dy, 9).value_or(-1));
        if (target) std::printf("%d %d\n", target->x, target->y); else std::printf("none\n");
        return 0;
    }
    if ((command == "replay" || command == "walkreplay") && argc == 5) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const int disk = data::map_disk(number);
        const auto map = data::read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
        const auto base = map.base_set.substr(0, map.base_set.size() - 4), sub = map.sub_set.substr(0, map.sub_set.size() - 4);
        const auto program = content.read("fodderc", 1);
        const game::Terrain terrain(map, content.read(base + ".hit", disk), content.read(sub + ".hit", disk),
                                    content.read(base + ".bht", disk), content.read(sub + ".bht", disk), program);
        const game::ProgramTables tables(program);
        const auto trace = platform::read_file(argv[4]);
        struct Range { std::uint32_t address, size, offset; };
        const Range ranges[] = {{0xfdb2c, 45 * 0x76, 0}, {0x81400, 0x1700, 0}, {0x85b00, 0x290, 0}, {0x80600, 0x80, 0},
                                {0x8cab0, 0x20, 0}, {0x9d840, 0x10, 0}, {0x82b00, 0x100, 0}};
        std::vector<Range> layout;
        std::uint32_t blob_size = 0;
        for (auto r : ranges) { r.offset = blob_size; blob_size += r.size; layout.push_back(r); }
        std::size_t at = 0;
        while (trace[at] != '\n') ++at;
        at += 1 + 0x1000 + 0x320 + 0x60 + 0x10;
        int ticks = 0, bad = 0, unhandled = 0, created = 0, falls = 0, unhandled_enemies = 0, random_skips = 0;
        while (at + 6 <= trace.size()) {
            const int tick = amiga::read_be16(trace, at + 2), randoms = amiga::read_be16(trace, at + 4);
            const auto before = std::span<const std::uint8_t>(trace).subspan(at + 6, blob_size);
            const auto after = std::span<const std::uint8_t>(trace).subspan(at + 6 + blob_size, blob_size);
            at += 6 + 2 * std::size_t(blob_size) + std::size_t(randoms) * 8;
            const auto next_before = at + 6 + blob_size <= trace.size() ? std::span<const std::uint8_t>(trace).subspan(at + 6, blob_size)
                                                                         : std::span<const std::uint8_t>();
            const auto locate = [&](std::uint32_t a) -> long {
                for (const auto& r : layout) if (a >= r.address && a < r.address + r.size) return long(r.offset + (a - r.address));
                return -1;
            };
            const auto W = [&](std::span<const std::uint8_t> b, std::uint32_t a) { const long o = locate(a); return o < 0 ? 0 : int(std::int16_t(amiga::read_be16(b, std::size_t(o)))); };
            const auto L = [&](std::span<const std::uint8_t> b, std::uint32_t a) { const long o = locate(a); return o < 0 ? 0 : std::int32_t(amiga::read_be32(b, std::size_t(o))); };
            const auto B = [&](std::span<const std::uint8_t> b, std::uint32_t a) { const long o = locate(a); return o < 0 ? 0 : int(b[std::size_t(o)]); };
            game::World world;
            const auto load_slot = [&](std::span<const std::uint8_t> b, int i, game::GameObject& o, game::Walker& s) {
                const std::uint32_t r = 0xfdb2c + std::uint32_t(i) * 0x76;
                const int first = W(b, r) & 0xffff;
                o.used = first != 0x8000 && first != 0xffff;
                o.x = L(b, r); o.y = L(b, r + 4); o.graphic = W(b, r + 8); o.frame = W(b, r + 0xa);
                o.kind = W(b, r + 0x18); o.height = W(b, r + 0x1e); o.side = W(b, r + 0x22); o.squad = W(b, r + 0x32);
                o.hidden_lines = W(b, r + 0x52); o.layer = W(b, r + 0x2c);
                s.heading = W(b, r + 0x10); s.speed = W(b, r + 0x36); s.facing = W(b, r + 0x3c); s.facing_timer = W(b, r + 0x3e);
                s.waypoint_x = W(b, r + 0x26); s.waypoint_y = W(b, r + 0x28); s.route_index = W(b, r + 0x40);
                s.took_waypoint = B(b, r + 0x42) != 0; s.idle = B(b, r + 0x43); s.wait = B(b, r + 0x44); s.pause = B(b, r + 0x45);
                s.stopped = B(b, r + 0x4c); s.wading = B(b, r + 0x4f) != 0; s.slow_target = W(b, r + 0x50);
                s.animation_state = B(b, r + 0x54); s.animation_step = B(b, r + 0x55); s.squeeze = B(b, r + 0x56);
                s.animation_timer = B(b, r + 0x57); s.aim_override = B(b, r + 0x5a) != 0; s.entering = B(b, r + 0x5b);
                s.terrain = B(b, r + 0x60); s.sinking = B(b, r + 0x61) != 0; s.state = W(b, r + 0x38);
                s.aim_x = W(b, r + 0x2e); s.aim_y = W(b, r + 0x30); s.step_counter = W(b, r + 0x3a); s.carried = B(b, r + 0x6e);
                const auto roster = std::uint32_t(L(b, r + 0x46));
                s.roster_weapon = roster ? W(b, roster + 6) : 0;
                s.rank = roster ? B(b, roster + 2) : 0;
                s.roster_index = roster >= 0x80664 && roster < 0x80664 + 8 * 0xc ? int((roster - 0x80664) / 0xc) : -1;
                s.recruit = roster ? W(b, roster) : 0;
                s.hit_timer = B(b, r + 0x64); s.state_timer = W(b, r + 0x12);
                s.vertical_speed = L(b, r + 0x1a); s.height_fraction = W(b, r + 0x20) & 0xffff; s.spin = W(b, r + 0x3a);
                s.fall_heading = W(b, r + 0x3e); s.fall_count = W(b, r + 0x2a); s.shadowed = B(b, r + 0x58) != 0;
                s.hit_from_behind = B(b, r + 0x59) != 0;
                o.on_screen = B(b, r + 0x5c) != 0;
                s.fire_delay = W(b, r + 0x4a); s.skill = W(b, r + 0x62);
                s.target = W(b, r + 0x5e);
                s.shot_by_roster = std::min((W(b, r + 0x5e) & 0xfff) / 12, 8) % 8; s.shot_by_mark = B(b, r + 0x5d);
                const auto building = std::uint32_t(L(b, r + 0x70));
                s.building = building >= 0xfdb2c && building < 0xfdb2c + 44 * 0x76 ? int((building - 0xfdb2c) / 0x76) : -1;
            };
            const auto load_bullet = [&](std::span<const std::uint8_t> b, int i, game::BulletState& t) {
                const std::uint32_t r = 0xfdb2c + std::uint32_t(i) * 0x76;
                const auto owner = std::uint32_t(L(b, r + 0x1a));
                t.owner = owner >= 0xfdb2c ? int((owner - 0xfdb2c) / 0x76) : -1;
                t.muzzle_ticks = W(b, r + 0x2a); t.offset_x = W(b, r + 0x14); t.offset_y = W(b, r + 0x16);
                t.lifetime = W(b, r + 0x12); t.age = B(b, r + 0x64); t.flown = W(b, r + 0x3a); t.cruise_speed = W(b, r + 0x4a);
                t.spread = W(b, r + 0x50); t.spread_done = B(b, r + 0x59) != 0; t.first_heading = W(b, r + 0x34);
                t.past_target = B(b, r + 0x44) != 0; t.ending = B(b, r + 0x43);
                t.owner_roster = (W(b, r + 0x5e) & 0xfff) / 12; t.owner_mark = B(b, r + 0x5d);
            };
            world.bullets_alive = W(before, 0x81df2);
            for (int i = 0; i < game::object_slots; ++i) {
                load_slot(before, i, world.objects.slots[std::size_t(i)], world.walkers[std::size_t(i)]);
                load_bullet(before, i, world.bullets[std::size_t(i)]);
                const auto link = std::uint32_t(L(before, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x1a));
                world.attachments[std::size_t(i)] = {link >= 0xfdb2c && link < 0xfdb2c + 44 * 0x76 ? int((link - 0xfdb2c) / 0x76) : -1,
                                                     W(before, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x62)};
                const auto& o = world.objects.slots[std::size_t(i)];
                const auto roster = std::uint32_t(L(before, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x46));
                if (o.used && o.kind == 0 && roster >= 0x80664 && roster < 0x80664 + 8 * 0xc)
                    world.roster_slots[(roster - 0x80664) / 0xc] = i;
            }
            for (int n = 0; n < 3; ++n) {
                auto& q = world.squads[std::size_t(n)];
                for (auto p = std::uint32_t(L(before, 0x85c16 + std::uint32_t(n) * 4)); p && W(before, p) >= 0 && locate(p) >= 0; p += 4)
                    q.route.push_back({W(before, p), W(before, p + 2)});
                for (auto p = std::uint32_t(L(before, 0x85b12 + std::uint32_t(n) * 4)); p && L(before, p) >= 0 && locate(p) >= 0; p += 4)
                    q.members.push_back(int((std::uint32_t(L(before, p)) - 0xfdb2c) / 0x76));
                q.mode = W(before, 0x85bf8 + std::uint32_t(n) * 2);
                q.merged_into = std::int8_t(B(before, 0x85c3e + std::uint32_t(n)));
            }
            world.selected_squad = W(before, 0x81556);
            world.clock.value = {W(before, 0x81578), W(before, 0x8157c), W(before, 0x81580)};
            world.camera = {W(before, 0x814ae), W(before, 0x814b2)};
            world.pointer_x = W(before, 0x8caba); world.pointer_y = W(before, 0x8cabc);
            world.firing = W(before, 0x81dec) != 0;
            world.both_buttons = W(before, 0x8147e) != 0;
            world.leader_slot = W(before, 0x81554) >= 0 ? W(before, 0x81554) / 0x76 : -1;
            for (int n = 0; n < 3; ++n) world.squads[std::size_t(n)].weapon = W(before, 0x821cc + std::uint32_t(n) * 2);
            world.saved_frame = W(before, 0x817b8);
            world.phase = W(before, 0x80626);
            world.objects.enemies = W(before, 0x81f5e);
            for (std::uint32_t n = 0; n < 8; ++n) world.kills[n] = n < 2 ? W(before, 0x8066e + n * 12) : 0; // (entries 0-1 recorded)
            world.last_mode = W(before, 0x81f64);
            world.other_squad = W(before, 0x81fce) != 0;
            world.walked = W(before, 0x8213e) != 0;
            for (std::uint32_t n = 0; n < 30; ++n) {
                const auto p = std::uint32_t(L(before, 0x85b26 + n * 4));
                world.recruit_lists[n] = p >= 0xfdb2c && p < 0xfdb2c + 44 * 0x76 ? int((p - 0xfdb2c) / 0x76) : -1;
            }
            world.keep_frame = W(before, 0x81de4) != 0;
            const auto load_fire = [&](std::span<const std::uint8_t> b, game::SquadFire& f) {
                f.stats = {W(b, 0x82146), W(b, 0x82148), W(b, 0x8214a), W(b, 0x8214c)};
                f.hold_timer = W(b, 0x81dd6); f.countdown = W(b, 0x82140); f.order_index = W(b, 0x82142) / 2;
                f.leader_shots = W(b, 0x82738); f.lifetime_bonus = W(b, 0x82150); f.speed_bonus = W(b, 0x82152);
            };
            load_fire(before, world.fire);
            world.frame_counter = W(before, 0x80618);
            // Slots the rebuild cannot update yet take the original's result,
            // so that later slots in the pass see what the original's did.
            std::array<int, game::object_slots> handled{}; // 1 soldier, 2 bullet, 3 new bullet
            std::array<bool, game::object_slots> was_used{};
            const auto walkers_before = world.walkers;
            for (int i = 0; i < game::object_slots; ++i) was_used[std::size_t(i)] = world.objects.slots[std::size_t(i)].used;
            for (int i = 0; i < game::object_slots; ++i) {
                const auto& o = world.objects.slots[std::size_t(i)];
                if (!o.used || o.removed) continue;
                bool done = false;
                if (o.kind == 0 || o.kind == 5) {
                    done = o.kind == 0 ? game::update_soldier(world, terrain, tables, i) : game::update_enemy(world, terrain, tables, i);
                    if (done) handled[std::size_t(i)] = o.kind == 0 ? 1 : 5;
                    else if (o.kind == 5) ++unhandled_enemies;
                } else if (o.kind == 6) {
                    done = game::update_bullet(world, terrain, tables, i);
                    if (done) handled[std::size_t(i)] = 2;
                } else if (o.kind == 0x16 || o.kind == 0x17) {
                    if (o.kind == 0x16) game::update_shadow(world, i); else game::update_blood(world, i);
                    done = true;
                    handled[std::size_t(i)] = 4;
                }
                if (!done) {
                    if (o.kind == 0 || o.kind == 6) ++unhandled;
                    if (o.kind == 0 && unhandled <= 5) std::printf("tick %d slot %d: soldier update not handled\n", tick, i);
                    auto& slot = world.objects.slots[std::size_t(i)];
                    game::GameObject replaced; game::Walker unused;
                    load_slot(after, i, replaced, unused);
                    slot.x = replaced.x; slot.y = replaced.y; slot.height = replaced.height;
                }
            }
            const auto report = [&](int i, const char* name, long native, long original) {
                if (native != original && ++bad <= 40)
                    std::printf("tick %d slot %d %s: native %ld original %ld\n", tick, i, name, native, original);
            };
            for (int i = 0; i < game::object_slots; ++i) {
                const auto& o = world.objects.slots[std::size_t(i)];
                if (!was_used[std::size_t(i)] && o.used && o.kind == 6) handled[std::size_t(i)] = 3;
                if (!was_used[std::size_t(i)] && o.used && (o.kind == 0x16 || o.kind == 0x17)) handled[std::size_t(i)] = 4;
                if (!handled[std::size_t(i)] && was_used[std::size_t(i)] != o.used) report(i, "used", o.used, !o.used);
            }
            for (int i = 0; i < game::object_slots; ++i) {
                if (!handled[std::size_t(i)]) continue;
                const auto& o = world.objects.slots[std::size_t(i)];
                game::GameObject expected; game::Walker e;
                load_slot(after, i, expected, e);
                auto& n = world.walkers[std::size_t(i)];
                const auto whole_x = [&](int k) { return int(std::int16_t(std::uint32_t(world.objects.slots[std::size_t(k)].x) >> 16)); };
                if (o.removed) {
                    report(i, "freed", 1, (W(after, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x24) != 0 || !expected.used) ? 1 : 0);
                    report(i, "kind", o.kind, expected.kind);
                    continue;
                }
                const std::pair<const char*, std::pair<long, long>> common[] = {
                    {"x", {o.x, expected.x}}, {"y", {o.y, expected.y}}, {"graphic", {o.graphic, expected.graphic}},
                    {"frame", {o.frame, expected.frame}}, {"heading", {n.heading, e.heading}},
                    {"state", {n.state, e.state}}, {"layer", {o.layer, expected.layer}}, {"kind", {o.kind, expected.kind}},
                    {"height", {o.height, expected.height}}, {"aim_x", {n.aim_x, e.aim_x}}, {"aim_y", {n.aim_y, e.aim_y}}};
                const bool fall_start = (handled[std::size_t(i)] == 1 || handled[std::size_t(i)] == 5) &&
                                        walkers_before[std::size_t(i)].state != 0 && walkers_before[std::size_t(i)].hit_timer != 0;
                // Enemies' random choices ($9D894, $9DAA6) differ from the
                // original's by design: a new fire delay is checked by its
                // range, a random look-around (aim moved by -128..127) skips
                // the aim and what follows from it this tick, and so does
                // the 1-in-64 lapse of attention (the target index moves on
                // in one and not the other).
                bool random_look = false, lapse = false;
                if (handled[std::size_t(i)] == 5) {
                    const auto& was = walkers_before[std::size_t(i)];
                    {
                        // A reload (also after a look-around, which clears the
                        // delay) lands in [max(base, 5), base + mask] and is
                        // counted down once in the same tick.
                        const int skill = std::uint16_t(was.skill), mask = 0x1f >> ((skill >> 2) & 63);
                        const int base = std::max(50 - int(std::int16_t(skill << 2)), 0);
                        const auto in_range = [&](int v) { return v >= std::max(base, 5) - 1 && v <= std::max(base + mask, 5) - 1; };
                        if (was.fire_delay == 0 && e.fire_delay > 0 && !in_range(e.fire_delay))
                            report(i, "original fire delay outside the rule", e.fire_delay, base);
                        if (n.fire_delay != e.fire_delay && in_range(n.fire_delay) && in_range(e.fire_delay)) n.fire_delay = e.fire_delay;
                    }
                    const auto wandered = [&](const game::Walker& w) {
                        return (w.aim_x != was.aim_x || w.aim_y != was.aim_y) && std::abs(w.aim_x - was.aim_x) <= 128 &&
                               std::abs(w.aim_y - was.aim_y) <= 128 && w.waypoint_x == whole_x(i) && w.target != was.target;
                    };
                    random_look = (n.aim_x != e.aim_x || n.aim_y != e.aim_y) && (wandered(n) || wandered(e));
                    lapse = n.target != e.target;
                }
                const bool skip_random = random_look || lapse;
                for (const auto& [name, v] : common) {
                    if (fall_start && (std::string(name) == "height" || std::string(name) == "graphic")) continue;
                    if (skip_random) { ++random_skips; break; }
                    report(i, name, v.first, v.second);
                }
                if (skip_random) continue;
                if (handled[std::size_t(i)] == 3) {
                    // A new bullet: its first speed and its spread are random
                    // (the rebuild's own generator), so only their ranges count.
                    const auto& t = world.bullets[std::size_t(i)];
                    game::BulletState x;
                    load_bullet(after, i, x);
                    if (n.speed % 8 != 0 || n.speed > 120) report(i, "first speed range", n.speed, e.speed);
                    if (t.spread < -15 || t.spread > 15) report(i, "spread range", t.spread, x.spread);
                    const std::pair<const char*, std::pair<long, long>> fields[] = {
                        {"owner", {t.owner, x.owner}}, {"muzzle", {t.muzzle_ticks, x.muzzle_ticks}}, {"lifetime", {t.lifetime, x.lifetime}},
                        {"cruise", {t.cruise_speed, x.cruise_speed}}, {"offset_y", {t.offset_y, x.offset_y}},
                        {"first_heading", {t.first_heading, x.first_heading}}, {"side", {o.side, expected.side}}};
                    for (const auto& [name, v] : fields) report(i, name, v.first, v.second);
                    ++created;
                    continue;
                }
                report(i, "speed", n.speed, e.speed);
                if (handled[std::size_t(i)] == 4) {
                    report(i, "timer", n.state_timer, e.state_timer);
                    continue;
                }
                if (handled[std::size_t(i)] == 2) {
                    const auto& t = world.bullets[std::size_t(i)];
                    game::BulletState x;
                    load_bullet(after, i, x);
                    const std::pair<const char*, std::pair<long, long>> fields[] = {
                        {"muzzle", {t.muzzle_ticks, x.muzzle_ticks}}, {"lifetime", {t.lifetime, x.lifetime}}, {"age", {t.age, x.age}},
                        {"flown", {t.flown, x.flown}}, {"spread_done", {t.spread_done, x.spread_done}},
                        {"first_heading", {t.first_heading, x.first_heading}},
                        {"past_target", {t.past_target, x.past_target}}, {"ending", {t.ending, x.ending}}};
                    for (const auto& [name, v] : fields) report(i, name, v.first, v.second);
                    continue;
                }
                const std::pair<const char*, std::pair<long, long>> fields[] = {
                    {"height", {o.height, expected.height}}, {"hidden", {o.hidden_lines, expected.hidden_lines}},
                    {"facing", {n.facing, e.facing}}, {"facing_timer", {n.state == 0 ? n.facing_timer : 0, e.state == 0 ? e.facing_timer : 0}},
                    {"waypoint_x", {n.waypoint_x, e.waypoint_x}}, {"waypoint_y", {n.waypoint_y, e.waypoint_y}},
                    {"route_index", {n.route_index, e.route_index}}, {"took", {n.took_waypoint, e.took_waypoint}},
                    {"idle", {n.idle, e.idle}}, {"wait", {n.wait, e.wait}}, {"pause", {n.pause, e.pause}},
                    {"stopped", {n.stopped, e.stopped}}, {"wading", {n.wading, e.wading}}, {"slow_target", {n.slow_target, e.slow_target}},
                    {"terrain", {n.terrain, e.terrain}}, {"sinking", {n.sinking, e.sinking}}, {"squeeze", {n.squeeze, e.squeeze}},
                    {"step_counter", {n.state == 0 ? n.step_counter : 0, e.state == 0 ? e.step_counter : 0}}, {"aim", {n.aim_override, e.aim_override}},
                    {"hit_timer", {n.hit_timer, e.hit_timer}}, {"fire_delay", {n.fire_delay, e.fire_delay}},
                    {"anim_state", {n.animation_state, e.animation_state}}, {"anim_timer", {n.animation_timer, e.animation_timer}},
                    {"anim_step", {n.animation_step, e.animation_step}}, {"state_timer", {n.state_timer, e.state_timer}},
                    {"shadowed", {n.shadowed, e.shadowed}}, {"waypoint_x", {n.waypoint_x, e.waypoint_x}}};
                if (handled[std::size_t(i)] == 5 && !(n.shot_by_mark || e.shot_by_mark)) report(i, "target", n.target, e.target);
                const auto& was = walkers_before[std::size_t(i)];
                if (fall_start) {
                    // The tick a fall starts: the throw (2-9 up) and the spin
                    // (half of the time) are random; checked by range.
                    const long thrown = (long(n.vertical_speed) + 0x18000 - long(was.vertical_speed)) >> 16;
                    if (o.height != 0 && (thrown < 2 || thrown > 9)) report(i, "throw range", thrown, 0);
                    if (n.spin != -1 && n.spin != ((was.heading + (n.speed + 2) * 4) & 0x1fe)) report(i, "spin start", n.spin, e.spin);
                    ++falls;
                    continue;
                }
                report(i, "vertical_speed", n.vertical_speed, e.vertical_speed);
                report(i, "height_fraction", n.height_fraction, e.height_fraction);
                if (was.state != 0) {
                    report(i, "spin", n.spin, e.spin);
                    report(i, "fall_heading", n.fall_heading, e.fall_heading);
                    report(i, "fall_count", n.fall_count, e.fall_count);
                }
                for (const auto& [name, v] : fields) report(i, name, v.first, v.second);
            }
            // Bullets fired by objects not rebuilt yet (enemies) are not counted natively.
            bool foreign_shot = false;
            for (int i = 0; i < game::object_slots; ++i) {
                game::GameObject a; game::Walker unused;
                load_slot(after, i, a, unused);
                if (a.used && a.kind == 6 && !was_used[std::size_t(i)] && !world.objects.slots[std::size_t(i)].used) foreign_shot = true;
            }
            if (!foreign_shot) report(-1, "bullets alive", world.bullets_alive, W(after, 0x81df2));
            report(-1, "leader shots", world.fire.leader_shots, W(after, 0x82738));
            report(-1, "enemies left", world.objects.enemies, W(after, 0x81f5e));
            report(-1, "kills of recruit 0", world.kills[0], W(after, 0x8066e));
            // The frames up to the next tick: the hold timer, and the fire
            // control while the right button is held.
            if (!next_before.empty() && amiga::read_be16(trace, at + 2) == tick + 1) {
                game::World between = world;
                load_fire(after, between.fire);
                for (int i = 0; i < game::object_slots; ++i) {
                    game::GameObject unused;
                    load_slot(after, i, unused, between.walkers[std::size_t(i)]);
                }
                const bool held = W(next_before, 0x81478) != 0;
                const bool pressed = held && W(after, 0x81478) == 0;
                between.pointer_x = W(next_before, 0x8caba); between.pointer_y = W(next_before, 0x8cabc);
                if (held) for (int f = 0; f < 3; ++f) game::hold_fire(between, tables.fire, pressed && f == 0);
                game::update_fire_control(between, tables.fire);
                game::SquadFire expect;
                load_fire(next_before, expect);
                report(-2, "hold timer", between.fire.hold_timer, expect.hold_timer);
                report(-2, "countdown", between.fire.countdown, expect.countdown);
                report(-2, "order index", between.fire.order_index, expect.order_index);
                if (expect.stats.bullet_speed != 0) // set from the mission's first tick on
                    report(-2, "weapon speed", between.fire.stats.bullet_speed, expect.stats.bullet_speed);
                for (int i = 0; i < game::object_slots; ++i) {
                    if (!world.objects.slots[std::size_t(i)].used || world.objects.slots[std::size_t(i)].kind != 0) continue;
                    game::GameObject unused; game::Walker e;
                    load_slot(next_before, i, unused, e);
                    report(i, "fire_delay between ticks", between.walkers[std::size_t(i)].fire_delay, e.fire_delay);
                    report(i, "aim_x between ticks", between.walkers[std::size_t(i)].aim_x, e.aim_x);
                }
            }
            ++ticks;
        }
        std::printf("%d ticks, %d updates not handled (%d enemy), %d bullets fired, %d falls started, %d enemy updates skipped "
                    "after random choices, %d fields differ\n", ticks, unhandled, unhandled_enemies, created, falls, random_skips, bad);
        return bad == 0 ? 0 : 1;
    }
    if (command == "panel" && (argc == 4 || argc == 5)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const game::PanelArt art(content.read("fodderc", 1), amiga::read_ilbm(content.read("pstuff.lbm", 1)));
        game::PanelState state;
        state.selected_squad = 0;
        state.soldiers = {2, 0, 0};
        state.icon = {1, 0, 0};
        state.roster = {{0, 0, 0, 0, true}, {1, 0, 0, 0, true}};
        const auto panel = game::compose_panel(art, state);
        const auto palette = data::read_palette(content.read("junbase.pal", 2)); // placeholder colours for the PNG
        platform::Image image(game::panel_width, game::panel_height);
        for (std::size_t i = 0; i < panel.size(); ++i) {
            image.pixels[i] = panel[i] ? data::to_color(palette[panel[i]]) : platform::Color{34, 34, 34, 255};
        }
        platform::save_png(image, argv[3]);
        if (argc == 5) {
            const auto dump = platform::read_file(argv[4]); // sprite memory from $394
            const auto word = [&](std::uint32_t address) { return amiga::read_be16(dump, address - 0x394); };
            const std::uint32_t even[3] = {0x398, 0xb30, 0x12c8}, odd[3] = {0x764, 0xefc, 0x1694};
            int bad = 0;
            for (int y = 0; y < game::panel_height; ++y) {
                for (int x = 0; x < game::panel_width; ++x) {
                    const int c = x / 16, bit = 0x8000 >> (x & 15);
                    const auto at = std::uint32_t(y * 4);
                    const int colour = ((word(even[c] + at) & bit) ? 1 : 0) | ((word(even[c] + at + 2) & bit) ? 2 : 0) |
                                       ((word(odd[c] + at) & bit) ? 4 : 0) | ((word(odd[c] + at + 2) & bit) ? 8 : 0);
                    if (colour != panel[std::size_t(y * game::panel_width + x)]) {
                        if (++bad <= 10) std::printf("  (%d,%d) native %d original %d\n", x, y, panel[std::size_t(y * game::panel_width + x)], colour);
                    }
                }
            }
            std::printf("panel pixels %d, differing %d\n", game::panel_width * game::panel_height, bad);
            return bad == 0 ? 0 : 1;
        }
        return 0;
    }
    if (command == "objects" && (argc == 5 || argc == 6)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const int disk = data::map_disk(number);
        const std::string prefix = argv[4];
        const auto map = data::read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
        const game::MapPicture picture(map, data::read_tile_set(content.read(map.base_set, disk)),
                                       data::read_tile_set(content.read(map.sub_set, disk)));
        const auto land = map.base_set.substr(0, 3);
        const game::ObjectSheets sheets{amiga::read_ilbm(content.read(land + "army.lbm", disk)),
                                        amiga::read_ilbm(content.read(land + "copt.lbm", disk))};
        const game::FrameTables frames(content.read("fodderc", 1));
        // The grabbed table, read into native objects (research input).
        const auto table = platform::read_file(prefix + ".objects");
        game::MissionObjects objects;
        for (int i = 0; i < game::object_slots; ++i) {
            const auto r = std::span<const std::uint8_t>(table).subspan(std::size_t(i) * 0x76, 0x76);
            auto& o = objects.slots[std::size_t(i)];
            const auto first = amiga::read_be16(r, 0);
            if (first == 0x8000 || first == 0xffff) continue;
            o.used = true;
            o.x = std::int32_t(amiga::read_be32(r, 0)); o.y = std::int32_t(amiga::read_be32(r, 4));
            o.graphic = std::int16_t(amiga::read_be16(r, 8)); o.frame = std::int16_t(amiga::read_be16(r, 0xa));
            o.kind = std::int16_t(amiga::read_be16(r, 0x18)); o.removed = amiga::read_be16(r, 0x24) != 0;
            o.height = std::int16_t(amiga::read_be16(r, 0x1e)); o.layer = std::int16_t(amiga::read_be16(r, 0x2c));
            o.hidden_lines = std::int16_t(amiga::read_be16(r, 0x52));
        }
        int camx, camy, snapx, snapy, dirx, diry;
        {
            std::ifstream in(prefix + ".txt");
            in >> camx >> camy >> snapx >> snapy >> dirx >> diry;
        }
        auto view = game::view_indices(picture, {camx, camy});
        const auto order = game::draw_list(objects, {snapx, snapy}, dirx, diry);
        game::draw_objects(view, {camx, camy}, objects, order, frames, sheets);
        const auto buffer = platform::read_file(prefix + ".buffer");
        const auto ring = [&](int x, int y) {
            const int index = ((y * 320 + x) % 81920 + 81920) % 81920;
            const int line = index / 320, column = index % 320;
            int colour = 0;
            for (int p = 0; p < 4; ++p) {
                if (buffer[std::size_t(p * 0x2828 + line * 40 + column / 8)] & (0x80 >> (column & 7))) colour |= 1 << p;
            }
            return colour;
        };
        platform::Image diff(game::view_width, game::view_height);
        int bad = 0, total = 0;
        for (int line = 0; line < game::view_height; ++line) {
            for (int column = 48; column < game::view_width; ++column) {
                const int x = camx + column - game::view_column_of_camera, y = camy + line;
                if (x >= picture.width() || y >= picture.height()) continue;
                ++total;
                const bool same = ring(x, y) == view[std::size_t(line * game::view_width + column)];
                bad += !same;
                diff.put(column, line, same ? platform::Color{30, 90, 30, 255} : platform::Color{255, 60, 60, 255});
            }
        }
        std::printf("draw list:");
        for (const int i : order) std::printf(" %d", i);
        std::printf("\nplay area pixels %d, differing %d\n", total, bad);
        if (argc == 6) platform::save_png(diff, argv[5]);
        return bad == 0 ? 0 : 1;
    }
    if (command == "spawn" && (argc == 4 || argc == 5)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const auto mission = game::spawn_objects(
            data::read_placements(content.read("mapm" + std::to_string(number) + ".spt", data::map_disk(number))));
        std::printf("enemies %d, kinds $6A/$48 %d\n", mission.enemies, mission.kind_6a_48);
        std::vector<std::uint8_t> dump;
        if (argc == 5) dump = platform::read_file(argv[4]);
        int bad = 0;
        for (int i = 0; i < game::object_slots; ++i) {
            const auto& o = mission.slots[std::size_t(i)];
            if (o.used) {
                std::printf("slot %2d kind %3d at %4d,%4d graphic $%02X squad %d delay %3d side %d\n", i, o.kind,
                            o.x >> 16, o.y >> 16, o.graphic, o.squad, o.start_delay, o.side);
            }
            if (dump.empty()) continue;
            const auto r = std::span<const std::uint8_t>(dump).subspan(std::size_t(i) * 0x76, 0x76);
            const auto w = [&](std::size_t at) { return int(std::int16_t(amiga::read_be16(r, at))); };
            const bool free_slot = amiga::read_be16(r, 0) == 0x8000;
            std::vector<std::pair<const char*, std::pair<int, int>>> fields;
            if (free_slot != !o.used) fields.push_back({"used", {o.used, !free_slot}});
            if (o.used && !free_slot) {
                fields = {{"x", {o.x >> 16, w(0)}}, {"x.frac", {o.x & 0xffff, w(2) & 0xffff}},
                          {"y", {o.y >> 16, w(4)}}, {"graphic", {o.graphic, w(8)}}, {"kind", {o.kind, w(0x18)}},
                          {"side", {o.side, w(0x22)}}, {"home_x", {o.home_x, w(0x26)}}, {"home_y", {o.home_y, w(0x28)}},
                          {"squad", {o.squad, w(0x32)}}, {"delay", {o.start_delay, w(0x4a)}}};
            }
            for (const auto& [name, values] : fields) {
                if (values.first != values.second) {
                    ++bad;
                    std::printf("  slot %d %s: native %d original %d\n", i, name, values.first, values.second);
                }
            }
        }
        if (!dump.empty()) std::printf("%d fields differ\n", bad);
        return bad == 0 ? 0 : 1;
    }
    if (command == "camreplay" && (argc == 4 || argc == 5)) {
        const bool native_holds = argc == 5 && std::string(argv[4]) == "--holds";
        using namespace fodder;
        const game::GeometryTables tables(data::Content(argv[2]).read("fodderc", 1));
        // frame camx camy vx vy ax ay fx fy speed maxspeed tx ty holdx holdy signx signy ticking vbl mapw maph fits
        // tick parity dirx diry drbx drby
        struct Row { long v[28]; };
        std::vector<Row> rows;
        std::ifstream in(argv[3]);
        for (std::string line; std::getline(in, line);) {
            std::istringstream fields(line);
            Row row{};
            for (auto& value : row.v) fields >> value;
            rows.push_back(row);
        }
        if (rows.empty()) throw std::runtime_error("empty trace");
        const auto& first = rows.front().v;
        game::CameraMotion motion;
        motion.x = std::int32_t(first[1]); motion.y = std::int32_t(first[2]);
        motion.velocity_x = std::int32_t(first[3]); motion.velocity_y = std::int32_t(first[4]);
        motion.speed = std::int32_t(first[9]);
        motion.sign_x = int(first[15]); motion.sign_y = int(first[16]);
        motion.hold_x = int(first[13]); motion.hold_y = int(first[14]);
        motion.drawer_direction_x = int(first[24]); motion.drawer_direction_y = int(first[25]);
        motion.drawer_ring_x = int(first[26]); motion.drawer_ring_line = int(first[27]);
        int bad = 0;
        for (std::size_t k = 0; k + 1 < rows.size(); ++k) {
            const auto& r = rows[k].v;
            const auto& next = rows[k + 1].v;
            motion.follow_x_axis = {r[7] != 0, int(r[5] >> 16)};
            motion.follow_y_axis = {r[8] != 0, int(r[6] >> 16)};
            motion.max_speed = int(r[10]);
            if (!native_holds) { motion.hold_x = int(r[13]); motion.hold_y = int(r[14]); }
            game::CameraFrame frame{int(r[19]), int(r[20]), r[21] != 0, r[17] != 0 && r[18] >= 3,
                                    {int(r[11]), int(r[12])}};
            game::step_camera(motion, tables, frame);
            // A tick starts after this frame when the next recording shows a
            // new tick number; on even ticks (parity 0) the drawer runs.
            if (native_holds && next[22] != r[22] && next[23] == 0) {
                game::tile_drawer_tick(motion);
            }
            if (motion.x != next[1] || motion.y != next[2] || motion.velocity_x != next[3] ||
                motion.velocity_y != next[4] || motion.speed != next[9]) {
                if (++bad <= 5) {
                    std::printf("frame %ld: native %d,%d v %d,%d speed %d; original %ld,%ld v %ld,%ld speed %ld\n", next[0],
                                motion.x, motion.y, motion.velocity_x, motion.velocity_y, motion.speed, next[1], next[2],
                                next[3], next[4], next[9]);
                }
                motion.x = std::int32_t(next[1]); motion.y = std::int32_t(next[2]); // resynchronise
                motion.velocity_x = std::int32_t(next[3]); motion.velocity_y = std::int32_t(next[4]);
                motion.speed = std::int32_t(next[9]);
            }
        }
        std::printf("%zu frames, %d differ\n", rows.size() - 1, bad);
        return bad == 0 ? 0 : 1;
    }
    std::fprintf(stderr, "usage: cannon_fodder_assets import <disk1> <disk2> <disk3> <content>\n"
                         "       cannon_fodder_assets export <content> <output folder>\n"
                         "       cannon_fodder_assets view <content> <map> <x> <y> <out.png> [out.raw]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
