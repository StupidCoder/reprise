// Rock 'n Roll research tool: runs the native rules against recordings of
// the original (.analysis/rock_n_roll_amiga/harness/play.py record).
//
//   rock_n_roll_replay ball <content> <level> <recording> [all]
//
// The recording holds the original's state at the top of each update of
// the in-level loop and the mouse counters the harness set for it. The
// native ball starts from the first line's state and runs one update per
// line; after each update its state is compared with the next line. The
// first differing update is reported (all of them with "all"). A recording
// that ends with the ball lost is followed to that update.

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/ball.hpp"
#include <cstdio>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace rocknroll;
using Row = std::map<std::string, std::int64_t>;

// Rows of a recording; `loop_left` tells whether it ends with the level
// loop left (the ball lost after the last row's update).
std::vector<Row> read_recording(const std::string& path, bool& loop_left) {
    loop_left = false;
    std::ifstream in(path);
    std::string line;
    std::vector<std::string> names;
    std::vector<Row> rows;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        if (line.empty()) continue;
        if (line.rfind("# update", 0) == 0) {
            std::string w;
            words >> w >> w;
            names.clear();
            while (words >> w) names.push_back(w);
            continue;
        }
        if (line[0] == '#') { // "# loop left before update n"
            loop_left = loop_left || line.find("loop left") != std::string::npos;
            continue;
        }
        Row row;
        std::string w;
        words >> w;
        row["update"] = std::stoll(w);
        for (const auto& name : names) {
            if (!(words >> w)) break;
            const auto v = std::stoull(w, nullptr, 16);
            // 32-bit fields are signed; narrower ones are kept as they are.
            row[name] = name == "grip" || name == "count" || name == "prevjoy" || name == "centre" || name == "joy" ||
                                name.rfind("f4", 0) == 0 || name == "collide" || name == "high" ||
                                name == "fallcount" || name == "lost" || name == "mappieces"
                            ? std::int64_t(v)
                            : std::int64_t(std::int32_t(std::uint32_t(v)));
        }
        rows.push_back(row);
    }
    return rows;
}

// The recording's rolling state routine ($6C274 and its variants).
bool rolling(const Row& r) {
    const auto s = r.at("state");
    return s == 0x6C274 || s == 0x6C258 || s == 0x6C266;
}

// The pocket's fields in a recording (older recordings have none).
const char* const key_fields[4] = {"key1", "key2", "key3", "key4"};
const char* const find_fields[4] = {"find1", "find2", "find3", "find4"};

game::Pocket pocket_from(const Row& r) {
    game::Pocket p;
    if (!r.count("money")) return p;
    for (int i = 0; i < 4; ++i) {
        p.keys[std::size_t(i)] = int(r.at(key_fields[i]));
        p.finds[std::size_t(i)] = int(r.at(find_fields[i]));
    }
    p.money = int(r.at("money"));
    p.energy = int(r.at("energy"));
    return p;
}

int compare_pocket(const game::Pocket& p, const Row& r, std::int64_t update) {
    if (!r.count("money")) return 0;
    int differing = 0;
    const auto check = [&](const char* name, int native) {
        if (r.at(name) != native) {
            std::printf("update %lld: %s native %d original %lld\n", (long long)update, name, native,
                        (long long)r.at(name));
            ++differing;
        }
    };
    for (int i = 0; i < 4; ++i) {
        check(key_fields[i], p.keys[std::size_t(i)]);
        check(find_fields[i], p.finds[std::size_t(i)]);
    }
    check("money", p.money);
    check("energy", p.energy);
    return differing;
}

game::Ball ball_from(const Row& r) {
    game::Ball b;
    b.camera_x = game::Fixed(r.at("camx"));
    b.camera_y = game::Fixed(r.at("camy"));
    b.view_x = game::Fixed(r.at("ballx"));
    b.view_y = game::Fixed(r.at("bally"));
    b.speed_x = game::Fixed(r.at("vx"));
    b.speed_y = game::Fixed(r.at("vy"));
    b.camera_limit_x = game::Fixed(r.at("cammax_x"));
    b.camera_limit_y = game::Fixed(r.at("cammax_y"));
    b.friction = game::Fixed(r.at("friction"));
    b.grip = r.at("grip") & 1;
    b.updates_without_read = int(r.at("count"));
    b.last_mouse = {std::uint8_t(r.at("prevjoy") & 0xFF), std::uint8_t(r.at("prevjoy") >> 8)};
    b.steered = r.at("f405") & 1;
    b.hold_x = r.at("f402") & 1;
    b.hold_y = r.at("f400") & 1;
    b.slow = r.at("slow") != 0;
    b.ice_push_x = game::Fixed(r.at("ice_x"));
    b.ice_push_y = game::Fixed(r.at("ice_y"));
    b.collisions = r.at("collide") & 1;
    b.high_cell = r.at("high") & 1;
    b.contact_rows = int(r.at("push_x"));
    b.contact_columns = int(r.at("push_y"));
    b.centre_cell = int(r.at("centre"));
    b.roll_x = game::Fixed(r.at("phase_x"));
    b.roll_y = game::Fixed(r.at("phase_y"));
    if (r.count("fallcount")) b.fall_updates = int(r.at("fallcount"));
    if (r.count("ignore")) b.mouse_ignored = r.at("ignore") & 1;
    if (!rolling(r)) b.mode = game::BallMode::falling;
    return b;
}

int compare(const game::Ball& b, const Row& r, bool all, std::int64_t update) {
    struct Field {
        const char* name;
        std::int64_t native;
    };
    const Field fields[] = {
        {"camx", b.camera_x}, {"camy", b.camera_y}, {"ballx", b.view_x}, {"bally", b.view_y},
        {"vx", b.speed_x}, {"vy", b.speed_y}, {"grip", b.grip}, {"count", b.updates_without_read},
        {"push_x", b.contact_rows}, {"push_y", b.contact_columns}, {"centre", b.centre_cell},
        {"phase_x", b.roll_x}, {"phase_y", b.roll_y}, {"slow", b.slow}, {"high", b.high_cell},
        {"f405", b.steered}, {"friction", b.friction}, {"collide", b.collisions},
    };
    int differing = 0;
    for (const auto& f : fields) {
        const auto want = r.at(f.name);
        if (want != f.native) {
            if (differing == 0 || all) {
                std::printf("update %lld: %s native %llx original %llx\n", (long long)update, f.name,
                            (unsigned long long)(std::uint32_t)f.native, (unsigned long long)(std::uint32_t)want);
            }
            ++differing;
        }
    }
    if (r.count("fallcount") && r.at("fallcount") != b.fall_updates) {
        std::printf("update %lld: fall count native %d original %lld\n", (long long)update, b.fall_updates,
                    (long long)r.at("fallcount"));
        ++differing;
    }
    if (r.count("lost") && bool(r.at("lost") & 1) != (b.mode == game::BallMode::lost)) {
        std::printf("update %lld: lost native %d original %lld\n", (long long)update, b.mode == game::BallMode::lost,
                    (long long)r.at("lost"));
        ++differing;
    }
    const bool rolls = b.mode == game::BallMode::rolling;
    if (b.mode != game::BallMode::lost && rolls != rolling(r)) {
        std::printf("update %lld: native %s, original state routine %llx\n", (long long)update,
                    rolls ? "rolling" : "falling", (unsigned long long)r.at("state"));
        ++differing;
    }
    return differing;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "ball" && argc >= 5) {
            const bool all = argc > 5 && std::string(argv[5]) == "all";
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            const auto& level = assets.levels.at(std::size_t(std::stoi(argv[3]) - 1));
            const auto& world = assets.world(level.world);
            auto map = game::LevelMap::from(level);
            // The original's map at the recording's first row, when saved
            // beside it (pickups and earlier runs change the map).
            if (const auto saved = std::filesystem::path(argv[4]).concat(".map"); std::filesystem::exists(saved)) {
                const auto cells = platform::read_file(saved);
                if (cells.size() == map.cells.size()) map.cells = cells;
            }
            bool loop_left = false;
            const auto rows = read_recording(argv[4], loop_left);
            if (rows.size() < 2) throw std::runtime_error("The recording has fewer than two updates.");
            auto ball = ball_from(rows.front());
            auto pocket = pocket_from(rows.front());
            int bad_updates = 0;
            std::size_t checked = 0;
            for (std::size_t n = 0; n + 1 < rows.size(); ++n) {
                const auto joy = std::uint16_t(rows[n].at("joy"));
                game::update_ball(ball, pocket, map, world, {std::uint8_t(joy & 0xFF), std::uint8_t(joy >> 8)});
                ++checked;
                if (compare(ball, rows[n + 1], all, rows[n].at("update")) +
                        compare_pocket(pocket, rows[n + 1], rows[n].at("update")) > 0) {
                    ++bad_updates;
                    if (!all) break;
                }
                if (ball.mode == game::BallMode::lost) break; // the level loop ends here
            }
            if (loop_left && bad_updates == 0 && ball.mode != game::BallMode::lost) {
                const auto joy = std::uint16_t(rows.back().at("joy"));
                game::update_ball(ball, pocket, map, world, {std::uint8_t(joy & 0xFF), std::uint8_t(joy >> 8)});
                ++checked;
                if (ball.mode != game::BallMode::lost) {
                    std::printf("update %lld: the original left the level loop, the native ball is not lost\n",
                                (long long)rows.back().at("update"));
                    ++bad_updates;
                }
            }
            std::printf("%zu updates checked, %d differ; %d unhandled cell visits%s\n", checked, bad_updates,
                        ball.unhandled_cells, ball.mode == game::BallMode::lost ? "; ball lost" : "");
            if (ball.unhandled_cells) {
                std::printf("unhandled kinds:");
                for (int v = 0; v < 256; ++v)
                    if (ball.unhandled_kinds[std::size_t(v)]) std::printf(" %02X", v);
                std::printf("\n");
            }
            return bad_updates ? 1 : 0;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: rock_n_roll_replay ball <content> <level> <recording> [all]\n");
    return 2;
}
