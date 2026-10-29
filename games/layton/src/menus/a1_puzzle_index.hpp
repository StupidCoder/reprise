#pragma once

// The first adventure's puzzle index (scene 0x1a, from the trunk; ARM9
// object 02031ef4 / run 0203471c / destructor 0203294c).
//
// How it plays
// ------------
// The lower screen lists the puzzles found so far in the order of their
// numbers, five rows at a time: a mark to tick (a puzzle can be marked to
// find it again), whether it is solved, its number and its title. The list
// scrolls with the arrows (slow and fast) or by dragging the bar on the
// right. A tab switches between all puzzles and the marked ones. Touching a
// row shows the puzzle on the upper screen: its picture, number, title,
// kind and where it was found (the shack once it was sent there), a medal
// for how it was solved, the hints used and its picarats now and at best.
// A solved puzzle can be played again.
namespace picarat::adventure1 {
class Story;

// Runs the index until it is closed (the scene becomes the trunk) or a
// puzzle is chosen (scene 4: the puzzle, then the index again).
void puzzleIndex(Story& story);

} // namespace picarat::adventure1
