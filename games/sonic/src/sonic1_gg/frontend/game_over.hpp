#pragma once
#include "gamegear/tile_screen.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/flow_assets.hpp"

namespace sonic1_gg {
struct StateAccess;

// Bank 0, 11E1-12C6: GAME OVER and the optional CONTINUE countdown.
// One tick() is the code between two original frame waits (0327).
//
// GAME OVER shows for 120 updates. Without continue credits it then waits up to
// 180 updates (a button skips) and the game restarts at the SEGA logo. With
// credits, the text scrolls up, one icon per credit and "CONTINUE ?" appear,
// and a 9-to-0 countdown runs (one digit per 60 updates); Start spends a credit
// and replays the act with three lives.
class GameOver {
    friend struct StateAccess;

public:
    GameOver(const FlowAssets&, int continues);
    // start: Game Gear Start (D203 bit 7). button: 1 or 2 (D203 bit 5;
    // the interrupt handler also clears bit 5 while button 1 is held, 00D1).
    void tick(bool start, bool button);
    enum class Phase { Off, Hold, Wait, Scroll, Countdown, Done };
    Phase phase{Phase::Off};
    bool continued{}; // Carry on return: spend a credit and replay the act.
    gg::TileScreen screen;
    int continues{}, digit{9}; // D27E (shown as icons), D217 countdown digit.
    unsigned phase_ticks{};
    std::vector<SoundEvent> sound_events;

private:
    const FlowAssets& assets_;
    void patch(unsigned address, int column = -1);
};
} // namespace sonic1_gg
