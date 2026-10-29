#pragma once
// The first adventure's puzzle engines that have no counterpart among the
// shared engines, and the factory of command 0x1b (ARM9 02011610).
#include "puzzles/a1_puzzle.hpp"

namespace picarat::adventure1 {

// The engine named by a parameter script, or null when it is not
// reconstructed yet.
std::unique_ptr<Engine> createFirstAdventureEngine(const std::string& name, PuzzleScene& scene);
// The engines with a file of their own (adventure1_<engine>.cpp).
std::unique_ptr<Engine> createTraceButton(PuzzleScene& scene);
std::unique_ptr<Engine> createCutPuzzle(PuzzleScene& scene);
std::unique_ptr<Engine> createTile(PuzzleScene& scene);
std::unique_ptr<Engine> createSlideBlocks(PuzzleScene& scene);
std::unique_ptr<Engine> createQueen(PuzzleScene& scene);
std::unique_ptr<Engine> createOnOff(PuzzleScene& scene);
std::unique_ptr<Engine> createTrace(PuzzleScene& scene);
std::unique_ptr<Engine> createCup(PuzzleScene& scene);
std::unique_ptr<Engine> createMatch(PuzzleScene& scene);
std::unique_ptr<Engine> createMatchTutorial(PuzzleScene& scene);
std::unique_ptr<Engine> createDragPuzzle(PuzzleScene& scene);
std::unique_ptr<Engine> createScale(PuzzleScene& scene);
std::unique_ptr<Engine> createConnect(PuzzleScene& scene);
// The second kind ("River Cross 2") has the farmer's rules.
std::unique_ptr<Engine> createRiverCross(PuzzleScene& scene, bool farmer);
std::unique_ptr<Engine> createPlaceTarget(PuzzleScene& scene);
std::unique_ptr<Engine> createCoin(PuzzleScene& scene);

// Whether the engine of that name has been reconstructed.
bool engineReconstructed(const std::string& name);

} // namespace picarat::adventure1
