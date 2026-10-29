// Rock 'n Roll research tool: runs the native rules against recordings of
// the original (.analysis/rock_n_roll_amiga/harness/play.py record).
//
//   rock_n_roll_replay ball <content> <level> <recording> [<recording> …] [all]
//
// The recording holds the original's state at the top of each update of
// the in-level loop and the mouse counters the harness set for it. The
// native ball starts from the first line's state and runs one update per
// line; after each update its state is compared with the next line. The
// first differing update is reported (all of them with "all"). A recording
// that ends with the ball lost is followed to that update; a further
// recording then continues it as the prompt's "continue" does (the native
// state carries over and must equal that recording's first row).

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/ball.hpp"
#include "rock_n_roll_amiga/game/module.hpp"
#include "rock_n_roll_amiga/sound/game_sound.hpp"
#include "platform/host.hpp"
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

// Research pokes the recorder made after a row ("# poke ADDR HEX after row
// N"); the native state gets the same change.
struct Poke {
    std::int64_t after_row;
    std::uint32_t address;
    std::uint64_t value;
};

// Rows of a recording; `loop_left` tells whether it ends with the level
// loop left (the ball lost after the last row's update).
// Sound effect calls the recorder logged, per update ("# sound N in update U").
using SoundLog = std::map<std::int64_t, std::vector<int>>;

std::vector<Row> read_recording(const std::string& path, bool& loop_left, std::vector<Poke>* pokes = nullptr,
                                SoundLog* sounds = nullptr) {
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
        if (line[0] == '#') { // "# loop left before update n", "# poke …"
            loop_left = loop_left || line.find("loop left") != std::string::npos;
            if (sounds && line.rfind("# sound ", 0) == 0) {
                std::istringstream w(line.substr(8));
                std::string number, in_word, update_word;
                std::int64_t update = 0;
                w >> number >> in_word >> update_word >> update;
                (*sounds)[update].push_back(int(std::stoul(number, nullptr, 16)));
            }
            if (pokes && line.rfind("# poke ", 0) == 0) {
                std::istringstream w(line.substr(7));
                std::string address, value, after, row_word;
                std::int64_t row = 0;
                w >> address >> value >> after >> row_word >> row;
                pokes->push_back({row, std::uint32_t(std::stoul(address, nullptr, 16)), std::stoull(value, nullptr, 16)});
            }
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
                                name == "fallcount" || name == "lost" || name == "mappieces" ||
                                name == "energy2" || name == "kindtime" || name == "kindoff" || name == "iceproof" ||
                                name == "protect" || name == "warpcount" || name == "rewardcount" ||
                                name == "shopwait" || name == "carried" || name == "countdown" || name == "ticks" ||
                                name == "drained" || name == "latch" || name == "frozen" || name == "done" ||
                                name == "pending" || name == "crumblecount" || name.rfind("ac", 0) == 0 || name == "blkcount" || name == "blkdir" ||
                                name == "blkvalue" || name == "warpflag" ||
                                name == "snddelay" || name == "sndquiet" || name == "bridgelatch"
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
    if (r.count("repairs")) {
        p.repair_sets = int(r.at("repairs"));
        p.bombs = int(r.at("eyes"));
        p.energy = int(r.at("energy2"));
        p.shown_energy = p.energy; // the panel's bar has caught up at a recording's start (not logged)
    }
    if (r.count("hourglass")) {
        p.rescues = int(r.at("hourglass"));
        p.continues = int(r.at("continues"));
    }
    p.score = int(r.at("money"));   // the recorder's "money" column is $719BA (the score)
    p.money = int(r.at("energy"));  // and "energy" is $719C2 (the money)
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
    if (r.count("repairs")) {
        check("repairs", p.repair_sets);
        check("eyes", p.bombs);
        check("energy2", p.energy);
    }
    if (r.count("hourglass")) {
        check("hourglass", p.rescues);
        check("continues", p.continues);
    }
    check("money", p.score);
    check("energy", p.money);
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
    if (r.count("repairs")) {
        b.extra_speed = int(r.at("extra"));
        b.kind_updates = int(r.at("kindtime"));
        b.kind_running = !(r.at("kindoff") & 1);
        b.ice_proof = r.at("iceproof") != 0;
        b.protective = r.at("protect") != 0;
        b.shop_wait = int(r.at("shopwait"));
        b.shop_spot = game::Fixed(r.at("shopspot"));
        b.carried = r.at("carried") != 0;
        b.max_speed_x = b.max_speed_y = game::Fixed(r.at("maxvx"));
        b.min_speed_x = b.min_speed_y = game::Fixed(r.at("minvx"));
    }
    if (r.count("hund")) {
        b.clock.hundredths = int(r.at("hund"));
        b.clock.seconds = int(r.at("secs"));
        b.clock.countdown = int(r.at("countdown"));
        b.clock.ticks = std::uint8_t(r.at("ticks"));
        b.clock.drained = r.at("drained") & 1;
        b.clock.switch_latch = int(r.at("latch"));
        b.clock.mouse_frozen = r.at("frozen") != 0;
        b.level_done = r.at("done") & 1;
    }
    if (r.count("remembered")) {
        b.remembered_cell = long(r.at("remembered")) - 0x73BA8; // the map starts at $73BA8
        b.pending_value = std::uint8_t(r.at("pending"));
    }
    if (r.count("save_cx")) {
        b.continue_camera_x = game::Fixed(r.at("save_cx"));
        b.continue_camera_y = game::Fixed(r.at("save_cy"));
        b.continue_view_x = game::Fixed(r.at("save_vx"));
        b.continue_view_y = game::Fixed(r.at("save_vy"));
    }
    if (!rolling(r)) b.mode = game::BallMode::falling;
    return b;
}

int compare(const game::Ball& b, const Row& r, bool all, std::int64_t update) {
    int differing_saved = 0;
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
    if (r.count("repairs")) {
        const Field more[] = {{"extra", b.extra_speed}, {"kindtime", b.kind_updates}, {"kindoff", !b.kind_running},
                              {"iceproof", b.ice_proof}, {"protect", b.protective}, {"shopwait", b.shop_wait},
                              {"shopspot", b.shop_spot}, {"carried", b.carried}, {"maxvx", b.max_speed_x},
                              {"minvx", b.min_speed_x}};
        for (const auto& f : more) {
            if (r.at(f.name) != f.native) {
                std::printf("update %lld: %s native %llx original %llx\n", (long long)update, f.name,
                            (unsigned long long)(std::uint32_t)f.native, (unsigned long long)(std::uint32_t)r.at(f.name));
                ++differing_saved;
            }
        }
    }
    if (r.count("hund")) {
        const auto& c = b.clock;
        const Field clock[] = {{"hund", c.hundredths}, {"secs", c.seconds}, {"countdown", c.countdown},
                               {"ticks", c.ticks}, {"drained", c.drained}, {"latch", c.switch_latch},
                               {"frozen", c.mouse_frozen}, {"done", b.level_done}};
        if (r.count("remembered")) {
            const Field crumble[] = {{"remembered", b.remembered_cell + 0x73BA8}, {"pending", b.pending_value}};
            for (const auto& f : crumble) {
                if (r.at(f.name) != f.native) {
                    std::printf("update %lld: %s native %llx original %llx\n", (long long)update, f.name,
                                (unsigned long long)(std::uint32_t)f.native, (unsigned long long)(std::uint32_t)r.at(f.name));
                    ++differing_saved;
                }
            }
        }
        for (const auto& f : clock) {
            if (r.at(f.name) != f.native) {
                std::printf("update %lld: %s native %llx original %llx\n", (long long)update, f.name,
                            (unsigned long long)(std::uint32_t)f.native, (unsigned long long)(std::uint32_t)r.at(f.name));
                ++differing_saved;
            }
        }
    }
    if (r.count("save_cx")) {
        const Field saved[] = {{"save_cx", b.continue_camera_x}, {"save_cy", b.continue_camera_y},
                               {"save_vx", b.continue_view_x}, {"save_vy", b.continue_view_y}};
        for (const auto& f : saved) {
            if (r.at(f.name) != f.native) {
                std::printf("update %lld: %s native %llx original %llx\n", (long long)update, f.name,
                            (unsigned long long)(std::uint32_t)f.native, (unsigned long long)(std::uint32_t)r.at(f.name));
                ++differing_saved;
            }
        }
    }
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
    differing += differing_saved;
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

// Research: 16-bit stereo WAV.
void save_wav16(const std::filesystem::path& file, std::span<const float> stereo, int rate) {
    std::vector<std::uint8_t> wav;
    const auto put = [&](std::uint32_t v, int n) { for (int k = 0; k < n; ++k) wav.push_back(std::uint8_t(v >> (8 * k))); };
    const auto bytes = std::uint32_t(stereo.size() * 2);
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'}); put(36 + bytes, 4);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(16, 4); put(1, 2); put(2, 2); put(std::uint32_t(rate), 4); put(std::uint32_t(rate) * 4, 4); put(4, 2); put(16, 2);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'}); put(bytes, 4);
    for (const float f : stereo) put(std::uint32_t(std::uint16_t(std::int16_t(std::clamp(f, -1.0f, 1.0f) * 32767))), 2);
    platform::write_file(file, wav);
}

// The block's slide state, when the recording logs it.
int compare_block(const game::LevelMap& map, const Row& r, std::int64_t update) {
    if (!r.count("blkcount")) return 0;
    const auto& k = map.block;
    int differing = 0;
    const auto check = [&](const char* name, std::int64_t native) {
        if (r.at(name) == native) return;
        std::printf("update %lld: %s native %llx original %llx\n", (long long)update, name, (unsigned long long)native,
                    (unsigned long long)r.at(name));
        ++differing;
    };
    check("blkcount", k.count);
    check("blkdir", k.direction);
    check("blkvx", k.last_vx);
    check("blkvy", k.last_vy);
    if (k.direction != 0 || r.at("blkdir") != 0) { // the cells only mean something during a slide
        check("blkfrom", k.from + 0x73BA8);
        check("blkto", k.to + 0x73BA8);
        check("blkvalue", k.value);
    }
    check("warpflag", k.counts_for_warp ? 1 : 0);
    check("warpcount", map.warp_count);
    return differing;
}

// The tile animations' cursor and counters, when the recording logs them.
int compare_animation(const game::TileAnimator& a, const Row& r, std::int64_t update) {
    if (!r.count("acur")) return 0;
    int differing = 0;
    const auto check = [&](const std::string& name, std::int64_t native) {
        if (r.at(name) == native) return;
        std::printf("update %lld: %s native %llx original %llx\n", (long long)update, name.c_str(),
                    (unsigned long long)native, (unsigned long long)r.at(name));
        ++differing;
    };
    check("acur", a.cursor());
    for (int i = 0; i < game::TileAnimator::slots; ++i) check("ac" + std::to_string(i), a.slot_counter(i));
    return differing;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "ball" && argc >= 5) {
            std::vector<std::string> recordings;
            bool all = false;
            for (int i = 4; i < argc; ++i) {
                if (std::string(argv[i]) == "all") all = true;
                else recordings.push_back(argv[i]);
            }
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            const auto& level = assets.levels.at(std::size_t(std::stoi(argv[3]) - 1));
            const auto& world = assets.world(level.world);
            auto map = game::LevelMap::from(level);
            map.animation = game::TileAnimator(assets.program); // counters from 0 (recordings do not log them)
            map.animation.set_world(level.world);
            // The original's map at the first recording's first row, when
            // saved beside it (pickups and earlier runs change the map).
            if (const auto saved = std::filesystem::path(recordings.front()).concat(".map"); std::filesystem::exists(saved)) {
                const auto cells = platform::read_file(saved);
                if (cells.size() == map.cells.size()) map.cells = cells;
            }
            game::Ball ball;
            game::Pocket pocket;
            int bad_updates = 0;
            std::size_t checked = 0;
            for (std::size_t k = 0; k < recordings.size() && bad_updates == 0; ++k) {
                bool loop_left = false;
                std::vector<Poke> pokes;
                SoundLog sound_log;
                const auto rows = read_recording(recordings[k], loop_left, &pokes, &sound_log);
                const bool has_sounds = !sound_log.empty();
                if (rows.size() < 2) throw std::runtime_error("A recording has fewer than two updates.");
                if (k == 0) {
                    ball = ball_from(rows.front());
                    pocket = pocket_from(rows.front());
                    if (rows.front().count("acur")) { // the animations as the original had them
                        map.animation.set_cursor(int(rows.front().at("acur")));
                        for (int i = 0; i < game::TileAnimator::slots; ++i)
                            map.animation.set_slot_counter(i, std::uint16_t(rows.front().at("ac" + std::to_string(i))));
                        map.animation.show_all(map.tiles);
                    }
                    if (rows.front().count("snddelay")) { // the sound state as the original had it
                        const auto& r = rows.front();
                        ball.sound_delay = int(r.at("snddelay"));
                        ball.energy_sound_quiet = r.at("sndquiet") & 1;
                        ball.bridge_latch = r.at("bridgelatch") & 1;
                        ball.bridge_end_sound = int(r.at("bridgesnd"));
                        ball.bridge_end_other = int(r.at("bridgesnd2"));
                    }
                    if (rows.front().count("blkcount")) { // the block as the original had it
                        const auto& r = rows.front();
                        map.warp_count = std::uint16_t(r.at("warpcount")); // earlier runs may have counted it down
                        map.reward_count = std::uint16_t(r.at("rewardcount"));
                        auto& k = map.block;
                        k.count = int(r.at("blkcount"));
                        k.direction = int(r.at("blkdir"));
                        k.last_vx = game::Fixed(r.at("blkvx"));
                        k.last_vy = game::Fixed(r.at("blkvy"));
                        k.from = long(r.at("blkfrom")) - 0x73BA8;
                        k.to = long(r.at("blkto")) - 0x73BA8;
                        k.value = std::uint8_t(r.at("blkvalue"));
                        k.sinking = r.at("blksink") == 0x80;
                        k.counts_for_warp = r.at("warpflag") & 1;
                    }
                } else {
                    // A later recording starts at the loop top right after the
                    // prompt was answered with "continue": the native ball goes
                    // through the same and must equal its first row.
                    if (ball.mode != game::BallMode::lost) throw std::runtime_error("Recordings chain only after a lost ball.");
                    game::continue_after_fall(ball);
                    // The recorder's first row comes after at least one update
                    // of the resumed loop, with the mouse as the earlier
                    // recording left it (an idle update: nothing moves).
                    const auto idle = std::uint16_t(rows.front().at("prevjoy"));
                    game::update_ball(ball, pocket, map, world, {std::uint8_t(idle & 0xFF), std::uint8_t(idle >> 8)});
                    if (compare(ball, rows.front(), all, -1) + compare_pocket(pocket, rows.front(), -1) > 0) {
                        std::printf("(the state right after continue differs)\n");
                        ++bad_updates;
                        break;
                    }
                }
                for (std::size_t n = 0; n + 1 < rows.size(); ++n) {
                    for (const auto& poke : pokes) {
                        if (poke.after_row != rows[n].at("update")) continue;
                        if (poke.address == 0x719C2) pocket.money = int(poke.value); // the money
                        else throw std::runtime_error("The recording pokes an address the replay does not know.");
                    }
                    const auto joy = std::uint16_t(rows[n].at("joy"));
                    game::update_ball(ball, pocket, map, world, {std::uint8_t(joy & 0xFF), std::uint8_t(joy >> 8)});
                    ++checked;
                    int sound_differences = 0;
                    if (has_sounds) { // the sound effects asked for in this update, in order
                        const auto it = sound_log.find(rows[n].at("update"));
                        const std::vector<int> want = it == sound_log.end() ? std::vector<int>{} : it->second;
                        if (want != ball.sounds) {
                            std::printf("update %lld: sounds native", (long long)rows[n].at("update"));
                            for (const int x : ball.sounds) std::printf(" %x", x);
                            std::printf(", original");
                            for (const int x : want) std::printf(" %x", x);
                            std::printf("\n");
                            ++sound_differences;
                        }
                    }
                    ball.sounds.clear();
                    if (sound_differences +
                            compare(ball, rows[n + 1], all, rows[n].at("update")) +
                            compare_pocket(pocket, rows[n + 1], rows[n].at("update")) +
                            compare_animation(map.animation, rows[n + 1], rows[n].at("update")) +
                            compare_block(map, rows[n + 1], rows[n].at("update")) > 0) {
                        ++bad_updates;
                        if (!all) break;
                    }
                    if (ball.mode == game::BallMode::lost || ball.mode == game::BallMode::warped) break; // the level loop ends here
                }
                const auto left_loop = [&] { return ball.mode == game::BallMode::lost || ball.mode == game::BallMode::warped; };
                if (loop_left && bad_updates == 0 && !left_loop()) {
                    const auto joy = std::uint16_t(rows.back().at("joy"));
                    game::update_ball(ball, pocket, map, world, {std::uint8_t(joy & 0xFF), std::uint8_t(joy >> 8)});
                    ++checked;
                    if (!left_loop()) {
                        std::printf("update %lld: the original left the level loop, the native ball is not lost\n",
                                    (long long)rows.back().at("update"));
                        ++bad_updates;
                    }
                }
            }
            // The map as the original left it at the last recording's last row.
            if (const auto end = std::filesystem::path(recordings.back()).concat(".endmap");
                bad_updates == 0 && std::filesystem::exists(end)) {
                const auto cells = platform::read_file(end);
                int differ = 0;
                for (std::size_t i = 0; i < cells.size() && i < map.cells.size(); ++i)
                    if (cells[i] != map.cells[i] && differ++ < 5)
                        std::printf("map cell %zu,%zu: native %02x original %02x\n", i % std::size_t(map.columns),
                                    i / std::size_t(map.columns), map.cells[i], cells[i]);
                if (differ) {
                    std::printf("%d map cells differ at the end\n", differ);
                    ++bad_updates;
                }
            }
            std::printf("%zu updates checked, %d differ; %d unhandled cell visits%s\n", checked, bad_updates,
                        ball.unhandled_cells,
                        ball.mode == game::BallMode::lost ? "; ball lost" : ball.mode == game::BallMode::warped ? "; secret warp" : "");
            if (ball.unhandled_cells) {
                std::printf("unhandled kinds:");
                for (int v = 0; v < 256; ++v)
                    if (ball.unhandled_kinds[std::size_t(v)]) std::printf(" %02X", v);
                std::printf("\n");
            }
            return bad_updates ? 1 : 0;
        }
        if (command == "probe" && argc >= 7) {
            // probe <content> <level> <col> <row> <updates> [a-b:dx,dy …]:
            // places the ball as play.py's place() does (top-left cell at
            // col,row, view centre, no speed), runs the native rules with
            // the mouse moves and prints the state per update.
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            const auto& level = assets.levels.at(std::size_t(std::stoi(argv[3]) - 1));
            const auto& world = assets.world(level.world);
            auto map = game::LevelMap::from(level);
            map.animation = game::TileAnimator(assets.program); // counters from 0 (recordings do not log them)
            map.animation.set_world(level.world);
            game::Ball ball = game::Ball::at_start(map);
            ball.camera_x = game::Fixed(std::stoi(argv[4]) - 7) << 16;
            ball.camera_y = game::Fixed(std::stoi(argv[5]) - 3) << 16;
            ball.view_x = 0x90000;
            ball.view_y = 0x30000;
            game::Pocket pocket;
            const int updates = std::stoi(argv[6]);
            struct Step { int a, b, dx, dy; };
            std::vector<Step> steps;
            for (int i = 7; i < argc; ++i) {
                Step st{};
                if (std::sscanf(argv[i], "%d-%d:%d,%d", &st.a, &st.b, &st.dx, &st.dy) == 4) steps.push_back(st);
            }
            game::MouseCounters mouse{};
            ball.last_mouse = mouse;
            for (int n = 0; n < updates; ++n) {
                for (const auto& st : steps) {
                    if (n < st.a || n > st.b) continue;
                    mouse.x = std::uint8_t(mouse.x + st.dx);
                    mouse.y = std::uint8_t(mouse.y + st.dy);
                }
                game::update_ball(ball, pocket, map, world, mouse);
                // The ball's top-left in map pixels (the cell under it is
                // two columns left of camera + view).
                const auto px = [](game::Fixed c, game::Fixed v) { return double(c + v) / 4096.0; };
                std::printf("%3d x %7.2f y %7.2f vx %6x vy %6x contact %d,%d centre %d mode %d\n", n,
                            px(ball.camera_x, ball.view_x) - 32, px(ball.camera_y, ball.view_y),
                            unsigned(ball.speed_x) & 0xFFFFFF, unsigned(ball.speed_y) & 0xFFFFFF,
                            ball.contact_rows, ball.contact_columns, ball.centre_cell, int(ball.mode));
            }
            return 0;
        }
        if (command == "musiccheck" && argc >= 5) {
            // musiccheck <content> <world 1-7 | title> <dump>: the player
            // against the original's state per frame (harness/snd_dump.py).
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            sound::GameSound sound(assets);
            const std::string where = argv[3];
            if (where == "title") sound.load_title(false);
            else sound.load_world(std::stoi(where));
            sound.start_music(0);
            std::ifstream in(argv[4]);
            std::string line;
            int frames = 0, bad = 0;
            std::int64_t pending = -1; // a song request the original had at this frame's start
            while (std::getline(in, line) && bad < 8) {
                if (pending >= 0 && pending < 0x8000) sound.start_music(int(pending));
                sound.tick();
                std::istringstream words(line);
                std::string w;
                words >> w;
                std::map<std::string, std::int64_t> original;
                while (words >> w) {
                    const auto eq = w.find('=');
                    original[w.substr(0, eq)] = std::int64_t(std::stoull(w.substr(eq + 1), nullptr, 16));
                }
                pending = original.count("request") ? original["request"] : -1;
                const auto snapshot = sound.player().snapshot(0x5740E);
                std::map<std::string, std::int64_t> native(snapshot.begin(), snapshot.end());
                for (const auto& [name, value] : snapshot) {
                    const auto it = original.find(name);
                    if (it == original.end()) continue;
                    // An idle channel's macro and key are left from its last note
                    // and read only once a new note sets them.
                    if (name.size() > 2 && name[0] == 'c' && (name.substr(2) == "macro" || name.substr(2) == "key")) {
                        const auto state = name.substr(0, 2) + "state";
                        if (original[state] == 0 && native[state] == 0) continue;
                    }
                    const std::int64_t mine = value & 0xFFFFFFFF;
                    if (it->second != mine) {
                        std::printf("frame %d: %s native %llx original %llx\n", frames, name.c_str(), (unsigned long long)mine,
                                    (unsigned long long)it->second);
                        ++bad;
                    }
                }
                ++frames;
            }
            std::printf("%d frames checked, %d differences\n", frames, bad);
            return bad ? 1 : 0;
        }
        if (command == "music" && argc >= 6) {
            // music <content> <world 1-7 | title> <seconds> <out.wav> [song]:
            // the game's music as a level or the title starts it, rendered.
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            sound::GameSound sound(assets);
            const std::string where = argv[3];
            if (where == "title") sound.load_title(false);
            else sound.load_world(std::stoi(where));
            sound.start_music(argc > 6 ? std::stoi(argv[6]) : 0);
            constexpr int rate = 48000;
            const int frames = int(std::stod(argv[4]) * 50);
            std::vector<float> all;
            std::vector<float> frame(std::size_t(rate / 50) * 2);
            for (int n = 0; n < frames; ++n) {
                sound.tick();
                std::fill(frame.begin(), frame.end(), 0.0f);
                sound.render(frame, rate);
                all.insert(all.end(), frame.begin(), frame.end());
            }
            save_wav16(platform::utf8_path(argv[5]), all, rate);
            float peak = 0;
            for (const float f : all) peak = std::max(peak, std::abs(f));
            std::printf("%d frames, peak %.3f\n", frames, double(peak));
            return 0;
        }
        if (command == "play" && argc >= 5) {
            // play <content> <updates> <out.png> [a-b:dx,dy[,l|r|f] …] [a-b:key=N …]
            // [@N=shot.png …] [@N:place=cx,cy,vx,vy,sx,sy …]: runs the game
            // module headless with mouse moves, buttons and held keys
            // (GameInput::c64_keys index N) per update, saving pictures after
            // update N and placing the ball before update N (hex 16.16 values,
            // as a recording's first row: camera, view, speed).
            auto game = game::start(platform::utf8_path(argv[2]));
            const int updates = std::stoi(argv[3]);
            struct Step { int a, b, dx, dy; bool l, r, f; int key; };
            std::vector<Step> steps;
            std::vector<std::pair<int, std::string>> shots;
            std::string wav_out; // wav=FILE: the module's audio, 48 kHz stereo
            struct Place { int at; std::uint32_t v[6]; };
            std::vector<Place> places;
            for (int i = 5; i < argc; ++i) {
                Step st{};
                st.key = -1;
                char buttons[4] = {};
                char shot[512] = {};
                if (std::string(argv[i]).rfind("wav=", 0) == 0) {
                    wav_out = std::string(argv[i]).substr(4);
                    continue;
                }
                Place pl{};
                if (std::sscanf(argv[i], "@%d:place=%x,%x,%x,%x,%x,%x", &pl.at, &pl.v[0], &pl.v[1], &pl.v[2], &pl.v[3], &pl.v[4],
                                &pl.v[5]) == 7) {
                    places.push_back(pl);
                    continue;
                }
                if (std::sscanf(argv[i], "@%d=%511s", &st.a, shot) == 2) {
                    shots.emplace_back(st.a, shot);
                    continue;
                }
                if (std::sscanf(argv[i], "%d-%d:key=%d", &st.a, &st.b, &st.key) == 3) {
                    steps.push_back(st);
                    continue;
                }
                st.key = -1;
                if (std::sscanf(argv[i], "%d-%d:%d,%d,%3s", &st.a, &st.b, &st.dx, &st.dy, buttons) < 4) continue;
                st.l = std::string(buttons).find('l') != std::string::npos;
                st.r = std::string(buttons).find('r') != std::string::npos;
                st.f = std::string(buttons).find('f') != std::string::npos; // fire (also the space bar)
                steps.push_back(st);
            }
            app::GameInput input;
            std::vector<float> audio_all;
            for (int n = 0; n < updates; ++n) {
                input.mouse_button = input.mouse_right = input.fire = false;
                input.mouse_dx = input.mouse_dy = 0;
                input.c64_keys = {};
                for (const auto& st : steps) {
                    if (n < st.a || n > st.b) continue;
                    if (st.key >= 0) {
                        input.c64_keys[std::size_t(st.key)] = true;
                        continue;
                    }
                    input.mouse_dx += float(st.dx);
                    input.mouse_dy += float(st.dy);
                    input.mouse_button = input.mouse_button || st.l;
                    input.mouse_right = input.mouse_right || st.r;
                    input.fire = input.fire || st.f;
                }
                for (const auto& pl : places)
                    if (pl.at == n && !game::place_ball(*game, std::int32_t(pl.v[0]), std::int32_t(pl.v[1]), std::int32_t(pl.v[2]),
                                                        std::int32_t(pl.v[3]), std::int32_t(pl.v[4]), std::int32_t(pl.v[5])))
                        std::fprintf(stderr, "update %d: no level to place the ball in\n", n);
                game->tick(input);
                if (!wav_out.empty()) {
                    std::vector<float> frame(960 * 2);
                    game->audio(frame, 48000);
                    audio_all.insert(audio_all.end(), frame.begin(), frame.end());
                }
                for (const auto& [at, file] : shots)
                    if (at == n) platform::save_png(game->picture(), platform::utf8_path(file));
            }
            platform::save_png(game->picture(), platform::utf8_path(argv[4]));
            if (!wav_out.empty()) {
                save_wav16(platform::utf8_path(wav_out), audio_all, 48000);
                // Loudness per second, to see where sound plays.
                for (std::size_t s0 = 0; s0 < audio_all.size(); s0 += 96000) {
                    float peak = 0;
                    for (std::size_t i = s0; i < std::min(audio_all.size(), s0 + 96000); ++i) peak = std::max(peak, std::abs(audio_all[i]));
                    std::printf("%.2f ", double(peak));
                }
                std::printf("\n");
            }
            return 0;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: rock_n_roll_replay ball <content> <level> <recording> [<recording> ...] [all]\n");
    return 2;
}
