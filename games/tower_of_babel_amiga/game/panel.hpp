#pragma once
#include "tower_of_babel_amiga/game/rules.hpp"
#include "tower_of_babel_amiga/game/tower_state.hpp"

// The control panel's rules ($ADFC, polled once a game frame): choosing a
// camera or spider, direct commands, the cameras' moves, program editing
// and running, the pause button. The original reads no clicks, only the
// left button held over a button; short lockouts (in game frames) keep a
// held button from repeating at once.
namespace babel::game {

// The panel's buttons in the program's order (the click areas at $31DE).
enum Button {
    camera_n = 0, camera_e, camera_s, camera_w, // 0-3
    zapper, pusher, grabber,                    // 4-6: the spiders' slots
    forward, back, left, right, fire, pause,    // 7-12
    stop_in, run_out, run_all, write, erase, clear, // 13-18 (erase: "Delete")
    strip_first,                                // 19-26: the program strip's eight places
    buttons = 27
};

class Panel {
public:
    // What the player does this game frame: the button the pointer is over
    // with the left button held (-1 none), or a click in the view.
    struct Input {
        int button{-1};
        bool view_click{};
    };
    // Settings the panel obeys: linked cameras (option $1110 = 0) and the
    // tower's flag 4 (cameras may not be used).
    struct Settings {
        bool link_cameras{true};
        bool cameras_forbidden{};
    };

    // One poll. `picked`: what the view's pick found for last frame's click
    // in the view (the object slot in the cell under the pointer, or -1).
    void poll(TowerState&, const Rules&, const Settings&, const Input&, int picked);

    // A button pressed by a script (the demonstrations, $AF3C): the
    // commands' part of the poll only.
    void press(TowerState&, const Rules&, const Settings&, int button);

    void clear_command_waits() { turn_wait_ = move_wait_ = 0; } // $15922

    bool pick_requested() const { return pick_; }     // $7EC = 0: the next view draw picks
    bool pause_requested() const { return pause_; }   // $14EA >= 0
    void clear_pause() { pause_ = false; }
    int selected_light() const { return selected_; }  // $1554
    int select_wait() const { return select_wait_; }  // $154C
    bool fire_failed() const { return fire_failed_; } // the "nothing happened" sound this poll ($31AA)

    // The lockouts ($154C-$1552) and the lit choice ($1554), for research tools.
    struct Waits {
        int select{}, turn{}, move{}, program{}, selected{};
    };
    Waits waits() const { return {select_wait_, turn_wait_, move_wait_, program_wait_, selected_}; }
    void set_waits(const Waits& w) {
        select_wait_ = w.select;
        turn_wait_ = w.turn;
        move_wait_ = w.move;
        program_wait_ = w.program;
        selected_ = w.selected;
    }

    // The lockouts at the start of a tower (cleared by the unpacker).
    void reset(const TowerState& t) { *this = Panel{}; selected_ = t.active; }

private:
    void select(TowerState&, int slot);
    void command(TowerState&, const Rules&, const Settings&, int k);
    void program_button(TowerState&, const Settings&, int k);
    void camera_shift(TowerState&, const Settings&, int slot) const; // $B44C
    void cameras_follow(TowerState&, const Settings&, int slot) const; // $B470 / $B478

    int select_wait_{}, turn_wait_{}, move_wait_{}, program_wait_{}; // $154C-$1552
    int selected_{};
    bool pick_{}, pause_{}, fire_failed_{};
};

} // namespace babel::game
