#pragma once
// The sixth adventure's puzzle types: which play loop a catalogue type runs
// on. The session (puzzles/a5_puzzle.hpp, shared with the fifth adventure)
// asks for it when the puzzle's intro has played.
//
// From the session's update 0x1d7888 (the switch on the puzzle's type byte,
// record +6) and the loop factory 0x28f7bc (loop number -> class). The type
// numbers are the sixth adventure's own; they do not match the fifth's.
// Loops (class names from the factory's name methods): 3100 Common (the
// standard loop), 3110 Slide, 3120 Line, 3130 Ltgram, 3140 Fit, 3150
// TraceRoute, 3160 ChangeTexture, 3170 Transfer, 3180 Partition, 3230 Cut,
// 3240 Pile, 3250 Mosaic, 3260 Route, 3270 Slide2, 3290 Beam, 3300 Othello,
// 3310 Vanish, 3320 GuardRobot, 3330 BridgeLabyrinth, 3340 TouchPad.

namespace picarat::adventure6 {

// The play loop's number for a catalogue type (types the switch does not
// list: 3100). Types from 101 up (the daily puzzles) are not mapped yet.
int playLoop(int type);

// The fifth adventure's catalogue type whose play-loop class has the same
// name (Ltgram, Slide, Line, Cut, Pile, Mosaic, Othello, Vanish), 0 for
// none. Same name does not mean the same rules: each pair has to be
// checked against the sixth adventure's code and the reference.
int fifthTypeOfSameClass(int type);

// The fifth adventure's type whose class plays this type with the sixth
// adventure's additions (checked against the sixth's code: Line 3120 with
// commands 6, 8, 9 and the error message's show switch; Othello 3300 with
// commands 60..62; Pile 3240 with its drop and judge; Mosaic 3250 with
// holes), 0 for the others. Reference comparison pending.
int fifthTypeWithSixthRules(int type);

// Whether the sixth adventure's own reconstruction runs this type (loops
// with no fifth-adventure class: 3140 Fit, 3160 ChangeTexture, 3170 Transfer, 3180 Partition,
// 3230 Cut, 3260 Route, 3320 GuardRobot, 3330 BridgeLabyrinth, 3340
// TouchPad).
bool hasOwnClass(int type);

// The standard play loop: the puzzle runs on its scripts alone.
inline constexpr int kCommonLoop = 3100;

} // namespace picarat::adventure6
