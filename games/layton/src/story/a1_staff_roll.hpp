#pragma once

// The first adventure's staff roll (scene 0x26; ARM9 02002b9c: the object
// 0203bd60 / run 0203c348 / "the end" 0203c648 / destructor 0203bf40).
//
// How it plays
// ------------
// After four seconds of black the credits scroll up the lower screen: 32
// tall pictures (staff_roll<N>.bgx) one after the other, a pixel every
// three frames, softened at the top and bottom edge by a warm glow. On the
// upper screen pictures of the story (staff_top<N>.bgx) fade in and out
// over a tiled backdrop. The ending theme starts after a short pause. The
// last credit fades in a closing picture, the music plays on for five
// seconds, then "The End" stays until a touch or a button.
namespace picarat::adventure1 {
class Story;

// 02002b9c's first half: the staff roll and "the end". Leaves the scene at
// Reset (0); the caller runs event 0x156 next.
void staffRoll(Story& story);

} // namespace picarat::adventure1
