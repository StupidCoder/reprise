#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/data/shape.hpp"
#include "tower_of_babel_amiga/front/effects.hpp"
#include "tower_of_babel_amiga/front/screen.hpp"
#include "tower_of_babel_amiga/game/panel.hpp"
#include "tower_of_babel_amiga/game/rules.hpp"
#include "tower_of_babel_amiga/game/tower_state.hpp"
#include "tower_of_babel_amiga/view/sky.hpp"
#include "tower_of_babel_amiga/view/tower_view.hpp"
#include <map>
#include <vector>

// Playing a tower: the game screen ($969A) and the play loop ($9968), one
// game frame at a time. Before the start (READY) the spiders can be
// programmed and turned, nothing else moves and the clock stands; the first
// direct move, shot or Run starts the tower. Then each frame the timer
// counts (a second every 7 frames), the panel is polled, the rules run and
// the view is drawn, until the pause button is pressed or the end countdown
// set by a win or a loss runs out.
namespace babel::front {

// The player's settings (the pause menu's Setup and View pages, $110E-$111C,
// $11D4); the original's defaults.
struct Settings {
    bool backwards_allowed{true}; // $110E = 0
    bool link_cameras{true};      // $1110 = 0
    int info{1};                  // $1112: compass and counters 0 off, 1 at the base, 2 at the top
    int timer{1};                 // $1114: the same for the timer
    int view_side{1};             // $1116: 0-2, the eye left of, at or right of the spider
    int view_height{};            // $1118: 0-2
    int view_distance{};          // $111A: 0-3 (table $9F7C)
    bool sights{};                // $111C >= 0
    int panel_style{};            // $11D4: 0-2, the panel set and its colours
};

class Play {
public:
    Play(const data::Assets&, Screen&, const data::Tower&, const Settings&, int game_type);

    // $969A: the game screen (title bar with the tower's name, the view's
    // frame, the panel), drawn before the first frame and after a pause.
    void draw_screen();

    enum class Outcome { playing, paused, ended };
    // The mouse for one game frame, in the screen's pixels; `left` is held
    // (or was pressed since the last frame: the original only sees the
    // button when it polls, a host that delivers short clicks keeps them).
    struct Pointer {
        int x{-1}, y{-1};
        bool left{};
    };
    Outcome frame(const Pointer&);
    // Back from the pause menu: the loop starts over ($9968), still before
    // or after the start as it was.
    void resume() { panel_.clear_pause(); }
    void set_settings(const Settings& s) { settings_ = s; apply_view_settings(); }
    // The designer's "Enemy Movement: Off" ($11E4 = -2): the start only
    // brings the freeze counter to -1, so nothing but the spiders moves.
    void freeze_enemies() { state_.clock = -2; }

    // The colours to fade to: the tower's, the sky gradient, the panel style's.
    std::array<data::Colour, 48> target_palette() const;

    // A demonstration (the interludes after a tower, the demo sequence,
    // $158AA): the tower runs at once; its first spider presses the
    // buttons its script names ($FE4(a6), 16 bytes a demonstration), the
    // camera above and behind it climbs as the script's top bits say.
    void start_demo(int index);
    void draw_demo_screen(); // the view's frame in pen 14
    void demo_frame();       // $15922
    bool demo_script_ended() const { return demo_byte_ == 0; } // $FDC

    const data::Tower& tower() const { return tower_; }
    int game_type() const { return game_type_; }
    // The spiders the tower has that are still there (the survivor bonus, $9318).
    int survivors() const;
    void draw_title(); // $994E: the tower's name on the top line
    void redraw_panel() { draw_panel(); }

    const game::TowerState& state() const { return state_; }
    game::TowerState& state() { return state_; }
    int end_reason() const { return state_.end_reason; } // 1 won, 2 time out, 3 all spiders, 4 Grabber
    // The effect records started during the last frame, in order.
    const std::vector<std::uint32_t>& sounds() const { return sounds_; }
    // What the sound player does at the end of the frame ($15342).
    Effects::Frame sound_frame() const;
    int volume_offset() const { return volume_offset_; } // $CE(a6)

private:
    void one_frame(const Pointer&, bool ready);
    void draw_view(const Pointer&, bool ready, const view::Camera* fixed = nullptr);
    void draw_noise();       // $D804: no picture (spider gone, cameras forbidden)
    void draw_panel();       // $AB5A / $AC44: every button in its state
    void apply_view_settings(); // $9F84
    int button_at(int x, int y) const; // the click areas at $31DE

    const data::Assets& assets_;
    Screen& screen_;
    const data::Tower& tower_;
    Settings settings_;
    int game_type_{};
    game::Rules rules_;
    game::TowerState state_;
    game::Panel panel_;
    view::Pens pens_;
    view::SkySettings sky_settings_;
    view::SkyState sky_;
    std::map<std::uint32_t, data::Shape> shapes_;
    std::vector<std::uint32_t> sounds_;
    bool running_{};      // past the READY loop ($99C2)
    bool even_frame_{true}; // the buffer the frame draws into ($20(a6) = 0)
    int picked_{-1};      // the pick's result for the next poll ($7F4)
    int view_offset_{}, view_height_{0x58}, view_distance_{-16}; // $FBC, $FB8, $FBA
    int noise_x_{}, noise_y_{}, noise_step_{}; // $88A-$88E
    int volume_offset_{};
    // The program shown in the strip, as last drawn (the original redraws
    // the strip only when asked, $1564).
    game::Program strip_{};
    int demo_{-1}, demo_step_{}, demo_byte_{}; // $FD8, $FDA, $FDC
    int demo_height_{0x120};                    // $FDE
};

} // namespace babel::front
