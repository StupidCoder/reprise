#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/level_map.hpp"
#include <array>
#include <optional>
#include <cstdint>

// The ball while it rolls: steering by the mouse, the camera following it,
// the cells under it, contact with railings and the bounce off them, and the
// start of a fall into the abyss (games/rock_n_roll_amiga/game.md).
//
// Like the original, the ball's place is kept as the camera (the view's
// top-left) plus the ball's place in the view; the rules depend on both.
namespace rocknroll::game {

// The mouse's two 8-bit counters as JOY0DAT holds them; the rules work on
// how far they moved since the last read.
struct MouseCounters {
    std::uint8_t x{}, y{};
    std::uint16_t word() const { return std::uint16_t(y << 8 | x); }
};

// rolling ($6C274), falling ($6C3D6), lost: the fall is over and the level
// loop ends for the continue/restart prompt ($6FDEA).
// What the player has collected; shown on the panel.
struct Pocket {
    std::array<int, 4> keys{};    // cells $01-$04: $719EC, $719F8, $719F0, $719F4 (at most 9 each)
    std::array<int, 4> finds{};   // cells $05-$08: $719FC, $71A08, $71A00, $71A04
    int score{};                  // $719BA: finds add to it
    int money{};                  // $719C2: coins $11-$13, at most 9,900 ($26AC); shops take it
    int map_pieces{};             // cells $0D/$0E: pieces of the magic map ($6EB22, not rebuilt)
    int repair_sets{};            // $719C6: bought at $33 shops
    int bombs{};                  // $71A1C: bought at $0F/$31 shops; item 6 places one ($6BE9A)
    int energy{0x7FF};            // $6F45A: shops take twice the missing money from it; some cells drain it
    int shown_energy{0x800};      // $6F458: the panel's bar; follows the energy down only ($6F466)
    int rescues{};                // $719B2: bought at $46 shops, at most 9; item 10 saves a falling ball ($6C488)
    int continues{};              // $719B6: extra balls bought at $47 shops (a game over continues with one)

    // Restarting a level clears $719C6-$71A23 (keys, finds, map pieces and
    // more); score and money lie below that range and stay.
    void clear_for_restart() {
        keys = {};
        finds = {};
        map_pieces = 0;
        repair_sets = 0;
        bombs = 0;
    }
};

// rolling ($6C274), falling ($6C3D6), lost: the fall is over and the level
// loop ends for the continue/restart prompt ($6FDEA); warped: a secret warp
// ended the level ($69F8A).
// The level clock ($6C84C, every update before steering).
struct Clock {
    int hundredths{};                 // $719CE, + step each update (2: 50 updates a second)
    int step{2};                      // $6C842
    int seconds{};                    // $719CC (low word of $719CA)
    std::uint16_t limit{0xFFFF};      // $6C846: out of time when the seconds reach it
    int countdown{0xFA};              // $6C848: $FA while time runs; counts down once out of time
    std::uint8_t ticks{};             // $6EC62: bit 1 after the first second, bit 0 after the second, bit 2 toggles
    bool drained{};                   // $6C840: an energy cell drained this period (cleared every second second)
    int switch_latch{};               // $6DDF0: a switch was used (cleared every second)
    bool mouse_frozen{};              // $70408: the read sees no movement (the ball coasts out of time)
    bool out_of_time{};               // the ball stopped after the time ran out: lost
};

enum class BallMode { rolling, falling, rescued, lost, warped };

// The ball bought at a shop for $800 updates ($6F45C): protective ($30:
// energy losses halved, $719EA) or ice-proof ($32: no slow on ice, $719E8).
enum class BallKind { normal, protective, ice_proof };

struct Ball {
    Fixed camera_x{}, camera_y{};     // $71A30/$71A34
    Fixed view_x{}, view_y{};         // $71A38/$71A3C
    Fixed speed_x{}, speed_y{};       // $71A48/$71A4C, per update
    Fixed camera_limit_x{}, camera_limit_y{}; // $71A50/$71A54
    Fixed max_speed_x{0x2600}, max_speed_y{0x2600}, min_speed_x{-0x2600}, min_speed_y{-0x2600}; // $71A58..$71A64
    Fixed friction{400};              // $71A76

    // Steering ($7040C).
    bool grip{true};                  // $70404: the mouse is read this update
    int updates_without_read{};       // $7040A
    MouseCounters last_mouse;         // $719E6
    bool steered{};                   // $70405: set after the first read
    bool hold_x{}, hold_y{};          // $70402/$70400: skip friction (and x after a still y)
    bool slow{};                      // $719A4: ice under the ball, accelerate by $20
    Fixed ice_push_x{}, ice_push_y{}; // $6C242/$6C246: ±$1200 in the last accelerated direction
    // The acceleration the last read used per axis ($71A6E/$71A72: $200
    // when the axis did not move, 100 for one count, $20 on ice); arrows and
    // slopes push by it.
    Fixed last_push_x{0x200}, last_push_y{0x200};

    // Contact with railings ($6FDEE).
    bool collisions{true};            // $6FDE8
    bool high_cell{};                 // $6D0F8: a cell under the ball is $50 or higher
    int contact_rows{}, contact_columns{}; // $71A7A (top +, bottom -) / $71A7E (left +, right -)

    int centre_cell{4};               // $6B4AA: 1-4, which of the 2 x 2 cells holds the centre
    Fixed roll_x{}, roll_y{};         // $6C232/$6C236: rolling picture phases

    BallMode mode{BallMode::rolling};
    bool energy_out{};                // the fall started by empty energy ($6C3CC: other pictures)
    int rescue_updates{};             // $6C486: the rescue's pictures ($6C488)
    bool repair_armed{};              // $6C9BE: item 8 held; an abyss under the centre is mended

    // A bomb ($6BE78 timer, $6BE7A its cell; $6BE86 the bomb spot ($10)
    // under the ball's centre this update, or none).
    int bomb_timer{};
    long bomb_cell{-1};
    long bomb_spot{-1};
    int fall_cell{};                  // $6C9BC
    int fall_updates{};               // $6C3C4: counts on through falls; reset at level start and on continue
    bool mouse_ignored{};             // $6C3C6: reads see no movement (while falling)
    Fixed falling_friction{0x800};    // $6C3C8

    // Bought speed ($2F shops): each purchase widens all four limits by $200
    // and adds $100 here ($71A68 low word, at most $400 = $71A6C).
    int extra_speed{};

    // A bought ball and its time ($6F45C; $6B164 set = none running). The
    // kind's effects ($719E8/$719EA) end one update after the time does.
    BallKind kind{BallKind::normal};
    bool ice_proof{}, protective{};
    int kind_updates{1};
    bool kind_running{};

    // Shops: updates spent on the same spot without enough money ($6E27C)
    // and that spot (camera x + view x, $6E27E).
    int shop_wait{};
    Fixed shop_spot{};

    // Bridges ($DD-$E2): this update ($6FDE6, cleared by $6ACE6) and the
    // next update's steering only moves the camera ($70406).
    bool on_bridge{};
    bool carried{};

    Clock clock;

    // Crumbling floor ($6ECB0): the last floor/crumble cell under the centre
    // and what it turns into when the centre leaves it ($6EC94/$6EC92).
    long remembered_cell{-1};
    std::uint8_t pending_value{};
    bool level_done{};                // $6FDEC: the exit was reached (the ball drops into it)
    bool hint_bought{};               // a $45 shop sold a hint ($69DFA, shown by the game)

    // Events for the game: a secret warp (the level ends, $69F8A) and the
    // level's reward ($6D8CE, table $78008), not carried out here.
    bool secret_warp{}, reward_due{};
    bool teleported{};                // this update, $6F8B8 (the picture flashes)

    // Where "continue" puts the ball back ($6C476: camera and view, saved by
    // $6F7B6 at the end of each update while the fall counter is 0).
    Fixed continue_camera_x{}, continue_camera_y{}, continue_view_x{}, continue_view_y{};

    // Cell kinds the rules met that are not rebuilt yet (for tools).
    int unhandled_cells{};
    std::array<bool, 256> unhandled_kinds{};

    static Ball at_start(const LevelMap&);
};

// One update of the in-level loop ($68940), the parts rebuilt so far, in
// the original's order (items collected go into the pocket): steer ($7040C), halve along a contact ($703B4),
// keep in view ($707AA), centre cell ($6B4AC), the cells' rules ($6CFC6),
// rolling picture ($6C274), contact ($6F666, $6F60A, $6FDEE), bounce
// ($700E8) and the speed limits ($6ABF0).
void update_ball(Ball&, Pocket&, LevelMap&, const data::World&, MouseCounters mouse);

// The start of a level ($688B4): steering counts, but the camera stays and
// there is no friction, until one speed reaches $1000-$1FFF; then the ball
// moves off and true is returned.
bool wait_for_start(Ball&, MouseCounters mouse);

// The prompt after a fall, left button ($6BB86 → $6C5BC): the ball goes
// back to the continue point and rolls again from rest; the map and the
// pocket stay as they are.
void continue_after_fall(Ball&);

// The panel's items ($6B4F4, 0-10 in steps of 2): 0 give up, 2 the eye
// (the overview map), 4 pause, 6 a bomb, 8 a repair set, 10 a rescue. The
// right button steps the choice ($6B4F8: from 2 on, past the last ($6B4F6)
// back to 2), the left button uses it while held ($6B54E).
struct Items {
    int selected{2};
    int last{10};
    bool right_latch{}; // $6B4F0
};

enum class ItemUse { none, give_up, overview, pause };

// The end of an update ($68A02): the buttons as held now. Bombs, repair
// sets and rescues act on the ball and the map here; the rest is the
// caller's to show.
ItemUse use_items(Ball&, Pocket&, LevelMap&, Items&, bool left, bool right);

// Where a level's secret warp leads (a level index), or none: then on to
// the next level as from the exit.
std::optional<int> secret_warp_target(int level_index, Pocket&);

} // namespace rocknroll::game
