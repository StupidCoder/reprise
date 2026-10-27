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
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <fstream>
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
int walk(const std::filesystem::path& content, std::uint32_t seed, const std::filesystem::path& script_path,
         const std::filesystem::path& log_path, int frame_limit, const std::filesystem::path& png) {
    using namespace mindwalker;
    const data::GameFiles files(content);
    const auto assets = data::load_assets(content);
    // Script events by module and read index; the held state carries over
    // from T (its events up to its last logged read).
    std::map<std::pair<char, int>, std::vector<std::vector<std::string>>> events;
    {
        std::ifstream in(script_path);
        std::string line;
        while (std::getline(in, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream f(line);
            std::vector<std::string> w;
            for (std::string x; f >> x;) w.push_back(x);
            if (w.size() >= 3 && (w[0] == "T" || w[0] == "A")) events[{w[0][0], std::stoi(w[1])}].push_back({w.begin() + 2, w.end()});
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
        while (std::getline(in, line)) {
            if (line.starts_with("T ")) ++t_reads;
            if (!line.starts_with("A ")) continue;
            std::istringstream f(line);
            std::string m, addr;
            int index;
            f >> m >> index >> addr;
            reads.push_back({index, addr, fields(line)});
        }
    }
    landscape::LandscapeInput held;
    const auto apply = [&](char module, int index) {
        if (!events.count({module, index})) return;
        for (const auto& ev : events.at({module, index})) {
            if (ev[0] == "button") held.button = ev[1] == "1";
            if (ev[0] == "fire") held.fire = ev[1] == "1";
            if (ev[0] == "joy") {
                held.up = ev[1].find('U') != std::string::npos;
                held.down = ev[1].find('D') != std::string::npos;
                held.left = ev[1].find('L') != std::string::npos;
                held.right = ev[1].find('R') != std::string::npos;
            }
        }
    };
    for (int i = 0; i < t_reads; ++i) apply('T', i);

    landscape::LandscapeStage stage(files, assets, {seed, 0, 3, 0});
    std::size_t next = 0;
    int frames = 0, differences = 0;
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
            {"x", me.x, signed32(4)}, {"z", me.z, signed32(0xC)}, {"level", me.level, signed32(8)},
            {"cell x", me.cell_x, signed32(0x10)}, {"cell y", me.cell_y, signed32(0x14)},
            {"screen x", me.screen_x, signed32(0x18)}, {"screen y", me.screen_y, signed32(0x1C)},
            {"terrain", me.terrain, signed32(0x30) & 0xFFFF}, {"frame", me.frame, signed32(0x76)},
            {"speed", me.speed, signed32(0x82)}, {"view x", stage.view_x(), word(r.fields.at("view"), 0, 2)},
            {"view y", stage.view_y(), word(r.fields.at("view"), 2, 2)},
            {"direction", stage.direction(), word(r.fields.at("direction"), 0, 4)}};
        for (const auto& f : list) {
            if (f.ours != f.theirs && ++differences <= 15)
                std::printf("A read %d (frame %d) %s: ours %ld, original %ld\n", r.index, frames, f.name, f.ours, f.theirs);
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
                if (ours[k] != theirs[k] && ++differences <= 15)
                    std::printf("A read %d (frame %d) creature %d %s: ours %ld, original %ld\n", r.index, frames, i,
                                names[k], ours[k], theirs[k]);
        }
        for (const auto& f : more)
            if (f.ours != f.theirs && ++differences <= 15)
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
        if (kind == landscape::InputRead::button) compare(r);
        apply('A', r.index);
        ++next;
        return held;
    };
    while (next < reads.size() && !out_of_step && frames < frame_limit) {
        stage.frame(reader);
        ++frames;
    }
    std::printf("%d frames, %zu reads compared, %d differences; score %u\n", frames, next, differences, stage.score());
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
