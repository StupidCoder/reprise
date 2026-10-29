#pragma once

// The title screen's attract demos (0800DF88, 08012874-08012C5A): an act
// played from recorded buttons with a "DEMO" overlay, until 30 seconds
// have passed or Start is pressed.
namespace sonic_advance_gba {

class Game;

// 0800DF88: the next of the four demos.
void start_demo(Game& g);

} // namespace sonic_advance_gba
