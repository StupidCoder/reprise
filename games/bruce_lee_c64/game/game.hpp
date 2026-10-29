#pragma once
#include "app/game_module.hpp"
#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/game/front.hpp"
#include "bruce_lee_c64/game/sound.hpp"
#include "bruce_lee_c64/game/world.hpp"
#include "c64/sid.hpp"
#include "c64/text_frame.hpp"
#include <array>

// The whole game frame by frame: the title and its options, the attract
// mode, the player and game-over screens, play (a pass every third frame,
// pause, the keys), the two-player modes, the sound, and the picture.
namespace bruce_lee::game {

// What the title's keys choose (kept between games, as the original keeps
// them in $42B8/$42B9 and $139B-$139D).
struct Options {
    bool alternate{};                     // F3: two players taking turns ($42B6)
    bool yamo_player{};                   // F5: a second player is the Yamo ($42B7)
    std::array<bool, 2> left_handed{};    // joystick turned for left-handed play ($139B/$139C = 0)
    bool title_music{true};               // the S key turns it off ($139D)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(alternate, yamo_player, left_handed, title_music); }
};

class Game {
public:
    explicit Game(const data::Assets&);

    // Starts at the title, as the original does after loading ($050F).
    void start();
    void tick(const app::GameInput&);
    const c64::Picture& picture() const { return picture_; }
    // For the presentation layer: the play screen is up (play or the demo),
    // and whether the game is paused.
    bool in_play() const { return phase_ == Phase::play || phase_ == Phase::attract; }
    bool paused() const { return paused_; }
    c64::Sid& sid() { return sid_; }

    Options options;
    World world;

private:
    enum class Phase { title, attract, player_screen, play, game_over };

    // Each phase's frame.
    void title_frame_tick(const app::GameInput&);
    void attract_tick(const app::GameInput&);
    void play_tick(const app::GameInput&);
    // Waits that run before the next step: until the fire button is up
    // ($093D), then a number of frames ($48).
    enum class Next { none, title_again, start_game, new_game, restart, resume, resume_swapped, after_game_over, title };
    struct Wait {
        bool release{};
        int frames{};
        Next then{Next::none};
        bool active() const { return then != Next::none; }

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(release, frames, then); }
    };
    bool run_wait(const app::GameInput&); // true while waiting
    void step(Next);

    void restart();                       // $050F
    void enter_title();                   // $0584
    void start_game();                    // $0700
    void start_attract();                 // $0531
    void show_player_screen(Next then);  // $0E9B
    void new_game();                      // $0F69 + $0755 + $09AE
    void lose_life();                     // $1830
    void next_player();                   // $1842
    void game_over();                     // $18A6
    void request_sounds();
    std::uint8_t stick_of(const app::GameInput&, int player) const; // $16BA
    void count_title_frame();             // $154A

    const data::Assets& assets_;
    c64::Sid sid_;
    EffectPlayer effects_;
    MusicPlayer music_;
    Phase phase_{Phase::title};
    Wait wait_;
    TitleText title_text_;
    std::uint16_t countdown_{};           // $49/$4A on the title
    std::int8_t music_wait_{};            // $09AD: titles until the music plays again
    int player_frames_{}, player_shown_{};
    std::uint32_t game_over_score_{};
    World::PlayerState other_player_{};   // $438C-$43A7, $0340-, $4382
    std::uint8_t last_keys_{};            // for new key presses
    bool paused_{};                       // $0105 bit 7
    int pause_guard_{};                   // $0105 bits 1-2: passes before fire can unpause
    int frames_to_pass_{1};
    std::uint16_t yamo_idle_{};           // $89/$8A: the Yamo player holding fire without moving
    c64::Picture picture_;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(options, world, sid_, effects_, music_, phase_, wait_, title_text_, countdown_, music_wait_,
           player_frames_, player_shown_, game_over_score_, other_player_, last_keys_, paused_, pause_guard_,
           frames_to_pass_, yamo_idle_, picture_);
    }
};

} // namespace bruce_lee::game
