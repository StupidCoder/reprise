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
                for (int px = 14; px < 320; ++px) bar_bad += idx[std::size_t(py * 320 + px)] != canvas.pen(px, py);
            if (direction == 0 && top == 10) std::printf("bar (x 14-319): %d of %d pens differ\n", bar_bad, 10 * 306);
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

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "title" && (argc == 5 || argc == 6)) {
            return title(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), platform::utf8_path(argv[4]),
                         argc == 6 ? platform::utf8_path(argv[5]) : std::filesystem::path());
        }
        if (command == "view" && argc == 6) {
            return view(platform::utf8_path(argv[2]), std::uint32_t(std::stoul(argv[3], nullptr, 16)),
                        platform::utf8_path(argv[4]), platform::utf8_path(argv[5]));
        }
        std::fprintf(stderr, "usage: mindwalker_replay title <content> <script> <run.log> [dump dir]\n"
                             "       mindwalker_replay view <content> <seed> <dump .idx> <out.png>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "mindwalker_replay: %s\n", error.what());
        return 1;
    }
}
