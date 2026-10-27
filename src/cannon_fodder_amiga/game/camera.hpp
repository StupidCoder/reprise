#pragma once
#include "cannon_fodder_amiga/game/map_view.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

// Where the play view's camera goes (.analysis/cannon_fodder_amiga/game.md
// "View and scrolling"). The original moves it per frame from two sources:
//
// - look-ahead ($8727C, $873FC): a point between the squad leader and the
//   mouse pointer, which the camera eases towards;
// - hard follow ($9C0D4): when the leader gets near the edge of the view,
//   a fixed 2 pixels per frame until the leader is back near the middle.
//
// The geometry uses the program's own tables (read from `fodderc`): a
// distance estimate, a 256-step sine and a 32 x 32 angle table.
namespace fodder::game {

class GeometryTables {
public:
    // From the installed program file `fodderc` (tables at $A128C, $A168E
    // and $A1890 in the $80000 layout; they hold no relocations).
    explicit GeometryTables(std::span<const std::uint8_t> program_file);

    // $A1062: |dx| and |dy| halved together until both are below 32, the
    // table entry shifted back; 1000 when either is 320 or more.
    int distance(int dx, int dy) const;

    // $9E28C / $A0B8E: direction of (dx, dy) in sine-table bytes (0-$1FE,
    // $200 per turn; x = sine(a), y = sine(a + $80)). Nothing for (0, 0).
    // `top_bit` is where the routine starts looking for the highest set
    // bit: 14 in $9E28C, 9 in $A0B8E (larger values then lose precision).
    std::optional<int> direction(int dx, int dy, int top_bit) const;

    // Sine at a direction in sine-table bytes; +/-32767 at the extremes.
    int sine(int direction) const { return sine_[std::size_t(direction & 0x1fe) / 2]; }

private:
    std::array<std::uint8_t, 1024> distance_{};
    std::array<std::int16_t, 256> sine_{};
    std::array<std::int16_t, 1024> angle_{};
};

struct Point {
    int x{}, y{};
};

// The look-ahead point ($87360-$873EE) while the pointer is over the play
// area (pointer x > 30): from the leader (x + 24, y) towards the pointer's
// map position, half the distance estimate capped at 140 (100 while
// `$82798` is set, hypothesis: in a vehicle), then x - 24, kept >= 0.
// Nothing when the pointer is exactly on that origin.
std::optional<Point> look_ahead_target(const GeometryTables& tables, Point leader, Point pointer_on_map,
                                       bool short_reach);

// The hard follow's decision for one axis ($9C0D4), from the leader's
// offset to the camera: start at +/-2 per frame beyond the edge limits,
// stop inside the middle band.
struct FollowAxis {
    bool active{};
    int step{}; // -2, 0 or 2 pixels per frame
};
FollowAxis follow_x(FollowAxis state, int leader_minus_camera);
FollowAxis follow_y(FollowAxis state, int leader_minus_camera);

// The camera's motion state, in 16.16 fixed point like the original
// ($814AE/$814B2 position, $814C4/$814C8 velocity, $814B8 chase speed).
struct CameraMotion {
    std::int32_t x{}, y{};
    std::int32_t velocity_x{}, velocity_y{}; // the chase's, for the next frame
    std::int32_t speed{};                    // chase speed, 16.16
    int max_speed{20};                       // $814BC, set to 20 by $8727C
    FollowAxis follow_x_axis, follow_y_axis;
    // Reversal holds ($81540/$81542): 9 after the direction of motion
    // changes; the tile drawer clears them (tile_drawer_tick).
    int hold_x{}, hold_y{};
    int sign_x{1}, sign_y{1};
    // What the tile drawer works from ($81526/$81528 direction,
    // $81532/$81534 ring column/line it last started from).
    int drawer_direction_x{1}, drawer_direction_y{1};
    int drawer_ring_x{}, drawer_ring_line{};

    Camera camera() const { return {x >> 16, y >> 16}; }
};

struct CameraFrame {
    int map_width{}, map_height{}; // pixels
    bool frozen{};       // one-screen map or a state that stops scrolling ($867BE)
    bool tick_overdue{}; // the last game tick ended and 3+ frames passed
    Point target;        // the look-ahead point ($815EA/$815EC)
};

// Where the camera's corner sits in the display ring ($814A6, $814AA): x
// modulo 320, and one line further for every 320 pixels of x (the ring is
// one long strip of 320-pixel lines), modulo 256 lines. Checked against the
// original on every frame of a recording (cam3).
struct RingPosition {
    int x{}, line{};
};
RingPosition ring_position(const CameraMotion& motion);

// The tile drawer's bookkeeping on every second game tick ($A38D6 when
// $80612 is 0, then $A3526/$A34B4). The drawer fills one tile column ahead
// of the camera (x + 304, or x - 16 moving left; $AB63A) and one tile row
// (y + 240 or y - 16; $AB46A) every tick; it starts from the direction and
// ring position taken here. Per axis: same direction as before -> the hold
// ends and the drawer moves its origin to the camera; a new direction is
// stored, and the hold also ends if the ring position is still in the
// drawer's 16-pixel column (line) - otherwise at the next such tick.
//
// The original takes this snapshot in the middle of the tick, so it sees the
// camera either before or after the next vertical blank depending on how
// long the tick's earlier work took (11 and 17 times in a 260-frame
// recording). We take it at the start of the tick; the hold then ends at
// most a frame away from the original's (2 of 259 frames differed).
void tile_drawer_tick(CameraMotion& motion);

// One frame of camera motion, in the original's order: drop the chase
// velocity on axes the hard follow drives ($867A0), speed limit ($867BE),
// add the follow step, reversal hold, map limits, 2 px cap ($8682A), move
// and stop at 0 ($869CE/$86A8C), then two chase updates towards target -
// (144, 112) ($873FC, called twice per frame).
void step_camera(CameraMotion& motion, const GeometryTables& tables, const CameraFrame& frame);

} // namespace fodder::game
