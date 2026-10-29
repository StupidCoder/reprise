#include "puzzles/a2_puzzle_progress.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace picarat;

static void check(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

struct Host final : PicaratCountHost {
    unsigned frames = 0, queries = 0, stops = 0, confirmFrame = 30;
    std::vector<std::array<std::uint32_t, 5>> draws;

    void waitOneFrame() override { ++frames; }

    bool confirmed() override {
        ++queries;
        return frames >= confirmFrame;
    }

    void stopCountSound() override { ++stops; }

    void update() override {}

    void present(std::uint32_t total, std::uint32_t remaining, std::uint32_t elapsed,
                 bool done) override {
        draws.push_back({frames, elapsed, total, remaining, unsigned(done)});
        ++frames;
        if (frames > 100) {
            throw std::runtime_error("counter failed to exit");
        }
    }
};

int main(int argc, char** argv) try {
    PuzzleSession session;
    session.first.packed = 0xc0;
    session.first.enter();
    check(session.first.packed == 0xc1, "enter preserves high bits");
    session.first.finishFailure();
    session.first.finishFailure();
    session.first.finishFailure();
    session.first.finishFailure();
    check(session.first.packed == 0xcd, "failures saturate");
    session.first.setFailures(0);
    check(session.first.failures() == 3, "failures monotonic");
    session.first.setFailures(256);
    check(session.first.failures() == 0, "truncate before cap; compare original argument");
    session.first.setHints(0xffffffff);
    check(session.first.hints() == 0, "signed hint comparison");
    session.first.setHints(0xffffffff, true);
    check(session.first.hints() == 3, "forced negative hint saturates");
    session.first.setHints(0, true);
    check(session.purchaseHint(1) && session.coins == 9, "hint coin debit");
    session.first.enter();
    check(session.first.hints() == 1 && session.coins == 9, "reentry retains purchased hint");
    session.coins = 0;
    check(!session.purchaseHint(2) && session.first.hints() == 1, "zero coins");
    std::uint32_t reward = 0;
    session.picarats = 65530;
    check(session.award(1, 0, {10, 9, 8}, reward) && reward == 10 && session.picarats == 4,
          "16-bit award storage wraps");
    check(session.first.status() == 1, "award precedes solved commit");
    session.first.setStatus(2);
    session.first.finishFailure();
    check(session.first.failures() == 0 && !session.award(1, 0, {10, 9, 8}, reward),
          "solved replay cannot gain failures or award");
    session.first.setStatus(0);
    check(session.first.status() == 2, "status monotonic");
    session.first.setStatus(0, true);
    session.first.setStatus(0xffffffff);
    check(session.first.status() == 2, "status caps full unsigned input");
    session.first.setStatus(1, true);
    check(!session.award(206, 0, {10, 9, 8}, reward) && !session.award(1, 22, {10, 9, 8}, reward),
          "special reward exclusions");
    Host small;
    countPicarats(0, 10, small);
    check(small.draws[0] == std::array<std::uint32_t, 5>{1, 2, 1, 9, 0}, "small award extra frame");
    check(small.draws[9][2] == 10 && small.draws[9][4] == 0 && small.draws.size() > 10,
          "zero remaining does not exit");
    Host early;
    early.confirmFrame = 0;
    countPicarats(0, 10, early);
    check(early.draws.size() == 2 && early.queries == 1 && early.draws.back()[2] == 10,
          "confirmation blocked for first three ticks");
    Host large;
    countPicarats(0, 25, large);
    check(large.draws[0][2] == 2 && large.draws[12][2] == 25,
          "large award two steps with final single step");
    Host zero;
    zero.confirmFrame = 0;
    countPicarats(42, 0, zero);
    check(zero.draws.size() == 4 && zero.draws.back()[2] == 42,
          "zero award waits through input gate");
    if (argc == 3 && std::string(argv[1]) == "--states") {
        std::ofstream out(argv[2]);
        auto write = [&](const char* name, const PuzzleSession& state) {
            out << name << '\t' << unsigned(state.first.packed) << '\t' << state.coins << '\t'
                << state.picarats << '\n';
        };
        PuzzleSession wrongState;
        wrongState.first.enter();
        wrongState.first.finishFailure();
        write("wrong", wrongState);
        PuzzleSession hintState;
        hintState.first.enter();
        hintState.purchaseHint(1);
        write("hint", hintState);
        PuzzleSession correctState;
        correctState.first.enter();
        correctState.award(1, 0, {10, 9, 8}, reward);
        write("award", correctState);
        correctState.first.setStatus(2);
        write("solved", correctState);
    } else if (argc == 3) {
        // Export the recovered loop independently of the scene/render adapter.
        Host reference;
        reference.confirmFrame = std::stoul(argv[1]);
        countPicarats(0, 10, reference);
        std::ofstream out(argv[2]);
        for (const auto& row : reference.draws) {
            for (unsigned i = 0; i < 5; ++i) {
                out << (i ? "\t" : "") << row[i];
            }
            out << '\n';
        }
    }
    std::cout << "Puzzle progress and count edge cases passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
