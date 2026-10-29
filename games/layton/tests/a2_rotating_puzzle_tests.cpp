#include "puzzles/a2_tile_puzzle.hpp"
#include "test_assets.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace picarat;

void check(bool value, const char* label) {
    if (!value) {
        throw std::runtime_error(label);
    }
}

int main(int argc, char** argv) try {
    requireTestPack(argc, argv);
    if (argc != 3) {
        throw std::runtime_error("a2_rotating_puzzle_tests ASSETS INPUTS");
    }
    auto script = readFile(std::filesystem::path(argv[1]) / "puzzle/q203_param.gds");
    TilePuzzle puzzle(script, 203);
    check(!puzzle.solved(), "empty answer list is not a solved special puzzle");
    std::ifstream file(std::filesystem::path(argv[2]) / "rotation-203-solve-c.tsv");
    std::string line;
    unsigned frame = 0;
    bool down = false;
    int x = -1, y = -1;
    auto advance = [&](unsigned until, unsigned amplitude) {
        while (frame < until) {
            bool held = x >= 0;
            puzzle.update({held, held && !down, !held && down, x, y}, amplitude);
            puzzle.animate();
            down = held;
            ++frame;
        }
    };
    while (std::getline(file, line)) {
        std::istringstream row(line);
        unsigned next;
        std::string mask;
        int nx, ny;
        row >> next >> mask >> nx >> ny;
        if (next >= 242) {
            break;
        }
        advance(next, 0);
        x = nx;
        y = ny;
    }
    advance(242, 0);
    check(puzzle.stage() == 2, "rotations and raised sun enter microphone stage");
    advance(frame + 60, 192);
    check(puzzle.secondaryCounter() == 0 && !puzzle.solved(),
          "192 does not exceed microphone threshold");
    advance(frame + 30, 193);
    check(puzzle.secondaryCounter() == 30 && puzzle.state() == 0,
          "thirty loud samples do not complete");
    advance(frame + 1, 192);
    check(puzzle.secondaryCounter() == 0, "quiet sample resets continuity");
    advance(frame + 31, 193);
    check(puzzle.state() == 5 && puzzle.stage() == 3 && !puzzle.solved(),
          "31 loud samples start acceptance");
    advance(frame + 120, 0);
    check(!puzzle.solved(), "wait through 120 acceptance updates");
    advance(frame + 1, 0);
    check(puzzle.solved() && puzzle.stage() == 5, "121 updates finish acceptance and brightness");
    puzzle.reset();
    check(!puzzle.solved() && puzzle.stage() == 0 && puzzle.pieces()[0].rotation == 2 &&
              puzzle.pieces()[1].rotation == 2 && puzzle.pieces()[2].slot == 2,
          "reset restores special puzzle stages");
    std::cout << "Microphone threshold, interruption, acceptance timing and reset passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
