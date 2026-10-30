#pragma once
#include "stunt_car_racer_amiga/race/tables.hpp"
#include <array>
#include <cstdint>

// A car as the game simulates it (game.md, "Car physics"): a sprung rigid body
// on three suspension points (front left, front right, rear), integrated once
// a frame in fixed point with semi-implicit Euler and a 238/256 damping at
// both stages. Positions are 16.16 with 128 units to a track cell (plan units
// = position >> 12, heights = position >> 11); angles count 65536 to the turn.
// Body axes: side, up, forward.
namespace stuntcar::race {

struct Spring {
    std::int32_t surface{};      // the road's height under the point
    std::int32_t contact{};      // the body's height there
    std::int32_t compression{};  // surface - contact - rest length
    std::int16_t travel{};       // compression, limited to -$300..$1400
    std::int16_t previous_travel{};
    std::int16_t force{};        // spring + damper, 0..$11FF
    std::uint8_t damage{};       // 0-255
};

// The joystick as the race reads it.
struct Controls {
    bool accelerate{}, brake{}, left{}, right{}, fire{};
};

struct Car {
    // Body.
    std::int32_t x{}, y{}, z{};
    std::int16_t pitch{}, yaw{}, roll{};
    std::int16_t vx{}, vy{}, vz{};                       // world velocity
    std::int16_t spin_pitch{}, spin_yaw{}, spin_roll{};  // body angular momentum
    std::array<std::int16_t, 3> force{};                 // world force this frame
    std::int16_t torque_pitch{}, torque_yaw{}, torque_roll{};
    std::int16_t rate_pitch{}, rate_yaw{}, rate_roll{};  // world angular rates
    std::array<std::int16_t, 36> matrix{};               // orientation and its products

    // Suspension.
    std::int32_t rest_length{};
    std::array<Spring, 3> springs{};
    std::array<std::int16_t, 6> contact_offsets{};       // where the points sit (side, along)
    std::uint8_t impact_frames{};
    std::uint8_t damage_tolerance{};   // per car, << 8 added to the impact threshold
    std::uint8_t damage_window{};      // per track: impact frames that count
    std::uint8_t bottomed{};           // hard landings this frame
    bool damaged{};                    // a spring added damage this frame
    std::int16_t peak_impact{};
    std::int16_t net_lift{}, pitch_lift{}, roll_lift{};
    std::uint8_t on_ground{};          // high | low byte of the net lift

    // Tyres, drive and drag.
    std::array<std::int16_t, 3> gravity{};       // in the body frame
    std::array<std::int16_t, 3> body_velocity{};
    std::array<std::int16_t, 3> body_force{};
    std::array<std::int16_t, 3> loads{};         // net lift along the body axes
    std::int16_t previous_forward_load{};
    std::int16_t drive{};                        // set by the controls each frame
    bool sliding{};
    std::array<std::int16_t, 3> impulse{};       // a knock from the other car, pending
    bool impulse_pending{};
    std::int16_t impulse_left{};
    std::array<std::int16_t, 3> knock{};
    std::uint8_t knock_timer{};

    // Controls.
    std::int8_t steer{};               // -15, 0, 15 (or the recovery clock)
    bool fire_up{true};                // the fire button is not held
    bool accelerator_latched{};
    std::uint8_t accelerate_low{}, accelerate_high{}; // per car
    std::uint8_t boost_reload{}, boost_timer{};
    bool boosting{};
    std::uint8_t boost_left{};         // BCD
    std::uint8_t boost_allowance{};    // BCD: what the race started with
    bool stalled{};

    // What the camera, dashboard and sound read.
    std::int16_t speed{};              // |forward velocity|
    std::int16_t engine_note{};
    std::int16_t view_pitch{};         // -pitch
    std::int16_t view_roll{};          // the roll the frame's matrix was built with
    bool pitch_at_limit{};

    // Spawn (hanging from the crane) and crash recovery: a clock, a servo that
    // levels the body's roll, and one that lowers it to the ride height.
    std::uint8_t recovery{};           // 0 = driving; counts down from $F0
    bool armed{};                      // drive forces apply
    std::uint8_t tick{};               // 0 on frames the slow clock advances
    std::uint8_t tick_phase{};
    std::uint8_t frame_count{};
    bool facing_back{};                // the spawn direction
    std::int16_t settle_roll{};        // the roll the servo drives
    std::int16_t height{};             // y >> 11 (the placement refreshes it)
    std::int16_t ride_height{};        // what the crane lowers the car to
    bool launched{};                   // a recovery has completed before
    bool launch_flag{};
    std::uint8_t launch_message{}, launch_message_length{};
    std::uint8_t crash_state{};        // bit 7 while crashed
    bool crash_started{};              // went over the edge this frame
    std::uint8_t limit_select{};       // bit 7 with off_track == $E0: the narrow angle limits
    std::uint8_t thud_holdoff{};       // frames before another landing thud
    std::uint8_t thud_volume{};        // of the last hard landing, 28-64
    bool thud{};                       // a landing thud starts this frame
    std::uint8_t drag_flags{};         // bit 7: lighter drag (with the opponent flag clear)
    std::uint8_t opponent_flags{};

    // Placement on the track (the render pass refreshes it each frame).
    // The world is turned in quarter turns so the car's heading stays within
    // 45 degrees of +z; positions within a cell are kept in that frame.
    std::uint16_t quadrant{};          // (yaw + $2000) & $C000
    std::uint8_t cell_x{}, cell_z{};   // the car's grid cell
    std::uint8_t fine_x{}, fine_z{};   // its place within the cell, 0-255
    std::int32_t turned_x{}, turned_z{};
    std::int16_t eye{};                // the camera's height
    std::int16_t corner_x{}, corner_z{}; // the cell corner relative to the car (turned)
    std::int16_t view_yaw{};           // yaw within its quadrant
    bool off_grid{};                   // no segment under the car
    std::uint8_t off_road_side{};      // $80 left / $02 right of the road, 0 on it
    std::int16_t across_centre{};      // across less half the road (the spawn servo leans by it)
    std::int16_t progress{};           // along, in driving direction
    bool near_end{};
    int segment{};                     // the car's segment
    std::int16_t across{};             // from the piece's centre line
    std::int16_t along{};              // along the piece
    std::int16_t heading{};            // the piece's heading at the car
    std::uint8_t step_index{};         // the car's cross-section in the piece
    std::uint8_t track_number{};       // selects the airborne self-righting
    bool league{};                     // a league race (the practice is not)

    // Road contact and track following (game.md, "Car physics").
    std::uint8_t off_track{};          // bits shifted in per point past the edge
    std::uint8_t edge_side{};
    std::uint8_t rear_along{};
    std::int16_t heading_error{};
    std::int16_t heading_error_scaled{};
    std::uint8_t curve_side{};
};

} // namespace stuntcar::race
