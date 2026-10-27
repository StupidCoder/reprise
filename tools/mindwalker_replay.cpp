// Mind Walker research tool: runs the rebuilt stages on a reference recording's
// script and compares them with the original (.analysis/mindwalker_amiga/
// harness/mwrec.py).
//
//   mindwalker_replay title <content> <script> <run.log> [dump dir]
//
// Every input read of the original is one frame of the rebuilt stage (T's
// ten-frame sound check reads nothing and is run in between). Per frame it
// compares the state the recorder logged; with a dump directory, also the
// screen dumps the script asked for (<run>-NAME.idx from rawpng.py: one pen
// per pixel) against the rebuilt canvas, and writes NAME-native.png.

#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/title/title_stage.hpp"
#include "mindwalker_amiga/landscape/view_scene.hpp"
#include "mindwalker_amiga/landscape/landscape_bar.hpp"
#include "mindwalker_amiga/landscape/landscape_stage.hpp"
#include "mindwalker_amiga/tunnel/tunnel_stage.hpp"
#include "mindwalker_amiga/arena/arena_stage.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <deque>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using mindwalker::title::Pointer;
using mindwalker::title::TitleStage;

struct Script {
    std::uint32_t seed{};
    bool has_seed{};
    std::map<int, std::vector<std::vector<std::string>>> events; // T frame -> events
    std::vector<std::string> end_dumps;                            // hook dumps of T (taken as it ends)
};

Script read_script(const std::filesystem::path& path) {
    Script script;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream f(line);
        std::vector<std::string> words;
        for (std::string w; f >> w;) words.push_back(w);
        if (words.empty()) continue;
        if (words[0] == "seed") {
            script.seed = std::uint32_t(std::stoul(words[1], nullptr, 16));
            script.has_seed = true;
        } else if (words[0] == "hook" && words.size() == 5 && words[1] == "T") {
            script.end_dumps.push_back(words[4]);
        } else if (words[0] == "T") {
            script.events[std::stoi(words[1])].push_back({words.begin() + 2, words.end()});
        }
    }
    return script;
}

std::map<std::string, std::string> fields(const std::string& line) {
    std::map<std::string, std::string> out;
    std::istringstream f(line);
    for (std::string w; f >> w;) {
        const auto eq = w.find('=');
        if (eq != std::string::npos) out[w.substr(0, eq)] = w.substr(eq + 1);
    }
    return out;
}

std::string hex(std::uint64_t v, int digits) {
    char text[32];
    std::snprintf(text, sizeof text, "%0*llx", digits, static_cast<unsigned long long>(v));
    return text;
}

int title(const std::filesystem::path& content, const std::filesystem::path& script_path,
          const std::filesystem::path& log_path, const std::filesystem::path& dumps) {
    const mindwalker::data::GameFiles files(content);
    const auto picture = platform::read_file(content / "MindB");
    const auto script = read_script(script_path);
    const auto& t = files.program(mindwalker::data::Stage::title);
    // The recorder pokes the mixed seed; the stage mixes its clock value in.
    const std::uint32_t clock = script.has_seed ? script.seed ^ t.u32(0x600AF8) : 0;
    TitleStage stage(files, picture, clock);

    std::ifstream log(log_path);
    std::string line;
    Pointer pointer;
    int frames = 0, differences = 0;
    // The dump is taken at the read, before this frame's work (or, for a
    // hook, where the hook stopped).
    const auto compare = [&](const std::string& name, int frame) {
        const auto& canvas = stage.phase() == TitleStage::Phase::loading ? stage.loading_canvas() : stage.canvas();
        const auto idx = platform::read_file(dumps / (log_path.stem().string() + "-" + name + ".idx"));
        int bad = 0, first_x = -1, first_y = -1;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
                if (idx[std::size_t(y * 320 + x)] != canvas.pen(x, y) && bad++ == 0) first_x = x, first_y = y;
        std::printf("dump %s at T %d: %d of 64000 pens differ", name.c_str(), frame, bad);
        if (bad) std::printf(" (first at %d,%d: ours %d, original %d)", first_x, first_y, canvas.pen(first_x, first_y),
                             idx[std::size_t(first_y * 320 + first_x)]);
        // The screen's base colours (16 on the title's HAM screen, 2 on S's).
        std::ifstream pal(dumps / (log_path.stem().string() + "-" + name + ".pal"));
        const bool loading = stage.phase() == TitleStage::Phase::loading;
        const std::vector<mindwalker::screen::Rgb4> ours =
            loading ? std::vector<mindwalker::screen::Rgb4>{0x000, 0x0F0} : stage.colours();
        int wrong_colours = 0;
        for (const auto c : ours) {
            unsigned v = 0;
            pal >> std::hex >> v;
            wrong_colours += v != c;
        }
        std::printf(", %d of %zu colours differ\n", wrong_colours, ours.size());
        differences += bad != 0 || wrong_colours != 0;
        platform::save_png(stage.picture(), dumps / (name + "-native.png"));
        std::vector<std::uint8_t> pens(64000);
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x) pens[std::size_t(y * 320 + x)] = canvas.pen(x, y);
        platform::write_file(dumps / (name + "-native.idx"), pens);
    };
    const auto differ = [&](int frame, const std::string& what, const std::string& ours, const std::string& theirs) {
        if (++differences <= 12) std::printf("T %d %s: ours %s, original %s\n", frame, what.c_str(), ours.c_str(), theirs.c_str());
    };
    while (std::getline(log, line)) {
        if (!line.starts_with("T ")) continue;
        const int frame = std::stoi(line.substr(2));
        while (stage.phase() == TitleStage::Phase::pause) stage.frame(pointer);
        for (const auto& ev : script.events.count(frame) ? script.events.at(frame) : std::vector<std::vector<std::string>>{}) {
            if (ev[0] == "button") pointer.button = ev[1] == "1";
            if (ev[0] == "mouse") pointer = {std::stoi(ev[1]), std::stoi(ev[2]), pointer.button};
            if (ev[0] == "poke" && ev[1] == "601402") stage.set_slider(std::uint16_t(std::stoul(ev[2], nullptr, 16)));
            if (ev[0] == "dump" && !dumps.empty()) compare(ev[1], frame);
        }
        const auto f = fields(line);
        // State at the read: the title's twinkle runs before its read; the
        // menu steps its seed before the read and its pillars after.
        if (stage.phase() == TitleStage::Phase::title) {
            stage.frame(pointer);
            std::string colours;
            for (const auto c : stage.colours_at_read()) colours += hex(c, 4);
            if (f.at("colours") != colours) differ(frame, "colours", colours, f.at("colours"));
        } else if (stage.phase() == TitleStage::Phase::menu) {
            const auto& m = stage.pillar_mixer();
            const std::string mixer = hex(m.reg, 8) + hex(m.mix, 4) + hex(m.counter, 4);
            std::string offsets;
            for (const int o : stage.pillar_offsets()) offsets += hex(std::uint32_t(o), 8);
            if (f.at("pillar_rng") != mixer) differ(frame, "pillar generator", mixer, f.at("pillar_rng"));
            if (f.at("pillars") != offsets) differ(frame, "pillar offsets", offsets, f.at("pillars"));
            stage.frame(pointer);
            if (f.at("newgame") != hex(stage.menu_seed(), 8)) differ(frame, "new game seed", hex(stage.menu_seed(), 8), f.at("newgame"));
        }
        ++frames;
    }
    if (!dumps.empty())
        for (const auto& name : script.end_dumps) compare(name, -1);
    if (stage.new_game()) {
        const auto& g = *stage.new_game();
        std::printf("new game: seed %08x stage %d progress %u lives %d bonus %u\n", g.seed, g.stage, g.progress, g.lives,
                    g.bonus);
    }
    std::printf("%d frames compared, %d differences\n", frames, differences);
    return differences == 0 ? 0 : 1;
}

// The landscape's opening view of a new game's world (A's set-up builds the
// view for the cell left of the start twice, $206944 and $200262), painted
// for each direction and compared with a screen dump's pens (rows from `top`).
int view(const std::filesystem::path& content, std::uint32_t seed, const std::filesystem::path& idx_path,
         const std::filesystem::path& out) {
    using namespace mindwalker;
    const data::GameFiles files(content);
    const auto& a = files.program(data::Stage::landscape);
    const world::World world(seed, a);
    landscape::ViewInputs in;
    in.home_x = world.start_x();
    in.home_y = world.start_y();
    in.colour17 = 0xF60; // as the oracle found them at the start (hypothesis: set by A's set-up)
    in.colour18 = 0xF00;
    const auto x = std::uint16_t((world.start_x() - 1) & 0xFFF), y = world.start_y();
    auto first = landscape::build_view(world, a, x, y, in, landscape::stored_colours(a));
    in.previous_x = x;
    in.previous_y = y;
    const auto cells = landscape::build_view(world, a, x, y, in, first.colours);
    const auto idx = platform::read_file(idx_path);
    std::vector<screen::Rgb4> colours;
    for (const auto c : cells.colours) colours.push_back(c & 0xFFF);
    int best = -1, best_bad = 1 << 30;
    for (int direction = 0; direction < 4; ++direction) {
        for (int top : {10, 11, 12}) {
            screen::Canvas canvas;
            landscape::LandscapeBar bar(files);
            bar.set(3, direction, 0);
            bar.draw(canvas);
            landscape::ViewPainter(a).paint(canvas, cells, direction, world.floor_level(), world.ceiling_level(), top);
            int bad = 0, bar_bad = 0;
            for (int py = 0; py < 10; ++py)
                for (int px = 0; px < 320; ++px) bar_bad += idx[std::size_t(py * 320 + px)] != canvas.pen(px, py);
            if (direction == 0 && top == 10) std::printf("bar: %d of %d pens differ\n", bar_bad, 10 * 320);
            for (int py = 12; py < 200; ++py)
                for (int px = 0; px < 320; ++px) bad += idx[std::size_t(py * 320 + px)] != canvas.pen(px, py);
            std::printf("direction %d top %d: %d of %d pens differ\n", direction, top, bad, 188 * 320);
            if (bad < best_bad) {
                best_bad = bad;
                best = direction * 100 + top;
                platform::save_png(canvas.picture(colours), out);
                if (const char* pens = std::getenv("MW_PENS")) { // research: our pens as .idx
                    std::ofstream f(pens, std::ios::binary);
                    for (int py = 0; py < 200; ++py)
                        for (int px = 0; px < 320; ++px) f.put(char(canvas.pen(px, py)));
                }
            }
        }
    }
    std::printf("best: direction %d top %d\n", best / 100, best % 100);
    return 0;
}

// The landscape stage on a recording's A reads (mwrec.py's log): each of the
// stage's input reads takes the next logged read (their kinds must agree) and
// gets the script's input for that read's index; at every frame's first read
// the walker's record is compared with the logged one.
// How many differences to print (MW_DIFFS overrides 15).
const int print_limit = std::getenv("MW_DIFFS") ? std::atoi(std::getenv("MW_DIFFS")) : 15;

int walk(const std::filesystem::path& content, std::uint32_t seed, const std::filesystem::path& script_path,
         const std::filesystem::path& log_path, int frame_limit, const std::filesystem::path& png) {
    using namespace mindwalker;
    const data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    // Script events by module and read index; the held state carries over
    // from T (its events up to its last logged read).
    std::map<std::pair<char, int>, std::vector<std::vector<std::string>>> events;
    std::deque<int> hook_gadgets;
    bool pause_shot_done = false;
    {
        std::ifstream in(script_path);
        std::string line;
        while (std::getline(in, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream f(line);
            std::vector<std::string> w;
            for (std::string x; f >> x;) w.push_back(x);
            if (w.size() >= 3 && (w[0] == "T" || w[0] == "A" || w[0] == "B")) events[{w[0][0], std::stoi(w[1])}].push_back({w.begin() + 2, w.end()});
            // Clicks the recorder posts from hooks (loops without reads: the pause).
            if (w.size() == 5 && w[0] == "hook" && w[1] == "A" && w[3] == "gadget") hook_gadgets.push_back(std::stoi(w[4], nullptr, 16));
        }
    }
    struct Read {
        int index;
        std::string address;
        std::map<std::string, std::string> fields;
    };
    std::vector<Read> reads;
    int t_reads = 0;
    {
        std::ifstream in(log_path);
        std::string line;
        // MW_SESSION=n: the nth run of A in the recording (from 1; a later
        // one resumes from the shared block).
        const int wanted = std::getenv("MW_SESSION") ? std::atoi(std::getenv("MW_SESSION")) : 1;
        int session = 0;
        while (std::getline(in, line)) {
            if (line.starts_with("T ")) ++t_reads;
            if (line.starts_with("# module A")) ++session;
            if (!line.starts_with("A ") || session != wanted) continue;
            std::istringstream f(line);
            std::string m, addr;
            int index;
            f >> m >> index >> addr;
            reads.push_back({index, addr, fields(line)});
        }
    }
    landscape::LandscapeInput held;
    int pending_gadget = -1, frame_first_read = -1, vbl_start = -1, clock_start = 0;
    // MW_SHOTS="read:file.png,...": our picture when the replay reaches a read
    // (the state the recorder's dump at that read shows).
    std::map<int, std::string> shots;
    if (const char* list = std::getenv("MW_SHOTS")) {
        std::istringstream in(list);
        for (std::string item; std::getline(in, item, ',');)
            shots[std::stoi(item.substr(0, item.find(':')))] = item.substr(item.find(':') + 1);
    }
    const auto apply = [&](char module, int index) {
        if (!events.count({module, index})) return;
        for (const auto& ev : events.at({module, index})) {
            if (ev[0] == "button") held.button = ev[1] == "1";
            if (ev[0] == "fire") held.fire = ev[1] == "1";
            if (ev[0] == "mouse" && ev.size() == 3) {
                held.pointer_x = std::stoi(ev[1]);
                held.pointer_y = std::stoi(ev[2]);
            }
            // A gadget message is read after the frame's button read: one
            // posted at a later read waits for the next frame.
            if (ev[0] == "gadget" && module == 'A' && index != frame_first_read) pending_gadget = std::stoi(ev[1], nullptr, 16);
            if (ev[0] == "joy") {
                held.up = ev[1].find('U') != std::string::npos;
                held.down = ev[1].find('D') != std::string::npos;
                held.left = ev[1].find('L') != std::string::npos;
                held.right = ev[1].find('R') != std::string::npos;
            }
        }
    };
    for (int i = 0; i < t_reads; ++i) apply('T', i);
    // A later run of A: the recorder's held inputs went on through A's
    // earlier reads and B's.
    if (!reads.empty() && reads.front().index > 0) {
        for (int i = 0; i < reads.front().index; ++i) apply('A', i);
        for (const auto& [key, list] : events)
            if (key.first == 'B') apply('B', key.second);
    }

    // The new game's stage, lives and score from the shared block at A's
    // first read (+$5C, +$6E, +$66; the log holds +$10 on).
    landscape::LandscapeStage::Start start{seed, 0, 3, 0, {}};
    if (!reads.empty() && reads.front().fields.count("shared")) {
        const auto& sh = reads.front().fields.at("shared");
        const auto at = [&](int offset) { return std::stoul(sh.substr(std::size_t((offset - 0x10) * 2), 8), nullptr, 16); };
        start.stage = int(at(0x5C));
        start.lives = int(at(0x6E));
        start.score = std::uint32_t(at(0x66));
        if (std::getenv("MW_SESSION") && std::atoi(std::getenv("MW_SESSION")) > 1) {
            const auto w16 = [&](int offset) { return std::uint16_t(std::stoul(sh.substr(std::size_t((offset - 0x10) * 2), 4), nullptr, 16)); };
            landscape::LandscapeStage::Resume r;
            r.home_x = std::uint16_t(at(0x18));
            r.home_y = std::uint16_t(at(0x1C));
            r.marked_u = std::uint16_t(at(0x20));
            r.marked_v = std::uint16_t(at(0x24));
            r.direction = int(at(0x28));
            r.cell_x = std::uint16_t(at(0x2C));
            r.cell_y = std::uint16_t(at(0x30));
            r.x = int(at(0x34));
            r.level = int(at(0x38));
            r.z = int(at(0x3C));
            r.slow = int(at(0x40));
            r.found = {w16(0x44) != 0, w16(0x46) != 0};
            r.form = int(at(0x48));
            r.view_x = std::uint16_t(at(0x50));
            r.view_y = std::uint16_t(at(0x54));
            r.damage = int(at(0x58));
            r.ramp_closed = w16(0x60) != 0;
            start.resume = r;
        }
    }
    landscape::LandscapeStage stage(files, assets, start);
    std::vector<std::array<int, 6>> music_ours;
    stage.sound().trace = &music_ours;
    std::size_t next = 0;
    int frames = 0, differences = 0;
    // The walker's mask is close, not exact (our polygon fill): its pixel
    // differences are counted apart from the state's.
    int sprite_pixels = 0, sprite_rows_differing = 0, hidden_differing = 0, clock_phase = 0;
    std::vector<std::array<int, 6>> music_theirs;
    bool out_of_step = false;
    const auto word = [](const std::string& hexs, int offset, int bytes) {
        return std::stol(hexs.substr(std::size_t(offset * 2), std::size_t(bytes * 2)), nullptr, 16);
    };
    const auto compare = [&](const Read& r) {
        const auto& w = r.fields.at("walker");
        const auto signed32 = [&](int o) { return std::int32_t(std::uint32_t(word(w, o, 4))); };
        const auto& me = stage.walker();
        struct Field { const char* name; long ours, theirs; };
        const Field list[] = {
            {"height", me.height, signed32(0)}, {"x", me.x, signed32(4)}, {"z", me.z, signed32(0xC)}, {"level", me.level, signed32(8)},
            {"cell x", me.cell_x, signed32(0x10)}, {"cell y", me.cell_y, signed32(0x14)},
            {"screen x", me.screen_x, signed32(0x18)}, {"screen y", me.screen_y, signed32(0x1C)},
            {"terrain", me.terrain, signed32(0x30) & 0xFFFF}, {"frame", me.frame, signed32(0x76)},
            {"speed", me.speed, signed32(0x82)}, {"view x", stage.view_x(), word(r.fields.at("view"), 0, 2)},
            {"view y", stage.view_y(), word(r.fields.at("view"), 2, 2)},
            {"direction", stage.direction(), word(r.fields.at("direction"), 0, 4)},
            {"aiming", me.aiming, signed32(0x86)}, {"cursor x", me.cursor_x, signed32(0x8A)},
            {"cursor y", me.cursor_y, signed32(0x8E)}, {"edge push", me.edge_push, signed32(0x92)},
            // $209984 on: creature places $2099AC, the way on closed $2099B6,
            // the finds $2099BC/$2099BE; the score (shared +$66).
            {"creature places", stage.creature_places(), word(r.fields.at("params"), 0x2A, 2)},
            {"ramp closed", stage.ramp_closed(), word(r.fields.at("params"), 0x32, 2)},
            {"find E=", stage.found()[0], word(r.fields.at("params"), 0x38, 2)},
            {"find mc2", stage.found()[1], word(r.fields.at("params"), 0x3A, 2)},
            {"score", long(stage.score()), word(r.fields.at("shared"), 0x56, 4)}};
        for (const auto& f : list) {
            // A beacon's bonus counts whole seconds of a real-time clock whose
            // second boundaries we cannot place: one point either way is
            // that phase, counted apart.
            if (std::string(f.name) == "score" && f.ours != f.theirs && std::abs(f.ours - f.theirs) == 1 &&
                !stage.ramp_closed()) {
                ++clock_phase;
                continue;
            }
            if (f.ours != f.theirs && ++differences <= print_limit)
                std::printf("A read %d (frame %d) %s: ours %ld, original %ld\n", r.index, frames, f.name, f.ours, f.theirs);
        }
        // The walker's sprite data after h11 (header, then plane A and B
        // words a row), in recordings that have it.
        if (r.fields.count("sprite") && stage.walker().sprite_rows > 0) {
            const auto& sp = r.fields.at("sprite");
            const auto rows = stage.walker_sprite();
            const int top = stage.walker().screen_y - stage.walker().height;
            for (std::size_t y = 0; y < rows.size() && 4 + y * 4 + 4 <= sp.size() / 2; ++y) {
                if (top + int(y) < 0) continue; // above the screen: h11 leaves these rows as they were
                const long theirs = word(sp, int(4 + y * 4), 4);
                if (long(rows[y]) == theirs) continue;
                sprite_pixels += std::popcount(std::uint32_t(rows[y] ^ std::uint32_t(theirs)));
                if (++sprite_rows_differing <= print_limit)
                    std::printf("A read %d (frame %d) sprite row %zu: ours %08lx, original %08lx\n", r.index, frames, y,
                                long(rows[y]), theirs);
            }
            const long hidden = word(r.fields.at("walker"), 0x96, 4);
            // (It follows the mask's pixels, so it is counted with them.)
            if (stage.walker().hidden_frames != hidden && ++hidden_differing <= print_limit)
                std::printf("A read %d (frame %d) hidden frames: ours %d, original %ld\n", r.index, frames,
                            stage.walker().hidden_frames, hidden);
        }
        // The sound's states in order (its timer runs on its own clock, so
        // only the sequence of changes is compared, at the end).
        if (r.fields.count("music")) {
            const auto& m = r.fields.at("music");
            const auto b = [&](int o) { return int(word(m, o, 1)); };
            const int at = int(word(m, 0x56, 4)) & 0xF;
            const std::array<int, 6> theirs{b(0x0C), int(word(m, 0x0E, 4)), at, int(word(m, 0x26 + 2 * at, 2)),
                                            int(std::int8_t(b(0x46 + at))), b(0x5E)};
            if (music_theirs.empty() || music_theirs.back() != theirs) music_theirs.push_back(theirs);

        }
        // The bolt's mixer ($20648E), in recordings that have it (its every
        // draw, so the branches' timing too).
        if (r.fields.count("bolt")) {
            const auto& b = r.fields.at("bolt");
            const auto& m = stage.bolt().random();
            const Field bolt[] = {{"bolt register", long(m.reg), word(b, 0, 4)}, {"bolt mix", m.mix, word(b, 4, 2)},
                                  {"bolt counter", m.counter, word(b, 6, 2)}};
            for (const auto& f : bolt)
                if (f.ours != f.theirs && ++differences <= print_limit)
                    std::printf("A read %d (frame %d) %s: ours %lx, original %lx\n", r.index, frames, f.name, f.ours, f.theirs);
        }
        // The flying creatures ($208128 on), the damage and the lives, in
        // recordings that have them.
        if (!r.fields.count("creatures")) return;
        const auto& c = r.fields.at("creatures");
        const auto long_at = [&](int o) { return long(std::int32_t(std::uint32_t(word(c, o, 4)))); };
        const auto word_at = [&](int o) { return long(std::int16_t(std::uint16_t(word(c, o, 2)))); };
        std::vector<Field> more = {
            {"attached", stage.creatures().attached(), long_at(0x286)},
            {"creature bonus", stage.creatures().score_bonus(), long_at(0x0A)},
            {"damage", stage.damage(), long(std::int32_t(std::uint32_t(word(r.fields.at("damage"), 0, 4))))},
            {"lives", stage.lives(), long(std::int32_t(std::uint32_t(word(r.fields.at("shared"), 0x5E, 4))))}};
        static const char* names[] = {"present", "kind", "x", "y", "depth", "tether x", "tether y"};
        for (int i = 0; i < 12; ++i) {
            const auto& me = stage.creatures().all()[std::size_t(i)];
            const long ours[] = {me.present, me.kind, me.x, me.y, me.depth, me.tether_x, me.tether_y};
            const long theirs[] = {word_at(0x252 + 2 * i), long_at(0x0E + 4 * i), long_at(0x102 + 4 * i),
                                   long_at(0xD2 + 4 * i), long_at(0xA2 + 4 * i), long_at(0x222 + 4 * i),
                                   long_at(0x1F2 + 4 * i)};
            for (int k = 0; k < 7; ++k)
                if (ours[k] != theirs[k] && ++differences <= print_limit)
                    std::printf("A read %d (frame %d) creature %d %s: ours %ld, original %ld\n", r.index, frames, i,
                                names[k], ours[k], theirs[k]);
        }
        for (const auto& f : more)
            if (f.ours != f.theirs && ++differences <= print_limit)
                std::printf("A read %d (frame %d) %s: ours %ld, original %ld\n", r.index, frames, f.name, f.ours, f.theirs);
    };
    const landscape::InputReader reader = [&](landscape::InputRead kind) {
        const char* expected = kind == landscape::InputRead::button ? "203f70" : kind == landscape::InputRead::stick ? "208562" : "2085ae";
        if (next >= reads.size()) return held;
        const auto& r = reads[next];
        if (r.address != expected && !out_of_step) {
            out_of_step = true;
            ++differences;
            std::printf("A read %d: the original read %s, the stage %s\n", r.index, r.address.c_str(), expected);
        }
        // The beacon's time bonus counts the original's seconds: our clock
        // follows its frame counter (vbl) from the first read on.
        if (r.fields.count("vbl")) {
            const int vbl = int(std::stoul(r.fields.at("vbl"), nullptr, 16));
            if (vbl_start < 0) {
                vbl_start = vbl;
                clock_start = stage.clock();
            }
            stage.set_clock(clock_start + vbl - vbl_start);
        }
        // The recorder pokes before it logs the read: the damage ($20052E).
        if (events.count({'A', r.index}))
            for (const auto& ev : events.at({'A', r.index}))
                if (ev.size() == 3 && ev[0] == "poke" && ev[1] == "20052E") stage.set_damage(int(std::stoul(ev[2], nullptr, 16)));
        if (kind == landscape::InputRead::button) compare(r);
        if (shots.count(r.index)) platform::save_png(stage.picture(), shots.at(r.index));
        apply('A', r.index);
        ++next;
        return held;
    };
    while (next < reads.size() && !out_of_step && frames < frame_limit) {
        int gadget = pending_gadget;
        pending_gadget = -1;
        if (stage.paused() && std::getenv("MW_PAUSE_SHOT") && !pause_shot_done) { // research: the pause as shown
            platform::save_png(stage.picture(), std::getenv("MW_PAUSE_SHOT"));
            pause_shot_done = true;
        }
        if (stage.paused() && !hook_gadgets.empty()) {
            gadget = hook_gadgets.front();
            hook_gadgets.pop_front();
        }
        frame_first_read = reads[next].index;
        if (events.count({'A', frame_first_read}))
            for (const auto& ev : events.at({'A', frame_first_read}))
                if (ev[0] == "gadget") gadget = std::stoi(ev[1], nullptr, 16);
        stage.frame(reader, gadget);
        ++frames;
    }
    // A picture sequence the last read started plays on without reads.
    while (frame_limit < (1 << 30) && frames < frame_limit && stage.in_sequence()) {
        stage.frame(reader);
        ++frames;
    }
    if (std::getenv("MW_STATE")) // research: the walker at the end
        std::printf("walker x %d y %d row %d column %d hidden %d\n", stage.walker().screen_x, stage.walker().screen_y,
                    stage.walker().view_row, stage.walker().view_column, stage.walker().hidden_frames);
    std::printf("%d frames, %zu reads compared, %d differences; score %u, lives %d%s", frames, next, differences,
                stage.score(), stage.lives(), stage.out_of_lives() ? " (out of lives: the stage ends)" : "");
    if (sprite_pixels) std::printf("; walker mask: %d pixels in %d rows differ", sprite_pixels, sprite_rows_differing);
    if (hidden_differing) std::printf(", hidden count in %d reads", hidden_differing);
    if (clock_phase) std::printf("; beacon bonus a second's phase apart in %d reads", clock_phase);
    if (!music_theirs.empty()) {
        // The longest common run of sequencer states, in order.
        // (The original's music starts before its first logged read: ours is
        // aligned on the original's first state.)
        std::size_t i = 0, j = 0, matched = 0;
        while (i < music_ours.size() && music_ours[i] != music_theirs[0]) ++i;
        if (i == music_ours.size()) i = 0;
        const std::size_t start = i;
        // The original's states (sampled at its reads) as a subsequence of
        // ours (every tick).
        while (i < music_ours.size() && j < music_theirs.size()) {
            if (music_ours[i] == music_theirs[j]) {
                ++matched;
                ++j;
            }
            ++i;
        }
        std::printf("; sound: %zu of %zu sequencer states in order", matched, music_theirs.size());
        if (const char* dump = std::getenv("MW_SOUND_DUMP")) { // research: both lists
            for (const auto& [list, name] : {std::pair{&music_ours, ".ours"}, std::pair{&music_theirs, ".theirs"}}) {
                std::ofstream f(std::string(dump) + name);
                for (const auto& v : *list) f << v[0] << ' ' << v[1] << ' ' << v[2] << ' ' << v[3] << ' ' << v[4] << ' ' << v[5] << '\n';
            }
        }
        if (std::getenv("MW_SOUND"))
            for (std::size_t k = 0; k < std::min<std::size_t>(400, std::max(music_ours.size(), music_theirs.size())); ++k) {
                const auto show = [](const std::vector<std::array<int, 6>>& v, std::size_t k) {
                    if (k >= v.size()) return std::string("-");
                    char t[80];
                    std::snprintf(t, sizeof t, "song %2d step %2d ring %2d period %4d vol %3d key %d", v[k][0], v[k][1],
                                  v[k][2], v[k][3], v[k][4], v[k][5]);
                    return std::string(t);
                };
                std::printf("\n  %-52s | %s", show(music_ours, start + k).c_str(), show(music_theirs, k).c_str());
            }
    }
    std::printf("\n");
    if (!png.empty()) platform::save_png(stage.picture(), png);
    return differences == 0 ? 0 : 1;
}

// Research: the landscape stage on a script alone (no recording), counting
// its input reads as the recorder would, to plan routes before recording
// them. Prints the walker every `every` reads and the events as they come.
// A script line "A n route X,Y X,Y ..." (hex cells) steers the walker from
// read n through those cells (one axis at a time, towards each cell's
// middle); with MW_EMIT=file the joystick changes go to a recorder script.
int sim(const std::filesystem::path& content, std::uint32_t seed, int stage_number, const std::filesystem::path& script_path,
        int every, const std::filesystem::path& png) {
    using namespace mindwalker;
    const data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    std::map<int, std::vector<std::vector<std::string>>> events;
    int last = 0;
    {
        std::ifstream in(script_path);
        std::string line;
        while (std::getline(in, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream f(line);
            std::vector<std::string> w;
            for (std::string x; f >> x;) w.push_back(x);
            if (w.size() >= 3 && w[0] == "A") {
                events[std::stoi(w[1])].push_back({w.begin() + 2, w.end()});
                last = std::max(last, std::stoi(w[1]));
            }
        }
    }
    landscape::LandscapeStage stage(files, assets, {seed, stage_number, 3, 0, {}});
    landscape::LandscapeInput held;
    int reads = 0, pending_gadget = -1;
    bool stop = false;
    std::vector<std::pair<int, int>> route;
    std::size_t route_next = 0;
    std::string last_joy = "-";
    std::FILE* emit = std::getenv("MW_EMIT") ? std::fopen(std::getenv("MW_EMIT"), "w") : nullptr;
    const landscape::InputReader reader = [&](landscape::InputRead kind) {
        if (route_next < route.size() && kind == landscape::InputRead::stick) {
            const auto& w = stage.walker();
            while (route_next < route.size() && w.cell_x == route[route_next].first && w.cell_y == route[route_next].second)
                ++route_next;
            std::string joy = "-";
            if (route_next < route.size()) {
                const int tx = route[route_next].first, ty = route[route_next].second;
                const int dx = ((tx - w.cell_x + 0x800) & 0xFFF) - 0x800, dy = ((ty - w.cell_y + 0x800) & 0xFFF) - 0x800;
                // Along x first; when x is right, keep to the cell's middle before turning.
                const int in_cell_x = (w.x % 80), in_cell_z = (w.z % 80);
                (void)in_cell_z;
                if (dx != 0) joy = dx < 0 ? "L" : "R";
                else if (dy != 0) {
                    if (in_cell_x < 24) joy = "R";
                    else if (in_cell_x > 56) joy = "L";
                    else joy = dy < 0 ? "D" : "U";
                }
            }
            if (std::getenv("MW_ROUTE_DEBUG"))
                std::printf("  r%d cell %03x %03x x %d next %zu joy %s\n", reads, stage.walker().cell_x, stage.walker().cell_y,
                            stage.walker().x, route_next, joy.c_str());
            held.left = joy == "L";
            held.right = joy == "R";
            held.up = joy == "U";
            held.down = joy == "D";
            if (joy != last_joy && emit) std::fprintf(emit, "A %d joy %s\n", reads, joy.c_str());
            last_joy = joy;
        }
        if (events.count(reads))
            for (const auto& ev : events.at(reads)) {
                if (ev[0] == "button") held.button = ev[1] == "1";
                if (ev[0] == "fire") held.fire = ev[1] == "1";
                if (ev[0] == "joy") {
                    held.up = ev[1].find('U') != std::string::npos;
                    held.down = ev[1].find('D') != std::string::npos;
                    held.left = ev[1].find('L') != std::string::npos;
                    held.right = ev[1].find('R') != std::string::npos;
                }
                if (ev[0] == "mouse") {
                    held.pointer_x = std::stoi(ev[1]);
                    held.pointer_y = std::stoi(ev[2]);
                }
                if (ev[0] == "gadget") pending_gadget = std::stoi(ev[1], nullptr, 16);
                if (ev[0] == "stop") stop = true;
                if (ev[0] == "route") {
                    route.clear();
                    route_next = 0;
                    for (std::size_t k = 1; k < ev.size(); ++k)
                        route.push_back({std::stoi(ev[k].substr(0, ev[k].find(',')), nullptr, 16),
                                         std::stoi(ev[k].substr(ev[k].find(',') + 1), nullptr, 16)});
                }
                if (emit && ev[0] != "route" && (ev[0] != "joy" || route_next >= route.size())) {
                    std::fprintf(emit, "A %d", reads);
                    for (const auto& word : ev) std::fprintf(emit, " %s", word.c_str());
                    std::fprintf(emit, "\n");
                }
            }
        if (reads % every == 0) {
            const auto& w = stage.walker();
            std::printf("read %5d cell %03x %03x view %03x %03x level %3d x %3d z %3d score %u lives %d damage %d "
                        "attached %d found %d%d places %d form %d%s%s\n",
                        reads, w.cell_x, w.cell_y, stage.view_x(), stage.view_y(), w.level, w.x, w.z, stage.score(),
                        stage.lives(), stage.damage(), stage.creatures().attached(), stage.found()[0], stage.found()[1],
                        stage.creature_places(), w.form, stage.ramp_closed() ? "" : " RAMP-OPEN", stage.finished() ? " FINISHED" : "");
        }
        ++reads;
        return held;
    };
    int frames = 0;
    std::vector<float> wav; // MW_WAV=file: the sound, 44.1 kHz stereo
    while (!stop && reads <= last && !stage.over() && frames < 200000) {
        const int g = pending_gadget;
        pending_gadget = -1;
        stage.frame(reader, g);
        ++frames;
        if (std::getenv("MW_WAV")) {
            std::vector<float> block(882 * 2, 0.0f);
            stage.render_audio(block, 44100);
            wav.insert(wav.end(), block.begin(), block.end());
        }
    }
    if (const char* path = std::getenv("MW_WAV")) {
        std::ofstream f(path, std::ios::binary);
        const auto put32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        const auto put16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("RIFF", 4);
        put32(std::uint32_t(36 + wav.size() * 2));
        f.write("WAVEfmt ", 8);
        put32(16);
        put16(1);
        put16(2);
        put32(44100);
        put32(44100 * 4);
        put16(4);
        put16(16);
        f.write("data", 4);
        put32(std::uint32_t(wav.size() * 2));
        for (const float v : wav) put16(std::uint16_t(std::int16_t(std::clamp(v, -1.0f, 1.0f) * 32000)));
    }
    const auto& w = stage.walker();
    std::printf("end: %d frames, %d reads, cell %03x %03x, score %u, lives %d%s, next seed %08x\n", frames, reads, w.cell_x,
                w.cell_y, stage.score(), stage.lives(), stage.finished() ? ", finished" : "", stage.next_seed());
    if (!png.empty()) platform::save_png(stage.picture(), png);
    return 0;
}

// The tunnel stage on a recording's B reads, compared at every frame's
// first read: pan, pace and its counters, the wander's mixer, the four
// rectangles and their mixers, the wait after a click.
int tunnel(const std::filesystem::path& content, const std::filesystem::path& script_path,
           const std::filesystem::path& log_path, int frame_limit, const std::filesystem::path& png) {
    using namespace mindwalker;
    const data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    std::map<int, std::vector<std::vector<std::string>>> events;
    std::map<int, std::vector<std::string>> carried; // T's and A's input events, in order
    int a_reads_before = 0;
    {
        std::ifstream in(script_path);
        std::string line;
        while (std::getline(in, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream f(line);
            std::vector<std::string> w;
            for (std::string x; f >> x;) w.push_back(x);
            if (w.size() >= 3 && w[0] == "B") events[std::stoi(w[1])].push_back({w.begin() + 2, w.end()});
            // The recorder holds the inputs across modules: A's last ones carry into B.
            if (w.size() >= 3 && (w[0] == "A" || w[0] == "T") && (w[2] == "joy" || w[2] == "button" || w[2] == "fire"))
                carried[std::stoi(w[1]) + (w[0] == "A" ? 100000 : 0)] = {w.begin() + 2, w.end()};
        }
    }
    struct Read {
        int index;
        std::string address;
        std::map<std::string, std::string> fields;
    };
    std::vector<Read> reads;
    {
        std::ifstream in(log_path);
        std::string line;
        // MW_SESSION=n: the nth time B ran in the recording (from 1).
        const int wanted = std::getenv("MW_SESSION") ? std::atoi(std::getenv("MW_SESSION")) : 1;
        int session = 0;
        while (std::getline(in, line)) {
            if (line.starts_with("# module B")) ++session;
            if (line.starts_with("A ") && session < wanted) ++a_reads_before; // (A's reads count on across its runs)
            if (!line.starts_with("B ") || session != wanted) continue;
            std::istringstream f(line);
            std::string m, addr;
            int index;
            f >> m >> index >> addr;
            auto fs = fields(line);
            // (A button read logged twice with the same state, the moving
            // reads missing between: the stub reported one stop twice.)
            if (!reads.empty() && addr == "300418" && reads.back().address == "300418" && reads.back().fields == fs) continue;
            reads.push_back({index, addr, std::move(fs)});
        }
    }
    if (reads.empty()) throw std::runtime_error("no B reads in the log");
    const auto word = [](const std::string& hexs, int offset, int bytes) {
        return std::stol(hexs.substr(std::size_t(offset * 2), std::size_t(bytes * 2)), nullptr, 16);
    };
    const auto& sh = reads.front().fields.at("shared");
    const auto at = [&](int offset) { return std::uint32_t(word(sh, offset - 0x10, 4)); };
    tunnel::TunnelStage::Start start{at(0x4C), int(at(0x5C)), int(at(0x6E)), at(0x66)};
    // (B's flags may already be set at its first read: the start state is the log's.)
    tunnel::TunnelStage stage(files, assets, start);
    tunnel::TunnelInput held;
    const auto apply = [&](const std::vector<std::string>& ev) {
        if (ev[0] == "button") held.button = ev[1] == "1";
        if (ev[0] == "fire") held.fire = ev[1] == "1";
        if (ev[0] == "joy") {
            held.up = ev[1].find('U') != std::string::npos;
            held.down = ev[1].find('D') != std::string::npos;
            held.left = ev[1].find('L') != std::string::npos;
            held.right = ev[1].find('R') != std::string::npos;
        }
    };
    for (const auto& [index, ev] : carried)
        if (index < 100000 || index - 100000 < a_reads_before) apply(ev);
    std::size_t next = 0;
    int frames = 0, differences = 0;
    bool out_of_step = false;
    const auto compare = [&](const Read& r) {
        struct Field {
            const char* name;
            long ours, theirs;
        };
        const auto s32 = [&](const std::string& f, int o) { return long(std::int32_t(std::uint32_t(word(r.fields.at(f), o, 4)))); };
        std::vector<Field> list = {{"ticks", stage.ticks(), s32("tunnel", 0)},  {"pace", stage.pace(), s32("tunnel", 4)},
                                   {"laps", stage.laps(), s32("tunnel", 8)},    {"pan x", stage.pan_x(), s32("tunnel", 0xC)},
                                   {"pan y", stage.pan_y(), s32("tunnel", 0x10)},
                                   {"wander", long(stage.wander().reg), long(word(r.fields.at("tunnel"), 0x14, 4))},
                                   {"frozen", stage.frozen(), long(word(r.fields.at("wait"), 0, 2))}};
        for (int k = 0; k < 4; ++k) {
            const auto& rect = stage.rects()[std::size_t(k)];
            list.push_back({"rect size", rect.size, s32("rects", 0x60 + 4 * k)});
            list.push_back({"rect x", rect.x, s32("rects", 0x70 + 4 * k)});
            list.push_back({"rect y", rect.y, s32("rects", 0x80 + 4 * k)});
        }
        for (int k = 0; k < 12; ++k)
            list.push_back({"rect mixer", long(stage.rect_random()[std::size_t(k)].reg), long(word(r.fields.at("rects"), 8 * k, 4))});
        for (const auto& f : list)
            if (f.ours != f.theirs && ++differences <= print_limit)
                std::printf("B read %d (frame %d) %s: ours %ld, original %ld\n", r.index, frames, f.name, f.ours, f.theirs);
    };
    const tunnel::TunnelReader reader = [&](tunnel::TunnelRead kind) {
        const char* expected = kind == tunnel::TunnelRead::button ? "300418" : kind == tunnel::TunnelRead::stick ? "3026ce" : "30271a";
        if (next >= reads.size()) return held;
        const auto& r = reads[next];
        if (r.address != expected && !out_of_step) {
            out_of_step = true;
            ++differences;
            std::printf("B read %d: the original read %s, the stage %s\n", r.index, r.address.c_str(), expected);
        }
        if (kind == tunnel::TunnelRead::button) compare(r);
        if (const char* list = std::getenv("MW_SHOTS")) { // research: read:file.png,...
            std::istringstream in(list);
            for (std::string item; std::getline(in, item, ',');)
                if (std::stoi(item.substr(0, item.find(':'))) == r.index)
                    platform::save_png(stage.picture(), item.substr(item.find(':') + 1));
        }
        if (events.count(r.index))
            for (const auto& ev : events.at(r.index)) apply(ev);
        ++next;
        return held;
    };
    while (next < reads.size() && !out_of_step && frames < frame_limit && !stage.over()) {
        stage.frame(reader);
        ++frames;
    }
    const char* outcome[] = {"playing", "caught", "ran out", "quit"};
    std::printf("%d frames, %zu reads compared, %d differences; outcome %s, score %u\n", frames, next, differences,
                outcome[int(stage.outcome())], stage.score());
    if (!png.empty()) platform::save_png(stage.picture(), png);
    return differences == 0 ? 0 : 1;
}

// The arena (C) of a recording: the original's maze taken from its first
// read (ours draws mazes from a standard generator), the scripted inputs
// (T's, A's and B's before C carried, as the recorder holds them), and at
// every read the deterministic state compared: read order, position,
// room, aim, target taken, the figure's frame while walking or aiming;
// pokes of the pulse's counter applied. Random parts (creatures, wind,
// pulse rounds) differ by design and are left out.
int arena(const std::filesystem::path& content, const std::filesystem::path& script_path,
          const std::filesystem::path& log_path, int frame_limit, const std::filesystem::path& png) {
    using namespace mindwalker;
    const data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    std::map<int, std::vector<std::vector<std::string>>> events;
    std::vector<std::pair<std::string, std::vector<std::string>>> carried; // T's, A's, B's input events
    std::vector<int> carried_index;
    {
        std::ifstream in(script_path);
        std::string line;
        while (std::getline(in, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream f(line);
            std::vector<std::string> w;
            for (std::string x; f >> x;) w.push_back(x);
            if (w.size() >= 3 && w[0] == "C") events[std::stoi(w[1])].push_back({w.begin() + 2, w.end()});
            if (w.size() >= 3 && (w[0] == "A" || w[0] == "T" || w[0] == "B") &&
                (w[2] == "joy" || w[2] == "button" || w[2] == "fire")) {
                carried.push_back({w[0], {w.begin() + 2, w.end()}});
                carried_index.push_back(std::stoi(w[1]));
            }
        }
    }
    struct Read {
        int index;
        std::string address;
        std::map<std::string, std::string> fields;
    };
    std::vector<Read> reads;
    std::map<std::string, int> reads_before; // each module's reads before C's (first) run
    std::map<int, int> duplicates;           // a read logged twice: its index -> the read it repeats
    {
        std::ifstream in(log_path);
        std::string line;
        bool started = false;
        while (std::getline(in, line)) {
            if (line.starts_with("# module C")) started = true;
            if (!started && line.size() > 2 && line[1] == ' ' && line[0] != '#') ++reads_before[line.substr(0, 1)];
            if (started && line.starts_with("# module") && !line.starts_with("# module C")) break;
            if (!started || !line.starts_with("C ")) continue;
            std::istringstream f(line);
            std::string m, addr;
            int index;
            f >> m >> index >> addr;
            auto fs = fields(line);
            if (line.find("=gone") != std::string::npos) continue; // (a stop after C freed its memory)
            // (A read logged twice with the same state: the stub reported
            // one stop twice. Its events go with the read before.)
            if (!reads.empty() && addr == reads.back().address && fs == reads.back().fields) {
                duplicates[index] = reads.back().index;
                continue;
            }
            reads.push_back({index, addr, std::move(fs)});
        }
    }
    if (reads.empty()) throw std::runtime_error("no C reads in the log");
    for (const auto& [dup, before] : duplicates)
        if (events.count(dup)) {
            auto moved = events.at(dup);
            events.erase(dup);
            for (auto& ev : moved) events[before].push_back(std::move(ev));
        }
    const auto word = [](const std::string& hexs, int offset, int bytes) {
        return std::stol(hexs.substr(std::size_t(offset * 2), std::size_t(bytes * 2)), nullptr, 16);
    };
    const auto& first = reads.front().fields;
    const auto& sh = first.at("shared");
    const auto at = [&](int offset) { return std::uint32_t(word(sh, offset - 0x10, 4)); };
    // The original's maze: the map's two planes, the rooms' creatures, the target, the difficulty.
    arena::ArenaMap map;
    for (int y = 0; y < arena::ArenaMap::height; ++y)
        for (int x = 0; x < arena::ArenaMap::width; ++x) {
            const int bit = 7 - x % 8, byte = y * 8 + x / 8;
            const int v = int(word(first.at("map0"), byte, 1) >> bit & 1) | int(word(first.at("map1"), byte, 1) >> bit & 1) << 1;
            map.set(x, y, arena::Cell(v));
        }
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y) map.creatures[std::size_t(x)][std::size_t(y)] = int(word(first.at("counts"), (x * 8 + y) * 4, 4));
    map.target_x = int(word(first.at("room"), 0x10, 4));
    map.target_y = int(word(first.at("room"), 0x14, 4));
    map.difficulty = int(word(first.at("room"), 0x18, 4));
    arena::ArenaStage::Start start{at(0x4C), int(at(0x5C)), int(at(0x6E)), at(0x66), int(at(0x48)), map};
    if (std::getenv("MW_OWN_MAZE")) start.map.reset(); // research: our generator's maze instead
    arena::ArenaStage stage(files, assets, start);
    std::printf("C: seed %08x stage %d lives %d score %u form %d; window top %ld (ours 10)\n", start.seed, start.stage,
                start.lives, start.score, start.form, word(first.at("flags"), 0xA, 2));
    arena::ArenaInput held;
    const auto apply = [&](const std::vector<std::string>& ev) {
        if (ev[0] == "button") held.button = ev[1] == "1";
        if (ev[0] == "fire") held.fire = ev[1] == "1";
        if (ev[0] == "joy") {
            held.up = ev[1].find('U') != std::string::npos;
            held.down = ev[1].find('D') != std::string::npos;
            held.left = ev[1].find('L') != std::string::npos;
            held.right = ev[1].find('R') != std::string::npos;
        }
        if (ev[0] == "poke" && ev[1] == "402EC4") stage.set_pulse(int(std::stoul(ev[2], nullptr, 16)));
    };
    for (std::size_t i = 0; i < carried.size(); ++i)
        if (carried[i].first == "T" || carried_index[i] < reads_before[carried[i].first]) apply(carried[i].second);
    std::size_t next = 0;
    int frames = 0, differences = 0;
    bool out_of_step = false;
    const auto compare = [&](const Read& r) {
        struct Field {
            const char* name;
            long ours, theirs;
        };
        const auto s32 = [&](const std::string& f, int o) { return long(std::int32_t(std::uint32_t(word(r.fields.at(f), o, 4)))); };
        const auto& p = stage.player();
        std::vector<Field> list = {{"x", p.x, s32("player", 0x3E)},          {"y", p.y, s32("player", 0x42)},
                                   {"room x", p.room_x, s32("room", 0)},      {"room y", p.room_y, s32("room", 4)},
                                   {"aim", p.aim, s32("aim", 0)},             {"target", p.has_target, word(r.fields.at("flags"), 8, 2)},
                                   {"frozen", stage.frozen(), word(r.fields.at("wait"), 0, 2)},
                                   {"drone", stage.sound_period(), word(r.fields.at("sound"), 0x16, 2)}};
        if (p.frame < 0x18 || s32("player", 0x36) < 0x18) list.push_back({"frame", p.frame, s32("player", 0x36)});
        for (const auto& f : list)
            if (f.ours != f.theirs && ++differences <= print_limit)
                std::printf("C read %d (frame %d) %s: ours %ld, original %ld\n", r.index, frames, f.name, f.ours, f.theirs);
    };
    const auto reader = [&](arena::ArenaRead kind) {
        const char* expected = kind == arena::ArenaRead::button ? "4005c0" : kind == arena::ArenaRead::stick ? "402528" : "402574";
        if (next >= reads.size()) {
            out_of_step = true;
            return held;
        }
        const auto& r = reads[next];
        if (r.address != expected && !out_of_step) {
            out_of_step = true;
            ++differences;
            std::printf("C read %d: the original read %s, the stage %s\n", r.index, r.address.c_str(), expected);
        }
        if (kind == arena::ArenaRead::button) compare(r);
        if (const char* list = std::getenv("MW_SHOTS")) { // research: read:file.png,...
            std::istringstream in(list);
            for (std::string item; std::getline(in, item, ',');)
                if (std::stoi(item.substr(0, item.find(':'))) == r.index)
                    platform::save_png(stage.picture(), item.substr(item.find(':') + 1));
        }
        if (events.count(r.index))
            for (const auto& ev : events.at(r.index)) apply(ev);
        ++next;
        return held;
    };
    while (next < reads.size() && !out_of_step && frames < frame_limit && !stage.over()) {
        stage.frame(reader);
        ++frames;
    }
    // Run out the closing sequence (no reads).
    while (!stage.over() && (stage.outcome() != arena::ArenaStage::Outcome::playing || stage.in_sequence()) && frames < frame_limit) {
        stage.frame(reader);
        ++frames;
    }
    const char* outcome[] = {"playing", "won", "left", "died", "quit"};
    const auto& last = reads.back().fields;
    std::printf("%d frames, %zu of %zu reads compared, %d differences; outcome %s, score %u (original at its last read %ld), "
                "strength %d (original %ld)\n",
                frames, next, reads.size(), differences, outcome[int(stage.outcome())], stage.score(),
                word(last.at("shared"), 0x66 - 0x10, 4), stage.player().strength,
                long(std::int32_t(std::uint32_t(word(last.at("player"), 0x3A, 4)))));
    if (!png.empty()) platform::save_png(stage.picture(), png);
    return differences == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "title" && (argc == 5 || argc == 6)) {
            return title(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), platform::utf8_path(argv[4]),
                         argc == 6 ? platform::utf8_path(argv[5]) : std::filesystem::path());
        }
        if (command == "walk" && argc >= 6 && argc <= 8) {
            return walk(platform::utf8_path(argv[2]), std::uint32_t(std::stoul(argv[3], nullptr, 16)),
                        platform::utf8_path(argv[4]), platform::utf8_path(argv[5]), argc >= 7 ? std::stoi(argv[6]) : 1 << 30,
                        argc == 8 ? platform::utf8_path(argv[7]) : std::filesystem::path());
        }
        if (command == "tunnel" && argc >= 5 && argc <= 7) {
            return tunnel(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), platform::utf8_path(argv[4]),
                          argc >= 6 ? std::stoi(argv[5]) : 1 << 30, argc == 7 ? platform::utf8_path(argv[6]) : std::filesystem::path());
        }
        if (command == "arena" && argc >= 5 && argc <= 7) {
            return arena(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), platform::utf8_path(argv[4]),
                         argc >= 6 ? std::stoi(argv[5]) : 1 << 30, argc == 7 ? platform::utf8_path(argv[6]) : std::filesystem::path());
        }
        if (command == "sim" && argc >= 6 && argc <= 8) {
            return sim(platform::utf8_path(argv[2]), std::uint32_t(std::stoul(argv[3], nullptr, 16)), std::stoi(argv[4]),
                       platform::utf8_path(argv[5]), argc >= 7 ? std::stoi(argv[6]) : 100,
                       argc == 8 ? platform::utf8_path(argv[7]) : std::filesystem::path());
        }
        if (command == "view" && argc == 6) {
            return view(platform::utf8_path(argv[2]), std::uint32_t(std::stoul(argv[3], nullptr, 16)),
                        platform::utf8_path(argv[4]), platform::utf8_path(argv[5]));
        }
        std::fprintf(stderr, "usage: mindwalker_replay title <content> <script> <run.log> [dump dir]\n"
                             "       mindwalker_replay view <content> <seed> <dump .idx> <out.png>\n"
                             "       mindwalker_replay walk <content> <seed> <script> <run.log> [frames [last picture.png]]\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "mindwalker_replay: %s\n", error.what());
        return 1;
    }
}
