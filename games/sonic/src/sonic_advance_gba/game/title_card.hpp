#pragma once

// The act's title card (080550C4-08055C18).
namespace sonic_advance_gba {

class Game;

class TitleCard;

// 080550C4: the card's six texts, its controller and pictures. When it
// ends, the player gets control and the HUD starts.
TitleCard& start_title_card(Game& g);
// The card leaves the screen's blending to another effect (its +27: no
// fade of its own, the blending not reset at its end).
void leave_blending_to_others(TitleCard& card);

} // namespace sonic_advance_gba
