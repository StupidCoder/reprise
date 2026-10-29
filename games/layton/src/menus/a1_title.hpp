#pragma once
// The first adventure's title screen (ARM9 0202ce14 / 0202d480 / 0202d288).
//
// How it plays
// ------------
// The upper screen shows the town's houses, the game's logo fading in after
// a second; on the lower screen a car drives along a fence while the
// landscape scrolls behind it in two layers. With no saved game there is one
// button, "new game"; otherwise "continue" and "secret" are added below it.
namespace picarat::adventure1 {
class Story;

enum class TitleChoice { NewGame, Continue, Secret };

// Runs the title screen until a button is chosen; ends faded to black.
TitleChoice titleScreen(Story& story);

} // namespace picarat::adventure1
