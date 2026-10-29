#pragma once

// The first adventure's torn painting (scene 8, from the trunk; ARM9
// 020019bc: a first visit runs event 0xb7; the page 0204b7a8 / run 0204ceb4
// / destructor 0204bd98).
//
// How it plays
// ------------
// The pieces found so far lie in a box. With the box on the touch screen,
// touching a piece takes it out: it flies off, the screens swap, and it
// rises into the tray below the frame. On the frame (now the touch screen)
// pieces are dragged by their middle and turned by their rim; let go, a
// piece snaps to the nearest place of the frame (swapping with a piece
// there) and turns to the nearest quarter. A piece dropped outside the
// frame goes back into the box. A button swaps the screens back. Once the
// 20 pieces lie in place the right way up, the painting is whole (event
// 0x102 the first time).
namespace picarat::adventure1 {
class Story;

// 020019bc's page: until it is closed or the painting is whole for the first
// time (the scene is then the event 0x102).
void paintingPage(Story& story);

} // namespace picarat::adventure1
