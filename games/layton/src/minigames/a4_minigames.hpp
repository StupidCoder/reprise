#pragma once
// The fourth adventure's minigames: the bag's three (the model railway
// 6200, the puppet show 6110/6120/6100, the fish 6401/6400) and the rest of
// the 6000 range, as loops on the game's stack. Each is a script loop
// (lt4CLoopScript) whose script drives the screens, with a class of its own
// for the native commands (ov36..40).
#include "system/a4_loop.hpp"
#include <memory>
#include <span>
#include <vector>

namespace picarat::adventure4 {

class Game;

// The loop for a minigame number, or null when `id` is none of them.
std::unique_ptr<Loop> makeMinigameLoop(Game& game, int id, std::span<const gss::Value> values);

// Free play (the app's minigame menu)
// ---------------------------------------
// The bag's three minigames by the loop the bag asks for (evt/prg/
// lt4_menu_bag MenuTrain / MenuDoll / MenuFish): the railway, the puppet
// show's menu, the fish game's menu.
inline constexpr int kMinigameLoops[3] = {6200, 6110, 6401};

struct GameData;

// Everything open as the bag offers it once the story has handed it all
// out: every railway course and fish tank, every acting action learned, the
// three tutorials seen (their help buttons still show them).
void openMinigames(GameData& data);

// The free play's record: the game data's flags and variables (all the
// minigames keep there: courses and tanks cleared, the plays' choices).
std::vector<std::uint8_t> minigameRecord(const GameData& data);
void applyMinigameRecord(GameData& data, std::span<const std::uint8_t> record);

// Progress: railway courses cleared (of 10), plays succeeded (of 3), tanks
// cleared (of 10).
struct MinigameProgress {
    int courses = 0, plays = 0, tanks = 0;
};

MinigameProgress minigameProgress(const GameData& data);

} // namespace picarat::adventure4
