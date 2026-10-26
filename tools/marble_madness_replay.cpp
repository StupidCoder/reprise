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
#include "marble_madness_amiga/game/race.hpp"
#include "marble_madness_amiga/game/view.hpp"
#include "marble_madness_amiga/sound/sound_board.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
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
            return k;
        }
    }
    return 0;
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

int lockstep(int argc, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    const auto run = platform::read_file(platform::utf8_path(argv[3]));
    const auto course = data::Course(argc > 4 ? std::stoi(argv[4]) : 0);
    const std::size_t frames = run.size() / record_size;
    const std::size_t start_record = race_start(run);
    const std::size_t first = std::max(start_record, argc > 5 ? std::size_t(std::stoul(argv[5])) : 0);
    std::size_t count = argc > 6 ? std::size_t(std::stoul(argv[6])) : frames;
    data::GameFiles files(content);
    game::Race race(files, course);
    const auto* start = run.data() + first * record_size + players_at;
    if (first == start_record) {
        // From the course start: the first record must equal the start,
        // apart from fields the course start ($114AA2) leaves as the last
        // race left them (zero after booting); those are taken over.
        auto& m = race.marbles[0];
        m.fall_start_z = be32(start + 0x2A);
        m.edge_cell_x = be16(start + 0x2E);
        m.edge_cell_y = be16(start + 0x30);
        m.cell_x = be16(start + 0x32);
        m.cell_y = be16(start + 0x34);
        m.edge_sides = start[0x37];
        m.saved_feature = std::int8_t(start[0x1D]);
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
    const auto covers = read_covers(argv[3]);
    std::size_t covers_compared = 0, covers_matched = 0;
    int covers_shown = 0;
    int shown = 0;
    std::size_t matched = 0;
    for (std::size_t k = first; k + 1 < frames && k < first + count; ++k) {
        const auto* rec = run.data() + k * record_size;
        game::Push p0{be16(rec + forces_at), be16(rec + forces_at + 4)};
        race.update({p0, game::Push{}});
        const auto* next = run.data() + (k + 1) * record_size + players_at;
        // The recording may go on into the next course (the course number
        // is in the recorded globals, $1005EE); the comparison ends there.
        const auto* globals = run.data() + (k + 1) * record_size + players_at + 0xF4 * 2;
        if (be16(globals + 0xE) != int(course)) {
            std::printf("the original moves on to course %d after update %zu\n", be16(globals + 0xE), k);
            count = k - first;
            break;
        }
        if (std::getenv("MM_SCROLL") != nullptr) {
            std::printf("update %zu scroll recorded %d native %d view %d\n", k,
                        be16(run.data() + (k + 1) * record_size + players_at + 0xF4 * 2 + 0x120), race.scroll_lines(),
                        race.view());
        }
        if (const auto c = covers.find(k); c != covers.end()) {
            ++covers_compared;
            const auto got = covers_of(race.marbles[0]);
            if (got == c->second) {
                ++covers_matched;
            } else if (covers_shown++ < 12) {
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
        if (diff.empty()) {
            ++matched;
            continue;
        }
        if (shown++ < 12) {
            std::printf("update %zu (push %d %d):%s\n", k, p0.forward, p0.side, diff.c_str());
        }
    }
    std::printf("%zu of %zu updates match (recorded/native)\n", matched,
                std::min(frames - 1, first + count) - first);
    if (covers_compared != 0) {
        std::printf("covers: %zu of %zu updates match\n", covers_matched, covers_compared);
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
    game::Race race(files, data::Course::practice);
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
        const auto* rec = run.data() + k * record_size;
        race.update({game::Push{be16(rec + forces_at), be16(rec + forces_at + 4)}, game::Push{}});
        view.update(race);
    }
    return 0;
}

// Runs a recorder script on the rebuilt race and prints the marble's mode
// changes (for finding out what a run does).
int trace(int argc, char** argv) {
    data::GameFiles files(platform::utf8_path(argv[2]));
    game::Race race(files, data::Course(std::stoi(argv[3])));
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
    int update = 0;
    std::string last;
    for (const auto& [count, keys] : steps) {
        for (int i = 0; i < count && update < limit; ++i, ++update) {
            const bool u = keys.find('U') != std::string::npos, d = keys.find('D') != std::string::npos;
            const bool l = keys.find('L') != std::string::npos, r = keys.find('R') != std::string::npos;
            race.update({game::Push{std::int16_t((d ? 0x20 : 0) - (u ? 0x20 : 0)), std::int16_t((l ? 0x20 : 0) - (r ? 0x20 : 0))},
                         game::Push{}});
            const auto& m = race.marbles[0];
            char line[160];
            std::snprintf(line, sizeof line, "mode %d air %d zone %d region %d status %d score %d", m.mode, m.air,
                          m.feature, m.region_code, m.status, m.score);
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
    const auto course = data::Course::practice;
    game::Race race(files, course);
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
    for (const auto& e : race.take_sounds()) {
        board.play(e.id, e.right);
    }
    flush_log(long(start) - 1);
    for (std::size_t k = start; k < frames; ++k) {
        const auto* rec = run.data() + k * record_size;
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
    const int hold = 4, width = argc > 6 ? std::stoi(argv[6]) : 256;
    data::GameFiles files(content);
    struct Candidate {
        game::Race race;
        std::string keys; // one character per hold step: direction 0-8
        double score{};
    };
    static const char* names[] = {"-", "U", "D", "L", "R", "UL", "UR", "DL", "DR"};
    auto push_of = [](int d) {
        const bool u = d == 1 || d == 5 || d == 6, dn = d == 2 || d == 7 || d == 8;
        const bool l = d == 3 || d == 5 || d == 7, r = d == 4 || d == 6 || d == 8;
        return game::Push{std::int16_t((dn ? 0x20 : 0) - (u ? 0x20 : 0)), std::int16_t((l ? 0x20 : 0) - (r ? 0x20 : 0))};
    };
    // MM_SEARCH_DROP=1: look for Practice's drop bonus instead of the goal.
    const bool drop = std::getenv("MM_SEARCH_DROP") != nullptr;
    auto score_of = [drop](const game::Race& race) {
        const auto& m = race.marbles[0];
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
    std::vector<Candidate> beam{{game::Race(files, course), "", 0}};
    // The countdown: the marble is held until the start gate opens.
    for (int step = 0; step * hold < limit; ++step) {
        std::vector<Candidate> next;
        for (const auto& c : beam) {
            for (int d = 0; d < 9; ++d) {
                Candidate n{c.race, c.keys + char('0' + d), 0};
                for (int i = 0; i < hold; ++i) n.race.update({push_of(d), game::Push{}});
                n.score = score_of(n.race);
                next.push_back(std::move(n));
            }
        }
        std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
        if (next.size() > std::size_t(width)) next.erase(next.begin() + width, next.end());
        beam = std::move(next);
        const auto& best = beam.front();
        if (step % 25 == 0 || best.score >= 1e9) {
            const auto& m = best.race.marbles[0];
            std::printf("update %d: zone %d at %d,%d respawns %d time %d\n", (step + 1) * hold, m.feature,
                        m.x >> 16, m.y >> 16, m.respawns, m.time);
        }
        if (best.score >= 1e9) break;
    }
    const auto& best = beam.front();
    std::string script;
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
