#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/view/camera.hpp"
#include "spindizzy_worlds_amiga/view/objects.hpp"
#include "spindizzy_worlds_amiga/view/screen.hpp"
#include "spindizzy_worlds_amiga/view/starfield.hpp"
#include "spindizzy_worlds_amiga/view/tile_map.hpp"
#include <memory>
#include <optional>
#include <vector>

// A world being played, level after level (the main loop $189DC … $18A42
// around the level loop $112F6): the playfield between the pillars, the
// panel below, clues shown over the playfield while the game waits.
namespace spindizzy::game {

// One frame's controls as the original reads them: the joystick byte and
// the keys the level loop acts on ($109B2).
struct PlayInput {
    std::uint8_t joystick{};
    bool space{};        // the brake ($11314)
    int turn{};          // r: +1, t: −1 ($11402)
    bool centre{};       // c ($1144A)
    bool pause{};        // p ($11332: the palette dimmed until fire)
};

class PlayScreen {
public:
    enum class Outcome { playing, world_done, game_over };

    PlayScreen(const data::Assets&, const play::SurfaceTable&, play::Progress&);

    // One frame: the rules ticks (one in slow mode, two otherwise) and the
    // rest of the level loop.
    void frame(const PlayInput&, bool slow);
    Outcome outcome() const { return outcome_; }

    // The whole screen (320 × 200): playfield, pillars, panel, a clue.
    void draw(view::Screen&) const;

    const play::LevelPlay& level() const { return *play_; }
    // After "Use reserve fuel": the same level goes on.
    void revive() { play_->revive(); outcome_ = Outcome::playing; }

private:
    void start_level();
    void draw_playfield(view::Screen&) const;

    const data::Assets& assets_;
    const play::SurfaceTable& surfaces_;
    play::Progress& progress_;
    std::unique_ptr<play::LevelPlay> play_;
    std::unique_ptr<view::TileMap> map_;
    int map_build_{};
    std::uint32_t explorer_frame_{view::explorer_frames};
    bool space_held_{};
    // A clue on show ($15FC0): 26 frames, then until fire or 126 more.
    std::optional<int> clue_;
    bool notice_{};   // the bonus-level notice, until fire
    bool paused_{};   // p: until fire ($109CC)
    bool fire_held_{};
    int clue_frames_{};
    Outcome outcome_{Outcome::playing};
};

} // namespace spindizzy::game
