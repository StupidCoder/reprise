// Mind Walker research tool: checks the rebuilt world generator against the
// original's answers recorded by .analysis/mindwalker_amiga/harness/worldref.py.
//
//   mindwalker_world check <content folder> <worldref .txt>
//   mindwalker_world show <content folder> <seed hex>
//   mindwalker_world views <content folder> <seed hex> <viewref .txt>

#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include "mindwalker_amiga/landscape/view_cells.hpp"
#include <vector>
#include "platform/host.hpp"
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

using mindwalker::world::World;

void print_world(const World& world) {
    std::printf("keys %04x %04x %04x %04x levels %d %d %d %d start %03x %03x\n", world.keys()[0], world.keys()[1],
                world.keys()[2], world.keys()[3], world.floor_level(), world.plain_level(), world.ceiling_level(),
                world.alternate_level(), world.start_x(), world.start_y());
}

int check(const std::filesystem::path& content, const std::filesystem::path& reference) {
    const mindwalker::data::GameFiles files(content);
    std::ifstream in(reference);
    std::string line;
    std::optional<World> world;
    int checked = 0, failed = 0;
    const auto fail = [&](const std::string& what) {
        if (++failed <= 10) std::printf("DIFF %s\n", what.c_str());
    };
    while (std::getline(in, line)) {
        std::istringstream f(line);
        std::string kind;
        f >> kind;
        if (kind == "seed") {
            std::uint32_t seed;
            f >> std::hex >> seed;
            world.emplace(seed, files.program(mindwalker::data::Stage::landscape));
            print_world(*world);
        } else if (kind == "keys") {
            std::ostringstream ours;
            char text[160];
            std::snprintf(text, sizeof text, "keys %04x %04x %04x %04x levels %d %d %d %d start %03x %03x",
                          world->keys()[0], world->keys()[1], world->keys()[2], world->keys()[3], world->floor_level(),
                          world->plain_level(), world->ceiling_level(), world->alternate_level(), world->start_x(),
                          world->start_y());
            ++checked;
            if (line != text) fail(line + " / ours: " + text);
        } else if (kind == "row" || kind == "col") {
            unsigned coordinate;
            f >> std::hex >> coordinate;
            const auto levels = (kind == "row" ? world->rows() : world->columns()).at(std::uint16_t(coordinate));
            for (const auto& level : levels) {
                unsigned a, b;
                f >> a >> b;
                ++checked;
                if (a != level.a || b != level.b) fail(line);
            }
        } else if (kind == "cell") {
            unsigned x, y;
            int height, centre, beacon, marked;
            f >> std::hex >> x >> y >> std::dec >> height >> centre >> beacon >> marked;
            const auto cell = world->platform(std::uint16_t(x), std::uint16_t(y));
            ++checked;
            // ($209206 clears the flags for open cells too.)
            if (cell.height != height || cell.centre != (centre != 0) || cell.beacon != (beacon != 0) ||
                cell.marked != (marked != 0))
                fail(line);
        }
    }
    std::printf("%d values checked, %d differ\n", checked, failed);
    return failed == 0 ? 0 : 1;
}

// Checks the view builder against viewref.py's file (same seed as `seed`).
int views(const std::filesystem::path& content, std::uint32_t seed, const std::filesystem::path& reference) {
    const mindwalker::data::GameFiles files(content);
    const auto& a = files.program(mindwalker::data::Stage::landscape);
    const World world(seed, a);
    std::ifstream in(reference);
    std::string line, kind;
    mindwalker::landscape::ViewInputs inputs;
    std::vector<std::uint8_t> map;
    unsigned x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    int wrap_x = 0, wrap_y = 0;
    std::array<std::uint16_t, 32> colours{};
    int checked = 0, failed = 0;
    while (std::getline(in, line)) {
        std::istringstream f(line);
        f >> kind;
        if (kind == "state") {
            unsigned hx, hy, c17, c18;
            int found0, found1;
            std::string hex;
            f >> std::dec >> inputs.stage >> std::hex >> hx >> hy >> std::dec >> found0 >> found1 >> std::hex >> c17 >> c18 >>
                x0 >> x1 >> y0 >> y1 >> std::dec >> wrap_x >> wrap_y >> hex;
            inputs.home_x = std::uint16_t(hx);
            inputs.home_y = std::uint16_t(hy);
            inputs.found = {found0 != 0, found1 != 0};
            inputs.colour17 = std::uint16_t(c17);
            inputs.colour18 = std::uint16_t(c18);
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2) map.push_back(std::uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
            // $205C12/$205C88: inside the 64 x 64 window (with wrap), then the
            // map's pixel ($205B10).
            inputs.visited = [&](std::uint16_t cx, std::uint16_t cy) {
                const auto inside = [](unsigned v, unsigned lo, unsigned hi, int wrap) {
                    return wrap ? (lo <= v || v <= hi) : (lo <= v && v <= hi);
                };
                if (!inside(cx, x0, x1, wrap_x) || !inside(cy, y0, y1, wrap_y)) return false;
                const unsigned col = (cx - x0) & 63, row = (cy - y0) & 63;
                return (map[row * 8 + col / 8] >> (7 - col % 8) & 1) != 0;
            };
        } else if (kind == "colours") {
            for (auto& c : colours) f >> std::hex >> c;
        } else if (kind == "view") {
            unsigned x, y, px, py;
            f >> std::hex >> x >> y >> px >> py;
            std::vector<unsigned> words(68);
            for (auto& v : words) f >> std::hex >> v;
            inputs.previous_x = std::uint16_t(px);
            inputs.previous_y = std::uint16_t(py);
            const auto view = mindwalker::landscape::build_view(world, a, std::uint16_t(x), std::uint16_t(y), inputs, colours);
            std::vector<unsigned> ours;
            for (const auto& column : view.terrain) for (const auto v : column) ours.push_back(v);
            for (const auto& column : view.objects) for (const auto v : column) ours.push_back(v);
            for (const auto v : view.colours) ours.push_back(v);
            // The original's tables are [y][x]... compare as stored: terrain
            // words in the original's order (index = column * 4 + row).
            bool same = true;
            for (std::size_t k = 0; k < 64; ++k) same &= ours[k] == words[k];
            // Flags: $2099A4, A6, A8, AA (words), AC (word), B0.., B8.
            const unsigned water = words[64], m2000 = words[65], beacon = words[66], find = words[67];
            same &= (water != 0) == view.water && (m2000 != 0) == view.marker_2000 && (beacon != 0) == view.beacon &&
                    (find != 0) == view.find;
            ++checked;
            if (!same && ++failed <= 6) {
                std::printf("DIFF view %03x %03x prev %03x %03x\n  ours:", x, y, px, py);
                for (std::size_t k = 0; k < 64; ++k) std::printf(" %04x", ours[k]);
                std::printf("\n  orig:");
                for (std::size_t k = 0; k < 68; ++k) std::printf(" %04x", words[k]);
                std::printf("\n");
            }
            colours = {};
            for (std::size_t k = 0; k < 32; ++k) colours[k] = std::uint16_t(words[32 + k]);
        }
    }
    std::printf("%d views checked, %d differ\n", checked, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "check" && argc == 4) {
            return check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "views" && argc == 5) {
            return views(platform::utf8_path(argv[2]), std::uint32_t(std::stoul(argv[3], nullptr, 16)),
                         platform::utf8_path(argv[4]));
        }
        if (command == "show" && argc == 4) {
            const mindwalker::data::GameFiles files(platform::utf8_path(argv[2]));
            print_world(World(std::uint32_t(std::stoul(argv[3], nullptr, 16)),
                              files.program(mindwalker::data::Stage::landscape)));
            return 0;
        }
        std::fprintf(stderr, "usage: mindwalker_world check <content> <ref.txt> | show <content> <seed>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "mindwalker_world: %s\n", error.what());
        return 1;
    }
}
