#pragma once
// The fourth adventure's whack-a-mouse (loop 6300, overlay 40; the story's
// side game, event 20_200150, script evt/prg/lt4_mini_mouse).
//
// How it plays
// ------------
// Mice pop out of hiding spots all over a back yard - out of a window,
// along the washing line, across a wall - and the player taps them before
// they hide again: a gold mouse is worth 100, a silver one 50, a common one
// 10, and the cheeky one (Toppy) costs 50. The game lasts as long as the
// player's title allows (30 seconds for a beginner, up to 300); the score
// raises the title (18 of them, each needing a score and giving a play
// time), and the best score and the counts per kind are kept.
//
// The script does the screens, the mice's moves (ActSeq_* blocks) and the
// scoring; the loop only decides who comes out where. Its data
// (res/sub/@/lt4_sub_mouse.cb) lists the spots (5310: a place, its layer,
// which moves it allows - up, sideways, hide up, hide sideways, run - the
// move's block and coordinates), patterns (5320..5322: steps, each the
// number of mice of each kind, the moves allowed and optionally the spots),
// one schedule of waves (5330..5332: the move set, the most mice at once and
// the patterns to pick from) and the titles (5340). Every 30 frames the
// script asks for the next step (2400 0): the step's mice in random order,
// on as many free allowed spots, picked at random, as the wave permits;
// then it asks for each one (2400 2). 2400 3..6 read the titles.
//
// Reconstructed from overlay 40 (02105220..02107010); see
// .analysis/adventure4/docs/minigames.md.
#include "story/a4_script_loop.hpp"
#include <array>
#include <vector>

namespace picarat::adventure4 {

class MouseLoop final : public ScriptLoop {
public:
    MouseLoop(Game& game, int id, std::span<const Value> values);

    bool command(gss::Call& c, int op) override;

private:
    // A hiding spot (5310, 0x20 bytes, kept sorted by its name's hash).
    struct Spot {
        std::uint32_t hash = 0;
        std::string name, place, block; // +4, +8 (the mouse's object), +0xc
        int layer = 0;                  // +0x10
        unsigned moves = 0;             // +0x14: the moves it allows (bits)
        std::vector<Value> extra;       // +0x18: coordinates for the block
    };

    // A step of a pattern (5322, 0x14 bytes).
    struct Step {
        std::array<int, 4> mice{}; // gold, silver, common, Toppy
        int total = 0;
        unsigned moves = 0;     // +0x10
        std::vector<int> spots; // +8: allowed spots (none: any)
    };

    // A pattern (5320..5321, sorted by its name's hash).
    struct Pattern {
        std::uint32_t hash = 0;
        std::vector<Step> steps;
    };

    // A wave of the schedule (5332, 0xc bytes).
    struct Wave {
        int set = 0;  // +0: the moves' block suffix ("ActSeq_AppU" + 1)
        int most = 0; // +2: the most mice at once
        std::vector<int> patterns;
    };

    // A title (5340, 0xc bytes).
    struct Title {
        int seconds = 0, score = 0;
        std::string text;
    };

    // A mouse to send out (+0x168, 8 bytes each, at most 64).
    struct Mouse {
        int spot = 0, kind = 0, set = 0;
    };

    void load();     // 021063d8
    bool nextStep(); // 021067e4 (2400 0)
    int spotByName(const std::string& name) const;
    int patternByName(const std::string& name) const;

    std::vector<Spot> spots_;       // +0x128
    std::vector<Pattern> patterns_; // +0x138
    std::vector<Wave> waves_;       // +0x148 (the first schedule's)
    std::vector<Title> titles_;     // +0x158
    std::vector<Mouse> out_;        // +0x168 (+0x368 how many)
    int pattern_ = -1;              // +0x36c: the wave's pattern
    int step_ = 0;                  // +0x374
    int wave_ = 0;                  // +0x376
};

} // namespace picarat::adventure4
