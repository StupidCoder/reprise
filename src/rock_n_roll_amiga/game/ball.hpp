#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/level_map.hpp"
#include <array>
#include <cstdint>

// The ball while it rolls: steering by the mouse, the camera following it,
// the cells under it, contact with railings and the bounce off them, and the
// start of a fall into the abyss (.analysis/rock_n_roll_amiga/game.md).
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

    // Restarting a level clears $719C6-$71A23 (keys, finds, map pieces and
    // more); score and money lie below that range and stay.
    void clear_for_restart() {
        keys = {};
        finds = {};
        map_pieces = 0;
    }
};

enum class BallMode { rolling, falling, lost };

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
    int fall_cell{};                  // $6C9BC
    int fall_updates{};               // $6C3C4: counts on through falls; reset at level start and on continue
    bool mouse_ignored{};             // $6C3C6: reads see no movement (while falling)
    Fixed falling_friction{0x800};    // $6C3C8

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

// The prompt after a fall, left button ($6BB86 → $6C5BC): the ball goes
// back to the continue point and rolls again from rest; the map and the
// pocket stay as they are.
void continue_after_fall(Ball&);

} // namespace rocknroll::game
