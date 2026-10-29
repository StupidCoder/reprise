#pragma once
// The fifth adventure's special puzzle loops: play loops whose puzzle
// scripts ask native code for the puzzle's rules.
//
// How a special puzzle runs
// -------------------------
// The puzzle session's update (0x18320c) picks the play loop by the
// puzzle's catalogue type: 9 -> 3200, 10 -> 3120, 11 -> 3130, 12 -> 3170,
// 13 -> 3230, 14 -> 3110, 15 -> 3220, 16 -> 3240, 27 -> 3140, 33 -> 3150,
// 34 -> 3160, 35 -> 3180, 36 -> 3190, 46 -> 3250, 47 -> 3270, every other
// type the standard loop 3100. The special loops run the same "Main" as the
// standard one; the puzzle's script talks to its loop through the command
// 2110 (0x12cb68: the loop's +0x4c handler with the first operand as the
// command number and the rest as its operands). A loop may also look at
// the input after the touch dispatch (+0x50, called from 0x37889c) and
// update itself before the frame's scripts (+0x54, 0x37a030).
#include "system/a5_game.hpp"
#include <array>
#include <memory>

namespace picarat::adventure5 {

// Building blocks the play loop classes share (the base loop's helpers).
using Vec2 = std::array<float, 2>;

// 0x297488 + 0x297400: an object on the lower screen (priority 111, kind
// 0) holding an untextured quad list (content 2), at first one 1x1 quad
// at (-1, 0).
adventure56::Object& createQuadObject(Game& game, const std::string& name);

// 0x28d5b0: a line from a to b as a quad `width` wide, reaching half the
// width past both ends; nothing when a and b are the same point.
void addLineQuad(adventure56::Object& o, Vec2 a, Vec2 b, float width);

// 0x28d488: a square of half size `half` around p.
void addDotQuad(adventure56::Object& o, Vec2 p, float half);

// The native rules for a puzzle of catalogue `type`; null for the standard
// loop (and for special types the port does not have yet).
std::unique_ptr<SpecialPuzzle> makeSpecialPuzzle(Game& game, int type);

// Whether `type` has a special loop (0x18320c).
bool isSpecialType(int type);

// Whether the port has the special loop of `type`.
bool hasSpecialRules(int type);

// The classes by loop.
std::unique_ptr<SpecialPuzzle> makeLinePuzzle(Game& game);     // type 9, loop 3200
std::unique_ptr<SpecialPuzzle> makeGridLinePuzzle(Game& game); // type 10, loop 3120
std::unique_ptr<SpecialPuzzle> makePairPuzzle(Game& game);     // type 11, loop 3130
std::unique_ptr<SpecialPuzzle> makeBlockPuzzle(Game& game);    // type 12, loop 3170
std::unique_ptr<SpecialPuzzle> makeGlidePuzzle(Game& game);    // type 13, loop 3230
std::unique_ptr<SpecialPuzzle> makePlacePuzzle(Game& game);    // type 14, loop 3110
std::unique_ptr<SpecialPuzzle> makePilePuzzle(Game& game);     // type 15, loop 3220
std::unique_ptr<SpecialPuzzle> makeFloatPuzzle(Game& game);    // type 16, loop 3240
std::unique_ptr<SpecialPuzzle> makeVanishPuzzle(Game& game);   // type 27, loop 3140
std::unique_ptr<SpecialPuzzle> makeCornPuzzle(Game& game);     // type 33, loop 3150
std::unique_ptr<SpecialPuzzle> makeBrickPuzzle(Game& game);    // type 34, loop 3160
std::unique_ptr<SpecialPuzzle> makeSlipPuzzle(Game& game);     // type 35, loop 3180
std::unique_ptr<SpecialPuzzle> makeFlipPuzzle(Game& game);     // type 36, loop 3190
std::unique_ptr<SpecialPuzzle> makeStrokePuzzle(Game& game);   // type 46, loop 3250
std::unique_ptr<SpecialPuzzle> makeBalloonPuzzle(Game& game);  // type 47, loop 3270
// The sixth adventure's own classes (a6_*.cpp).
std::unique_ptr<SpecialPuzzle> makeRopePuzzle(Game& game);      // sixth type 10, loop 3230 (Cut)
std::unique_ptr<SpecialPuzzle> makeFitPuzzle(Game& game);       // sixth type 20, loop 3140 (Fit)
std::unique_ptr<SpecialPuzzle> makePartitionPuzzle(Game& game); // sixth loop 3180 (Partition)
std::unique_ptr<SpecialPuzzle> makeTransferPuzzle(Game& game);  // sixth loop 3170 (Transfer)
std::unique_ptr<SpecialPuzzle> makeTouchPadPuzzle(Game& game);  // sixth loop 3340 (TouchPad)
std::unique_ptr<SpecialPuzzle> makeRoutePuzzle(Game& game);     // sixth loop 3260 (Route)
std::unique_ptr<SpecialPuzzle> makeBeamPuzzle(Game& game);      // sixth loop 3290 (Beam)
std::unique_ptr<SpecialPuzzle> makeSlide2Puzzle(Game& game);    // sixth loop 3270 (Slide2)
std::unique_ptr<SpecialPuzzle> makeTexturePuzzle(Game& game);   // sixth loop 3160 (ChangeTexture)
std::unique_ptr<SpecialPuzzle> makeGuardPuzzle(Game& game);     // sixth loop 3320 (GuardRobot)
std::unique_ptr<SpecialPuzzle> makeBridgePuzzle(Game& game);    // sixth loop 3330 (BridgeLabyrinth)
// The sixth adventure's own class for a play loop (null for none).
std::unique_ptr<SpecialPuzzle> makeSixthPuzzle(Game& game, int loop);
// Any sixth-adventure play loop: its base class's undo history (2110
// 10000..10003, a6_loop.cpp) around the loop's own class or none.
std::unique_ptr<SpecialPuzzle> makeSixthLoop(Game& game, std::unique_ptr<SpecialPuzzle> own);

} // namespace picarat::adventure5
