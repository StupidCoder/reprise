#pragma once

// The first adventure's robot dog (scene 10, from the trunk; ARM9 02001b88:
// a first visit runs event 0xb5; the page 02024de4 / run 02025b80 /
// destructor 02025220).
//
// How it plays
// ------------
// The parts the story has handed over wait in a row at the bottom, five
// at a time (an arrow shows when more wait). Touching one sends it flying
// to its place on the dog, where it fades in; the row closes up. With all
// 20 parts in place the dog comes to life (event 0x100 the first time,
// then naming it); afterwards the page shows the complete dog, its name
// and how many things it has sniffed out.
namespace picarat::adventure1 {
class Story;

// 02001b88's page: until its close button, or until the dog is complete
// for the first time (the scene is then the event 0x100).
void robotDogPage(Story& story);

} // namespace picarat::adventure1
