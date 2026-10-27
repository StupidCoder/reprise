// Marble Madness research tool: replays a recording of the original
// (.analysis/marble_madness_amiga/harness/mmref.py) on the rebuilt race and
// compares player 1's marble after every world update.
//
//   marble_madness_replay lockstep <content> <run.bin> [course] [first] [count]
//
// With <run>-covers.txt beside the run (mmref.py --covers), the marble's
// visibility flag and wall covers are compared too.
//
// The record of update k holds the state before that update and the push
// injected for it; the race runs update k with that push and is compared
// with record k + 1. Record offsets are the original's marble record
// (game/marble.hpp names them).
#include "marble_madness_amiga/data/assets.hpp"
#include "marble_madness_amiga/data/preview.hpp"
#include "marble_madness_amiga/game/front_end.hpp"
#include "marble_madness_amiga/game/probe.hpp"
#include "marble_madness_amiga/game/race.hpp"
#include "marble_madness_amiga/game/view.hpp"
#include "marble_madness_amiga/sound/sound_board.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <chrono>
#include <cstdio>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace marble_madness;

constexpr std::size_t record_size = 8 + 0xF4 * 2 + 0x120 + 2 + 2 + 8;
constexpr std::size_t players_at = 8;
constexpr std::size_t forces_at = record_size - 8;

std::int32_t be32(const std::uint8_t* p) {
    return std::int32_t(std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]);
}
std::int16_t be16(const std::uint8_t* p) { return std::int16_t(p[0] << 8 | p[1]); }

struct Field {
    const char* name;
    std::size_t offset;
    int size; // 1, 2, 4 (signed)
    std::int32_t game::Marble::*m32;
    std::int16_t game::Marble::*m16;
    std::int8_t game::Marble::*m8s;
    std::uint8_t game::Marble::*m8u;
};

std::int32_t recorded(const std::uint8_t* p, const Field& f) {
    if (f.size == 4) return be32(p + f.offset);
    if (f.size == 2) return be16(p + f.offset);
    return f.m8s ? std::int8_t(p[f.offset]) : p[f.offset];
}
std::int32_t native(const game::Marble& m, const Field& f) {
    if (f.m32) return m.*f.m32;
    if (f.m16) return m.*f.m16;
    if (f.m8s) return m.*f.m8s;
    return m.*f.m8u;
}

const std::vector<Field>& fields() {
    using M = game::Marble;
    static const std::vector<Field> list = {
        {"vx", 0x00, 4, &M::vx, nullptr, nullptr, nullptr},
        {"vy", 0x04, 4, &M::vy, nullptr, nullptr, nullptr},
        {"vz", 0x08, 4, &M::vz, nullptr, nullptr, nullptr},
        {"x", 0x0C, 4, &M::x, nullptr, nullptr, nullptr},
        {"y", 0x10, 4, &M::y, nullptr, nullptr, nullptr},
        {"z", 0x14, 4, &M::z, nullptr, nullptr, nullptr},
        {"status", 0x18, 1, nullptr, nullptr, nullptr, &M::status},
        {"mode", 0x1A, 1, nullptr, nullptr, nullptr, &M::mode},
        {"feature", 0x1B, 1, nullptr, nullptr, &M::feature, nullptr},
        {"saved_feature", 0x1D, 1, nullptr, nullptr, &M::saved_feature, nullptr},
        {"fall_start_z", 0x2A, 4, &M::fall_start_z, nullptr, nullptr, nullptr},
        {"edge_cell_x", 0x2E, 2, nullptr, &M::edge_cell_x, nullptr, nullptr},
        {"edge_cell_y", 0x30, 2, nullptr, &M::edge_cell_y, nullptr, nullptr},
        {"cell_x", 0x32, 2, nullptr, &M::cell_x, nullptr, nullptr},
        {"cell_y", 0x34, 2, nullptr, &M::cell_y, nullptr, nullptr},
        {"air", 0x36, 1, nullptr, nullptr, nullptr, &M::air},
        {"edge_sides", 0x37, 1, nullptr, nullptr, nullptr, &M::edge_sides},
        {"bounce", 0x60, 1, nullptr, nullptr, nullptr, &M::bounce},
        {"delay", 0x61, 1, nullptr, nullptr, &M::delay, nullptr},
        {"region_code", 0x62, 1, nullptr, nullptr, &M::region_code, nullptr},
        {"time", 0x74, 2, nullptr, &M::time, nullptr, nullptr},
        {"tenths", 0x76, 1, nullptr, nullptr, &M::tenths, nullptr},
        {"timer_frames", 0x78, 1, nullptr, nullptr, &M::timer_frames, nullptr},
        {"score", 0xCE, 4, &M::score, nullptr, nullptr, nullptr},
        {"bonus_wobble", 0x72, 1, nullptr, nullptr, &M::bonus_wobble, nullptr},
        {"bonus_wobble_step", 0x73, 1, nullptr, nullptr, &M::bonus_wobble_step, nullptr},
        {"bonus_on", 0xEA, 1, nullptr, nullptr, nullptr, &M::bonus_on},
        {"roll_sound", 0xD6, 2, nullptr, &M::roll_sound, nullptr, nullptr},
    };
    return list;
}

// Copies the recorded marble into the race's marble (to start a run).
void adopt(game::Marble& m, const std::uint8_t* p) {
    for (const auto& f : fields()) {
        const auto v = recorded(p, f);
        if (f.m32) m.*f.m32 = v;
        else if (f.m16) m.*f.m16 = std::int16_t(v);
        else if (f.m8s) m.*f.m8s = std::int8_t(v);
        else m.*f.m8u = std::uint8_t(v);
    }
    for (int i = 0; i < 3; ++i) {
        m.screen[std::size_t(i)] = be32(p + 0x1E + std::size_t(i) * 4);
    }
}

// The first record of the race: a recording may begin a tick before the
// course start, while player 1's clock is still stopped (+$78 = -1).
std::size_t race_start(const std::vector<std::uint8_t>& run) {
    const std::size_t frames = run.size() / record_size;
    for (std::size_t k = 0; k < frames; ++k) {
        if (run[k * record_size + players_at + 0x78] != 0xFF) {
            // Some runs have a tick more with the clock set but not yet
            // counting (the timer's frame count steps every update once it
            // runs): start at the last of those.
            while (k + 1 < frames && run[k * record_size + players_at + 0x78] == run[(k + 1) * record_size + players_at + 0x78]) {
                ++k;
            }
            return k;
        }
    }
    return 0;
}

// The original's world-update count at a record (0 in runs made before
// the recorder kept its low word).
int counted_frames(const std::vector<std::uint8_t>& run, std::size_t record) {
    return be16(run.data() + record * record_size + players_at + 0xF4 * 2 + 0x120 + 2);
}

// Whether the run has the count (the recorder keeps it since Beginner).
bool has_counts(const std::vector<std::uint8_t>& run) {
    for (std::size_t k = 0; k < run.size() / record_size; ++k) {
        if (counted_frames(run, k) != 0) return true;
    }
    return false;
}

// Before update k: the count the original had then (it jumps at a race's
// start, and the scroll looks every eighth update).
void follow_count(game::Race& race, const std::vector<std::uint8_t>& run, std::size_t k, bool counts) {
    if (counts) race.set_frame(counted_frames(run, k));
}

// Whether record k repeats record k - 1 exactly: the breakpoint stopped
// twice for one update (an interrupt taken at its instruction; mmref.py
// drops such stops since Ultimate's recordings). No update lies between.
bool repeated(const std::vector<std::uint8_t>& run, std::size_t k) {
    if (k == 0 || (k + 1) * record_size > run.size()) return false;
    return std::equal(run.begin() + std::ptrdiff_t(k * record_size + players_at),
                      run.begin() + std::ptrdiff_t((k + 1) * record_size),
                      run.begin() + std::ptrdiff_t((k - 1) * record_size + players_at));
}

// The covers recorded during each update (the last stop's line for player
// 1): "flag cell,x,y ...".
std::map<std::size_t, std::string> read_covers(const std::string& run) {
    std::map<std::size_t, std::string> out;
    std::ifstream in(run.substr(0, run.size() - 4) + "-covers.txt");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::size_t update = 0;
        int player = 0;
        if (!(words >> update >> player) || player != 0) {
            continue;
        }
        std::string rest;
        std::getline(words, rest);
        while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
        while (!rest.empty() && rest.back() == ' ') rest.pop_back();
        out[update] = rest;
    }
    return out;
}

// The creature log (mmref.py --creatures): per update, the two steelies
// ($86 bytes each), four slinkies ($6A) and 25 regions ($56).
// (Since the display list was added: then the list, $20 bytes, and its 31
// slots of 14 bytes.)
constexpr std::size_t creatures_base = 0x86 * 2 + 0x6A * 4 + 0x56 * 25;
constexpr std::size_t creatures_with_display = creatures_base + 0x20 + 14 * 31;

// The ranges of a creature log by name (from <run>-creatures-layout.txt
// when the recorder wrote one, else the older fixed layouts): offset in an
// update's block, and the block's size.
struct CreatureLayout {
    std::map<std::string, std::size_t> at;
    std::size_t size{};
    bool has(const std::string& name) const { return at.contains(name); }
};

CreatureLayout creature_layout(const std::string& run, std::size_t log_size, std::size_t frames) {
    CreatureLayout l;
    try {
        const auto text = platform::read_file(platform::utf8_path(run.substr(0, run.size() - 4) + "-creatures-layout.txt"));
        std::istringstream in(std::string(text.begin(), text.end()));
        std::string name, address;
        std::size_t size = 0;
        while (in >> name >> address >> size) {
            // (Aerial's pistons were logged as "tiles" at first.)
            if (name == "tiles") name = "pistons";
            if (name == "tile_groups") name = "piston_groups";
            l.at[name] = l.size;
            l.size += size;
        }
        return l;
    } catch (const std::exception&) {
    }
    l.at = {{"steelies", 0}, {"slinkies", 0x86 * 2}, {"regions", 0x86 * 2 + 0x6A * 4}};
    l.size = creatures_base;
    if (log_size != 0 && log_size % creatures_with_display == 0 && log_size / creatures_with_display == frames) {
        l.at["display"] = creatures_base;
        l.at["display_slots"] = creatures_base + 0x20;
        l.size = creatures_with_display;
    }
    return l;
}

// Ultimate's moving goal area: how many updates each picture took to render
// in the original (creature log "picture": $11E98F set in the record after
// the request, $11E98C flipped in the record after the swap).
std::deque<int> logged_render_times(const std::string& path, const std::vector<std::uint8_t>& run) {
    std::deque<int> out;
    std::vector<std::uint8_t> log;
    try {
        log = platform::read_file(platform::utf8_path(path.substr(0, path.size() - 4) + "-creatures.bin"));
    } catch (const std::exception&) {
        return out;
    }
    const std::size_t frames = run.size() / record_size;
    const auto layout = creature_layout(path, log.size(), frames);
    if (!layout.has("picture")) return out;
    int updates = 0, requested = -1;
    int buffer = -1, rendering = 0;
    for (std::size_t k = 0; k < frames && (k + 1) * layout.size <= log.size(); ++k) {
        if (repeated(run, k)) continue;
        ++updates;
        const auto* p = log.data() + k * layout.size + layout.at.at("picture");
        if (buffer >= 0 && p[0] != buffer && requested >= 0) {
            out.push_back(updates - requested);
            requested = -1;
        }
        if (!rendering && p[3]) requested = updates;
        buffer = p[0];
        rendering = p[3];
    }
    // A rendering the log ends in had not finished by then.
    if (requested >= 0) out.push_back(updates - requested + 1000);
    return out;
}

std::string acid_diff(const game::Acid& a, const std::uint8_t* r) {
    std::string out;
    auto check = [&](const char* name, std::int32_t want, std::int32_t got) {
        if (want != got) {
            char line[80];
            std::snprintf(line, sizeof line, " %s %d/%d", name, want, got);
            out += line;
        }
    };
    check("status", r[0x18], a.status);
    if (r[0x18] == 0 && a.status == 0) return out;
    check("index", r[0x19], a.index);
    check("zone", std::int8_t(r[0x1B]), a.zone);
    check("x", be32(r + 0xC), a.x);
    check("y", be32(r + 0x10), a.y);
    check("z", be32(r + 0x14), a.z);
    check("vx", be32(r + 0), a.vx);
    check("vy", be32(r + 4), a.vy);
    check("tick", r[0x24], a.tick);
    check("period", r[0x25], a.period);
    check("screen", be32(r + 0x28), a.screen);
    check("waypoint", be32(r + 0x2C) - be32(r + 0x30), std::int32_t(a.waypoint - a.path));
    check("frame", be32(r + 0x3A) - be32(r + 0x3E), std::int32_t(a.frame - a.animation));
    check("bands", be16(r + 0x34) * 256 + be16(r + 0x36), a.band_from * 256 + a.band_to);
    return out;
}

// MM_SEED=n: the race's random numbers from that seed (default: the
// generator's fixed seed). The original's numbers depend on its display
// timing, so random choices part from a recording wherever they matter.
game::Random race_random() {
    const char* seed = std::getenv("MM_SEED");
    return seed ? game::Random(std::uint32_t(std::stoul(seed))) : game::Random();
}

std::string steelie_diff(const game::Steelie& s, const std::uint8_t* r) {
    const auto& m = s.body;
    std::string out;
    auto check = [&](const char* name, std::int32_t want, std::int32_t got) {
        if (want != got) {
            char line[80];
            std::snprintf(line, sizeof line, " %s %d/%d", name, want, got);
            out += line;
        }
    };
    check("status", r[0x18], m.status);
    if (r[0x18] == 0 && m.status == 0) return out;
    check("mode", r[0x1A], m.mode);
    check("x", be32(r + 0xC), m.x);
    check("y", be32(r + 0x10), m.y);
    check("z", be32(r + 0x14), m.z);
    check("vx", be32(r + 0), m.vx);
    check("vy", be32(r + 4), m.vy);
    check("vz", be32(r + 8), m.vz);
    check("air", r[0x36], m.air);
    check("bounce", r[0x60], m.bounce);
    check("speed", be32(r + 0x72), s.speed);
    check("chase", be16(r + 0x76), s.chase_time);
    check("waypoint", be32(r + 0x78) - be32(r + 0x7C), std::int32_t(s.waypoint - s.path));
    check("target", be16(r + 0x84), s.target);
    check("flag", r[0x1C], m.sprite_flag);
    return out;
}

std::string slinky_diff(const game::Slinky& s, const std::uint8_t* r) {
    std::string out;
    auto check = [&](const char* name, std::int32_t want, std::int32_t got) {
        if (want != got) {
            char line[80];
            std::snprintf(line, sizeof line, " %s %d/%d", name, want, got);
            out += line;
        }
    };
    check("status", r[0x18], s.status);
    if (r[0x18] == 0 && s.status == 0) return out;
    check("state", r[0x1A], s.state);
    check("x", be32(r + 0xC), s.x);
    check("y", be32(r + 0x10), s.y);
    check("z", be32(r + 0x14), s.z);
    check("vx", be32(r + 0), s.vx);
    check("vy", be32(r + 4), s.vy);
    check("tick", r[0x24], s.tick);
    check("hop", r[0x27], s.hop);
    check("target", be16(r + 0x60), s.target);
    check("frame", be32(r + 0x62) - be32(r + 0x66), std::int32_t(s.frame - s.animation));
    check("waypoint", be32(r + 0x54) - be32(r + 0x58), std::int32_t(s.waypoint - s.path));
    return out;
}

// A region slot against the log (addresses as offsets from the script start).
std::string region_diff(const game::Region& g, const std::uint8_t* r) {
    std::string out;
    auto check = [&](const char* name, std::int32_t want, std::int32_t got) {
        if (want != got) {
            char line[80];
            std::snprintf(line, sizeof line, " %s %d/%d", name, want, got);
            out += line;
        }
    };
    check("active", r[0x18], g.active);
    if (r[0x18] == 0 && !g.active) return out;
    const std::int32_t script = be32(r + 0x3A);
    check("state", r[0x1A], g.state);
    check("count", be16(r + 0x1C), g.count);
    check("kind", r[0x1E], g.kind);
    check("code", std::int8_t(r[0x1F]), g.code);
    check("pc", be32(r + 0x36) - script, std::int32_t(g.pc - g.script));
    check("frame", be32(r + 0x3E) - script, std::int32_t(g.frame - g.script));
    check("x", be32(r + 0xC), g.x);
    check("y", be32(r + 0x10), g.y);
    check("z", be32(r + 0x14), g.z);
    check("sx", be16(r + 0x4E), g.screen_x);
    check("sy", be16(r + 0x50), g.screen_y);
    return out;
}

std::string covers_of(const game::Marble& m) {
    std::string out = std::to_string(m.sprite_flag);
    for (const auto& c : m.covers) {
        if (c.cell == -1) break;
        char item[48];
        std::snprintf(item, sizeof item, " %x,%d,%d", unsigned(c.cell), c.x, c.y);
        out += item;
    }
    return out;
}

// The course start ($114AA2) leaves some marble fields as the last race
// left them, mmref.py --time sets the time, and a tick before the clock
// runs may have touched a region: these are taken over from the run's
// first record.
void take_start(game::Race& race, const std::uint8_t* start) {
    auto& m = race.marbles[0];
    m.fall_start_z = be32(start + 0x2A);
    m.edge_cell_x = be16(start + 0x2E);
    m.edge_cell_y = be16(start + 0x30);
    m.cell_x = be16(start + 0x32);
    m.cell_y = be16(start + 0x34);
    m.edge_sides = start[0x37];
    m.saved_feature = std::int8_t(start[0x1D]);
    m.time = be16(start + 0x74);
    m.region_code = std::int8_t(start[0x62]);
    m.region_timer = start[0x63];
}

// What a race shows of its random choices: the player, the creatures and
// the pistons. Races that differ only in their seeds agree on it until a
// random number first matters.
std::vector<std::int64_t> fingerprint(const game::Race& race) {
    std::vector<std::int64_t> out;
    for (const auto& f : fields()) out.push_back(native(race.marbles[0], f));
    out.push_back(race.marbles[0].screen[0]);
    for (const auto& s : race.steelies) {
        for (const auto& f : fields()) out.push_back(native(s.body, f));
    }
    for (const auto& s : race.slinkies) out.insert(out.end(), {s.status, s.state, s.x, s.y, s.z});
    for (const auto& a : race.acids) out.insert(out.end(), {a.status, a.x, a.y, std::int64_t(a.path)});
    out.push_back(race.pistons_on());
    for (const auto& t : race.pistons) out.insert(out.end(), {t.state, t.z});
    for (const auto& g : race.piston_groups) out.insert(out.end(), {g.timer, g.column, g.row, g.pattern});
    for (const auto& c : race.critters) out.insert(out.end(), {c.status, c.state, c.x, c.y, c.direction});
    for (const auto& b : race.birds) out.insert(out.end(), {b.status, b.state, b.y, b.count, b.period});
    return out;
}

// The original's random choices that its creature log shows, checked by
// the rules that make them (the numbers themselves come from its display
// timing): a piston group's pattern and first piston ($11DB4C) and its wait
// after coming down (random(4) * 16), a critter's start in the pen
// (pen + 4 * random(32), $11C78C), a bird's new walk count (random(2) + 4
// at the fast pace, + 1 at the slow one).
struct RandomRules {
    std::size_t checked = 0, allowed = 0;
    std::string first_broken;
    void check(bool ok, std::size_t k, const std::string& what) {
        ++checked;
        if (ok) {
            ++allowed;
        } else if (first_broken.empty()) {
            first_broken = "update " + std::to_string(k) + " " + what;
        }
    }
};

void check_random_rules(RandomRules& rules, data::Course course, const CreatureLayout& layout,
                        const std::uint8_t* before, const std::uint8_t* now, std::size_t k) {
    if (course == data::Course::aerial && layout.has("piston_groups")) {
        for (int g = 0; g < 3; ++g) {
            const auto* p = before + layout.at.at("piston_groups") + std::size_t(g) * 0x14;
            const auto* r = now + layout.at.at("piston_groups") + std::size_t(g) * 0x14;
            const int timer = std::int16_t(be16(r + 8)), was = std::int16_t(be16(p + 8));
            const int column = be16(r + 0xE), row = be16(r + 0x10), pattern = be16(r + 0x12);
            if (timer < 0 && was >= 0) {
                const bool ok = pattern == 0   ? column == 0 && row < 4
                                : pattern == 1 ? column < 3 && row < 2
                                : pattern <= 3 ? column == 0 && row < 2
                                               : false;
                rules.check(ok, k, "group " + std::to_string(g) + " pattern " + std::to_string(pattern));
            } else if (was == -1 && timer >= 0) {
                rules.check(timer % 16 == 0 && timer <= 48, k, "group " + std::to_string(g) + " wait " + std::to_string(timer));
            }
        }
    }
    if (course == data::Course::silly && layout.has("critters")) {
        constexpr int pen_x = 0x2CA, pen_y = 0x2D2;
        for (int i = 0; i < 9; ++i) {
            const auto* p = before + layout.at.at("critters") + std::size_t(i) * 0x28;
            const auto* r = now + layout.at.at("critters") + std::size_t(i) * 0x28;
            if (p[0x18] == 0 && r[0x18] == 1) {
                // Spawned this update; it may have taken its first step.
                const int x = be32(r + 0xC) >> 16, y = be32(r + 0x10) >> 16;
                auto near_grid = [](int v, int pen) { return v >= pen - 2 && v <= pen + 124 + 2; };
                rules.check(near_grid(x, pen_x) && near_grid(y, pen_y), k, "critter " + std::to_string(i) + " start " +
                            std::to_string(x) + "," + std::to_string(y));
            }
        }
        for (int i = 0; i < 10; ++i) {
            const auto* p = before + layout.at.at("birds") + std::size_t(i) * 0x3E;
            const auto* r = now + layout.at.at("birds") + std::size_t(i) * 0x3E;
            if (r[0x1B] > p[0x1B] && p[0x1B] <= 1) {
                const bool ok = r[0x25] == 1 ? (r[0x1B] == 4 || r[0x1B] == 5) : (r[0x1B] == 1 || r[0x1B] == 2);
                rules.check(ok, k, "bird " + std::to_string(i) + " count " + std::to_string(r[0x1B]));
            }
        }
    }
}

int lockstep(int argc, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    const auto run = platform::read_file(platform::utf8_path(argv[3]));
    const auto course = data::Course(argc > 4 ? std::stoi(argv[4]) : 0);
    const std::size_t frames = run.size() / record_size;
    const std::size_t start_record = race_start(run);
    const std::size_t first = std::max(start_record, argc > 5 ? std::size_t(std::stoul(argv[5])) : 0);
    std::size_t count = argc > 6 ? std::size_t(std::stoul(argv[6])) : frames;
    data::GameFiles files(content);
    const auto random = race_random();
    game::Race race(files, course, 1, 0, &random);
    if (!std::getenv("MM_NO_RENDER_TIMES")) race.regions().forced_render_times = logged_render_times(argv[3], run);
    // MM_NO_COUNTS=1: keep the port's own update count (research).
    const bool counts = has_counts(run) && !std::getenv("MM_NO_COUNTS");
    const auto* start = run.data() + first * record_size + players_at;
    if (first == start_record) {
        // From the course start: the first record must equal the start,
        // apart from fields the course start ($114AA2) leaves as the last
        // race left them (zero after booting); those are taken over.
        take_start(race, start);

        std::string diff;
        for (const auto& f : fields()) {
            const auto want = recorded(start, f), got = native(race.marbles[0], f);
            if (want != got) {
                diff += std::string(" ") + f.name + " " + std::to_string(want) + "/" + std::to_string(got);
            }
        }
        std::printf("start:%s\n", diff.empty() ? " equal" : diff.c_str());
    } else {
        adopt(race.marbles[0], start);
    }
    // The port's random numbers cannot follow the original's (those come
    // from its display timing). Races on other seeds show where a random
    // choice first matters: up to that update the race must equal the
    // recording; from it on the comparison only reports, and the
    // original's random choices are checked by their rules instead.
    std::vector<game::Race> shadows;
    for (std::uint32_t seed = 1; seed <= 3; ++seed) {
        const game::Random other(game::Random::default_seed + seed * 0x9E3779B9u);
        auto& shadow = shadows.emplace_back(files, course, 1, 0, &other);
        shadow.regions().forced_render_times = race.regions().forced_render_times;
        if (first == start_record) {
            take_start(shadow, start);
        } else {
            adopt(shadow.marbles[0], start);
        }
    }
    std::optional<std::size_t> random_from;
    std::size_t matched_before_random = 0, compared_before_random = 0;
    RandomRules rules;
    const bool show_all = std::getenv("MM_SHOW_ALL") != nullptr;
    const auto covers = read_covers(argv[3]);
    bool race_over_shown = false;
    std::vector<std::uint8_t> creatures;
    try {
        const std::string path = argv[3];
        creatures = platform::read_file(platform::utf8_path(path.substr(0, path.size() - 4) + "-creatures.bin"));
    } catch (const std::exception&) {
    }
    const auto layout = creature_layout(argv[3], creatures.size(), frames);
    const std::size_t creatures_size = layout.size;
    std::size_t steelies_compared = 0, steelies_matched = 0;
    int steelies_shown = 0;
    std::size_t covers_compared = 0, covers_matched = 0;
    int covers_shown = 0;
    int shown = 0;
    std::size_t matched = 0;
    for (std::size_t k = first; k + 1 < frames && k < first + count; ++k) {
        if (repeated(run, k + 1)) continue;
        const auto* rec = run.data() + k * record_size;
        game::Push p0{be16(rec + forces_at), be16(rec + forces_at + 4)};
        follow_count(race, run, k, counts);
        race.update({p0, game::Push{}});
        if (!random_from) {
            const auto own = fingerprint(race);
            for (auto& shadow : shadows) {
                follow_count(shadow, run, k, counts);
                shadow.update({p0, game::Push{}});
                shadow.take_sounds();
                if (fingerprint(shadow) != own) random_from = k;
            }
            if (random_from) shadows.clear();
        }
        const bool before_random = !random_from;
        const bool report = before_random || show_all;
        if (const char* dd = std::getenv("MM_DISPLAY_DUMP"); dd && std::size_t(std::atoi(dd)) <= k && k < std::size_t(std::atoi(dd)) + 3) {
            for (int i = 0; i < game::DisplayList::capacity; ++i) {
                const auto& s = race.display().slot(i);
                if (s.type != 0) std::printf("update %zu slot %d %d:%d box %d %d %d %d %d %d\n", k, i, s.type, s.index, s.box.x0, s.box.y0, s.box.z0, s.box.x1, s.box.y1, s.box.z1);
            }
        }
        // MM_SOUNDS=1: the race's sound calls, as mmref.py --sounds logs them.
        for (const auto& e : race.take_sounds()) {
            if (std::getenv("MM_SOUNDS")) std::printf("sound %zu %d %d\n", k, e.id, e.right ? 1 : 0);
        }
        const auto* next = run.data() + (k + 1) * record_size + players_at;
        // The recording may go on into the next course (the course number
        // is in the recorded globals, $1005EE); the comparison ends there.
        const auto* globals = run.data() + (k + 1) * record_size + players_at + 0xF4 * 2;
        if (be16(globals + 0xE) != int(course)) {
            std::printf("the original moves on to course %d after update %zu\n", be16(globals + 0xE), k);
            count = k - first;
            break;
        }
        if (race.game_mode != 0 && !race_over_shown) {
            std::printf("update %zu: native mode %d (the recording's last record: %zu)\n", k, race.game_mode, frames - 1);
            race_over_shown = true;
        }
        // Ultimate's colour cycling ($1006A6 phase, $1006A7 count).
        if (course == data::Course::ultimate &&
            (globals[0xC6] != race.colour_phase() || std::int8_t(globals[0xC7]) != race.colour_count())) {
            std::printf("update %zu colours: recorded %d,%d native %d,%d\n", k, globals[0xC6], std::int8_t(globals[0xC7]),
                        race.colour_phase(), race.colour_count());
        }
        if (std::getenv("MM_SCROLL") != nullptr) {
            std::printf("update %zu scroll recorded %d native %d view %d\n", k,
                        be16(run.data() + (k + 1) * record_size + players_at + 0xF4 * 2 + 0x120), race.scroll_lines(),
                        race.view());
        }
        if ((k + 2) * creatures_size <= creatures.size()) {
            check_random_rules(rules, course, layout, creatures.data() + k * creatures_size,
                               creatures.data() + (k + 1) * creatures_size, k);
            ++steelies_compared;
            std::string diff;
            for (int i = 0; i < 2; ++i) {
                const auto d = steelie_diff(race.steelies[std::size_t(i)], creatures.data() + (k + 1) * creatures_size +
                                                                               layout.at.at("steelies") + std::size_t(i) * 0x86);
                if (!d.empty()) diff += " [" + std::to_string(i) + "]" + d;
            }
            for (int i = 0; i < 4; ++i) {
                const auto d = slinky_diff(race.slinkies[std::size_t(i)], creatures.data() + (k + 1) * creatures_size +
                                                                              layout.at.at("slinkies") + std::size_t(i) * 0x6A);
                if (!d.empty()) diff += " slinky " + std::to_string(i) + d;
            }
            if (layout.has("acid")) {
                for (int i = 0; i < 5; ++i) {
                    const auto d = acid_diff(race.acids[std::size_t(i)], creatures.data() + (k + 1) * creatures_size +
                                                                            layout.at.at("acid") + std::size_t(i) * 0x42);
                    if (!d.empty()) diff += " acid " + std::to_string(i) + d;
                }
            }
            if (layout.has("cover_set")) {
                const int set = be16(creatures.data() + (k + 1) * creatures_size + layout.at.at("cover_set"));
                if (set != race.regions().cover_set() * 0x90) {
                    diff += " cover_set " + std::to_string(set) + "/" + std::to_string(race.regions().cover_set() * 0x90);
                }
            }
            if (layout.has("pistons") && course == data::Course::aerial) {
                const auto* base = creatures.data() + (k + 1) * creatures_size;
                const auto* flags = base + layout.at.at("aerial_flags");
                if (flags[0] != (race.pistons_on() ? 1 : 0)) diff += " tiles_on " + std::to_string(flags[0]);
                for (int i = 0; i < 36; ++i) {
                    const auto* r = base + layout.at.at("pistons") + std::size_t(i) * 16;
                    const auto& t = race.pistons[std::size_t(i)];
                    if (be16(r + 2) != t.state || std::int8_t(r[4]) != t.cell_x || std::int8_t(r[5]) != t.cell_y ||
                        be16(r + 6) != t.z) {
                        diff += " piston " + std::to_string(i) + " " + std::to_string(be16(r + 2)) + "/" + std::to_string(t.state);
                    }
                }
                for (int g = 0; g < 3; ++g) {
                    const auto* r = base + layout.at.at("piston_groups") + std::size_t(g) * 0x14;
                    const auto& t = race.piston_groups[std::size_t(g)];
                    if (be16(r + 8) != t.timer || be16(r + 0xE) != t.column || be16(r + 0x10) != t.row ||
                        be16(r + 0x12) != t.pattern) {
                        diff += " group " + std::to_string(g) + " timer " + std::to_string(be16(r + 8)) + "/" +
                                std::to_string(t.timer) + " pattern " + std::to_string(be16(r + 0x12)) + "/" +
                                std::to_string(t.pattern);
                    }
                }
            }
            if (layout.has("critters") && course == data::Course::silly) {
                const auto* base = creatures.data() + (k + 1) * creatures_size;
                for (int i = 0; i < 9; ++i) {
                    const auto* r = base + layout.at.at("critters") + std::size_t(i) * 0x28;
                    const auto& c = race.critters[std::size_t(i)];
                    if (r[0x18] == 0 && c.status == 0) continue;
                    if (r[0x18] != c.status || be32(r + 0xC) != c.x || be32(r + 0x10) != c.y || be32(r) != c.vx ||
                        be32(r + 4) != c.vy || r[0x1A] != c.state || r[0x26] != c.direction || r[0x24] != c.tick) {
                        diff += " critter " + std::to_string(i) + " x " + std::to_string(be32(r + 0xC) >> 16) + "/" +
                                std::to_string(c.x >> 16) + " y " + std::to_string(be32(r + 0x10) >> 16) + "/" +
                                std::to_string(c.y >> 16) + " dir " + std::to_string(r[0x26]) + "/" + std::to_string(c.direction) +
                                " state " + std::to_string(r[0x1A]) + "/" + std::to_string(c.state);
                    }
                }
                for (int i = 0; i < 10; ++i) {
                    const auto* r = base + layout.at.at("birds") + std::size_t(i) * 0x3E;
                    const auto& b = race.birds[std::size_t(i)];
                    if (r[0x18] == 0 && b.status == 0) continue;
                    if (r[0x18] != b.status || be32(r + 0xC) != b.x || be32(r + 0x10) != b.y || be32(r + 4) != b.vy ||
                        r[0x1A] != b.state || r[0x24] != b.tick || r[0x25] != b.period || r[0x1B] != b.count) {
                        diff += " bird " + std::to_string(i) + " y " + std::to_string(be32(r + 0x10) >> 16) + "/" +
                                std::to_string(b.y >> 16) + " state " + std::to_string(r[0x1A]) + "/" + std::to_string(b.state);
                    }
                }
            }
            if (layout.has("display")) {
                // The display list: (type, index) in drawing order.
                const auto* r = creatures.data() + (k + 1) * creatures_size + layout.at.at("display");
                const auto* slots = creatures.data() + (k + 1) * creatures_size + layout.at.at("display_slots") - 0x20;
                std::string want, got;
                for (int i = 0; i < 31 && r[i] != 0xFF; ++i) {
                    want += " " + std::to_string(slots[0x20 + r[i] * 14]) + ":" + std::to_string(slots[0x20 + r[i] * 14 + 1]);
                }
                for (const auto slot : race.display().order()) {
                    if (slot == 0xFF) break;
                    got += " " + std::to_string(race.display().slot(slot).type) + ":" +
                           std::to_string(race.display().slot(slot).index);
                }
                if (want != got) diff += " display [" + want + " ] / [" + got + " ]";
            }
            if (std::getenv("MM_REGIONS")) {
                for (int i = 0; i < 25; ++i) {
                    const auto d = region_diff(race.regions().all()[std::size_t(i)],
                                               creatures.data() + (k + 1) * creatures_size + layout.at.at("regions") +
                                                   std::size_t(i) * 0x56);
                    if (!d.empty()) diff += " region " + std::to_string(i) + d;
                }
            }
            if (diff.empty()) {
                ++steelies_matched;
            } else if (report && steelies_shown++ < 12) {
                std::printf("update %zu creatures:%s\n", k, diff.c_str());
            }
        }
        if (const auto c = covers.find(k); c != covers.end()) {
            ++covers_compared;
            const auto got = covers_of(race.marbles[0]);
            if (got == c->second) {
                ++covers_matched;
            } else if (report && covers_shown++ < 12) {
                std::printf("update %zu covers: recorded [%s] native [%s]\n", k, c->second.c_str(), got.c_str());
            }
        }
        std::string diff;
        for (const auto& f : fields()) {
            const auto want = recorded(next, f), got = native(race.marbles[0], f);
            if (want != got) {
                char line[96];
                std::snprintf(line, sizeof line, " %s %d/%d", f.name, want, got);
                diff += line;
            }
        }
        // The screen position (+$1E: x << 16 | y).
        if (be32(next + 0x1E) != race.marbles[0].screen[0]) {
            char line[96];
            std::snprintf(line, sizeof line, " screen %d,%d/%d,%d", be16(next + 0x1E), be16(next + 0x20),
                          std::int16_t(std::uint32_t(race.marbles[0].screen[0]) >> 16),
                          std::int16_t(race.marbles[0].screen[0]));
            diff += line;
        }
        // The creatures and covers above count the same way: before the
        // first random choice they must match too.
        if (before_random) {
            ++compared_before_random;
            if (diff.empty() && steelies_shown == 0 && covers_shown == 0) ++matched_before_random;
        }
        if (diff.empty()) {
            ++matched;
            continue;
        }
        if (report && shown++ < 12) {
            std::printf("update %zu (push %d %d):%s\n", k, p0.forward, p0.side, diff.c_str());
        }
    }
    // The update after the last record: a recording that ends with the
    // race (no world update follows) should end it here too.
    if (count == frames && frames > 0 && !race_over_shown) {
        const auto* rec = run.data() + (frames - 1) * record_size;
        follow_count(race, run, frames - 1, counts);
        race.update({game::Push{be16(rec + forces_at), be16(rec + forces_at + 4)}, game::Push{}});
        std::printf("after the last record: native mode %d\n", race.game_mode);
    }
    std::printf("%zu of %zu updates match (recorded/native)\n", matched,
                std::min(frames - 1, first + count) - first);
    if (steelies_compared != 0) {
        std::printf("creatures: %zu of %zu updates match\n", steelies_matched, steelies_compared);
    }
    if (covers_compared != 0) {
        std::printf("covers: %zu of %zu updates match\n", covers_matched, covers_compared);
    }
    if (random_from) {
        std::printf("random: choices matter from update %zu; %zu of %zu updates before match\n", *random_from,
                    matched_before_random, compared_before_random);
    } else {
        std::printf("random: no choice matters (%zu of %zu updates match)\n", matched_before_random,
                    compared_before_random);
    }
    if (rules.checked != 0) {
        std::printf("random rules: %zu of %zu of the original's choices match%s%s\n", rules.allowed, rules.checked,
                    rules.first_broken.empty() ? "" : "; first not: ", rules.first_broken.c_str());
    }
    return 0;
}

// Runs the recording's pushes from the course start and saves the picture
// before the listed updates (what the original's window shows when the
// recorder stops there).
int pictures(int argc, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    const auto run = platform::read_file(platform::utf8_path(argv[3]));
    const auto out = platform::utf8_path(argv[4]);
    std::vector<std::size_t> wanted;
    for (int i = 5; i < argc; ++i) wanted.push_back(std::size_t(std::stoul(argv[i])));
    data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    // MM_COURSE=n: a course other than Practice.
    const auto random = race_random();
    game::Race race(files, data::Course(std::getenv("MM_COURSE") ? std::stoi(std::getenv("MM_COURSE")) : 0), 1, 0,
                    &random);
    race.regions().forced_render_times = logged_render_times(argv[3], run);
    const bool counts = has_counts(run);
    take_start(race, run.data() + race_start(run) * record_size + players_at);
    game::View view(files, assets, race);
    view.update(race);
    const std::size_t frames = run.size() / record_size;
    for (std::size_t k = race_start(run); k < frames; ++k) {
        for (const auto w : wanted) {
            if (w == k) {
                platform::save_png(view.draw(race), out / ("native-" + std::to_string(k) + ".png"));
                if (std::getenv("MM_MARKS") != nullptr) {
                    const auto& mk = race.drop_marks()[0];
                    std::printf("update %zu mark kind %d at %d,%d,%d screen %d,%d\n", k, mk.kind, mk.x, mk.y, mk.z,
                                std::int16_t(std::uint32_t(mk.screen) >> 16), std::int16_t(mk.screen));
                }
                if (std::getenv("MM_INDEX_DUMP")) {
                    // Colour indices of the view (for checking colour bands).
                    platform::write_file(out / ("index-" + std::to_string(k) + ".bin"), view.indices(race));
                }
            }
        }
        if (std::getenv("MM_MODES")) {
            // The player's mode and what the view shows of it, per update.
            const auto& m = race.marbles[0];
            std::printf("update %zu mode %d sprite %d sparkles %d/%d delay %d animation %08x\n", k, int(m.mode),
                        int(m.sprite_flag), int(m.sparkles_shown), int(m.sparkle_count), int(m.delay), unsigned(m.animation));
        }
        if (repeated(run, k + 1)) continue;
        const auto* rec = run.data() + k * record_size;
        follow_count(race, run, k, counts);
        race.update({game::Push{be16(rec + forces_at), be16(rec + forces_at + 4)}, game::Push{}});
        view.update(race);
    }
    return 0;
}

// Plays a course with random controls (after an optional prefix of a
// recorded run's pushes) and reports any exception the race or the view
// throws: fuzz <content> <course> <run.bin|-> <prefix> <seeds> <updates>.
int fuzz(int /*argc*/, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    const auto course = data::Course(std::stoi(argv[3]));
    std::vector<std::uint8_t> run;
    if (std::string(argv[4]) != "-") run = platform::read_file(platform::utf8_path(argv[4]));
    const std::size_t prefix = std::stoul(argv[5]);
    const int seeds = std::stoi(argv[6]), updates = std::stoi(argv[7]);
    const bool draw = std::getenv("MM_NO_DRAW") == nullptr;
    for (int seed = 0; seed < seeds; ++seed) {
        std::uint32_t rng = 0x9E3779B9u * std::uint32_t(seed + 1);
        auto next = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        game::Race race(files, course);
        race.cheats.stop_timer = true;
        if (std::getenv("MM_CROWD")) race.set_crowd(true); // with the crowd race
        game::View view(files, assets, race);
        view.update(race);
        game::Push push{};
        int hold = 0, k = 0;
        try {
            const std::size_t start = run.empty() ? 0 : race_start(run);
            for (std::size_t r = start; r < run.size() / record_size && r - start < prefix; ++r, ++k) {
                if (repeated(run, r + 1)) continue;
                const auto* rec = run.data() + r * record_size;
                race.update({game::Push{be16(rec + forces_at), be16(rec + forces_at + 4)}, game::Push{}});
                view.update(race);
            }
            for (int i = 0; i < updates; ++i, ++k) {
                if (hold-- <= 0) {
                    hold = int(next() % 40);
                    const int dir = int(next() % 9), fire = next() % 4 == 0 ? 2 : 1;
                    static constexpr int dx[9] = {0, 1, 1, 0, -1, -1, -1, 0, 1}, dy[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
                    push = {std::int16_t(dy[dir] * 0x20 * fire), std::int16_t(-dx[dir] * 0x20 * fire)};
                }
                race.update({push, game::Push{}});
                view.update(race);
                if (draw) (void)view.draw(race);
            }
        } catch (const std::exception& e) {
            const auto& m = race.marbles[0];
            std::printf("seed %d update %d: %s (marble at %d,%d,%d mode %d)\n", seed, k, e.what(), m.x >> 16, m.y >> 16,
                        m.z >> 16, int(m.mode));
        }
    }
    return 0;
}

// The crowd race with the computer driving all eight marbles (marble 0
// too), the clock stopped: crowd <content> <course> <updates> [dir [ticks]]
// reports when each finishes and how often it was lost; pictures after
// the listed ticks (crowd-<tick>.png).
int crowd(int argc, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    game::Race race(files, data::Course(std::stoi(argv[3])));
    const int updates = std::stoi(argv[4]);
    std::set<int> wanted;
    for (int i = 6; i < argc; ++i) wanted.insert(std::stoi(argv[i]));
    race.cheats.stop_timer = true;
    race.crowd_close = std::getenv("MM_NO_BAND") == nullptr;
    race.crowd_skill = std::getenv("MM_SKILL") ? std::atoi(std::getenv("MM_SKILL")) : game::Race::hard; // (the tool's default: the planner alone)
    // MM_IDLE=n: marble 0 stays idle and the crowd starts after n updates
    // (the computer's marbles alone, from different moments).
    const int idle = std::getenv("MM_IDLE") ? std::atoi(std::getenv("MM_IDLE")) : -1;
    for (int k = 0; k < idle; ++k) race.update({});
    // MM_PLAYER_AT=x,y,zone: marble 0 put there (on the floor) and left
    // idle for 200 updates (the view follows), then the crowd starts.
    if (const char* pa = std::getenv("MM_PLAYER_AT")) {
        int x = 0, y = 0, zone = 0, z = 0;
        std::sscanf(pa, "%d,%d,%d,%d", &x, &y, &zone, &z);
        race.set_crowd(true);
        race.set_crowd(false);
        auto& m = race.marbles[0];
        m.x = x << 16;
        m.y = y << 16;
        m.z = (z != 0 ? z : race.crowd_map()->floor(x >> 3, y >> 3) + 2) << 16;
        m.fall_start_z = m.z;
        m.vx = m.vy = m.vz = 0;
        m.feature = std::int8_t(zone);
        for (int k = 0; k < 200; ++k) race.update({});
        std::printf("player at %d,%d,%d zone %d mode %d\n", m.x >> 16, m.y >> 16, m.z >> 16, int(m.feature), int(m.mode));
        // MM_GROUND=x0,y0,x1,y1: the floor's heights (cells) now.
        if (const char* g = std::getenv("MM_GROUND")) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            std::sscanf(g, "%d,%d,%d,%d", &x0, &y0, &x1, &y1);
            game::Probe probe;
            for (int cy = y0; cy <= y1; ++cy) {
                std::printf("y%3d", cy);
                for (int cx = x0; cx <= x1; ++cx) {
                    probe.place((cx * 8 + 4) << 16, (cy * 8 + 4) << 16);
                    std::printf(" %5d", probe.ground_height_here(race.terrain()) >> 16);
                }
                std::printf("\n");
            }
        }
    }
    // MM_PLAYER_ZONE=z: the computer drives marble 0 into zone z and stops
    // it there (the view stays), then the crowd starts from the start.
    if (const char* pz = std::getenv("MM_PLAYER_ZONE")) {
        const int zone = std::atoi(pz);
        race.set_crowd(true);
        race.set_crowd(false); // (the map stays)
        race.bot_drives_player = true;
        for (int k = 0; k < 5000 && race.marbles[0].feature != zone; ++k) race.update({});
        race.bot_drives_player = false;
        for (int k = 0; k < 60; ++k) race.update({});
        std::printf("player stopped in zone %d at %d,%d\n", int(race.marbles[0].feature), race.marbles[0].x >> 16, race.marbles[0].y >> 16);
    }
    race.set_crowd(true);
    race.bot_drives_player = idle < 0 && !std::getenv("MM_PLAYER_ZONE") && !std::getenv("MM_PLAYER_AT");
    // MM_SOLO=1: marble 0 alone (the driving without the crowd's bumping).
    if (std::getenv("MM_SOLO")) {
        race.set_crowd(false);
    }
    // MM_PILE=1: marbles 1 and 2 put on marble 0's spot (as a funnel's exit
    // can), then whether they come apart.
    if (std::getenv("MM_PILE")) {
        for (int i = 1; i <= 2; ++i) {
            auto& m = race.marbles[std::size_t(i)];
            m.x = race.marbles[0].x + (i << 15);
            m.y = race.marbles[0].y;
            m.z = race.marbles[0].z;
            m.vx = m.vy = 0;
        }
        for (int k = 0; k < 120; ++k) {
            race.update({});
            if (k % 30 == 29) {
                const auto& a = race.marbles[0];
                std::printf("pile after %d: marble 1 %d,%d marble 2 %d,%d from marble 0\n", k + 1, (race.marbles[1].x - a.x) >> 16,
                            (race.marbles[1].y - a.y) >> 16, (race.marbles[2].x - a.x) >> 16, (race.marbles[2].y - a.y) >> 16);
            }
        }
        return 0;
    }
    game::View view(files, assets, race);
    view.update(race);
    for (int i = 0; i < game::Race::max_marbles; ++i) {
        const auto& m = race.marbles[std::size_t(i)];
        std::printf("start %d: %d,%d,%d cell %d,%d screen %d,%d\n", i, m.x >> 16, m.y >> 16, m.z >> 16, m.x >> 19, m.y >> 19,
                    std::int16_t(std::uint32_t(m.screen[0]) >> 16), std::int16_t(m.screen[0]));
    }
    std::array<int, game::Race::max_marbles> finished{}, lost{};
    std::map<std::tuple<int, int, int>, int> losses, lost_points;
    std::map<std::string, int> causes;
    std::array<int, game::Race::max_marbles> captures{};
    long lead_sum = 0, lead_count = 0, far_ahead = 0, far_behind = 0;
    std::vector<std::tuple<int, int, int>> trail;
    finished.fill(-1);
    std::array<int, game::Race::max_marbles> respawns{};
    for (int k = 0; k <= updates; ++k) {
        if (wanted.contains(k) && argc > 5) {
            platform::save_png(view.draw(race), platform::utf8_path(argv[5]) / ("crowd-" + std::to_string(k) + ".png"));
        }
        std::array<int, game::Race::max_marbles> before{};
        for (int i = 0; i < game::Race::max_marbles; ++i) {
            before[std::size_t(i)] = race.marbles[std::size_t(i)].status;
            respawns[std::size_t(i)] = std::max(respawns[std::size_t(i)], int(race.marbles[std::size_t(i)].respawns));
        }
        // MM_LOSS_FRAMES=dir: the 16 pictures before marble 0's first loss
        // (every second update), as loss-<n>.png.
        static std::deque<platform::Image> recent;
        static bool saved = false;
        if (std::getenv("MM_LOSS_FRAMES") && !saved) {
            recent.push_back(view.draw(race));
            if (recent.size() > 32) recent.pop_front();
        }
        static int losses_seen = 0;
        const int wanted_zone = std::getenv("MM_LOSS_ZONE") ? std::atoi(std::getenv("MM_LOSS_ZONE")) : -99;
        const bool lost_now = race.marbles[0].respawns > losses_seen;
        if (lost_now) losses_seen = race.marbles[0].respawns;
        if (std::getenv("MM_LOSS_FRAMES") && !saved && lost_now && (wanted_zone == -99 || race.marbles[0].feature == wanted_zone)) {
            saved = true;
            for (std::size_t f = 0; f < recent.size(); f += 2) {
                platform::save_png(recent[f], platform::utf8_path(std::getenv("MM_LOSS_FRAMES")) / ("loss-" + std::to_string(f / 2) + ".png"));
            }
            std::printf("first loss of marble 0 at update %d\n", k);
        }
        if (const char* tm = std::getenv("MM_TRACE_MARBLE")) {
            int which = 0, from = 0, to = 0;
            std::sscanf(tm, "%d,%d,%d", &which, &from, &to);
            const auto& m = race.marbles[std::size_t(which)];
            if (k >= from && k <= to) {
                std::printf("%d: %d,%d,%d v %d,%d mode %d air %d zone %d lost %d\n", k, m.x >> 16, m.y >> 16, m.z >> 16,
                            m.vx >> 12, m.vy >> 12, int(m.mode), int(m.air), int(m.feature), int(m.respawns));
            }
        }
        // Marbles waiting to respawn for a long time (a respawn point kept
        // occupied).
        static std::array<int, game::Race::max_marbles> waiting{};
        for (int i = 0; i < game::Race::max_marbles; ++i) {
            const auto& m = race.marbles[std::size_t(i)];
            waiting[std::size_t(i)] = m.status == 1 && m.mode == game::Marble::respawning ? waiting[std::size_t(i)] + 1 : 0;
            if (waiting[std::size_t(i)] == 400) {
                std::printf("marble %d stuck respawning at %d,%d from update %d\n", i, m.x >> 16, m.y >> 16, k - 400);
            }
        }
        // Marbles a course piece holds for long (stuck in a funnel).
        static std::array<int, game::Race::max_marbles> held{};
        for (int i = 0; i < game::Race::max_marbles; ++i) {
            const auto& m = race.marbles[std::size_t(i)];
            held[std::size_t(i)] = m.status == 1 && m.region_code != 0 ? held[std::size_t(i)] + 1 : 0;
            if (held[std::size_t(i)] == 300) {
                std::printf("marble %d held by region code %d at %d,%d mode %d from update %d\n", i, int(m.region_code), m.x >> 16,
                            m.y >> 16, int(m.mode), k - 300);
            }
        }
        // MM_VACUUMS=1: while a vacuum sucks, how near the nearest crowd
        // marble comes (pixels; every 20th such update).
        if (std::getenv("MM_VACUUMS")) {
            static int sucking = 0;
            for (const auto& r : race.regions().all()) {
                if (!r.active || r.state == 0 || (r.code != 11 && r.code != 13)) continue;
                if (r.anim_frames != race.regions().block_entry(r.code == 13 ? 0x19 : 0x18)) continue;
                int best = 9999;
                for (int i = 1; i < game::Race::max_marbles; ++i) {
                    const auto& m = race.marbles[std::size_t(i)];
                    if (m.status == 1) best = std::min(best, std::abs((m.x - r.x) >> 16) + std::abs((m.y - r.y) >> 16));
                    // In its reach ($1170E8's box, as touch_regions tests it).
                    const int dx = std::int16_t((r.x >> 16) - (m.x >> 16)), dy = std::int16_t((r.y >> 16) - (m.y >> 16));
                    const int a = r.code == 13 ? dy : dx, b = r.code == 13 ? dx : dy;
                    static std::array<int, game::Race::max_marbles> in_reach{};
                    if (m.status == 1 && a >= -0xC && a <= 0x1B && b >= -0x20 && b <= -9) {
                        if (in_reach[std::size_t(i)]++ == 0) std::printf("  marble %d in the vacuum's reach at %d (mode %d, z %d, vacuum z %d, air %d)\n", i, k, int(m.mode), m.z >> 16, r.z >> 16, int(m.air));
                    } else {
                        in_reach[std::size_t(i)] = 0;
                    }
                }
                if (sucking++ % 20 == 0) std::printf("vacuum code %d at %d,%d sucking at %d: nearest crowd marble %d\n", r.code, r.x >> 16, r.y >> 16, k, best);
            }
        }
        // Crowd marbles lingering by a funnel's way in (MM_FUNNEL_WAIT).
        if (std::getenv("MM_FUNNEL_WAIT")) {
            static std::array<int, game::Race::max_marbles> by{};
            for (int i = 1; i < game::Race::max_marbles; ++i) {
                const auto& m = race.marbles[std::size_t(i)];
                const bool near = m.status == 1 && race.crowd_map()->funnel_exit(m.x >> 19, m.y >> 19).has_value();
                by[std::size_t(i)] = near ? by[std::size_t(i)] + 1 : 0;
                if (by[std::size_t(i)] == 60) std::printf("marble %d by a funnel at %d,%d for 60 updates (update %d)\n", i, m.x >> 16, m.y >> 16, k);
            }
        }
        // Crowd marbles that stay where they are (within 4 pixels) for 300
        // updates.
        static std::array<std::pair<int, int>, game::Race::max_marbles> anchor{};
        static std::array<int, game::Race::max_marbles> still{};
        for (int i = 1; i < game::Race::max_marbles; ++i) {
            const auto& m = race.marbles[std::size_t(i)];
            const int x = m.x >> 16, y = m.y >> 16;
            auto& a = anchor[std::size_t(i)];
            if (m.status != 1 || std::abs(x - a.first) > 4 || std::abs(y - a.second) > 4) {
                a = {x, y};
                still[std::size_t(i)] = 0;
            } else if (++still[std::size_t(i)] == 300) {
                std::printf("marble %d still at %d,%d zone %d mode %d from update %d\n", i, x, y, int(m.feature), int(m.mode), k - 300);
            }
        }
        // The pack against the player: each racing crowd marble's lead (the
        // player's distance to the goal less its own, map units), summed.
        if (race.marbles[0].status == 1 && race.crowd_map()) {
            const auto mine = race.crowd_map()->to_goal(race.marbles[0].x >> 19, race.marbles[0].y >> 19);
            for (int i = 1; i < game::Race::max_marbles; ++i) {
                const auto& m = race.marbles[std::size_t(i)];
                const auto theirs = race.crowd_map()->to_goal(m.x >> 19, m.y >> 19);
                if (m.status == 1 && mine != game::CrowdMap::none && theirs != game::CrowdMap::none) {
                    const int lead = int(mine) - int(theirs);
                    lead_sum += lead;
                    ++lead_count;
                    if (lead > 400) ++far_ahead;
                    if (lead < -400) ++far_behind;
                }
            }
        }
        // MM_MISTAKES=n: the player loses its marble every n updates (a
        // player's mistakes: back to its zone's respawn point).
        if (const char* mk = std::getenv("MM_MISTAKES"); mk && k > 0 && k % std::atoi(mk) == 0) {
            auto& p0 = race.marbles[0];
            if (p0.status == 1 && p0.mode == game::Marble::rolling) {
                p0.delay = 0x65;
                p0.mode = game::Marble::respawning;
                ++p0.respawns;
            }
        }
        std::array<game::Marble, game::Race::max_marbles> was = race.marbles;
        race.update({game::Push{}, game::Push{}});
        view.update(race);
        for (int i = 0; i < game::Race::max_marbles; ++i) {
            const auto& m = race.marbles[std::size_t(i)];
            // Where marbles get lost: the point it was at when it began to
            // break or respawn.
            if (m.status == 1 && (k % 3) == 0) trail.push_back({m.x >> 16, m.y >> 16, m.z >> 16});
            if (m.mode == game::Marble::captured && was[std::size_t(i)].mode != game::Marble::captured) {
                ++captures[std::size_t(i)];
            }
            if (m.respawns > was[std::size_t(i)].respawns) {
                const auto& w = was[std::size_t(i)];
                lost_points[{w.x >> 16, w.y >> 16, w.z >> 16}]++;
                // What was near: the nearest steelie or slinky.
                int near = 9999;
                for (const auto& st : race.steelies) {
                    if (st.body.status != 0) near = std::min(near, std::abs((st.body.x - w.x) >> 16) + std::abs((st.body.y - w.y) >> 16));
                }
                for (const auto& sl : race.slinkies) {
                    if (sl.status != 0) near = std::min(near, std::abs((sl.x - w.x) >> 16) + std::abs((sl.y - w.y) >> 16));
                }
                causes[std::to_string(int(w.mode)) + (near < 24 ? " creature" : "")]++;
                losses[{int(was[std::size_t(i)].feature), (was[std::size_t(i)].x >> 16) / 16, (was[std::size_t(i)].y >> 16) / 16}]++;
            }
            if (finished[std::size_t(i)] < 0 && ((i > 0 && before[std::size_t(i)] == 1 && m.status == 0) || (i == 0 && m.status == 3))) {
                finished[std::size_t(i)] = k;
                // The player's controls must still reach marble 0: pushed and
                // not pushed, two copies must part.
                const auto& p0 = race.marbles[0];
                if (i > 0 && p0.status == 1 && p0.mode == game::Marble::rolling && p0.air == 0 && p0.region_code == 0) {
                    game::Race pushed = race, idle_copy = race;
                    pushed.bot_drives_player = idle_copy.bot_drives_player = false;
                    for (int t = 0; t < 8; ++t) {
                        pushed.update({game::Push{0x40, 0x40}, game::Push{}});
                        idle_copy.update({game::Push{}, game::Push{}});
                    }
                    const bool same = pushed.marbles[0].vx == idle_copy.marbles[0].vx && pushed.marbles[0].vy == idle_copy.marbles[0].vy;
                    std::printf("marble %d home at %d: the player's push %s\n", i, k, same ? "IGNORED" : "works");
                }
            }
        }
    }
    int home = 0;
    for (int i = 0; i < game::Race::max_marbles; ++i) {
        const auto& m = race.marbles[std::size_t(i)];
        home += finished[std::size_t(i)] >= 0;
        std::printf("marble %d: %s, lost %d times, zone %d at %d,%d\n", i,
                    finished[std::size_t(i)] >= 0 ? ("home at " + std::to_string(finished[std::size_t(i)])).c_str() : "not home",
                    respawns[std::size_t(i)], int(m.feature), m.x >> 16, m.y >> 16);
        (void)lost;
    }
    std::printf("%d of 8 home\n", home);
    if (lead_count > 0) {
        std::printf("  pack lead: mean %ld, far ahead %.0f%%, far behind %.0f%%\n", lead_sum / lead_count,
                    100.0 * far_ahead / lead_count, 100.0 * far_behind / lead_count);
    }
    // MM_LOSS_MAP=file: the course with a dot where marbles were lost (the
    // projection the view uses: rows from the course's first view).
    if (const char* map = std::getenv("MM_LOSS_MAP")) {
        auto image = data::course_image(assets, race.course());
        const int view0 = race.terrain().view_offset();
        for (const auto& [where, count] : lost_points) {
            const auto [x, y, z] = where;
            const int ix = y - x + 0x88 + 4, iy = 0x93 + 7 - view0 - z + (x + y) / 2;
            const int r = std::min(1 + count, 6);
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy <= r * r) image.put(ix + dx, iy + dy, platform::Color{255, 0, 255, 255});
                }
            }
        }
        for (const auto& [x, y, z] : trail) {
            const int ix = y - x + 0x88 + 4, iy = 0x93 + 7 - view0 - z + (x + y) / 2;
            image.put(ix, iy, platform::Color{0, 255, 0, 255});
        }
        platform::save_png(image, platform::utf8_path(map));
    }
    for (const auto& [c, n] : causes) std::printf("  mode before loss %s: %d\n", c.c_str(), n);
    {
        int total = 0;
        for (int i = 1; i < game::Race::max_marbles; ++i) total += captures[std::size_t(i)];
        std::printf("  crowd taken by vacuums: %d (marble 0: %d)\n", total, captures[0]);
    }
    std::vector<std::pair<int, std::tuple<int, int, int>>> worst;
    for (const auto& [where, count] : losses) worst.push_back({count, where});
    std::sort(worst.rbegin(), worst.rend());
    for (std::size_t i = 0; i < worst.size() && i < 8; ++i) {
        const auto [zone, x, y] = worst[i].second;
        std::printf("  lost %d times in zone %d near %d,%d\n", worst[i].first, zone, x * 16 + 8, y * 16 + 8);
    }
    return 0;
}

// Runs a recorder script on the rebuilt race and prints the marble's mode
// changes (for finding out what a run does).
int trace(int argc, char** argv) {
    data::GameFiles files(platform::utf8_path(argv[2]));
    game::Race race(files, data::Course(std::stoi(argv[3])));
    if (const char* t = std::getenv("MM_TIME")) race.marbles[0].time = std::int16_t(std::atoi(t));
    const auto text = platform::read_file(platform::utf8_path(argv[4]));
    std::string all(text.begin(), text.end());
    std::vector<std::pair<int, std::string>> steps;
    for (std::size_t at = 0; at < all.size();) {
        const auto end = all.find('\n', at);
        const auto line = all.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? all.size() : end + 1;
        const auto space = line.find(' ');
        if (space != std::string::npos) steps.emplace_back(std::stoi(line.substr(0, space)), line.substr(space + 1));
    }
    const int limit = argc > 5 ? std::stoi(argv[5]) : 2000;
    // MM_START=x,y[,scroll[,z]]: start the marble elsewhere (research),
    // with the view scrolled that many updates first.
    if (const char* st = std::getenv("MM_START")) {
        int x = 0, y = 0, lines = 0, z = 0x4000;
        std::sscanf(st, "%d,%d,%d,%d", &x, &y, &lines, &z);
        auto& m = race.marbles[0];
        for (int i = 0; i < lines; ++i) {
            m.screen[0] = 0x00800000 | 0x10; // low on the screen: the view scrolls down
            race.update({game::Push{}, game::Push{}});
        }
        m.x = x << 16;
        m.y = y << 16;
        m.vx = m.vy = m.vz = 0;
        m.air = 0;
        m.mode = game::Marble::rolling;
        m.fall_start_z = m.z = z << 16;
    }
    // MM_CHEATS=timer,falls: try the pause menu's cheats.
    if (const char* cheats = std::getenv("MM_CHEATS")) {
        race.cheats.stop_timer = std::strstr(cheats, "timer") != nullptr;
        race.cheats.safe_falls = std::strstr(cheats, "falls") != nullptr;
    }
    // MM_PICTURES=dir,first: also write the view after each update from
    // `first` on (native-<update>.png).
    std::unique_ptr<data::Assets> assets;
    std::unique_ptr<game::View> view;
    std::filesystem::path pictures;
    int first_picture = 0;
    if (const char* pic = std::getenv("MM_PICTURES")) {
        std::string spec(pic);
        const auto comma = spec.find(',');
        pictures = platform::utf8_path(spec.substr(0, comma));
        first_picture = comma == std::string::npos ? 0 : std::stoi(spec.substr(comma + 1));
        assets = std::make_unique<data::Assets>(data::load_assets(platform::utf8_path(argv[2])));
        view = std::make_unique<game::View>(files, *assets, race);
        view->update(race);
    }
    int update = 0;
    std::string last;
    for (const auto& [count, keys] : steps) {
        for (int i = 0; i < count && update < limit; ++i, ++update) {
            const bool u = keys.find('U') != std::string::npos, d = keys.find('D') != std::string::npos;
            const bool l = keys.find('L') != std::string::npos, r = keys.find('R') != std::string::npos;
            const int fire = keys.find('F') != std::string::npos ? 2 : 1;
            race.update({game::Push{std::int16_t(((d ? 0x20 : 0) - (u ? 0x20 : 0)) * fire),
                                    std::int16_t(((l ? 0x20 : 0) - (r ? 0x20 : 0)) * fire)},
                         game::Push{}});
            if (view) {
                view->update(race);
                if (update >= first_picture) {
                    platform::save_png(view->draw(race), pictures / ("native-" + std::to_string(update) + ".png"));
                }
            }
            if (std::getenv("MM_REGION_CODES")) {
                static std::set<std::pair<int, int>> seen;
                for (const auto& r : race.regions().all()) {
                    if (r.active && seen.insert({r.code, int(r.script)}).second) {
                        std::printf("region code %d kind %d script %06x at %d,%d (update %d)\n", r.code, r.kind, r.script,
                                    r.x >> 16, r.y >> 16, update);
                    }
                }
            }
            const auto& m = race.marbles[0];
            char line[160];
            std::snprintf(line, sizeof line, "mode %d air %d zone %d region %d status %d score %d time %d sy %d", m.mode,
                          m.air, m.feature, m.region_code, m.status, m.score, m.time, std::int16_t(m.screen[0]));
            if (line != last) {
                std::printf("%5d %-3s at %4d,%4d z %5d: %s\n", update, keys.c_str(), m.x >> 16, m.y >> 16, m.z >> 16, line);
                last = line;
            }
        }
    }
    return 0;
}

// Finds joystick input that takes the marble to the goal: a beam search
// over directions held for `hold` updates, scoring the zone reached, the
// depth down the course and respawns. Writes a recorder script
// (harness/mmref.py: "count keys" lines) and prints how far it got.
// Replays a run's pushes with sound: prints the channels' requests per
// update in the format of mmref.py --audio (sample addresses as hunks of
// the course's sound file) and writes the sound as a WAV file. Given the
// original's request log, each update lasts as many vertical blanks as it
// did there (the original slows down now and then; the port does not).
//
//   marble_madness_replay audio <content> <run.bin> <out.wav> [original-audio.txt]
int audio(int argc, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    const auto run = platform::read_file(platform::utf8_path(argv[3]));
    const auto wav = platform::utf8_path(argv[4]);
    data::GameFiles files(content);
    const auto course = data::Course(std::getenv("MM_COURSE") ? std::stoi(std::getenv("MM_COURSE")) : 0);
    const auto random = race_random();
    game::Race race(files, course, 1, 0, &random);
    race.regions().forced_render_times = logged_render_times(argv[3], run);
    const bool counts = has_counts(run);
    sound::SoundBoard board(files, course);
    std::vector<amiga::Paula::Request> log;
    board.log_requests(&log);
    const auto& bank = files.sounds(course);
    std::vector<std::uint32_t> hunks;
    for (std::size_t n = 0;; ++n) {
        try {
            hunks.push_back(bank.hunk(n));
        } catch (const std::exception&) {
            break;
        }
    }
    auto name = [&](std::uint32_t a) {
        for (std::size_t n = hunks.size(); n-- > 0;) {
            if (hunks[n] <= a) {
                char text[32];
                std::snprintf(text, sizeof text, "h%zu+%x", n, a - hunks[n]);
                return std::string(text);
            }
        }
        return std::string("?");
    };
    auto flush_log = [&](long update) {
        for (const auto& r : log) {
            const auto& w = r.write;
            std::printf("%ld %d %x %x %s %u %u %u %d\n", update, r.command, 1u << r.channel,
                        r.command == 3 ? (w.set_period_volume ? 0x10 : 0) : 1,
                        r.command == 3 ? name(w.data).c_str() : "-", r.command == 3 ? w.length : 0, w.period,
                        w.volume, w.cycles);
        }
        log.clear();
    };
    constexpr int rate = 48000;
    std::vector<float> samples;
    std::map<long, long> blank_at; // update -> the original's vertical blank count at its start
    if (argc > 5) {
        std::ifstream in(argv[5]);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream words(line);
            long update = 0, blank = 0;
            std::string kind, vbl;
            if (words >> update >> kind >> vbl >> blank && kind == "tick") {
                blank_at[update] = blank;
            }
        }
    }
    auto blanks_of = [&](long update) {
        const auto a = blank_at.find(update), b = blank_at.find(update + 1);
        return a != blank_at.end() && b != blank_at.end() ? int(b->second - a->second) : 2;
    };
    const std::size_t start = race_start(run);
    const std::size_t frames = run.size() / record_size;
    take_start(race, run.data() + start * record_size + players_at);
    for (const auto& e : race.take_sounds()) {
        board.play(e.id, e.right);
    }
    flush_log(long(start) - 1);
    for (std::size_t k = start; k < frames; ++k) {
        if (repeated(run, k + 1)) continue;
        const auto* rec = run.data() + k * record_size;
        follow_count(race, run, k, counts);
        race.update({game::Push{be16(rec + forces_at), be16(rec + forces_at + 4)}, game::Push{}});
        for (const auto& e : race.take_sounds()) {
            board.play_after_blank(e.id, e.right);
        }
        std::vector<float> chunk(std::size_t(rate / 50 * blanks_of(long(k))) * 2);
        board.render(chunk, rate);
        samples.insert(samples.end(), chunk.begin(), chunk.end());
        flush_log(long(k));
    }
    // 16-bit stereo PCM.
    std::vector<std::uint8_t> out;
    auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i))); };
    auto u16 = [&](std::uint16_t v) { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); };
    const auto bytes = std::uint32_t(samples.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    u32(36 + bytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    u32(bytes);
    for (const float v : samples) {
        u16(std::uint16_t(std::int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
    }
    platform::write_file(wav, out);
    return 0;
}

int search(int argc, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    const auto course = data::Course(std::stoi(argv[3]));
    const auto out = platform::utf8_path(argv[4]);
    const int limit = argc > 5 ? std::stoi(argv[5]) : 3000;
    // MM_SEARCH_HOLD=n: updates per step (4).
    const int hold = std::getenv("MM_SEARCH_HOLD") ? std::atoi(std::getenv("MM_SEARCH_HOLD")) : 4;
    const int width = argc > 6 ? std::stoi(argv[6]) : 256;
    data::GameFiles files(content);
    struct Candidate {
        game::Race race;
        std::string keys; // one character per hold step: direction 0-8
        double score{};
        std::size_t stage{}; // MM_SEARCH_PLAN: waypoints passed
        int picture{-1};     // the moving area's picture last seen
    };
    // MM_SEARCH_PLAN=file: lines "picture x y": be near that point (course
    // pixels) when Ultimate's moving goal area turns to that picture, one
    // line after the other; then head for the goal at MM_SEARCH_TARGET.
    struct Waypoint {
        int picture, x, y;
    };
    std::vector<Waypoint> plan;
    if (const char* pf = std::getenv("MM_SEARCH_PLAN")) {
        std::ifstream in(pf);
        Waypoint w{};
        while (in >> w.picture >> w.x >> w.y) plan.push_back(w);
    }
    // Directions 0-8; 9-16 are 1-8 with fire (twice the push), with
    // MM_SEARCH_FIRE=1.
    static const char* names[] = {"-", "U", "D", "L", "R", "UL", "UR", "DL", "DR",
                                  "UF", "DF", "LF", "RF", "ULF", "URF", "DLF", "DRF"};
    const int directions = std::getenv("MM_SEARCH_FIRE") ? 17 : 9;
    auto push_of = [](int d) {
        const int fire = d > 8 ? 2 : 1;
        if (d > 8) d -= 8;
        const bool u = d == 1 || d == 5 || d == 6, dn = d == 2 || d == 7 || d == 8;
        const bool l = d == 3 || d == 5 || d == 7, r = d == 4 || d == 6 || d == 8;
        return game::Push{std::int16_t(((dn ? 0x20 : 0) - (u ? 0x20 : 0)) * fire),
                          std::int16_t(((l ? 0x20 : 0) - (r ? 0x20 : 0)) * fire)};
    };
    // MM_SEARCH_DROP=1: look for Practice's drop bonus instead of the goal.
    const bool drop = std::getenv("MM_SEARCH_DROP") != nullptr;
    // MM_SEARCH_TARGET=x,y: get near that point (course pixels) instead.
    int target_x = -1, target_y = -1;
    if (const char* t = std::getenv("MM_SEARCH_TARGET")) {
        std::sscanf(t, "%d,%d", &target_x, &target_y);
    }
    // MM_SEARCH_ZONES=1: per zone, head for the middle of the line into the
    // neighbouring zone nearest the goal (zone -1) in the Track's zone
    // graph (lines at header +8); zones score by that distance.
    std::map<int, std::pair<double, double>> zone_target;
    std::map<int, int> zone_rank;
    if (std::getenv("MM_SEARCH_ZONES")) {
        const auto& t = files.track(course);
        struct Line { int a, b; std::pair<double, double> mid; };
        std::vector<Line> lines;
        for (std::uint32_t e = t.u32(files.track_header(course) + 8);; e += 5) {
            const int a = t.s8(e), b = t.s8(e + 1), end = t.s8(e + 2), z1 = t.s8(e + 3), z2 = t.s8(e + 4);
            if (z1 == -1 && z2 == -1) break;
            const bool along_x = a < 0;
            const int fixed = along_x ? std::abs(b) : std::abs(a), from = along_x ? std::abs(a) : std::abs(b);
            const double mid = (from + end + 1) * 4.0, at = fixed * 8.0 + 4.0;
            lines.push_back({z1, z2, along_x ? std::pair{mid, at} : std::pair{at, mid}});
            if (std::getenv("MM_ZONE_DUMP")) std::fprintf(stderr, "line %d %d %d zones %d %d\n", a, b, end, z1, z2);
        }
        zone_rank[-1] = 0;
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& l : lines) {
                for (auto [from, to] : {std::pair{l.a, l.b}, std::pair{l.b, l.a}}) {
                    if (zone_rank.contains(to) && (!zone_rank.contains(from) || zone_rank[from] > zone_rank[to] + 1)) {
                        zone_rank[from] = zone_rank[to] + 1;
                        zone_target[from] = l.mid;
                        changed = true;
                    }
                }
            }
        }
    }
    if (std::getenv("MM_ZONE_DUMP")) {
        for (const auto& [z, r] : zone_rank) {
            const auto t = zone_target.find(z);
            std::fprintf(stderr, "zone %d rank %d target %.0f,%.0f\n", z, r, t == zone_target.end() ? -1.0 : t->second.first,
                         t == zone_target.end() ? -1.0 : t->second.second);
        }
    }
    // MM_SEARCH_ZONE_TARGET=z:x,y: another target for zone z.
    if (const char* zt = std::getenv("MM_SEARCH_ZONE_TARGET")) {
        int z = 0, x = 0, y = 0;
        if (std::sscanf(zt, "%d:%d,%d", &z, &x, &y) == 3) zone_target[z] = {double(x), double(y)};
    }
    // MM_SEARCH_PATH=x,y: head for that point along the floor: a flood
    // fill over the 8 x 8 cells from it, stepping to a neighbour whose edge
    // is at most a little higher (no walls) and not far lower (no deep
    // drops); the score is the number of cells still to go.
    constexpr int grid = 160;
    game::Terrain terrain(files, course);
    // MM_MAP_PATCH=address: first apply that patch of the animated heights
    // (Ultimate's moving goal area: one per picture).
    if (const char* mp = std::getenv("MM_MAP_PATCH")) {
        const auto& track = files.track(course);
        const std::uint32_t patch = std::uint32_t(std::stoul(mp, nullptr, 16));
        const int first = track.s16(patch), n = track.s16(patch + 2);
        for (int i = 0; i < n; ++i) {
            if (std::size_t(first + i) < terrain.animated.size()) {
                terrain.animated[std::size_t(first + i)] = track.u16(patch + 4 + std::uint32_t(i) * 2);
            }
        }
    }
    game::Probe probe;
    std::vector<std::int32_t> ground(grid * grid, 0);
    // The game's own side analysis ($10F390) at a cell's middle: side 0 the
    // x = 0 edge, 1 y = 8, 2 x = 8, 3 y = 0; kinds 1-2 a wall up, 3-4 a
    // drop (height relative to the point). Also notes the floor's height.
    auto sides = [&](int cx, int cy, std::array<std::int8_t, 4>& kind, std::array<std::int16_t, 4>& height) {
        probe.place((cx * 8 + 4) << 16, (cy * 8 + 4) << 16);
        const std::int32_t z = probe.ground_height_here(terrain);
        if (cx >= 0 && cy >= 0 && cx < grid && cy < grid) ground[std::size_t(cy * grid + cx)] = z >> 16;
        if (z == 0) return false;
        probe.locate(terrain, (cx * 8 + 4) << 16, (cy * 8 + 4) << 16, z);
        probe.ground_height();
        probe.analyse_sides();
        kind = probe.side_kind;
        height = probe.side_height;
        return true;
    };
    // A flood over the 8 x 8 cells from a point: a step to a neighbour
    // whose side towards the cell has no wall higher than `rise` and whose
    // floor is not more than `fall` above (the marble would drop that far).
    // Cells to go per cell, -1 where the point cannot be reached.
    auto flood = [&](int px, int py, int rise, int fall, int climb) {
        std::vector<int> distance(grid * grid, -1);
        std::vector<std::pair<int, int>> queue{{px >> 3, py >> 3}};
        distance[std::size_t((py >> 3) * grid + (px >> 3))] = 0;
        {
            std::array<std::int8_t, 4> kind{};
            std::array<std::int16_t, 4> height{};
            sides(px >> 3, py >> 3, kind, height);
        }
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const auto [ax, ay] = queue[q];
            // Neighbour b and the side of b that faces a.
            const int dirs[4][3] = {{1, 0, 0}, {-1, 0, 2}, {0, 1, 3}, {0, -1, 1}};
            for (const auto& d : dirs) {
                const int bx = ax + d[0], by = ay + d[1];
                if (bx < 0 || by < 0 || bx >= grid || by >= grid || distance[std::size_t(by * grid + bx)] >= 0) continue;
                std::array<std::int8_t, 4> kind{};
                std::array<std::int16_t, 4> height{};
                if (!sides(bx, by, kind, height)) continue;
                const int k = kind[std::size_t(d[2])], h = height[std::size_t(d[2])];
                const int za = ground[std::size_t(ay * grid + ax)], zb = ground[std::size_t(by * grid + bx)];
                if ((k == 1 || k == 2) && h > rise) continue;
                if (za - zb > climb || zb - za > fall) continue;
                distance[std::size_t(by * grid + bx)] = distance[std::size_t(ay * grid + ax)] + 1;
                queue.push_back({bx, by});
            }
        }
        std::fprintf(stderr, "path: %zu cells reach %d,%d\n", queue.size(), px, py);
        return distance;
    };
    auto cells_to_go = [](const std::vector<int>& distance, const game::Marble& m) {
        const int cx = (m.x >> 16) >> 3, cy = (m.y >> 16) >> 3;
        const int d = cx >= 0 && cy >= 0 && cx < grid && cy < grid ? distance[std::size_t(cy * grid + cx)] : -1;
        return d < 0 ? 5000 : d;
    };
    // MM_SEARCH_PATH=x,y[,rise,fall,climb]: head for that point along the
    // floor (climb: the most a floor may rise from cell to cell, rise + 8);
    // the score is the number of cells still to go.
    std::vector<int> path_distance;
    if (const char* pt = std::getenv("MM_SEARCH_PATH")) {
        int px = 0, py = 0, rise = 4, fall = 48, climb = -1;
        std::sscanf(pt, "%d,%d,%d,%d,%d", &px, &py, &rise, &fall, &climb);
        path_distance = flood(px, py, rise, fall, climb < 0 ? rise + 8 : climb);
        if (std::getenv("MM_ANIMATED")) {
            for (int vy = 0; vy < grid; ++vy) {
                for (int vx = 0; vx < grid; ++vx) {
                    const auto c = terrain.stored_code(vx, vy);
                    if (c >= 0x800 && c < 0x1000) std::fprintf(stderr, "animated %d,%d code %x\n", vx, vy, c);
                }
            }
        }
        if (const char* sc = std::getenv("MM_SIDES")) {
            int cx = 0, cy = 0;
            std::sscanf(sc, "%d,%d", &cx, &cy);
            for (int y = cy - 1; y <= cy + 1; ++y) {
                for (int x = cx - 1; x <= cx + 1; ++x) {
                    std::array<std::int8_t, 4> kind{};
                    std::array<std::int16_t, 4> height{};
                    const bool g = sides(x, y, kind, height);
                    std::fprintf(stderr, "cell %d,%d ground %d z %d sides", x, y, g, probe.z());
                    for (int k = 0; k < 4; ++k) std::fprintf(stderr, " %d:%d", kind[k], height[k]);
                    std::fprintf(stderr, "\n");
                }
            }
        }
        // MM_PATH_MAP=file: per cell "height:cells to go" as text.
        if (const char* map = std::getenv("MM_PATH_MAP")) {
            std::ofstream out(map);
            for (int cy = 0; cy < grid; ++cy) {
                for (int cx = 0; cx < grid; ++cx) {
                    if (ground[std::size_t(cy * grid + cx)] == 0) {
                        std::array<std::int8_t, 4> kind{};
                        std::array<std::int16_t, 4> height{};
                        sides(cx, cy, kind, height);
                    }
                    out << ground[std::size_t(cy * grid + cx)] << ":" << path_distance[std::size_t(cy * grid + cx)] << " ";
                }
                out << "\n";
            }
        }
    }
    // MM_SEARCH_ZONEPATH=rise,fall (with MM_SEARCH_ZONES): within a zone,
    // the floor distance to its target instead of the straight one.
    std::map<int, std::vector<int>> zone_distance;
    if (const char* zp = std::getenv("MM_SEARCH_ZONEPATH")) {
        int rise = 2, fall = 24, climb = -1;
        std::sscanf(zp, "%d,%d,%d", &rise, &fall, &climb);
        for (const auto& [zone, t] : zone_target) {
            zone_distance[zone] = flood(int(t.first), int(t.second), rise, fall, climb < 0 ? rise + 8 : climb);
        }
    }
    auto score_of = [drop, target_x, target_y, &zone_target, &zone_rank, &path_distance, &zone_distance,
                     &cells_to_go](const game::Race& race) {
        const auto& m = race.marbles[0];
        // MM_SEARCH_CODE=n: done once a region of that code takes the marble
        // (mode 3, the region timer just set).
        static const int done_code = std::getenv("MM_SEARCH_CODE") ? std::atoi(std::getenv("MM_SEARCH_CODE")) : -1;
        if (done_code >= 0 && m.region_code == done_code && m.mode == game::Marble::recoiling) return 1e9;
        // MM_SEARCH_MODE=n: done once the marble is in that mode.
        static const int done_mode = std::getenv("MM_SEARCH_MODE") ? std::atoi(std::getenv("MM_SEARCH_MODE")) : -1;
        static const int only_acid = std::getenv("MM_SEARCH_ACID") ? std::atoi(std::getenv("MM_SEARCH_ACID")) - 2 : -1;
        if (done_mode >= 0 && m.mode == done_mode) {
            return only_acid < 0 || m.region_code == only_acid ? 1e9 : -1e6;
        }
        // MM_SEARCH_CATCH=1: catch one of Silly's critters (heading for the
        // nearest).
        if (std::getenv("MM_SEARCH_CATCH")) {
            double best = 1e9;
            for (const auto& c : race.critters) {
                if (c.status == 0) continue;
                if (c.state == game::Critter::caught) return 1e9;
                const double dx = ((c.x - m.x) >> 16), dy = ((c.y - m.y) >> 16);
                best = std::min(best, std::sqrt(dx * dx + dy * dy));
            }
            if (best < 1e9) return -best - 20000.0 * m.respawns;
        }
        // MM_SEARCH_ACID=n: head for the nearest acid puddle (n = 1), or
        // for puddle n - 2.
        if (const char* acid = std::getenv("MM_SEARCH_ACID")) {
            const int only = std::atoi(acid) - 2;
            double best = 1e9;
            for (const auto& a : race.acids) {
                if (a.status == 0 || (only >= 0 && a.index != only)) continue;
                const double dx = ((a.x - m.x) >> 16), dy = ((a.y - m.y) >> 16);
                best = std::min(best, std::sqrt(dx * dx + dy * dy));
            }
            if (best < 1e9) return -best - 20000.0 * m.respawns;
        }
        if (m.status == 3) return 1e9;
        if (!path_distance.empty()) {
            const int d = cells_to_go(path_distance, m);
            if (d <= 1 && m.air == 0) return 1e9;
            return -double(d) * 8.0 - 20000.0 * m.respawns -
                   ((m.mode == game::Marble::respawning || m.air == 2 || m.mode == game::Marble::breaking) ? 2000.0 : 0.0);
        }
        if (!zone_target.empty()) {
            if (m.status == 3 || m.feature == -1) return 1e9;
            const auto t = zone_target.find(m.feature);
            double d = 0;
            if (const auto z = zone_distance.find(m.feature); z != zone_distance.end()) {
                d = cells_to_go(z->second, m) * 8.0;
            } else if (t != zone_target.end()) {
                const double dx = (m.x >> 16) - t->second.first, dy = (m.y >> 16) - t->second.second;
                d = std::sqrt(dx * dx + dy * dy);
            }
            const auto rank = zone_rank.find(m.feature);
            return -double(rank == zone_rank.end() ? 99 : rank->second) * 100000.0 - d - 3000.0 * m.respawns -
                   ((m.mode == game::Marble::respawning || m.air == 2 || m.mode == game::Marble::breaking) ? 2000.0 : 0.0);
        }
        if (target_x >= 0) {
            const double dx = (m.x >> 16) - target_x, dy = (m.y >> 16) - target_y;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d < 12 && m.air == 0) return 1e9;
            return -d - 20000.0 * m.respawns -
                   ((m.mode == game::Marble::respawning || m.air == 2 || m.mode == game::Marble::breaking) ? 2000.0 : 0.0);
        }
        if (drop) {
            if (race.drop_marks()[0].kind != -1) return 1e9;
            const int f = m.feature;
            return (f >= 1 && f <= 3 ? 200000.0 : f == 0 ? 0.0 : -100000.0) + double((m.x + m.y) >> 16) -
                   20000.0 * m.respawns -
                   ((m.mode == game::Marble::respawning || m.mode == game::Marble::breaking) ? 20000.0 : 0.0);
        }
        if (m.status == 3 || m.feature == -1) return 1e9;
        return double(std::uint8_t(m.feature)) * 100000.0 + double((m.x + m.y) >> 16) - 20000.0 * m.respawns -
               ((m.mode == game::Marble::respawning || m.air == 2 || m.mode == game::Marble::breaking) ? 20000.0 : 0.0);
    };
    // MM_SEARCH_PREFIX=script: play that script first and search on from
    // its end (the output starts with it).
    const auto random = race_random();
    game::Race first_race(files, course, 1, 0, &random);
    // MM_TIME=n: start with that many seconds.
    if (const char* t = std::getenv("MM_TIME")) first_race.marbles[0].time = std::int16_t(std::atoi(t));
    // MM_SEARCH_START=run.bin: the start fields of a recording of the
    // original (as the lockstep takes them over).
    if (const char* st = std::getenv("MM_SEARCH_START")) {
        const auto run = platform::read_file(platform::utf8_path(st));
        take_start(first_race, run.data() + race_start(run) * record_size + players_at);
    }
    // MM_SEARCH_RENDER=run.bin: the moving goal area's rendering times a
    // recording of the original logged (Ultimate).
    if (const char* rr = std::getenv("MM_SEARCH_RENDER")) {
        first_race.regions().forced_render_times =
            logged_render_times(rr, platform::read_file(platform::utf8_path(rr)));
        std::fprintf(stderr, "%zu rendering times\n", first_race.regions().forced_render_times.size());
    }
    // MM_SEARCH_COUNTS=run.bin: the original's update count from that
    // recording before each update (its main loop lags now and then; the
    // scroll looks every eighth count).
    std::vector<std::uint8_t> count_run;
    std::size_t count_start = 0;
    if (const char* cr = std::getenv("MM_SEARCH_COUNTS")) {
        count_run = platform::read_file(platform::utf8_path(cr));
        count_start = race_start(count_run);
    }
    auto follow = [&](game::Race& race, int update) {
        const std::size_t k = count_start + std::size_t(update);
        if (!count_run.empty() && (k + 1) * record_size <= count_run.size()) race.set_frame(counted_frames(count_run, k));
    };
    std::string prefix;
    int prefix_updates = 0;
    if (const char* pre = std::getenv("MM_SEARCH_PREFIX")) {
        const auto text = platform::read_file(platform::utf8_path(pre));
        std::istringstream in(std::string(text.begin(), text.end()));
        std::string line;
        while (std::getline(in, line)) {
            const auto space = line.find(' ');
            if (space == std::string::npos) continue;
            const int count = std::stoi(line.substr(0, space));
            const std::string keys = line.substr(space + 1);
            if (count >= 1000) break; // the idle tail
            int d = 0;
            for (int k = 0; k < 17; ++k) if (keys == names[k]) d = k;
            for (int i = 0; i < count; ++i) {
                follow(first_race, prefix_updates + i);
                first_race.update({push_of(d), game::Push{}});
                // MM_SEARCH_TRACE=n: print the marble after each prefix update from n on.
                if (const char* tr = std::getenv("MM_SEARCH_TRACE"); tr && prefix_updates + i >= std::atoi(tr)) {
                    const auto& m = first_race.marbles[0];
                    std::printf("%d %s at %d,%d z %d mode %d air %d vz %d zone %d picture %d status %d\n", prefix_updates + i, names[d],
                                m.x >> 16, m.y >> 16, m.z >> 16, m.mode, m.air, m.vz, m.feature,
                                first_race.regions().cover_set(), m.status);
                }
            }
            prefix += line + "\n";
            prefix_updates += count;
        }
    }
    std::fprintf(stderr, "after the prefix: picture %d (row %d)\n", first_race.regions().cover_set(),
                 first_race.regions().picture_shown_row());
    const int first_respawns = first_race.marbles[0].respawns;
    // MM_PLAN_SLACK=n: how near a waypoint counts (12).
    const int plan_slack = std::getenv("MM_PLAN_SLACK") ? std::atoi(std::getenv("MM_PLAN_SLACK")) : 12;
    std::vector<Candidate> beam{{first_race, "", 0}};
    // The countdown: the marble is held until the start gate opens.
    for (int step = 0; step * hold < limit; ++step) {
        std::vector<Candidate> next;
        for (const auto& c : beam) {
            for (int d = 0; d < directions; ++d) {
                Candidate n{c.race, c.keys + char('0' + d), 0, c.stage, c.picture};
                bool dead = false;
                for (int i = 0; i < hold; ++i) {
                    follow(n.race, prefix_updates + step * hold + i);
                    n.race.update({push_of(d), game::Push{}});
                    if (plan.empty()) continue;
                    const int shown = n.race.regions().picture_shown_row();
                    if (shown == n.picture) continue;
                    n.picture = shown;
                    if (n.stage < plan.size() && n.race.regions().cover_set() == plan[n.stage].picture) {
                        const auto& m = n.race.marbles[0];
                        if (std::abs((m.x >> 16) - plan[n.stage].x) <= plan_slack &&
                            std::abs((m.y >> 16) - plan[n.stage].y) <= plan_slack) {
                            ++n.stage;
                        }
                    }
                }
                // A fall from the blocks breaks the marble: not a way.
                if (!plan.empty() && n.race.marbles[0].respawns > first_respawns) dead = true;
                if (dead) continue;
                n.score = score_of(n.race);
                if (!plan.empty()) {
                    const auto& m = n.race.marbles[0];
                    const bool done = n.stage >= plan.size();
                    const double tx = done ? target_x : plan[n.stage].x, ty = done ? target_y : plan[n.stage].y;
                    double dx = (m.x >> 16) - tx, dy = (m.y >> 16) - ty;
                    if (!done) {
                        // Where the marble will be when the picture changes.
                        const double ahead = n.race.regions().picture_due_in();
                        dx += m.vx / 65536.0 * ahead;
                        dy += m.vy / 65536.0 * ahead;
                    }
                    n.score = -double(plan.size() - n.stage) * 100000.0 - std::sqrt(dx * dx + dy * dy) -
                              20000.0 * m.respawns - (m.air == 2 ? 2000.0 : 0.0);
                    if (m.status == 3 || (done && std::sqrt(dx * dx + dy * dy) < 6 && m.air == 0)) n.score = 1e9;
                }
                next.push_back(std::move(n));
            }
        }
        if (next.empty()) {
            std::printf("update %d: no way left\n", prefix_updates + step * hold);
            break;
        }
        std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
        if (next.size() > std::size_t(width)) next.erase(next.begin() + width, next.end());
        beam = std::move(next);
        const auto& best = beam.front();
        if (step % 25 == 0 || best.score >= 1e9) {
            const auto& m = best.race.marbles[0];
            std::printf("update %d: stage %zu zone %d mode %d air %d z %d at %d,%d respawns %d time %d\n", prefix_updates + (step + 1) * hold, best.stage, m.feature, m.mode, m.air, m.z >> 16,
                        m.x >> 16, m.y >> 16, m.respawns, m.time);
        }
        if (best.score >= 1e9) break;
    }
    const auto& best = beam.front();
    std::string script = prefix;
    for (std::size_t i = 0; i < best.keys.size();) {
        std::size_t j = i;
        while (j < best.keys.size() && best.keys[j] == best.keys[i]) ++j;
        script += std::to_string((j - i) * hold) + " " + names[best.keys[i] - '0'] + "\n";
        i = j;
    }
    script += "1000 -\n";
    platform::write_file(out, std::span(reinterpret_cast<const std::uint8_t*>(script.data()), script.size()));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "starts" && argc >= 3) {
            // Research: each course's start cells (descriptor +$14, a word
            // per player) and the floor height there.
            data::GameFiles files(platform::utf8_path(argv[2]));
            for (int c = 0; c < data::course_count; ++c) {
                const auto& t = files.track(data::Course(c));
                const std::uint32_t d = t.u32(files.track_header(data::Course(c)));
                game::Terrain terrain(files, data::Course(c));
                game::Probe probe;
                std::printf("course %d:", c);
                for (int p = 0; p < 2; ++p) {
                    const std::uint16_t w = t.u16(d + 0x14 + std::uint32_t(p) * 2);
                    const int x = (w >> 8) * 8 + 4, y = (w & 0xFF) * 8 + 4;
                    probe.place(x << 16, y << 16);
                    std::printf(" player %d cell %d,%d (%d,%d) z %d", p, w >> 8, w & 0xFF, x, y,
                                probe.ground_height_here(terrain) >> 16);
                }
                std::printf("\n");
            }
            return 0;
        }
        if (command == "words" && argc >= 6) {
            // Research: words of a course's loaded Track at an address.
            data::GameFiles files(platform::utf8_path(argv[2]));
            const auto& track = files.track(data::Course(std::stoi(argv[3])));
            const std::uint32_t at = std::uint32_t(std::stoul(argv[4], nullptr, 16));
            const int count = std::stoi(argv[5]);
            for (int i = 0; i < count; ++i) {
                std::printf("%s%04x", i % 16 == 0 ? (i ? "\n" : "") : " ", track.u16(at + std::uint32_t(i) * 2));
            }
            std::printf("\n");
            return 0;
        }
        if (command == "ground" && argc >= 7) {
            // Research: the height map's cell heights (corner 3 of each
            // vertex) around cell x, y.
            data::GameFiles files(platform::utf8_path(argv[2]));
            game::Terrain terrain(files, data::Course(std::stoi(argv[3])));
            const int cx = std::stoi(argv[4]), cy = std::stoi(argv[5]), r = std::stoi(argv[6]);
            for (int y = cy - r; y <= cy + r; ++y) {
                std::printf("y%3d", y);
                for (int x = cx - r; x <= cx + r; ++x) {
                    std::printf(" %5d", terrain.vertex(x, y).height[1]);
                }
                std::printf("\n");
            }
            return 0;
        }
        if (command == "cell" && argc >= 4) {
            // Research: a marble-library cell as text (colour indices).
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            const auto& cell = assets.marble.cells.at(std::size_t(std::stoul(argv[3], nullptr, 16)));
            std::printf("%d x %d\n", cell.width, cell.height);
            for (int y = 0; y < cell.height; ++y) {
                for (int x = 0; x < cell.width; ++x) std::printf("%d", cell.pixels[std::size_t(y * cell.width + x)]);
                std::printf("\n");
            }
            return 0;
        }
        if (command == "audio" && argc >= 5) {
            return audio(argc, argv);
        }
        if (command == "lockstep" && argc >= 4) {
            return lockstep(argc, argv);
        }
        if (command == "flow" && argc >= 5) {
            // The whole game (options screen, introductions, races, the
            // screens between): a script's joystick ("<count> <keys>", U D L
            // R F), pictures after the listed ticks (MM_CHEATS=timer,falls).
            data::GameFiles files(platform::utf8_path(argv[2]));
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            game::GameFlow flow(files, assets);
            if (const char* cheats = std::getenv("MM_CHEATS")) {
                flow.cheats.stop_timer = std::strstr(cheats, "timer") != nullptr;
                flow.cheats.safe_falls = std::strstr(cheats, "falls") != nullptr;
            }
            const auto text = platform::read_file(platform::utf8_path(argv[3]));
            std::istringstream in(std::string(text.begin(), text.end()));
            // "race" holds the script until a race runs (the next line is
            // its first update).
            std::vector<std::string> keys;
            for (std::string line; std::getline(in, line);) {
                if (line == "race" || line == "finish" || line == "crowd on" || line == "crowd off") {
                    keys.push_back(line);
                    continue;
                }
                const auto space = line.find(' ');
                if (space == std::string::npos) continue;
                keys.insert(keys.end(), std::size_t(std::stoi(line.substr(0, space))), line.substr(space + 1));
            }
            const auto out = platform::utf8_path(argv[4]);
            std::set<long> wanted;
            for (int i = 5; i < argc; ++i) wanted.insert(std::stol(argv[i]));
            const long last = wanted.empty() ? long(keys.size()) * 2 : *wanted.rbegin();
            game::Push next{};
            std::size_t at = 0;
            bool was_racing = false, started = false;
            // MM_SAVE_AT=tick: copy the game there (as the quick save does)
            // and run both on, comparing pictures and sound every tick.
            const long save_at = std::getenv("MM_SAVE_AT") ? std::atol(std::getenv("MM_SAVE_AT")) : -1;
            std::optional<game::GameFlow> copy;
            std::vector<float> flow_audio;
            for (long tick = 0; tick <= last; ++tick) {
                started = started || (flow.racing() && !was_racing);
                was_racing = flow.racing();
                if (at < keys.size() && keys[at] == "race" && started) {
                    std::printf("tick %ld: the race on course %d\n", tick, int(flow.course()));
                    ++at;
                    started = false;
                }
                if (at < keys.size() && (keys[at] == "crowd on" || keys[at] == "crowd off")) {
                    // As the pause menu's Crowd race option does.
                    flow.set_crowd(keys[at] == "crowd on");
                    if (copy) copy->set_crowd(keys[at] == "crowd on");
                    std::printf("tick %ld: %s\n", tick, keys[at].c_str());
                    ++at;
                }
                if (at < keys.size() && keys[at] == "finish") {
                    std::printf("tick %ld: finish on course %d\n", tick, int(flow.course()));
                    flow.finish_now();
                    if (copy) copy->finish_now();
                    ++at;
                }
                const std::string k = at < keys.size() && keys[at] != "race" ? keys[at++] : "-";
                const bool u = k.find('U') != std::string::npos, d = k.find('D') != std::string::npos;
                const bool l = k.find('L') != std::string::npos, r = k.find('R') != std::string::npos;
                const bool f = k.find('F') != std::string::npos;
                if (save_at == tick) copy.emplace(flow);
                flow.tick({u, d, l, r, f, next});
                if (copy) {
                    // The quick save check: the copy plays on alike.
                    copy->tick({u, d, l, r, f, next});
                    std::vector<float> a(1764), b(1764);
                    flow.audio(a, 44100);
                    copy->audio(b, 44100);
                    const auto pa = flow.picture(), pb = copy->picture();
                    const bool same = a == b && pa.pixels.size() == pb.pixels.size() &&
                                      std::equal(pa.pixels.begin(), pa.pixels.end(), pb.pixels.begin(), [](auto x, auto y) {
                                          return x.r == y.r && x.g == y.g && x.b == y.b;
                                      });
                    if (!same) {
                        std::printf("tick %ld: the copy differs\n", tick);
                        copy.reset();
                    }
                } else if (save_at >= 0) {
                    std::vector<float> a(1764);
                    flow.audio(a, 44100);
                } else if (std::getenv("MM_FLOW_WAV")) {
                    // The game's sound, an update's worth (two blanks).
                    std::vector<float> a(1764 * 2);
                    flow.audio(a, 44100);
                    flow_audio.insert(flow_audio.end(), a.begin(), a.end());
                }
                const int fire = f ? 2 : 1;
                next = {std::int16_t(((d ? 0x20 : 0) - (u ? 0x20 : 0)) * fire), std::int16_t(((l ? 0x20 : 0) - (r ? 0x20 : 0)) * fire)};
                if (wanted.contains(tick) && copy) std::printf("tick %ld: the copy matches\n", tick);
                if (wanted.contains(tick)) {
                    platform::save_png(flow.picture(), out / ("flow-" + std::to_string(tick) + ".png"));
                }
            }
            if (const char* wav = std::getenv("MM_FLOW_WAV")) {
                std::vector<std::uint8_t> out;
                auto u32 = [&](std::uint32_t v) { for (int b = 0; b < 4; ++b) out.push_back(std::uint8_t(v >> (8 * b))); };
                auto u16 = [&](std::uint16_t v) { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); };
                const auto bytes = std::uint32_t(flow_audio.size() * 2);
                out.insert(out.end(), {'R', 'I', 'F', 'F'});
                u32(36 + bytes);
                out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
                u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
                out.insert(out.end(), {'d', 'a', 't', 'a'});
                u32(bytes);
                double sum = 0;
                for (const float v : flow_audio) {
                    u16(std::uint16_t(std::int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
                    sum += double(v) * v;
                }
                platform::write_file(platform::utf8_path(wav), out);
                std::printf("audio: %.1f s, rms %.4f\n", flow_audio.size() / 2.0 / 44100.0, std::sqrt(sum / std::max<std::size_t>(1, flow_audio.size())));
            }
            return 0;
        }
        if (command == "copytime" && argc >= 4) {
            // Research: what copying a race (crowd on) and 24 updates cost.
            data::GameFiles files(platform::utf8_path(argv[2]));
            game::Race race(files, data::Course(std::stoi(argv[3])));
            race.set_crowd(true);
            for (int i = 0; i < 100; ++i) race.update({});
            const auto t0 = std::chrono::steady_clock::now();
            long sum = 0;
            for (int i = 0; i < 1000; ++i) {
                game::Race copy = race;
                sum += copy.marbles[3].x;
            }
            const auto t1 = std::chrono::steady_clock::now();
            for (int i = 0; i < 100; ++i) {
                game::Race copy = race;
                for (int k = 0; k < 24; ++k) copy.update({});
                sum += copy.marbles[3].x;
            }
            const auto t2 = std::chrono::steady_clock::now();
            std::printf("copy %.1f us, copy + 24 updates %.1f us (%ld) size %zu\n",
                        std::chrono::duration<double, std::micro>(t1 - t0).count() / 1000,
                        std::chrono::duration<double, std::micro>(t2 - t1).count() / 100, sum % 7, sizeof(game::Race));
            return 0;
        }
        if (command == "regioncount" && argc >= 3) {
            // Research: each course's region list (block entry 0: band
            // range, script; 0 = a random group of four) against the slots.
            data::GameFiles files(platform::utf8_path(argv[2]));
            for (int c = 0; c < data::course_count; ++c) {
                game::Regions regions(files, data::Course(c));
                const auto& t = files.track(data::Course(c));
                int single = 0, groups = 0;
                for (std::uint32_t e = regions.block_entry(0); t.u8(e) != 0xFF; e += 6) {
                    (t.u32(e + 2) == 0 ? groups : single)++;
                }
                std::printf("course %d: %d regions + %d random groups of 4 (%d slots)\n", c, single, groups, game::Regions::slots);
            }
            return 0;
        }
        if (command == "crowdmap" && argc >= 8) {
            // Research: the crowd's distance to go for a zone round a cell.
            data::GameFiles files(platform::utf8_path(argv[2]));
            game::Race race(files, data::Course(std::stoi(argv[3])));
            race.set_crowd(true);
            const int zone = std::stoi(argv[4]), cx = std::stoi(argv[5]), cy = std::stoi(argv[6]), r = std::stoi(argv[7]);
            for (int y = cy - r; y <= cy + r; ++y) {
                std::printf("y%3d", y);
                for (int x = cx - r; x <= cx + r; ++x) {
                    const auto v = race.crowd_map()->to_go(zone, x, y);
                    const int f = race.crowd_map()->floor(x, y);
                    std::printf(" %5s", v == game::CrowdMap::none ? (f ? "." : " ") : std::to_string(v).c_str());
                }
                std::printf("\n");
            }
            return 0;
        }
        if (command == "crowd" && argc >= 5) {
            return crowd(argc, argv);
        }
        if (command == "fuzz" && argc >= 8) {
            return fuzz(argc, argv);
        }
        if (command == "trace" && argc >= 5) {
            return trace(argc, argv);
        }
        if (command == "search" && argc >= 5) {
            return search(argc, argv);
        }
        if (command == "pictures" && argc >= 6) {
            return pictures(argc, argv);
        }
        std::fprintf(stderr, "usage: marble_madness_replay lockstep <content> <run.bin> [course] [first] [count]\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "marble_madness_replay: %s\n", error.what());
        return 1;
    }
}
