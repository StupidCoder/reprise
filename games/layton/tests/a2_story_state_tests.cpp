#include "test_assets.hpp"
// GameState (src/a2_game_state.*) against the packed original data. ROM-RAM
// differential coverage is a local research check.
#include "story/a2_game_state.hpp"
#include <iostream>
#include <stdexcept>
using namespace picarat;

namespace {
void check(bool yes, const char* why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}
} // namespace

int main(int argc, char** argv) try {
    requireTestPack(argc, argv);
    if (argc != 2) {
        throw std::runtime_error("usage: a2_story_state_tests ASSETS");
    }
    const std::filesystem::path root = argv[1];
    GameState g(root);
    g.newGame();
    // 02083368: step 5, place 1 (version 1 for step 5), ten hint coins, no
    // pending event, default hamster name.
    check(g.progress.storyStep() == 5 && g.progress.place() == 1 && g.progress.placeVersion() == 1,
          "new game step/place/version");
    check(g.progress.hintCoins() == 10 && g.progress.hintCoinsFound() == 10 &&
              g.progress.pendingEvent() == -1,
          "new game coins/pending");
    check(std::string(reinterpret_cast<const char*>(&g.progress.bytes[0x2f7])) == "NO NAME",
          "default hamster name");
    // The first automatic event (scene-bootstrap-a: 18390, steps 0..999).
    check(g.findAutoEvent() && g.eventId == 18390, "opening automatic event");
    g.setEventViewed(18390, true);
    GameState h = g;
    check(!h.findAutoEvent(), "viewed automatic event is not selected again");
    // storyflag2 row 0: step 5 needs story flag 72 (set by event 10035's ev_inf2).
    check(g.loadEventInfo(10035) && g.current.storyFlag == 72, "ev_inf2 10035 story flag");
    g.advanceStory();
    check(g.progress.storyStep() == 5, "no advance without flag");
    g.progress.setStoryFlag(72, true);
    g.advanceStory();
    check(g.progress.storyStep() == 10, "advance 5 -> 10");
    // Puzzle list: internal 116 is puzzle 2; status by number.
    check(g.puzzleNumber(116) == 2 && g.puzzleNumber(1) == 1 && g.puzzleNumber(9999) == -1,
          "nz_lst internal -> number");
    const auto p2 = g.puzzleByNumber(2);
    check(p2 && p2->internal == 116, "nz_lst number -> row");
    check(g.puzzleStatus(-1) == 0 && g.puzzleStatus(0) == 0, "out-of-range puzzle status reads 0");
    g.setPuzzleStatus(2, 2, false);
    g.setPuzzleStatus(2, 1, false);
    check(g.puzzleStatus(2) == 2, "status is monotonic unless forced");
    g.setPuzzleStatus(2, 1, true);
    check(g.puzzleStatus(2) == 1, "forced status");
    // ev_fix: 10036 uses viewed bit 131; 20000 is puzzle 1's event.
    const auto* fix = g.eventFix(10036);
    check(fix && (fix[4] | fix[5] << 8) == 131, "ev_fix viewed index");
    fix = g.eventFix(20000);
    check(fix && (fix[2] | fix[3] << 8) == 1, "ev_fix puzzle");
    check(!g.eventViewed(10036) && (g.setEventViewed(10036, true), g.progress.eventViewed(131)),
          "viewed through ev_fix");
    // Place versions: place 3 at step 10 is 1, or 2 once
    // counter 4 equals 1 (script 0x36(4,1) in event 10050).
    g.progress.setStoryStep(10);
    g.setPlace(3);
    check(g.progress.placeVersion() == 1 && g.placeChanged, "place 3 version 1");
    g.progress.orCounter(4, 1);
    g.updatePlaceVersion();
    check(g.progress.placeVersion() == 2, "place 3 version 2 by counter");
    g.setPlace(3);
    check(!g.placeChanged && g.previousPlace == 3, "same place is not a change");
    // SaveProgress bounds (020820ec / 02082110 / 02082608).
    SaveProgress s;
    s.setBit(0x84, 128, 128, true);
    check(s.bytes[0x84 + 16] == 0, "bit write past the bound ignored");
    check(!s.compareCounter(128, 0, 0) && s.compareCounter(5, 0, 0) && !s.compareCounter(5, 0, 1) &&
              s.compareCounter(5, 0, 2) && !s.compareCounter(5, 0, 3),
          "counter compare modes");
    std::cout << "GameState tests passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
