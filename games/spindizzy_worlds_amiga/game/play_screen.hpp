#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/play/level_play.hpp"
#include "spindizzy_worlds_amiga/view/camera.hpp"
#include "spindizzy_worlds_amiga/view/objects.hpp"
#include "spindizzy_worlds_amiga/view/screen.hpp"
#include "spindizzy_worlds_amiga/view/starfield.hpp"
#include "spindizzy_worlds_amiga/view/tile_map.hpp"
#include <functional>
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
    bool hold{};         // [ ($1133C: the same without dimming)
    bool overview{};     // m ($1135A: the overview map until fire)
    bool key{};          // any key pressed this frame (kept in $107A8 until read)
};

class PlayScreen {
public:
    enum class Outcome { playing, world_done, game_over };

    PlayScreen(const data::Assets&, const play::SurfaceTable&, play::Progress&);
    // For a saved state: the level is read in by persist.
    PlayScreen(const data::Assets&, const play::SurfaceTable&, play::Progress&, play::LevelPlay::Restore);

    template <class A> void persist(A& ar) {
        play_->persist(ar);
        map_->persist(ar);
        ar(map_build_, explorer_frame_, space_held_, clue_, notice_, paused_, dimmed_, overview_, held_, message_frames_, key_waiting_,
           clue_frames_, outcome_);
    }

    // One frame: the rules ticks (one in slow mode, two otherwise) and the
    // rest of the level loop.
    void frame(const PlayInput&, bool slow, play::StickByte&);
    Outcome outcome() const { return outcome_; }

    // The whole screen (320 × 200): playfield, pillars, panel, a clue.
    void draw(view::Screen&) const;

    const play::LevelPlay& level() const { return *play_; }
    // After "Use reserve fuel": the same level goes on.
    void revive() { play_->revive(); outcome_ = Outcome::playing; }

private:
    void start_level();
    void finish_level();
    // $1135A: the 16 views squeezed into the box.
    void open_overview(bool slow);
    void draw_view(view::Canvas&, const view::Camera&) const;
    void draw_playfield(view::Screen&) const;
    void draw_live(view::Screen&) const;
    void print_clue(view::Screen&) const;
    // A wait's picture: the one on screen with its text printed over it.
    void hold_picture(const std::function<void(view::Screen&)>& print);

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
    bool paused_{};   // p or [: until fire ($109CC)
    bool dimmed_{};   // p dims the palette ($11074)
    std::optional<view::Screen> overview_; // m: until fire
    std::optional<view::Screen> held_;          // the picture while a text waits
    mutable std::optional<view::Screen> shown_; // the last picture drawn (not state)
    int message_frames_{-1}; // "Reserve fuel collected": frames left ($109E2)
    bool key_waiting_{}; // a key in $107A8 not read yet
    int clue_frames_{};
    Outcome outcome_{Outcome::playing};
};

} // namespace spindizzy::game
