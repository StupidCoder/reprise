#pragma once
// The sixth adventure's minigames: script loops with a native class each
// (the loop factory 0x28f7bc): 6000 lt6CLoopMiniAirShip, 6100
// lt6CLoopMiniFashion, 6200 lt6CLoopMiniGarden, 6300 lt6CLoopMiniRisu.
//
// How a minigame runs
// -------------------
// The loop's start (+0x28; Risu 0x2c3590) runs the script loop's own start
// (0x247514), loads the minigame's script (seq/mini_risu, ...) and sets up
// the class's records; the script loop then runs the script's "Init" and,
// once that has ended, "Main". The script talks to the class through 2110
// (the class's command method, vtable +0x4c); the class also looks at the
// input (+0x50) and updates itself before the frame's scripts (+0x54).
//
// MinigameSession plays one minigame on its own (research and the
// minigame menu), outside the story: the game loop's scripts, the
// minigame's script, the loop values given.
#include "puzzles/a5_special.hpp"
#include "system/a5_game.hpp"
#include <memory>
#include <vector>

namespace picarat::adventure6 {

constexpr int AirShipLoopId = 6000;
constexpr int FashionLoopId = 6100;
constexpr int GardenLoopId = 6200;
constexpr int RisuLoopId = 6300;

// The script a minigame loop loads ("seq/mini_risu"), or null.
const char* minigameScript(int loopId);

// The native class of a minigame loop (null where the port has none yet).
std::unique_ptr<adventure5::SpecialPuzzle> makeMinigame(adventure5::Game& game, int loopId);

class MinigameSession {
public:
    MinigameSession(adventure5::Game& game, int loopId, std::vector<xq::Value> values = {});
    // One frame; false once the minigame has ended.
    bool frame(const adventure5::Input& input);
    bool update();

    xq::Thread& thread() { return *main_; }

private:
    enum class State { Init, Main, Ended };
    adventure5::Game& game_;
    std::unique_ptr<xq::Thread> main_, second_;
    State state_ = State::Init;
};

} // namespace picarat::adventure6
