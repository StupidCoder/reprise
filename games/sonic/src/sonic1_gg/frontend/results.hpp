#pragma once
#include "gamegear/tile_screen.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/flow_assets.hpp"
#include "sonic1_gg/game/score.hpp"

namespace sonic1_gg {
// Bank 1, 449D-4662 / 483B-49B1: the tally after an act or special stage.
//
// The screen holds for 120 updates, then counts the remaining rings (each worth
// `ring_value` points) and the time bonus (100 points per update) into the score
// with a tick sound every fourth update, and holds again for 180 updates or until
// a button is pressed. Extra lives earned by the score are awarded while counting.
// Amounts are kept as decimal values here; the cartridge stores BCD.
class Results {
public:
    Results(const FlowAssets&, unsigned act, int score, int rings, int lives, int elapsed_ticks,
            int emeralds, int ring_value = 100, int continues = 0, unsigned score_threshold = 5);
    void tick(bool skip);
    // `skip` (Start or a button) only shortens the final hold.
    enum class Phase { Hold, Count, After, Done } phase{Phase::Hold};
    gg::TileScreen screen;
    int score{}, rings{}, lives{}, time_bonus{}, ring_value{100}, continues{};
    unsigned next_life_score_bcd{5};
    bool special{};
    unsigned ticks{}, phase_ticks{},
        sound_counter{2}; // D217 is 2 after the final dry-land player update (4B63 / 4FD7).
    std::vector<SoundEvent> sound_events;

private:
    void numbers();
    void number(int value, unsigned digits, int x, int y, bool leading = true);
};
} // namespace sonic1_gg
