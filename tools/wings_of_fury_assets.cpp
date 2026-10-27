// Wings of Fury research tool: imports the disk image into a content folder
// and exports the decoded data (.analysis/wings_of_fury_amiga/formats.md).
//
//   wings_of_fury_assets import <image.adf> <content folder>
//   wings_of_fury_assets export <content folder> <output folder>
//   wings_of_fury_assets view <content folder> <map> <state file> <out.png>
//   wings_of_fury_assets dash <content folder> <map> <grab> <out.idx>
//
// The export writes PNG pictures, the samples as 8-bit WAV files (at 8,000 Hz:
// the playback periods are not read yet), each library's shape headers as a
// text file, and a summary on stdout (palettes, maps, texts, high scores).

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/data/preview.hpp"
#include "wings_of_fury_amiga/game/dashboard.hpp"
#include "wings_of_fury_amiga/game/player_view.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include "wings_of_fury_amiga/import/disk_import.hpp"
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(std::uint8_t(v));
    out.push_back(std::uint8_t(v >> 8));
}

std::vector<std::uint8_t> wav(const wings::data::Sample& sample, std::uint32_t rate) {
    std::vector<std::uint8_t> out = {'R', 'I', 'F', 'F'};
    put32(out, std::uint32_t(36 + sample.data.size()));
    for (char c : std::string("WAVEfmt ")) out.push_back(std::uint8_t(c));
    put32(out, 16);
    put16(out, 1);
    put16(out, 1);
    put32(out, rate);
    put32(out, rate);
    put16(out, 1);
    put16(out, 8);
    for (char c : std::string("data")) out.push_back(std::uint8_t(c));
    put32(out, std::uint32_t(sample.data.size()));
    for (const auto v : sample.data) out.push_back(std::uint8_t(v + 128)); // WAV 8-bit is unsigned
    return out;
}

std::string shape_table(const wings::data::ShapeLibrary& library) {
    std::string text = "name  w   h  off_x off_y sheet_x sheet_y clear set planes\n";
    char line[160];
    for (const auto& shape : library.shapes) {
        std::snprintf(line, sizeof line, "%-4s %3d %3d %6d %5d %7d %7d   %02X  %02X ", shape.name.c_str(), shape.width(),
                      shape.height, shape.offset_x, shape.offset_y, shape.sheet_x, shape.sheet_y,
                      shape.cleared_planes, shape.set_planes);
        text += line;
        for (const auto& plane : shape.planes) {
            std::snprintf(line, sizeof line, " %02X", plane.bitplanes);
            text += line;
        }
        text += '\n';
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    using namespace wings::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = wings::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (const auto& file : result.files) {
                platform::write_file(out / file.name, file.bytes);
            }
            std::printf("%s: %zu files\n", result.release.c_str(), result.files.size());
            return 0;
        }
        if (command == "export" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (const auto& picture : assets.pictures) {
                platform::save_png(picture_image(picture.picture), out / (picture.name + ".png"));
                std::printf("picture %-12s %3d x %3d, %d planes, %zu colours\n", picture.name.c_str(),
                            picture.picture.width, picture.picture.height, picture.picture.planes,
                            picture.picture.palette.size());
            }
            for (const auto& library : assets.libraries) {
                const auto& palette = find_palette(assets, preview_palette_for(library.name));
                platform::save_png(library_sheet(library.library, palette), out / (library.name + ".png"));
                platform::write_text(out / (library.name + ".txt"), shape_table(library.library));
                std::printf("library %-15s %3zu shapes\n", library.name.c_str(), library.library.shapes.size());
            }
            for (const auto& palette : assets.palettes) {
                platform::save_png(palette_image(palette.colours), out / ("palette-" + palette.name + ".png"));
                std::printf("palette %-13s", palette.name.c_str());
                for (const auto colour : palette.colours) std::printf(" %03X", colour);
                std::printf("\n");
            }
            for (const auto& sample : assets.samples) {
                platform::write_file(out / (sample.name + ".wav"), wav(sample, 8000));
                platform::save_png(sample_image(sample), out / (sample.name + ".png"));
                std::printf("sample %-14s %5zu bytes\n", sample.name.c_str(), sample.data.size());
            }
            const auto& day = find_palette(assets, "wingspalette");
            for (const auto& map : assets.maps) {
                platform::save_png(map_image(assets, map, day), out / (map.name + ".png"));
                std::printf("map %s carrier x %d, %zu columns\n", map.name.c_str(), map.carrier_x(), map.words.size());
            }
            std::printf("ranks:");
            for (const auto& rank : assets.rank_names) std::printf(" [%s]", rank.c_str());
            std::printf("\nstory:\n");
            for (const auto& line : assets.story) std::printf("  |%s\n", line.c_str());
            std::printf("high scores:");
            for (const auto& score : assets.high_scores) std::printf(" %u/%d/%s", score.score, score.rank, score.name.c_str());
            std::printf("\n");
            return 0;
        }
        if (command == "view" && argc == 6) {
            // view <content> <map> <state file> <out.png>: the play view's
            // backdrop (game/scenery) as out.idx (320 x 162 colour indices)
            // and out.png (day colours above row 151, the ocean palette
            // below). The state file (harness/viewref.py) holds "key values"
            // lines: camera, wave, vscroll, us_flag, crew a b, enemy_flag
            // (before the frame), ship first last bob (in search order),
            // island_flags 1 0 ...
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            wings::game::BackdropState state;
            wings::game::PlayerPicture player;
            bool has_player = false;
            struct {
                int upset{}, pitch{};
                bool facing_left{};
            } eighth;
            wings::game::CarrierDeckPicture deck;
            bool has_deck = false;
            struct {
                bool engine{};
                int counter{}, attitude{};
                bool left{};
            } propeller;
            // "library name" -> the shape (or null for "none").
            const auto shape_named = [&](std::istringstream& in) -> const wings::data::Shape* {
                std::string library, name;
                in >> library;
                if (library == "none") {
                    in >> name; // the mirrored flag's placeholder
                    return nullptr;
                }
                in >> name;
                for (const auto& l : assets.libraries) {
                    if (l.name == library) return l.library.find(name);
                }
                return nullptr;
            };
            const auto text = platform::read_file(platform::utf8_path(argv[4]));
            std::istringstream lines(std::string(text.begin(), text.end()));
            for (std::string line; std::getline(lines, line);) {
                std::istringstream in(line);
                std::string key;
                in >> key;
                if (key == "camera") in >> state.camera;
                else if (key == "wave") in >> state.wave_frame;
                else if (key == "vscroll") in >> state.vertical_scroll;
                else if (key == "us_flag") in >> state.us_flag_step;
                else if (key == "crew") in >> state.crew_pose_a >> state.crew_pose_b;
                else if (key == "enemy_flag") in >> state.enemy_flag_counter;
                else if (key == "ship") {
                    wings::game::BackdropState::Ship ship;
                    in >> ship.first >> ship.last >> ship.bob;
                    state.ships.push_back(ship);
                } else if (key == "island_flags") {
                    for (int up; in >> up;) state.island_flag_up.push_back(up != 0);
                } else if (key == "scale") {
                    int scale = 8;
                    in >> scale;
                    state.eighth_scale = player.eighth_scale = scale == 1;
                } else if (key == "ground") {
                    in >> state.ground_line;
                    player.ground_line = state.ground_line;
                } else if (key == "deck") {
                    has_deck = true;
                    in >> deck.carrier_x >> deck.afloat >> deck.spare_planes >> deck.elevator_state >> deck.lift;
                } else if (key.starts_with("player_") || key == "carrier_deck") {
                    has_player = true;
                    if (key == "player_altitude") in >> player.altitude;
                    else if (key == "player_elevator") {
                        int elevator = 0;
                        in >> elevator;
                        player.hidden = elevator == 1;
                        player.clip_at_deck = elevator != 0;
                    } else if (key == "carrier_deck") in >> player.carrier_deck >> player.carrier_bob;
                    else if (key == "player_plane") {
                        player.plane = shape_named(in);
                        in >> player.plane_mirrored;
                    } else if (key == "player_deck_part") {
                        player.deck_part = shape_named(in);
                        in >> player.deck_part_mirrored >> player.deck_part_shown;
                    } else if (key == "player_gear") {
                        player.gear = shape_named(in);
                        in >> player.gear_mirrored >> player.gear_shown >> player.gear_offset;
                    } else if (key == "player_upset") in >> player.upset;
                    else if (key == "player_caught") in >> player.caught >> player.wire_screen_x >> player.facing_left;
                    else if (key == "player_eighth") in >> eighth.upset >> eighth.pitch >> eighth.facing_left;
                    else if (key == "player_propeller") {
                        int engine = 0, counter = 0, attitude = 0, left = 0;
                        in >> engine >> counter >> attitude >> left;
                        propeller = {engine != 0, counter, attitude, left != 0};
                    }
                }
            }
            player.vertical_scroll = state.vertical_scroll;
            if (player.eighth_scale) {
                player.plane = wings::game::eighth_scale_plane(assets, eighth.upset, eighth.pitch, eighth.facing_left);
            }
            deck.camera = state.camera;
            deck.eighth_scale = state.eighth_scale;
            deck.ground_line = state.ground_line;
            deck.vertical_scroll = state.vertical_scroll;
            deck.deck = player.carrier_deck;
            deck.bob = player.carrier_bob;
            player.gun_flash = wings::game::gun_flash_shape(assets, propeller.engine, player.upset, propeller.counter,
                                                            propeller.attitude, propeller.left);
            for (const auto& map : assets.maps) {
                if (map.name != argv[3]) continue;
                auto canvas = wings::game::draw_sky_and_scenery(assets, map, state);
                if (has_deck) wings::game::draw_carrier_deck(canvas, assets, deck);
                if (has_player) wings::game::draw_player(canvas, player);
                wings::game::draw_sea_and_sand(canvas, assets, map, state);
                const auto out = platform::utf8_path(argv[5]);
                auto idx = out;
                platform::write_file(idx.replace_extension(".idx"), canvas.pixels);
                const auto& day = find_palette(assets, "wingspalette");
                const auto& sea = find_palette(assets, "ocean.palette");
                platform::Image image(canvas.width, canvas.height);
                for (int y = 0; y < canvas.height; ++y) {
                    for (int x = 0; x < canvas.width; ++x) {
                        image.put(x, y, to_color((y < 151 ? day : sea)[canvas.at(x, y)]));
                    }
                }
                platform::save_png(image, out);
                return 0;
            }
            std::fprintf(stderr, "wings_of_fury_assets: no map %s\n", argv[3]);
            return 1;
        }
        if (command == "dash" && argc == 6) {
            // dash <content> <map> <grab> <out.idx>: the dashboard
            // (game/dashboard) from a harness/dashgrab.py grab: GRAB.a4 (the
            // small-data area that drew it) and GRAB.cache (that buffer's
            // dashboard cache: the warning lights and needles it shows).
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const std::string grab = argv[4];
            const auto a4 = platform::read_file(platform::utf8_path(grab + ".a4"));
            int cache = 0;
            if (auto* f = std::fopen((grab + ".cache").c_str(), "r")) {
                if (std::fscanf(f, "%d", &cache) != 1) cache = 0;
                std::fclose(f);
            }
            const auto u8 = [&](int off) { return int(a4[std::size_t(0x8000 - off)]); };
            const auto s16 = [&](int off) { return int(std::int16_t(u8(off) << 8 | u8(off - 1))); };
            const auto s32 = [&](int off) { return long(std::int32_t(std::uint32_t(s16(off)) << 16 | std::uint16_t(s16(off - 2)))); };
            wings::game::DashboardInput in;
            in.mode = s16(0x5F7A);
            in.oil = s16(0x5F74);
            in.fuel = s16(0x5F78);
            in.engine = s16(0x3216) != 0;
            in.weapon = s16(0x5C5A);
            in.load = u8(0x5C91);
            in.spare_planes = std::int8_t(u8(0x5CA2));
            in.score = s32(0x5CB2);
            in.kills = u8(0x5C7F);
            in.camera = s16(0x41A2);
            in.altitude = s16(0x419E);
            in.facing = s16(0x5F72);
            in.eighth_scale = s16(0x60C8) == 1;
            // The ships $104A52 searches: active enemy slots, then the carrier.
            const std::pair<int, int> slots[] = {{0x5B9E, 0x5C84}, {0x5B80, 0x5C87}, {0x5B62, 0x5C83}, {0x5B44, 0x5C86}};
            for (const auto& [ship, flag] : slots) {
                if (u8(flag)) in.ships.push_back({s16(ship), s16(ship - 2), s16(ship - 0x1C), s16(ship - 0x12)});
            }
            in.ships.push_back({s16(0x5B26), s16(0x5B26 - 2), s16(0x5B26 - 0x1C), s16(0x5B26 - 0x12)});
            for (int i = 0; i < 4; ++i) {
                const auto f = [&](int field) { return s16(0x5DD4 - i * 0x34 - field); }; // (-$5DD4: the enemy planes)
                if (f(0) != 0) in.enemies.push_back({f(0x2E), f(0x30), f(0x26), f(0x20), f(2)});
            }
            wings::game::DashboardGauges gauges;
            const int c = 0x30CE - cache; // the buffer's cache: light, needle (oil), light, needle (fuel)
            gauges.oil_lit = s16(c) == 0x10;
            gauges.oil_needle = s16(c - 2);
            gauges.fuel_lit = s16(c - 4) == 0x10;
            gauges.fuel_needle = s16(c - 6);
            gauges.load_tens = s16(0x30D4);
            gauges.load_units = s16(0x30D2);
            gauges.spares_roll = s16(0x30D0);
            for (const auto& map : assets.maps) {
                if (map.name != argv[3]) continue;
                in.map = &map;
                const auto tables = wings::game::read_dashboard_tables(assets.program);
                const auto canvas = wings::game::draw_dashboard(assets, tables, gauges, in);
                platform::write_file(platform::utf8_path(argv[5]), canvas.pixels);
                return 0;
            }
            std::fprintf(stderr, "wings_of_fury_assets: no map %s\n", argv[3]);
            return 1;
        }
        std::fprintf(stderr, "usage: wings_of_fury_assets import <adf> <content> | export <content> <out> | "
                             "view <content> <map> <state file> <out.png> | dash <content> <map> <grab> <out.idx>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "wings_of_fury_assets: %s\n", error.what());
        return 1;
    }
}
