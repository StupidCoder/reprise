#pragma once
#include "gamegear/tile_screen.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/flow_assets.hpp"

namespace sonic1_gg {
struct StateAccess;

// The ending after the final act (1FAE-2254, 4663-483A).
//
// With all six emeralds the emeralds first circle out and the screen whites out
// into the good ending's map. Then the final tally converts emeralds (20,000
// each), remaining lives (5,000 each) and a completion bonus into points, and
// the credits roll: four animated characters walk across the screen while text
// commands write and scroll the credit lines.
//
// The completion bonus (in units of 10,000 points) adds 5 each for all six
// emeralds, 18 life monitors, eight special stages entered and eight cleared,
// and 10 for never dying; a perfect 30 becomes 50.
class Ending {
    friend struct StateAccess;

public:
    Ending(const FlowAssets&, int score, int lives, int emeralds, int life_monitors,
           int bonus_attempts, int bonus_successes, bool died, const Bytes& sprite_patterns);
    void tick();
    enum class Phase {
        EmeraldHold,
        EmeraldFlight,
        WhiteOut,
        GoodMapIn,
        MapHold,
        Tally,
        ResultsHold,
        BlackOut,
        BlackHold,
        CreditsPrepare,
        CreditsIn,
        Credits,
        CreditsHold,
        CreditsOut,
        Done
    };
    Phase phase{};
    unsigned ticks{}, phase_ticks{};
    gg::TileScreen screen;
    int score{}, lives{}, emeralds{}, bonus{};
    int tally_stage{}, tally_timer{};
    std::vector<SoundEvent> sound_events;
    unsigned text_pointer{0x2315}, text_x{}, text_y{}, text_wait{}, scroll_rows{};
    std::array<unsigned, 4> actor_pointer{0x2258, 0x2267, 0x2279, 0x2282}, actor_ticks{};

private:
    const FlowAssets& assets_;
    Bytes fade_target_;
    unsigned tally_step_{};
    int tally_wait_{};
    unsigned pending_tile_{};
    bool pending_letter_{};
    void next(Phase);
    void fade(Bytes);
    void animate_credits();
    void credit_commands();
    void patch(unsigned);
    void number(int, unsigned, int, int, bool = true);
    void tally_numbers();
    void tally_tick();
    void mapping(unsigned, int, int);
    void load_screen(unsigned);
};
} // namespace sonic1_gg
