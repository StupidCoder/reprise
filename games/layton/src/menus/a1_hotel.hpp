#pragma once

// The first adventure's hotel rooms (scene 9, from the trunk; ARM9 02001a94:
// a first visit runs event 0xb6; the page 0202467c / run 02024c58 /
// destructor 020249bc, the rooms 02062608 and their data 0206318c).
//
// How it plays
// ------------
// The furniture found in the story belongs to one of the two hotel rooms.
// The touch screen shows each room's furniture as icons in a grid of its
// own; the other screen shows the two rooms furnished. Dragging an icon
// into the other room's grid moves the piece: it fades out of one room and
// into the other with a puff; each guest comments on what arrives (from
// the hotel script, depending on what the room already holds). Two buttons
// ask the guests how they like their room. Each room's happiness (a bar
// and a face) follows rules about which pieces it holds; with both rooms
// as happy as can be, event 0x101 runs the first time.
namespace picarat::adventure1 {
class Story;

// 02001a94's page: until it is closed or both rooms are perfect for the
// first time (the scene is then the event 0x101).
void hotelPage(Story& story);

} // namespace picarat::adventure1
