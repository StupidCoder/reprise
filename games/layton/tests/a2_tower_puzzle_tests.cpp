#include "puzzles/a2_tower_puzzle.hpp"
#include "test_assets.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace picarat;

void require(bool b, const char* message) {
    if (!b) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char** argv) try {
    requireTestPack(argc, argv);
    require(argc == 2, "assets argument");
    for (auto [id, n] : {std::pair{5, 3}, std::pair{82, 4}, std::pair{83, 5}, std::pair{141, 7}}) {
        TowerPuzzle p(readFile(std::filesystem::path(argv[1]) /
                               ("puzzle/q" + std::to_string(id) + "_param.gds")));
        auto step = [&](Touch t) {
            p.update(t);
            p.animate();
        };
        auto wait = [&](int frames) {
            for (int f = 0; f < frames; ++f) {
                step({});
            }
        };
        wait(100);
        require(p.count() == n, "GDS count");
        require(p.stacks()[0].count == n, "initial stack");
        step({true, true, false, 32, 95});
        require(p.selected() == -1, "upper area ignores press");
        step({});
        step({true, true, false, 193, 140});
        require(p.selected() == -1, "control strip ignores press");
        step({});
        step({true, true, false, 192, 140});
        require(p.selected() == -1, "empty right stack");
        step({});
        auto move = [&](int a, int b, int y = 75) {
            step({true, true, false, 32 + 64 * a, 140});
            for (int f = 0; f < 12; ++f) {
                step({true, false, false, 32 + 64 * b, y});
            }
            step({false, false, true, -1, -1});
            wait(60);
        };
        move(0, 0);
        require(p.moves() == 0, "same plate no count");
        move(0, 1);
        move(0, 1);
        require(p.moves() == 1 && p.stacks()[0].count == n - 1, "illegal bigger pancake rejected");
        move(0, 1, 180);
        require(p.moves() == 1, "low rejection no count");
        p.reset();
        wait(100);
        require(p.moves() == 0 && p.stacks()[0].count == n, "reset");
        step({true, true, false, 32, 140});
        step({true, false, false, 0, 0});
        require(p.pieces()[0].x == -4 && p.pieces()[0].y == 14, "upper drag clamps");
        step({true, false, false, 255, 191});
        require(p.pieces()[0].x == 140 && p.pieces()[0].y == 172, "lower drag clamps");
        step({false, false, true});
        wait(60);
        require(p.stacks()[2].count == 1, "right edge release plate");
        p.reset();
        wait(100);
        // Exact x64 and x128 ties belong to the middle plate.
        step({true, true, false, 32, 140});
        step({true, false, false, 128, 75});
        step({false, false, true});
        wait(60);
        require(p.stacks()[1].count == 1, "x128 middle tie");
        p.reset();
        wait(100);
        std::function<void(int, int, int, int)> solve = [&](int count, int a, int b, int c) {
            if (!count) {
                return;
            }
            solve(count - 1, a, c, b);
            move(a, b);
            solve(count - 1, c, b, a);
        };
        solve(n, 0, 2, 1);
        require(p.solved() && p.moves() == (1 << n) - 1, "solution and minimum move count");
        p.reset();
        wait(100);
        require(!p.solved() && p.moves() == 0, "reset clears completion");
    }
    std::cout
        << "All four type13 puzzles, rejection, hit edges, clamp, reset and solutions passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
