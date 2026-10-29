#pragma once

// The heads-up display: rings, score, time and lives in sprite text
// (08053888-08053CDA).
namespace sonic_advance_gba {

class Game;

// 08054170: the HUD's process and pictures. `leftover`: see Hud::blink_.
void start_hud(Game& g, unsigned char leftover);
// 08053888: the HUD's pictures (also loaded by the true ending for its
// results).
void load_hud_pictures(Game& g);

} // namespace sonic_advance_gba
