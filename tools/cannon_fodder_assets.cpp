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
//   cannon_fodder_assets music <content folder> <disk:song.sng> <disk:song.ins> <song|eN> <seconds> <out.wav>
//   cannon_fodder_assets play <content folder> <frames> <out.png> [phase=N] [F0-F1:X,Y,l|r|b ...]
//   cannon_fodder_assets sndreplay <content folder> <song.sng> <song.ins> <trace> <sng address> <ins address>
//
// music renders a song (or with eN, effect pattern N on channel 2) through
// the native player at 50 frames a second to a 44.1 kHz stereo WAV file.
//
// sndreplay checks the RJP1 player frame by frame against a recording of the
// original's player (harness/sndtrace.py): each frame starts from the
// original's player state; every field is compared after the native frame.
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
#include "cannon_fodder_amiga/game/dispatch.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
#include "cannon_fodder_amiga/game/front_end.hpp"
#include "cannon_fodder_amiga/game/module.hpp"
#include "cannon_fodder_amiga/game/scenery.hpp"
#include "cannon_fodder_amiga/sound/board.hpp"
#include "cannon_fodder_amiga/sound/rjp1.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/game/object_view.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include "platform/sha256.hpp"
#include "state/archive.hpp"
#include <algorithm>
#include <thread>
#include <chrono>
#include <set>
#include <cstdio>
#include <cstdlib>
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
        game::Terrain terrain(map, content.read(base + ".hit", disk), content.read(sub + ".hit", disk),
                              content.read(base + ".bht", disk), content.read(sub + ".bht", disk), program);
        terrain.set_swaps(content.read(base + ".swp", disk), content.read(sub + ".swp", disk));
        sound::Rjp1 music(content.read(base + ".sng", disk), content.read(base + ".ins", disk), program);
        int sound_requests = 0, bird_cries = 0, walk_orders = 0, chance_shots = 0;
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
        int ticks = 0, bad = 0, unhandled = 0, created = 0, falls = 0, unhandled_enemies = 0, random_skips = 0, created_effects = 0,
            map_changes = 0;
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
                s.fall_count = W(b, r + 0x2a); s.shadowed = B(b, r + 0x58) != 0;
                s.hit_from_behind = B(b, r + 0x59) != 0;
                o.on_screen = B(b, r + 0x5c) != 0;
                s.fire_delay = W(b, r + 0x4a); s.skill = W(b, r + 0x62);
                s.target = W(b, r + 0x5e);
                {
                    // Vehicles and the like (game/vehicles.cpp).
                    const auto slot_of = [&](std::uint32_t p) {
                        return p >= 0xfdb2c && p < 0xfdb2c + 44 * 0x76 ? int((p - 0xfdb2c) / 0x76) : -1;
                    };
                    const int k = W(b, r + 0x18);
                    s.vehicle_type = B(b, r + 0x6f); s.is_vehicle = B(b, r + 0x65) != 0;
                    s.rest_x = W(b, r + 0x46); s.rest_y = W(b, r + 0x48); s.patrol = W(b, r + 0x62);
                    s.fire_held = B(b, r + 0x57) != 0; s.fire_request = B(b, r + 0x54) != 0;
                    s.barrel = B(b, r + 0x5b) != 0; s.gun_toggle = B(b, r + 0x55) != 0; s.sink = B(b, r + 0x56);
                    s.flags75 = B(b, r + 0x75); s.hits = B(b, r + 0x74);
                    s.crashing = (W(b, r + 0x26) & 0xffff) == 0x7171;
                    s.boarding = slot_of(std::uint32_t(L(b, r + 0x66)));
                    s.vehicle = slot_of(std::uint32_t(L(b, r + 0x6a)));
                    if (k != 0) s.land = B(b, r + 0x6e) != 0;
                    if (k == 0x2e || k == 0x61) s.homing_target = slot_of(std::uint32_t(L(b, r + 0x1a)));
                    if (k == 0x2d || k == 0x2e || k == 0x61) { s.thrust = L(b, r + 0x6a); s.wall_check = W(b, r + 0x32) != 0; }
                    static const int by_offset[] = {0x4e, 0x4f, 0x54, 0x55, 0x69, 0x50, 0x51, 0x52, 0x3f, 0x40, 0x48, 0x6a};
                    if (std::find(std::begin(by_offset), std::end(by_offset), k) != std::end(by_offset)) s.target = W(b, r + 0x5e) / 0x76;
                    if (k == 0x48 || k == 0x6a) s.idle_frame = W(b, r + 0x32); // the hostage's standing picture
                }
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
                const int kind_here = W(before, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x18);
                const auto link = std::uint32_t(L(before, 0xfdb2c + std::uint32_t(i) * 0x76 + (kind_here == 2 || kind_here == 0x21 ? 0x46 : 0x1a)));
                world.attachments[std::size_t(i)] = {link >= 0xfdb2c && link < 0xfdb2c + 44 * 0x76 ? int((link - 0xfdb2c) / 0x76) : -1,
                                                     W(before, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x62)};
                const auto& o = world.objects.slots[std::size_t(i)];
                const auto roster = std::uint32_t(L(before, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x46));
                if (o.used && o.kind == 0 && roster >= 0x80664 && roster < 0x80664 + 8 * 0xc)
                    world.roster_slots[(roster - 0x80664) / 0xc] = i;
            }
            // The roster's own links (+4, an object offset; -1 none) where the
            // recording has them (entries 0 and 1): a phase reached by poking
            // the phase number leaves them all at 0.
            for (std::uint32_t n = 0; n < 2; ++n) {
                const int link = W(before, 0x80664 + n * 12 + 4);
                world.roster_slots[n] = link < 0 ? -1 : link / 0x76;
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
            world.objects.full = W(before, 0x826ea) != 0;
            world.objects.full_below_30 = W(before, 0x826ec) != 0;
            for (std::uint32_t n = 0; n < 2; ++n) {
                world.grenades_in_air[n] = B(before, 0x81df0 + n);
                world.rockets_in_air[n] = B(before, 0x81df4 + n);
            }
            for (std::uint32_t n = 0; n < 3; ++n) {
                world.squads[n].grenades = W(before, 0x81f4c + n * 2);
                world.squads[n].rockets = W(before, 0x81f52 + n * 2);
            }
            world.landscape = W(before, 0x8278c);
            {
                const auto slot_of = [&](std::uint32_t p) {
                    return p >= 0xfdb2c && p < 0xfdb2c + 44 * 0x76 ? int((p - 0xfdb2c) / 0x76) : -1;
                };
                world.driven_vehicle = slot_of(std::uint32_t(L(before, 0x82798)));
                world.tick_counter = W(before, 0x8061a);
                world.enemy_helicopter = W(before, 0x82702) != 0;
                world.skill_average = W(before, 0x80628);
                world.rescue_x = W(before, 0x82a6c) < 0 ? -1 : W(before, 0x82a6c);
                world.rescue_y = W(before, 0x82a6e);
                world.target_distance = W(before, 0x81fd8);
                world.target_distance2 = W(before, 0x82704);
                world.civilian_house = slot_of(std::uint32_t(L(before, 0x82a18)));
                world.rescue_tent = slot_of(std::uint32_t(L(before, 0x82b96)));
                world.hostage_held = W(before, 0x8287a) != 0;
                for (std::uint32_t n = 0; n < 3; ++n) world.boarding_count[n] = W(before, 0x82a02 + n * 2);
            }
            for (std::uint32_t n = 0; n < 3; ++n) world.route_window[n] = W(before, 0x81dde + n * 2);
            const auto load_mission = [&](std::span<const std::uint8_t> b, game::World& m) {
                m.skill_min = W(b, 0x8062a); m.skill_max = W(b, 0x8062c); m.skill_next = W(b, 0x8062e); m.skill_step = W(b, 0x80630);
                m.spawns = W(b, 0x80650);
                for (std::uint32_t k = 0; k < 8; ++k) m.objectives[k] = W(b, 0x82a08 + k * 2) != 0;
                m.civilian_home = W(b, 0x82a1c) != 0;
                m.objects.kind_6a_48 = W(b, 0x81f60);
                m.buildings_left = W(b, 0x81dd0);
                m.outcome = W(b, 0x81dcc) ? game::World::Outcome::won : W(b, 0x81dca) ? game::World::Outcome::lost : game::World::Outcome::playing;
            };
            load_mission(before, world);
            world.snapshot = {W(before, 0x81506), W(before, 0x8150a)};
            world.drawn_objects = W(before, 0x82736);
            world.jitter_next_effect = W(before, 0x827da) != 0;
            {
                const auto read = std::uint32_t(L(before, 0x826ac)), write = std::uint32_t(L(before, 0x826a8));
                world.blast_read = int((read - 0x82498) / 4);
                world.blast_write = int((write - 0x82498) / 4);
                world.blast_count = 0;
                for (std::uint32_t k = 0; k < 128; ++k) {
                    world.blast_points[k] = {W(before, 0x82498 + k * 4), W(before, 0x82498 + k * 4 + 2)};
                }
                world.blast_count = (world.blast_write - world.blast_read + 128) % 128;
            }
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
            const int world_enemies_before = world.objects.enemies;
            const auto walkers_before = world.walkers;
            for (int i = 0; i < game::object_slots; ++i) // (a negative x counts as free)
                was_used[std::size_t(i)] = world.objects.slots[std::size_t(i)].used && world.objects.slots[std::size_t(i)].x >= 0;
            for (int i = 0; i < game::object_slots; ++i) {
                const auto& o = world.objects.slots[std::size_t(i)];
                if (!o.used || o.removed || std::int16_t(std::uint32_t(o.x) >> 16) < 0) continue; // (negative x: skipped, as free)
                bool done = false;
                if (o.kind == 0 || o.kind == 5 || o.kind == 0x24) {
                    done = o.kind == 0 ? game::update_soldier(world, terrain, tables, i) : game::update_object(world, terrain, tables, i);
                    if (done) handled[std::size_t(i)] = o.kind == 0 ? 1 : 5;
                    else if (o.kind != 0) ++unhandled_enemies;
                } else if (o.kind == 6) {
                    done = game::update_bullet(world, terrain, tables, i);
                    if (done) handled[std::size_t(i)] = 2;
                } else if (o.kind == 0x16 || o.kind == 0x17) {
                    if (o.kind == 0x16) game::update_shadow(world, i); else game::update_blood(world, i);
                    done = true;
                    handled[std::size_t(i)] = 4;
                } else {
                    // Every other kind through the game's own dispatch.
                    done = game::update_object(world, terrain, tables, i);
                    if (done) handled[std::size_t(i)] = 6;
                    else if (!done) done = false;
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
                if (native != original && ++bad <= (std::getenv("CF_ALL") ? 100000 : 40))
                    std::printf("tick %d slot %d %s: native %ld original %ld\n", tick, i, name, native, original);
            };
            for (int i = 0; i < game::object_slots; ++i) {
                const auto& o = world.objects.slots[std::size_t(i)];
                if (!was_used[std::size_t(i)] && o.used && o.kind == 6) handled[std::size_t(i)] = 3;
                if (!was_used[std::size_t(i)] && o.used && o.kind == 0x17) handled[std::size_t(i)] = 4;
                else if (!was_used[std::size_t(i)] && o.used && o.kind == 0x16) handled[std::size_t(i)] = 7; // follows a body thrown by chance
                else if (!was_used[std::size_t(i)] && o.used && o.kind != 6 && o.kind != 0x16 && o.kind != 0x17) {
                    // New this tick: grenades and rockets (and their shadows)
                    // are made without chance; the rest by kind only.
                    const bool exact = o.kind == 2 || o.kind == 0x21 ||
                                       (o.kind == 3 && i > 0 && (world.objects.slots[std::size_t(i - 1)].kind == 2 ||
                                                                 world.objects.slots[std::size_t(i - 1)].kind == 0x21));
                    handled[std::size_t(i)] = exact ? 6 : 7;
                }
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
                // An enemy tank's turret with no target turns one step either
                // way at random (every 16th tick, $99AE8).
                const bool turret_wander = o.graphic == 0xd2 && expected.graphic == 0xd2 &&
                                           (((o.frame - expected.frame) & 15) == 2 || ((o.frame - expected.frame) & 15) == 14);
                // The hostage's heading wobbles at random (as a civilian's,
                // $9912A); a held one stays in place.
                const bool hostage = o.kind == 0x48 || o.kind == 0x6a;
                const std::pair<const char*, std::pair<long, long>> common[] = {
                    {"x", {o.x, expected.x}}, {"y", {o.y, expected.y}}, {"graphic", {o.graphic, expected.graphic}},
                    {"frame", {turret_wander ? expected.frame : o.frame, expected.frame}}, {"heading", {hostage ? e.heading : n.heading, e.heading}},
                    {"state", {n.state, e.state}}, {"layer", {o.layer, expected.layer}}, {"kind", {o.kind, expected.kind}},
                    {"height", {o.height, expected.height}}, {"aim_x", {n.aim_x, e.aim_x}}, {"aim_y", {n.aim_y, e.aim_y}}};
                // A shot or rocket the native side made in a tick where an
                // enemy's random choices differed (the original's slot is
                // still free): by chance.
                if (!was_used[std::size_t(i)] && (o.kind == 6 || o.kind == 0x21 || o.kind == 3 || o.kind == 0x2d) &&
                    (!expected.used || std::int16_t(std::uint32_t(expected.x) >> 16) < 0)) {
                    ++chance_shots;
                    continue;
                }
                if (handled[std::size_t(i)] == 7) { // new, partly by chance: the kind only (a splash's either way)
                    const bool splashes = (o.kind == 0x56 || o.kind == 0x57) && (expected.kind == 0x56 || expected.kind == 0x57);
                    if (!splashes) report(i, "kind", o.kind, expected.kind);
                    ++created_effects;
                    continue;
                }
                if (handled[std::size_t(i)] == 6 && (o.kind == 0x3d || o.kind == 0x3e || o.kind == 0x46 || o.kind == 0x53)) {
                    // A civilian's heading (all four variants) wobbles at random (0-14 either way)
                    // each tick: position and heading by range, the rest exact.
                    const auto& was = walkers_before[std::size_t(i)];
                    const bool new_place = n.waypoint_x != e.waypoint_x || n.waypoint_y != e.waypoint_y;
                    if (std::abs(o.x - expected.x) > 0x20000 || std::abs(o.y - expected.y) > 0x20000)
                        report(i, "civilian place range", o.x / 65536, expected.x / 65536);
                    if (!new_place && (std::abs(n.stopped - e.stopped) > 0)) report(i, "civilian wait", n.stopped, e.stopped);
                    report(i, "civilian graphic", o.graphic, expected.graphic);
                    (void)was;
                    ++random_skips;
                    continue;
                }
                {
                    // The end pose a recruit picks in the first tick after the
                    // phase ends ($96658) is random: salute, jump or arms.
                    const auto pose = [](int g) { return g == 0x9f || g == 0x41 || g == 0x38; };
                    const auto& was = walkers_before[std::size_t(i)];
                    (void)was;
                    if (o.kind == 0 && pose(o.graphic) && pose(expected.graphic) && o.graphic != expected.graphic) {
                        ++random_skips;
                        continue;
                    }
                }
                if (hostage) {
                    // Its heading wobbles (so the place, by range: 2 pixels),
                    // and a standing one picks its picture at random every
                    // 16th tick ($99106).
                    if (std::abs(o.x - expected.x) > 0x20000 || std::abs(o.y - expected.y) > 0x20000)
                        report(i, "hostage place range", o.x / 65536, expected.x / 65536);
                    report(i, "hostage graphic", o.graphic, expected.graphic);
                    if (o.graphic != 0xda || expected.graphic != 0xda) report(i, "hostage frame", o.frame, expected.frame);
                    report(i, "hostage kind", o.kind, expected.kind);
                    report(i, "hostage waypoint x", n.waypoint_x, e.waypoint_x);
                    report(i, "hostage waypoint y", n.waypoint_y, e.waypoint_y);
                    report(i, "hostage target", n.target, e.target);
                    report(i, "hostage riding", n.carried, e.carried);
                    ++random_skips;
                    continue;
                }
                                if (handled[std::size_t(i)] == 6 && o.kind == 0x18) {
                    // The spawn point's timer is drawn at random (10-265).
                    const auto& was = walkers_before[std::size_t(i)];
                    if (e.state_timer > was.state_timer && n.state_timer > was.state_timer) { // drawn this tick
                        if (e.state_timer < 10 || e.state_timer > 265 || n.state_timer < 10 || n.state_timer > 265)
                            report(i, "spawn timer range", n.state_timer, e.state_timer);
                        n.state_timer = e.state_timer;
                    }
                }
                if (handled[std::size_t(i)] == 6 && (o.kind == 0x42 || o.kind == 0x43)) {
                    // A bird comes in again at a random place, and starts with a
                    // random wing position: checked by kind only then.
                    const auto& was = walkers_before[std::size_t(i)];
                    const bool was_on_screen = W(before, 0xfdb2c + std::uint32_t(i) * 0x76 + 0x5c) & 0xff00;
                    if (was.state_timer == 0 || (!was_on_screen && was.animation_timer == 0)) {
                        ++random_skips;
                        continue;
                    }
                }
                if (handled[std::size_t(i)] == 6 && o.kind == 0x14) {
                    // A hut's open-door time and its throw are drawn by chance:
                    // checked by their ranges.
                    const auto& was = walkers_before[std::size_t(i)];
                    if (was.state == 5 || was.state == 7) {
                        if (n.state != 6 || e.state != 6) report(i, "hut thrown", n.state, e.state);
                        ++random_skips;
                        continue;
                    }
                    if (std::int8_t(was.idle) == 0 && world_enemies_before < 10) {
                        const int base = std::max(20 - world.skill_max, 0) * 8;
                        if (e.state_timer < base - 1 || e.state_timer > base + 14) report(i, "original door time outside the rule", e.state_timer, base);
                        if (n.state_timer < base - 1 || n.state_timer > base + 14) report(i, "door time range", n.state_timer, base);
                        n.state_timer = e.state_timer;
                    }
                }
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
                if (handled[std::size_t(i)] == 6) {
                    report(i, "timer", n.state_timer, e.state_timer);
                    report(i, "speed", n.speed, e.speed);
                    report(i, "hidden", o.hidden_lines, expected.hidden_lines);
                    report(i, "vertical_speed", n.vertical_speed, e.vertical_speed);
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
                    {"facing", {n.facing, e.facing}}, {"facing_timer", {n.facing_timer, e.facing_timer}},
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
            {
                // The sound board over this tick's requests ($A3A30).
                sound::SoundBoard board;
                board.last_channel = W(before, 0x81df6);
                for (std::uint32_t k = 0; k < 4; ++k) {
                    board.priority[k] = std::int8_t(B(before, 0x81df8 + k));
                    board.busy[k] = B(before, 0x81dfc + k);
                }
                board.silenced = W(before, 0x81dce) != 0;
                sound_requests += int(world.sounds.size());
                board.play_all(world, tables.geometry, music);
                // Birds cry at random (1 in 128 a tick each): a tick where
                // only the number of requests differs then counts as chance.
                bool birds = false;
                for (const auto& o : world.objects.slots) birds = birds || (o.used && (o.kind == 0x42 || o.kind == 0x43));
                if (birds && board.last_channel != W(after, 0x81df6)) {
                    ++bird_cries;
                } else {
                    report(-5, "sound channel", board.last_channel, W(after, 0x81df6));
                    for (std::uint32_t k = 2; k < 4; ++k) {
                        report(-5, "sound priority", board.priority[k], std::int8_t(B(after, 0x81df8 + k)));
                        report(-5, "sound busy", board.busy[k], B(after, 0x81dfc + k));
                    }
                    board.tick();
                    if (!next_before.empty() && amiga::read_be16(trace, at + 2) == tick + 1)
                        for (std::uint32_t k = 0; k < 4; ++k) report(-5, "sound busy next tick", board.busy[k], B(next_before, 0x81dfc + k));
                }
            }
            report(-1, "skill next", world.skill_next, W(after, 0x8062e));
            report(-1, "skill max", world.skill_max, W(after, 0x8062c));
            report(-1, "grenades in the air", world.grenades_in_air[0], B(after, 0x81df0));
            report(-1, "rockets in the air", world.rockets_in_air[0], B(after, 0x81df4));
            report(-1, "no free slot", world.objects.full, W(after, 0x826ea) != 0);
            report(-1, "no free slot below 30", world.objects.full_below_30, W(after, 0x826ec) != 0);
            report(-1, "squad grenades", world.squads[0].grenades, W(after, 0x81f4c));
            report(-1, "squad rockets", world.squads[0].rockets, W(after, 0x81f52));
            report(-1, "blast ring write", world.blast_write, int((std::uint32_t(L(after, 0x826a8)) - 0x82498) / 4));
            report(-1, "kills of recruit 0", world.kills[0], W(after, 0x8066e));
            // The ranks on the roster (the top-rank power-up raises them; the
            // trace holds the first three places).
            for (int k = 0; k < 8; ++k) {
                const auto& w = world.walkers[std::size_t(k)];
                const auto roster = std::uint32_t(L(after, 0xfdb2c + std::uint32_t(k) * 0x76 + 0x46));
                if (world.objects.slots[std::size_t(k)].used && roster && roster + 2 < 0x80680) report(k, "rank", w.rank, B(after, roster + 2));
            }
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
                // (Both-button presses: the original's first fire-control frame
                // is sometimes one later than modelled here; open, see game.md.)
                if (held) for (int f = 0; f < 3; ++f) game::hold_fire(between, tables.fire, pressed && f == 0);
                game::update_fire_control(between, tables.fire);
                // The mission checks of the next tick ($9DC18, $9DD64), from the
                // state the frames left.
                {
                    game::World m = between;
                    load_mission(after, m);
                    for (int i = 0; i < game::object_slots; ++i) {
                        load_slot(next_before, i, m.objects.slots[std::size_t(i)], m.walkers[std::size_t(i)]);
                    }
                    m.roster_slots = world.roster_slots;
                    game::rebuild_squads(m);
                    for (std::uint32_t k = 0; k < 27; ++k) {
                        const auto p = std::uint32_t(L(next_before, 0x85b26 + k * 4));
                        const int expect = p >= 0xfdb2c && p < 0xfdb2c + 44 * 0x76 ? int((p - 0xfdb2c) / 0x76) : -1;
                        if (k % 9 != 8) report(-4, "recruit list", m.recruit_lists[k], expect);
                    }
                    game::check_objectives(m);
                    report(-4, "buildings left", m.buildings_left, W(next_before, 0x81dd0));
                    report(-4, "won", m.outcome == game::World::Outcome::won, W(next_before, 0x81dcc) != 0);
                    report(-4, "lost", m.outcome == game::World::Outcome::lost, W(next_before, 0x81dca) != 0);
                }
                // A walk order between the ticks ($9EB36): the click's window
                // restarted; its target is at $815EA/$815EC.
                const int sel = W(after, 0x81556);
                // (The next tick has run the window down once: 7.)
                if (sel >= 0 && sel < 3 && W(next_before, 0x81dde + std::uint32_t(sel) * 2) == 7 &&
                    W(after, 0x81dde + std::uint32_t(sel) * 2) != 8) {
                    game::World o = world;
                    for (int i = 0; i < game::object_slots; ++i) load_slot(after, i, o.objects.slots[std::size_t(i)], o.walkers[std::size_t(i)]);
                    for (int n = 0; n < 3; ++n) {
                        auto& q = o.squads[std::size_t(n)];
                        q.route.clear();
                        for (auto p = std::uint32_t(L(after, 0x85c16 + std::uint32_t(n) * 4)); p && W(after, p) >= 0 && locate(p) >= 0; p += 4)
                            q.route.push_back({W(after, p), W(after, p + 2)});
                        q.members.clear();
                        for (auto p = std::uint32_t(L(after, 0x85b12 + std::uint32_t(n) * 4)); p && L(after, p) >= 0 && locate(p) >= 0; p += 4)
                            q.members.push_back(int((std::uint32_t(L(after, p)) - 0xfdb2c) / 0x76));
                        q.route_end = W(after, 0x81dd8 + std::uint32_t(n) * 2) / 4;
                        o.route_window[std::size_t(n)] = W(after, 0x81dde + std::uint32_t(n) * 2);
                    }
                    o.selected_squad = sel;
                    o.leader_slot = W(after, 0x81554) >= 0 ? W(after, 0x81554) / 0x76 : -1;
                    o.last_click = {W(after, 0x826ee), W(after, 0x826f0)};
                    std::vector<std::array<int, 2>> expect;
                    for (auto p = std::uint32_t(L(next_before, 0x85c16 + std::uint32_t(sel) * 4)); p && W(next_before, p) >= 0 && locate(p) >= 0; p += 4)
                        expect.push_back({W(next_before, p), W(next_before, p + 2)});
                    // The target: the route's last waypoint; it should be the
                    // pointer's place (camera + pointer - (22, 3)), give or take
                    // the camera's motion in those frames.
                    const auto target = expect.empty() ? std::array<int, 2>{0, 0} : expect.back();
                    const int px = W(next_before, 0x814ae) + W(next_before, 0x8caba) - 22, py = W(next_before, 0x814b2) + W(next_before, 0x8cabc) - 3;
                    if (std::abs(px - target[0]) > 4 || std::abs(py - target[1]) > 4) report(-6, "target from the pointer", px * 1000 + py, target[0] * 1000 + target[1]);
                    game::order_walk(o, target[0], target[1]);
                    ++walk_orders;
                    auto& q = o.squads[std::size_t(sel)];
                    report(-6, "route length", long(q.route.size()), long(expect.size()));
                    for (std::size_t k = 0; k < std::min(q.route.size(), expect.size()); ++k) {
                        report(-6, "route x", q.route[k][0], expect[k][0]);
                        report(-6, "route y", q.route[k][1], expect[k][1]);
                    }
                    report(-6, "route end", q.route_end, W(next_before, 0x81dd8 + std::uint32_t(sel) * 2) / 4);
                    for (const int m : q.members) {
                        game::GameObject unused; game::Walker e;
                        load_slot(next_before, m, unused, e);
                        report(m, "order waypoint x", o.walkers[std::size_t(m)].waypoint_x, e.waypoint_x);
                        report(m, "order waypoint y", o.walkers[std::size_t(m)].waypoint_y, e.waypoint_y);
                        report(m, "order route index", o.walkers[std::size_t(m)].route_index, e.route_index);
                    }
                }
                // The tile drawer's map damage ($AAFCE), once a tick.
                {
                    game::World damage = between;
                    damage.blast_read = int((std::uint32_t(L(after, 0x826ac)) - 0x82498) / 4);
                    damage.blast_write = int((std::uint32_t(L(after, 0x826a8)) - 0x82498) / 4);
                    damage.blast_count = (damage.blast_write - damage.blast_read + 128) % 128;
                    for (std::uint32_t k = 0; k < 128; ++k) damage.blast_points[k] = {W(after, 0x82498 + k * 4), W(after, 0x82498 + k * 4 + 2)};
                    damage.map_damage_drawing = W(after, 0x82494) != 0;
                    damage.snapshot = {W(next_before, 0x81506), W(next_before, 0x8150a)};
                    for (int i = 0; i < game::object_slots; ++i) load_slot(after, i, damage.objects.slots[std::size_t(i)], damage.walkers[std::size_t(i)]);
                    const int changed = game::apply_map_damage(damage, terrain, tables.blasts);
                    if (changed >= 0) ++map_changes;
                    report(-3, "blast ring read", damage.blast_read, int((std::uint32_t(L(next_before, 0x826ac)) - 0x82498) / 4));
                    report(-3, "map damage drawing", damage.map_damage_drawing, W(next_before, 0x82494) != 0);
                }
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
                    "after random choices, %d effects created, %d map tiles changed, %d sounds (%d ticks with a bird cry), %d fields differ\n",
                    ticks, unhandled, unhandled_enemies, created, falls, random_skips, created_effects, map_changes, sound_requests, bird_cries, bad);
        std::printf("%d walk orders checked, %d shots made on one side only (by chance)\n", walk_orders, chance_shots);
        return bad == 0 ? 0 : 1;
    }
    if (command == "frontcheck" && argc >= 3) {
        // Quick saves of the front end's screens: the debrief (heroes, then
        // with a fallen recruit) and the load screen, saved after some
        // frames, read into a fresh FrontEnd, both run on with the same
        // input: the state and the screen must agree.
        using namespace fodder;
        const data::Content content(argv[2]);
        const auto program = content.read("fodderc", 1);
        const game::MissionTables tables(program);
        const game::PanelArt panel(program, amiga::read_ilbm(content.read("pstuff.lbm", 1)));
        game::Campaign campaign;
        campaign.missions_started = 1;
        for (int k = 0; k < 2; ++k) {
            auto& r = campaign.roster[std::size_t(k)];
            r.name = k;
            r.served = 1;
            r.on_map = true;
        }
        const auto input_at = [](int f) {
            app::GameInput in;
            in.pointer = true;
            in.pointer_x = 150;
            in.pointer_y = f % 200 < 100 ? 60 : 200; // (the load screen: UP, then DOWN)
            in.mouse_button = f % 97 == 50;
            return in;
        };
        int failures = 0;
        for (int variant = 0; variant < 3; ++variant)
            for (const int at : {30, 120, 300, 700}) {
                const auto start = [&](game::FrontEnd& f) {
                    auto c = campaign;
                    if (variant == 1) c.heroes.push_back({5, 3});
                    if (variant < 2) f.start_debrief(c);
                    else f.start_save_list({"A", "B", "C", "D", "E", "F", "G", "H"});
                };
                game::FrontEnd a(content, program, panel), b(content, program, panel);
                start(a);
                for (int f = 0; f < at; ++f) a.tick(input_at(f));
                state::Writer w;
                a.persist(w, tables);
                state::Reader r(w.bytes);
                b.persist(r, tables);
                r.finish();
                state::Writer w2;
                b.persist(w2, tables);
                bool same = w2.bytes == w.bytes && a.indices() == b.indices();
                for (int f = at; f < at + 200; ++f) {
                    a.tick(input_at(f));
                    b.tick(input_at(f));
                    same = same && a.indices() == b.indices();
                }
                failures += !same;
                std::printf("%s at %d: %s\n", variant == 0 ? "debrief" : variant == 1 ? "debrief with fallen" : "load screen", at,
                            same ? "identical" : "DIFFERS");
            }
        return failures == 0 ? 0 : 1;
    }
    if (command == "debrief" && argc >= 5) {
        // The debrief of mission 1 headless, for harness/debriefrec.py: the
        // recruits 0 and 1 back at rank 0 after a phase (and, with
        // `fallen NAME RANK`, one fallen); at each listed round the screen's
        // colour indices go to <out>-<round>.idx (320 x 256 bytes).
        //   debrief <content> <out> [fallen NAME RANK] rounds...
        using namespace fodder;
        const data::Content content(argv[2]);
        const auto program = content.read("fodderc", 1);
        const game::PanelArt panel(program, amiga::read_ilbm(content.read("pstuff.lbm", 1)));
        game::FrontEnd front(content, program, panel);
        game::Campaign campaign;
        campaign.missions_started = 1;
        for (int k = 0; k < 2; ++k) {
            auto& r = campaign.roster[std::size_t(k)];
            r.name = k;
            r.rank = 0;
            r.served = 1;
            r.on_map = true;
        }
        int a = 4;
        if (std::string(argv[a]) == "fallen" && argc >= a + 3) {
            campaign.heroes.push_back({std::stoi(argv[a + 1]), std::stoi(argv[a + 2])});
            a += 3;
        }
        std::set<int> rounds;
        for (; a < argc; ++a) rounds.insert(std::stoi(argv[a]));
        front.start_debrief(campaign);
        app::GameInput input;
        int last = -1;
        for (int f = 0; f < 100000 && front.active(); ++f) {
            const int round = front.debrief_state().round;
            const bool in_rounds = front.debrief_state().mode == game::FrontEnd::DebriefState::Mode::rounds &&
                                   front.debrief_state().frames == 0;
            if (in_rounds && round != last && rounds.count(round)) {
                last = round;
                // (the colour indices: the picture's pixels back to indices)
                platform::write_file(std::string(argv[3]) + "-" + std::to_string(round) + ".idx", front.indices());
                platform::save_png(front.picture(), std::string(argv[3]) + "-" + std::to_string(round) + ".png");
            }
            front.tick(input);
        }
        std::printf("rounds %d\n", front.debrief_state().round);
        return 0;
    }
    if (command == "parallax" && argc >= 5) {
        // The parallax screen headless: per frame the state in the words of
        // the reference's $826B0-$826EA and $85BE4/$85BE6 (harness/paratrace.py),
        // after the frame (the reference's line n is the state before frame n).
        //   parallax <content> <mission> <frames> [png] [landscape: jun, des, ice, mor, int]
        using namespace fodder;
        const data::Content content(argv[2]);
        const auto program = content.read("fodderc", 1);
        const game::PanelArt panel(program, amiga::read_ilbm(content.read("pstuff.lbm", 1)));
        game::FrontEnd front(content, program, panel);
        game::Campaign campaign;
        campaign.missions_started = std::stoi(argv[3]);
        const auto map = data::read_map(content.read("mapm1.map", 1));
        front.start_parallax(campaign, argc >= 7 ? std::string(argv[6]) : map.base_set.substr(0, 3));
        const int frames = std::stoi(argv[4]);
        const auto line = [&](int n) {
            const auto& st = front.parallax_state();
            const auto hi = [](std::int32_t v) { return int(std::int16_t(std::uint32_t(v) >> 16)); };
            const auto lo = [](std::int32_t v) { return int(std::int16_t(v & 0xffff)); };
            std::printf("%d %d 0 %d %d %d %d %d %d %d %d %d 0 0 %d %d %d %d %d %d 0 %d 0 %d %d %d %d %d %d %d 0 %d %d\n", n, st.name_x,
                        hi(st.scroll[0]), lo(st.scroll[0]), hi(st.scroll[1]), lo(st.scroll[1]), hi(st.scroll[2]), lo(st.scroll[2]),
                        hi(st.scroll[3]), lo(st.scroll[3]), 0x158 - st.name_x, n == 0 ? 0 : -32768, hi(st.heli_x), lo(st.heli_x),
                        hi(st.heli_y), lo(st.heli_y), st.speed, st.wanted_speed, st.heading, st.wanted, st.leg * 6, st.leg_left,
                        st.fade, st.rotor, st.ended ? -1 : 0, st.sprite_x - 0x20, st.sprite_y);
        };
        line(0);
        app::GameInput input;
        for (int n = 1; n <= frames && front.active(); ++n) {
            front.tick(input);
            line(n);
        }
        if (argc >= 6) platform::save_png(front.picture(), argv[5]);
        if (std::getenv("CF_IDX")) platform::write_file(std::getenv("CF_IDX"), front.indices());
        return 0;
    }
    if (command == "savecheck" && argc >= 4) {
        // Quick saves: a new game run with scripted clicks; at each listed
        // frame the state is written, read into a fresh game and both run on
        // 120 frames: the state, the picture and the sound must agree.
        //   savecheck <content> <frame>...
        const auto input_at = [](int f) {
            app::GameInput in;
            in.pointer = true;
            // A click every 100 frames: through the screens, then walking
            // orders around the map (and the right button now and then).
            in.pointer_x = 160 + (f / 100 % 5) * 20 - 40;
            in.pointer_y = 60 + (f / 100 % 3) * 40;
            in.mouse_button = f % 100 < 3;
            in.mouse_right = f > 600 && f % 350 < 30;
            if (f % 1000 >= 500 && f % 1000 < 503) { // the globe: the map's overview (closed by the next click)
                in.pointer_x = 10;
                in.pointer_y = 216;
                in.mouse_right = false;
            }
            return in;
        };
        const auto picture_hash = [](const app::GameModule& g) {
            const auto im = g.picture();
            return platform::sha256({reinterpret_cast<const std::uint8_t*>(im.pixels.data()), im.pixels.size() * sizeof(platform::Color)});
        };
        const auto run = [&](app::GameModule& g, int from, int to) {
            std::vector<float> audio(2 * 882);
            std::vector<std::uint8_t> heard;
            for (int f = from; f < to; ++f) {
                g.tick(input_at(f));
                g.audio(audio, 44100);
                const auto* b = reinterpret_cast<const std::uint8_t*>(audio.data());
                heard.insert(heard.end(), b, b + audio.size() * sizeof(float));
            }
            return platform::sha256(heard);
        };
        std::vector<int> points;
        int first_phase = -1; // phase=N: straight into that phase (no screens)
        bool smooth = false;  // smooth: smooth motion; wide=W,H: widescreen in that window
        int wide_w = 0, wide_h = 0;
        for (int a = 3; a < argc; ++a) {
            if (std::string(argv[a]).rfind("phase=", 0) == 0) first_phase = std::stoi(std::string(argv[a]).substr(6));
            else if (std::string(argv[a]) == "smooth") smooth = true;
            else if (std::sscanf(argv[a], "wide=%d,%d", &wide_w, &wide_h) == 2) {}
            else points.push_back(std::stoi(argv[a]));
        }
        const auto configure = [&](app::GameModule& g) {
            if (smooth) g.set_option(1, 1);
            if (wide_w > 0) {
                g.set_option(0, 1);
                g.set_canvas(wide_w, wide_h);
            }
        };
        std::sort(points.begin(), points.end());
        auto game = fodder::game::start_game(argv[2], first_phase);
        configure(*game);
        int at = 0, failures = 0;
        for (const int point : points) {
            run(*game, at, point);
            at = point;
            const auto bytes = game->write_state();
            const auto described = fodder::game::describe(*game);
            if (const char* dir = std::getenv("CF_SHOTS"))
                platform::save_png(game->picture(), std::string(dir) + "/save" + std::to_string(point) + ".png");
            auto copy = fodder::game::start_game(argv[2], first_phase);
            configure(*copy);
            copy->read_state(bytes);
            const bool same_state = copy->write_state() == bytes;
            const bool same_picture = picture_hash(*copy) == picture_hash(*game);
            // Continue both from here (the original game goes on afterwards).
            const auto heard_copy = run(*copy, at, at + 120);
            const auto heard = run(*game, at, at + 120);
            at += 120;
            const bool same_after = copy->write_state() == game->write_state() && picture_hash(*copy) == picture_hash(*game);
            const bool same_sound = heard == heard_copy;
            const bool ok = same_state && same_picture && same_after && same_sound;
            failures += !ok;
            std::printf("frame %6d: %zu bytes, %s%s%s%s%s | %s\n", point, bytes.size(), ok ? "identical" : "DIFFERS:",
                        same_state ? "" : " state", same_picture ? "" : " picture", same_after ? "" : " continued",
                        same_sound ? "" : " sound", described.c_str());
        }
        return failures == 0 ? 0 : 1;
    }
    if (command == "realtime" && argc >= 4) {
        // The shell's loop against the wall clock (research): pictures at
        // 120 Hz, ticks when due, a new game clicked through, the phase won
        // after `win` seconds; per second the ticks, samples and the slowest
        // picture.
        //   realtime <content> <seconds> [smooth] [wide=W,H] [win=S]
        using clock = std::chrono::steady_clock;
        auto game = fodder::game::start_game(argv[2], -1);
        double win_at = -1;
        for (int a = 4; a < argc; ++a) {
            int w = 0, h = 0;
            if (std::string(argv[a]) == "smooth") game->set_option(1, 1);
            else if (std::sscanf(argv[a], "wide=%d,%d", &w, &h) == 2) { game->set_option(0, 1); game->set_canvas(w, h); }
            else std::sscanf(argv[a], "win=%lf", &win_at);
        }
        const double seconds = std::stod(argv[3]);
        const auto start = clock::now();
        const auto since = [&] { return std::chrono::duration<double>(clock::now() - start).count(); };
        double game_clock = 0, sample_credit = 0, slowest = 0;
        int ticks = 0, samples_total = 0, second = 0, frames = 0;
        bool won = false;
        std::vector<float> audio;
        app::GameInput input;
        input.pointer = true;
        input.pointer_x = 150;
        input.pointer_y = 100;
        for (int display = 0;; ++display) {
            const double now = since();
            if (now >= seconds) break;
            if (!won && win_at >= 0 && now >= win_at) { fodder::game::win_phase(*game); won = true; }
            game_clock = std::max(game_clock, now - 4 * game->step_seconds());
            while (game_clock + game->step_seconds() <= now) {
                input.mouse_button = frames % 100 < 3;
                game->tick(input);
                ++frames;
                ++ticks;
                game_clock += game->step_seconds();
                sample_credit += game->step_seconds() * 44100;
                const int samples = int(sample_credit);
                sample_credit -= samples;
                samples_total += samples;
                audio.resize(std::size_t(samples) * 2);
                game->audio(audio, 44100);
            }
            game->set_frame_fraction((now - game_clock) / game->step_seconds());
            const auto t0 = clock::now();
            (void)game->picture();
            slowest = std::max(slowest, std::chrono::duration<double>(clock::now() - t0).count());
            if (int(now) != second) {
                std::printf("second %d: %d ticks, %d samples, slowest picture %.2f ms | %s\n", second, ticks, samples_total,
                            slowest * 1000, fodder::game::describe(*game).substr(0, 40).c_str());
                second = int(now);
                ticks = samples_total = 0;
                slowest = 0;
            }
            std::this_thread::sleep_until(start + std::chrono::duration_cast<clock::duration>(
                                                       std::chrono::duration<double>((display + 1) / 120.0)));
        }
        return 0;
    }
    if (command == "play" && argc >= 5) {
        // The launcher's game module, headless: frames with scripted mouse
        // input (picture pixels; l, r or b buttons), the last picture saved.
        int first_phase = 0;
        for (int a = 5; a < argc; ++a)
            if (std::string(argv[a]).rfind("phase=", 0) == 0) {
                const auto v = std::string(argv[a]).substr(6);
                first_phase = v == "new" ? -1 : std::stoi(v); // new: the whole game with its screens
            }
        auto game = fodder::game::start_game(argv[2], first_phase);
        for (int a = 5; a < argc; ++a) // wide=W,H: widescreen in a window of that shape
            if (int w = 0, h = 0; std::sscanf(argv[a], "wide=%d,%d", &w, &h) == 2) {
                game->set_option(0, 1);
                game->set_canvas(w, h);
            } else if (std::string(argv[a]) == "smooth") {
                game->set_option(1, 1); // smooth motion
            }
        struct Hold { int from, to, x, y; bool left, right, modifier; };
        std::vector<Hold> holds;
        for (int a = 5; a < argc; ++a) {
            int f0 = 0, f1 = 0, x = 0, y = 0;
            char b = 'l';
            // l left, r right, b both, m a modifier key (Control), M the left button with it
            if (std::sscanf(argv[a], "%d-%d:%d,%d,%c", &f0, &f1, &x, &y, &b) >= 4)
                holds.push_back({f0, f1, x, y, b == 'l' || b == 'b' || b == 'M', b == 'r' || b == 'b', b == 'm' || b == 'M'});
        }
        const int frames = std::stoi(argv[3]);
        std::vector<float> audio(2 * 882);
        double peak = 0;
        app::GameInput input;
        input.pointer = true;
        input.pointer_x = 200;
        input.pointer_y = 100;
        // display=N: as the shell does, N pictures a second between the
        // frames (with the time since the last frame), the sound's hash at
        // the end.
        int display = 0, win_at = -1; // win=F: the phase won at frame F
        for (int a = 5; a < argc; ++a) {
            std::sscanf(argv[a], "display=%d", &display);
            std::sscanf(argv[a], "win=%d", &win_at);
        }
        std::vector<std::uint8_t> heard;
        for (int f = 0; f < frames && game->stopped().empty(); ++f) {
            if (display > 0) {
                for (int k = 0; k * 50 < display; ++k) {
                    game->set_frame_fraction(double(k * 50 % display) / display);
                    (void)game->picture();
                }
            }
            if (f == win_at) fodder::game::win_phase(*game);
            input.mouse_button = input.mouse_right = input.modifier = false;
            for (const auto& h : holds) {
                if (f < h.from || f > h.to) continue;
                input.pointer_x = h.x;
                input.pointer_y = h.y;
                input.mouse_button = h.left;
                input.mouse_right = h.right;
                input.modifier = h.modifier;
            }
            game->tick(input);
            game->audio(audio, 44100);
            for (const float v : audio) peak = std::max(peak, double(std::abs(v)));
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(audio.data());
            heard.insert(heard.end(), bytes, bytes + audio.size() * sizeof(float));
        }
        platform::save_png(game->picture(), argv[4]);
        std::printf("%s\n", fodder::game::describe(*game).c_str());
        {
            const auto state = game->write_state();
            std::printf("state %s sound %s\n", platform::sha256(state).substr(0, 16).c_str(),
                        platform::sha256(heard).substr(0, 16).c_str());
        }
        std::printf("%d frames, audio peak %.3f%s%s\n", frames, peak, game->stopped().empty() ? "" : ", stopped: ",
                    game->stopped().c_str());
        return 0;
    }
    if (command == "music" && argc == 8) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const auto read = [&](std::string name) {
            int disk = 1;
            if (name.size() > 2 && name[1] == ':') {
                disk = name[0] - '0';
                name = name.substr(2);
            }
            return content.read(name, disk);
        };
        sound::Rjp1 player(read(argv[3]), read(argv[4]), content.read("fodderc", 1));
        amiga::PaulaChannels paula;
        paula.set_memory(player.samples());
        const std::string which = argv[5];
        if (!which.empty() && which[0] == 'e') player.start_effect(2, std::stoi(which.substr(1)));
        else player.start_song(std::stoi(which));
        constexpr int rate = 44100;
        const int frames = int(std::stod(argv[6]) * 50);
        std::vector<float> audio(std::size_t(frames) * (rate / 50) * 2);
        for (int f = 0; f < frames; ++f) {
            player.frame(paula);
            paula.render(std::span<float>(audio).subspan(std::size_t(f) * (rate / 50) * 2, (rate / 50) * 2), rate);
        }
        // 16-bit stereo WAV.
        std::vector<std::uint8_t> out;
        const auto put = [&](std::uint32_t v, int bytes) { for (int b = 0; b < bytes; ++b) out.push_back(std::uint8_t(v >> (8 * b))); };
        const auto data_bytes = std::uint32_t(audio.size() * 2);
        for (char c : std::string("RIFF")) out.push_back(std::uint8_t(c));
        put(36 + data_bytes, 4);
        for (char c : std::string("WAVEfmt ")) out.push_back(std::uint8_t(c));
        put(16, 4); put(1, 2); put(2, 2); put(rate, 4); put(rate * 4, 4); put(4, 2); put(16, 2);
        for (char c : std::string("data")) out.push_back(std::uint8_t(c));
        put(data_bytes, 4);
        for (const float v : audio) put(std::uint32_t(std::uint16_t(std::int16_t(std::max(-1.0f, std::min(1.0f, v)) * 32000))), 2);
        platform::write_file(argv[7], out);
        std::printf("%d frames rendered\n", frames);
        return 0;
    }
    if (command == "sndreplay" && argc == 8) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const auto program = content.read("fodderc", 1);
        // Files as "disk:name" (disk 1 when no disk is given).
        const auto read = [&](std::string name) {
            int disk = 1;
            if (name.size() > 2 && name[1] == ':') {
                disk = name[0] - '0';
                name = name.substr(2);
            }
            return content.read(name, disk);
        };
        const auto sng_file = read(argv[3]), ins_file = read(argv[4]);
        const auto trace = platform::read_file(argv[5]);
        const std::uint32_t sng_at = std::uint32_t(std::stoul(argv[6], nullptr, 16)), ins_at = std::uint32_t(std::stoul(argv[7], nullptr, 16)) + 4;
        constexpr std::size_t blob = 0x30 + 4 * 0xb0 + 2;
        std::size_t at = 0;
        while (trace[at] != '\n') ++at;
        ++at;
        int frames = 0, bad = 0;
        sound::Rjp1 reference(sng_file, ins_file, program);
        const auto sec = reference.sections();
        while (at + 2 + 2 * blob <= trace.size()) {
            const auto before = std::span<const std::uint8_t>(trace).subspan(at + 2, blob);
            const auto after = std::span<const std::uint8_t>(trace).subspan(at + 2 + blob, blob);
            at += 2 + 2 * blob;
            const auto B = [](std::span<const std::uint8_t> b, std::size_t o) { return int(b[o]); };
            const auto W = [](std::span<const std::uint8_t> b, std::size_t o) { return int(std::int16_t((b[o] << 8) | b[o + 1])); };
            const auto L = [](std::span<const std::uint8_t> b, std::size_t o) {
                return std::uint32_t((std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o + 1]) << 16) | (std::uint32_t(b[o + 2]) << 8) | b[o + 3]);
            };
            const auto load = [&](std::span<const std::uint8_t> b, sound::Rjp1& p) {
                p.mode() = W(b, 0x22);
                p.half_step() = B(b, 0x25) != 0;
                p.music_on() = B(b, 0x26) != 0;
                p.effects_on() = B(b, 0x27) != 0;
                p.busy() = B(b, 0x2c);
                p.on() = W(b, 0x30 + 4 * 0xb0) != 0;
                for (int n = 0; n < 4; ++n) p.set_channel_volume_raw(n, B(b, 0x28 + std::size_t(n)));
                for (int n = 0; n < 4; ++n) {
                    const std::size_t r = 0x30 + std::size_t(n) * 0xb0;
                    auto& c = p.channels()[std::size_t(n)];
                    const auto pointer = [&](std::size_t o, std::uint32_t base) { const auto v = L(b, r + o); return v == 0 ? -1 : int(v - base); };
                    const auto voice = [&](std::size_t d, sound::Rjp1::Voice& v) {
                        v.sample = pointer(d + 0x04, ins_at);
                        v.period_table = pointer(d + 0x08, ins_at);
                        v.volume_table = pointer(d + 0x0c, ins_at);
                        v.envelope = pointer(d + 0x10, sng_at + std::uint32_t(sec.envelopes));
                        v.attack_length = W(b, r + d + 0x14);
                        v.loop_start = W(b, r + d + 0x16) & 0xffff;
                        v.loop_length = W(b, r + d + 0x18) & 0xffff;
                        v.period_loop = W(b, r + d + 0x1a) & 0xffff;
                        v.period_end = int(L(b, r + d + 0x1c));
                        v.period_position = int(L(b, r + d + 0x20));
                        v.volume_loop = W(b, r + d + 0x24) & 0xffff;
                        v.volume_end = int(L(b, r + d + 0x26));
                        v.volume_position = int(L(b, r + d + 0x2a));
                        v.volume_scale = W(b, r + d + 0x30);
                        v.volume = W(b, r + d + 0x32);
                        v.base_period = W(b, r + d + 0x34);
                        v.period = W(b, r + d + 0x36);
                        v.key_on = B(b, r + d + 0x38) & 1;
                        v.loop_next = B(b, r + d + 0x39) & 1;
                        v.envelope_stage = std::int8_t(B(b, r + d + 0x3a));
                        v.envelope_delta = std::int8_t(B(b, r + d + 0x3b));
                        v.envelope_count = B(b, r + d + 0x3c);
                        v.envelope_length = B(b, r + d + 0x3d);
                        v.envelope_target = B(b, r + d + 0x3e);
                        v.envelope_value = B(b, r + d + 0x3f);
                        v.slide_count = B(b, r + d + 0x40);
                        v.slide_step = std::int32_t(L(b, r + d + 0x42));
                        v.slide = std::int32_t(L(b, r + d + 0x46));
                        // (From $52 on the effect's fields lie $54 after the music's.)
                        const std::size_t t = d ? 0x54 : 0;
                        v.pattern = pointer(t + 0x52, sng_at);
                        v.note_length = B(b, r + t + 0x56);
                        v.note_count = B(b, r + t + 0x57);
                        v.rows = B(b, r + t + 0x58);
                        v.row_count = B(b, r + t + 0x59);
                        v.instrument = B(b, r + t + 0x5a);
                    };
                    voice(0, c.music);
                    voice(0x58, c.effect);
                    c.effect.pattern = pointer(0xa6, sng_at);
                    c.music.pattern = pointer(0x52, sng_at);
                    c.sequence = pointer(0x4e, sng_at);
                    c.active = B(b, r + 0x5b) != 0;
                }
            };
            sound::Rjp1 player(sng_file, ins_file, program), expected(sng_file, ins_file, program);
            load(before, player);
            load(after, expected);
            amiga::PaulaChannels paula;
            player.frame(paula);
            const auto report = [&](int n, const char* name, long a, long e) {
                if (a != e && ++bad <= 40) std::printf("frame %d channel %d %s: native %ld original %ld\n", frames, n, name, a, e);
            };
            report(-1, "busy", player.busy(), expected.busy());
            report(-1, "half step", player.half_step(), expected.half_step());
            for (int n = 0; n < 4; ++n) {
                const auto& c = player.channels()[std::size_t(n)];
                const auto& x = expected.channels()[std::size_t(n)];
                report(n, "sequence", c.sequence, x.sequence);
                report(n, "active", c.active, x.active);
                for (int part = 0; part < 2; ++part) {
                    const auto& v = part ? c.effect : c.music;
                    const auto& e = part ? x.effect : x.music;
                    if (part == 0 && !x.active && !c.active && e.pattern < 0) continue;
                    const std::pair<const char*, std::pair<long, long>> fields[] = {
                        {"instrument", {v.instrument, e.instrument}}, {"sample", {v.sample, e.sample}},
                        {"volume", {v.volume, e.volume}}, {"period", {v.period, e.period}}, {"base period", {v.base_period, e.base_period}},
                        {"key", {v.key_on, e.key_on}}, {"loop", {v.loop_next, e.loop_next}}, {"stage", {v.envelope_stage, e.envelope_stage}},
                        {"env delta", {v.envelope_delta, e.envelope_delta}}, {"env count", {v.envelope_count, e.envelope_count}},
                        {"env target", {v.envelope_target, e.envelope_target}}, {"env value", {v.envelope_value, e.envelope_value}},
                        {"slide", {v.slide, e.slide}}, {"slide count", {v.slide_count, e.slide_count}},
                        {"pattern", {v.pattern, e.pattern}}, {"note length", {v.note_length, e.note_length}},
                        {"note count", {v.note_count, e.note_count}}, {"rows", {v.rows, e.rows}}, {"row count", {v.row_count, e.row_count}},
                        {"period pos", {v.period_position, e.period_position}}, {"volume pos", {v.volume_position, e.volume_position}},
                        {"volume scale", {v.volume_scale, e.volume_scale}}};
                    for (const auto& [name, val] : fields) report(n + part * 10, name, val.first, val.second);
                }
            }
            ++frames;
        }
        std::printf("%d frames, %d fields differ\n", frames, bad);
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
    if (command == "front" && argc == 6) {
        // Research: a front-end screen after some frames, without input:
        // intro, hill, briefing (a new game's first phase).
        using namespace fodder;
        const data::Content content(argv[2]);
        const auto program = content.read("fodderc", 1);
        const game::MissionTables tables(program);
        game::Campaign campaign;
        campaign.start(tables);
        const game::PanelArt panel(program, amiga::read_ilbm(content.read("pstuff.lbm", 1)));
        game::FrontEnd front(content, program, panel);
        campaign.call_up(2);
        const std::string which = argv[3];
        if (which == "intro") front.start_intro();
        else if (which == "hill") {
            for (int k = 0; k < 40; ++k) campaign.heroes.push_back({k, k % 16}); // research: 40 graves
            front.start_hill(campaign);
        }
        else if (which == "debrief") { campaign.roster[0].on_map = campaign.roster[1].on_map = true; front.start_debrief(campaign); }
        else if (which == "ending") front.start_ending();
        else if (which == "diskrequest") {
            for (const auto& a : front.draw_disk_request()) std::printf("area %d %d %d %d\n", a[0], a[1], a[2], a[3]);
            platform::write_file(std::string(argv[5]) + ".idx", front.indices());
            return 0;
        }
        else if (which == "saves") {
            std::vector<std::string> names;
            for (int k = 1; k <= 7; ++k) names.push_back("MISSION " + std::to_string(k) + " PHASE 1");
            front.start_save_list(names);
        }
        else front.start_briefing(campaign, tables);
        app::GameInput input;
        input.pointer = true;
        input.pointer_x = 150;
        input.pointer_y = 100;
        const int frames = std::stoi(argv[4]);
        for (int f = 0; f < frames && front.active(); ++f) front.tick(input);
        platform::save_png(front.picture(), argv[5]);
        std::printf("%s after %d frames: %s\n", which.c_str(), frames, front.active() ? "running" : "done");
        return 0;
    }
    if (command == "graphics" && argc == 7) {
        // Research: frame groups first..last of a map's landscape, one row
        // each (up to 16 frames, 48 x 48 cells), for naming object kinds.
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const int disk = data::map_disk(number);
        const int first = std::stoi(argv[4], nullptr, 0), last = std::stoi(argv[5], nullptr, 0);
        const auto map = data::read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
        const auto land = map.base_set.substr(0, 3);
        const game::ObjectSheets sheets{amiga::read_ilbm(content.read(land + "army.lbm", disk)),
                                        amiga::read_ilbm(content.read(land + "copt.lbm", disk))};
        const auto palette = data::read_palette(content.read(land + "base.pal", disk));
        const game::FrameTables frames(content.read("fodderc", 1));
        constexpr int cell = 48;
        const int rows = last - first + 1;
        std::vector<std::uint8_t> pixels(std::size_t(16 * cell * rows * cell), 0);
        for (int g = first; g <= last; ++g) {
            for (int f = 0; f < 16; ++f) {
                const auto info = frames.frame(g, f);
                if (!info) break;
                game::MissionObjects objects;
                auto& o = objects.slots[0];
                o.used = true;
                o.graphic = g;
                o.frame = f;
                o.x = (f * cell + 8) << 16;
                o.y = ((g - first) * cell + 40) << 16;
                game::ObjectTarget target{pixels, 16 * cell, rows * cell, 0, 0, 0};
                game::draw_objects(target, objects, {0}, frames, sheets);
            }
        }
        platform::Image image(16 * cell, rows * cell);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            image.pixels[i] = pixels[i] ? data::to_color(palette[pixels[i] & 15]) : platform::Color{40, 40, 60, 255};
        }
        platform::save_png(image, argv[6]);
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
