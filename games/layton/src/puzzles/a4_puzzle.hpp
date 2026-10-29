#pragma once
// The fourth adventure's puzzles: a puzzle session plays the title card,
// the puzzle and the verdict, each a loop running the game's scripts.
//
// How it plays
// ------------
// The title card (lt4_nazo_intro) shows the puzzle's number and title and
// what it is worth; a tap starts the puzzle. The puzzle loop runs
// lt4_prog_nazo with the puzzle's own script (res/naz/seq/nNNN.cq), which
// builds the puzzle's objects and judges the answer; hint, quit and answer
// input are scripts too (lt4_nazo_hint, _quit, _input). The verdict
// (lt4_nazo_judge) shows right or wrong and offers to try again.
//
// Reconstructed from ov7 (lt4CLoop: 020e2004 start, 020e22e4 frame,
// 020e222c drawing order, 020e1dec thread run) and ov17 (lt4CLoopNazo, the
// session: 02104c70 start, 02105148 states). See
// .analysis/adventure4/docs/script-machine.md (loops).
#include "story/a4_game.hpp"
#include <memory>

namespace picarat::adventure4 {

using gss::Value;

// What the game keeps of a puzzle between plays (its save record): 0 new,
// 1 seen, 2 solved; wrong answers; hints opened.
using PuzzleProgress = GameData::Progress;

// The puzzle loops by number (ov7 020c971c): the session 3000 and the play
// loops 3100.. (3100 standard; 3110 cut, 3120 line, 3130 slide, 3140
// slide 2, 3150 tangram, 3160 bulldozer, 3170 jump, 3180 lead, 3190
// vanish, 3200 trampoline); null for other numbers. `below` is the loop
// under the new one (a play loop's session).
std::unique_ptr<Loop> makePuzzleLoop(int id, Scene& scene, Services& services,
                                     std::span<const Value> values, Loop* below);

// The play loop a puzzle type asks for (ov17 02105148).
int playLoopFor(int type);

// A puzzle played on its own, as the puzzle index starts it: a loop stack
// with only the session on it (ov17 lt4CLoopNazo, loop 3000), which asks
// for the title card (2200), the puzzle's play loop and the verdict (2210)
// in turn.
class PuzzleSession {
public:
    using Progress = PuzzleProgress;

    // `fromIndex`: started from the puzzle index (sets the game's
    // "puzzle dictionary" flag, which shows the quit button).
    // `sound`: the sound manager (null: silent).
    PuzzleSession(Assets& assets, int number, bool fromIndex, Progress progress = {},
                  Adventure4Sound* sound = nullptr);
    ~PuzzleSession();

    Progress progress() const;

    // Whether the port plays puzzles of a type yet (all but 19, the
    // bulldozer, ov23).
    static bool playable(int type);

    void frame(bool touching, int x, int y);

    bool finished() const;

    void render(Surface& out) const;

    // What the player got: 1 solved, 0 wrong, 3 quit (the verdict
    // variable).
    int verdict() const;

    Assets& assets;

private:
    std::unique_ptr<Game> game_;
    int number_ = 0;

public:
    Scene& scene;
    GameData& data;
    Services& services;
};

} // namespace picarat::adventure4
