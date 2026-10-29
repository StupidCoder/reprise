#pragma once
#include "sonic1_gg/frontend/ending.hpp"
#include "sonic1_gg/frontend/flow.hpp"
#include "sonic1_gg/frontend/game_over.hpp"
#include "sonic1_gg/frontend/results.hpp"
#include "sonic1_gg/game/game.hpp"
#include <memory>
#include <optional>

namespace sonic1_gg {

struct StateAccess;

// The whole game from power-on: the cartridge's top-level state machine.
//
//   logo -> title -> (attract demo) -> map -> act -> results -> map -> next act ...
//
// with special stages after a goal sign's special face, linked rooms reloaded
// within an act, the death/game over/continue flow, and the ending after the
// final act. The Session owns the current Game and the screen objects and passes
// the player's input to whichever part is running. Attract demos replay the
// cartridge's own recorded controller bytes.
class Session {
    friend struct StateAccess;

public:
    // The levels are the extracted act data: Green Hill 1-3, Jungle 1 (used by
    // the second attract demo), the special stages and every other act/room by
    // act number.
    Session(const FlowAssets& flow, const Level& green_hill, const Level& jungle,
            const Level* act2 = nullptr, const Level* act3 = nullptr,
            const std::vector<Level>* special = nullptr,
            const std::map<unsigned, Level>* additional = nullptr);

    // Back to the SEGA logo with a new game.
    void reset();
    // One update. `start` is the Game Gear Start button.
    void tick(Input input, bool start);
    // Shell action: restart the current act (or the game).
    void restart_act();
    // Level selector: start at an act's map, or directly in a special stage.
    void start_level(unsigned act);
    bool supports_level(unsigned act) const;
    void start_special(unsigned index = 28, unsigned resume = 0);
    // Level selector: the finale after the escape (area 18, the flyby) into
    // the ending, with no or all six emeralds.
    void start_ending(bool all_emeralds);

    bool gameplay() const { return front.scene == FrontEnd::Scene::Gameplay; }

    FrontEnd front;                      // Logo, title, map and fades
    std::unique_ptr<Game> game;          // The act being played (kept during results)
    std::unique_ptr<Results> results;    // Act/special stage tally
    std::unique_ptr<Ending> ending;      // Ending sequence and credits
    std::unique_ptr<GameOver> game_over; // GAME OVER / CONTINUE screen

    bool fading_game{};      // The act is fading out (to results, a room, or after a death)
    bool paused{};           // The cartridge's own Start pause (unused by the shell)
    bool voice_requested{};  // The SEGA voice should start playing this update
    unsigned demo_pointer{}; // Position in the attract demo's controller script
    std::vector<SoundEvent> sound_events;

    unsigned next_special{28}; // Act number of the next special stage
    unsigned return_act{};     // Act to continue with after a special stage
    int continues{};           // Continue credits
    int special_attempts{}, special_successes{};
    int special_ring_value{}; // Points per ring in the special stage tally (+100 per success)

private:
    const FlowAssets& flow_assets_;
    const Level& green_hill_;
    const Level& jungle_;
    const Level* act2_;
    const Level* act3_;
    const std::vector<Level>* special_;
    const std::map<unsigned, Level>* additional_;
    std::optional<Level> selected_level_; // The act data the current Game was built from

    int pending_area_{-1};   // Linked room to load after the fade (-1: none)
    bool advance_area_{};    // ... and whether that also advances the act number
    bool carry_progress_{};  // The next act keeps score, emeralds and other progress
    bool respawn_pending_{}; // 13E4/13C5: reload at the checkpoint once the black hold ends
    bool previous_start_{};

    void begin_act();
    void finish_life();
    void end_demo();
    Bytes death_fade_palette() const;
};

} // namespace sonic1_gg
