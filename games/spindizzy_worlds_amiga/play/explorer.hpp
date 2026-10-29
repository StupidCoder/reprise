#pragma once
#include "spindizzy_worlds_amiga/play/surfaces.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include <cstdint>
#include <vector>

// The explorer's movement: steering, friction, gravity, the block tops it
// stands on and the walls it bounces off. One call per rules tick (the
// original runs its object ticks once per frame in slow mode, twice
// otherwise; $11510).
//
// Units are the original's: positions are words with 256 units per cell
// (x, y 0 … $1FFF over 32 cells, z 256 per level), velocities are 16.16
// fixed point in those units per tick. The arithmetic keeps the original's
// word and long wrap-around and rounding, so a recorded run replays tick
// for tick (`spindizzy_worlds_assets physcheck`).
//
// Evidence: games/spindizzy_worlds_amiga/game.md ("Explorer movement").
namespace spindizzy::play {

// Joystick bits as the reader $104C0 leaves them in $10A63.
namespace stick {
constexpr std::uint8_t up = 0x01;    // −x
constexpr std::uint8_t down = 0x02;  // +x
constexpr std::uint8_t left = 0x04;  // −y
constexpr std::uint8_t right = 0x08; // +y
constexpr std::uint8_t fire = 0x80;  // doubles the push on the ground
} // namespace stick

// The joystick byte $10A63 as the game reads it: the reader ($104C0, from
// the CIA-B interrupt) rewrites it only when the stick changes ($10A64
// keeps the last read); the waits take its fire bit away ($109CC, $109E2:
// bclr #7) and the dying kind clears it ($12CCC), so fire counts again
// only once the stick changes. The explorer steers by it too.
struct StickByte {
    std::uint8_t byte{}, last{}; // $10A63, $10A64
    void read(std::uint8_t now) {
        if (now != last) byte = last = now;
    }
    bool take_fire() {
        const bool fire = byte & stick::fire;
        byte = std::uint8_t(byte & ~stick::fire);
        return fire;
    }
    template <class A> void persist(A& ar) { ar(byte, last); }
};

// Contact flags ($2510), set by the surface the explorer touches this tick.
namespace contact {
constexpr std::uint16_t grounded = 0x8000; // on a top (landing sound, respawn point)
constexpr std::uint16_t looked_through = 0x4000; // a lookup passed a $22/$23 cell (not read)
constexpr std::uint16_t ice = 0x0200;      // on ice (steering allowed; the clues call it ice)
constexpr std::uint16_t bouncy = 0x0100;   // on a bouncy top (no landing sound)
constexpr std::uint16_t traction = 0x0080; // full steering and friction
constexpr std::uint16_t unheld_lift_top = 0x0002; // touching a lift's top without the lift holding it
constexpr std::uint16_t on_lift = 0x0001;  // carried by a lift (set by the lift's code)
} // namespace contact

struct Explorer {
    std::int16_t x{}, y{}, z{};          // $24C6
    std::int32_t vx{}, vy{}, vz{};       // $24D4, 16.16
    std::uint16_t contact{};             // $2510
    std::uint16_t previous_contact{};    // $2512
    std::int16_t friction{};             // $24E2, the last touched block's
    bool on_surface{};                   // $2516 is the block's routine (else the explorer's own z)
    int floor_block{};                   // $250E: the block found below the explorer
    std::int16_t floor_base{};           // $2504: that block's level << 8 ($FF00 when none)
    int column_x{}, column_y{};          // $24FE: the column of the last floor lookup
    int wall_level{};                    // $250A: highest neighbour cell the wall test looks at
    int ice_ticks{};                     // $2514
    int brake_lock{};                    // $24EC
    bool brake_requested{};              // $24EE bit 7 (space bar, $11314)
    std::int16_t respawn_x{}, respawn_y{}, respawn_z{}; // $24CC
    std::int16_t timer_a{}, timer_b{}, arrival_ticks{}; // $24F4, $24F6, $24F2
    std::int16_t falling_speed{};        // $24D2: vz's whole part before the top acted
    template <class A> void persist(A& ar) {
        ar(x, y, z, vx, vy, vz, contact, previous_contact, friction, on_surface, floor_block, floor_base, column_x, column_y,
           wall_level, ice_ticks, brake_lock, brake_requested, respawn_x, respawn_y, respawn_z, timer_a, timer_b,
           arrival_ticks, falling_speed);
    }
};

// What a tick did besides moving the explorer.
struct Death {
    int hint;          // the death's clue ($15FC0, shown once per kind of death); −1 none
    int becomes;       // the explorer's next object kind: −2 lost (a pause, then −3), −3 respawn
    bool costs_energy; // $2060(a5) is taken from the energy $206C(a5) (the respawn takes $10 more)
};

struct TickEvents {
    std::vector<Death> deaths;
    int landing_volume{};  // effect 6 with this volume ($12F52), 0 = none
    bool bounced{};        // effect $24 ($1352C)
    int switch_sound{};    // effect played by a switch pad ($1325A), 0 = none
};

// The level the explorer moves through.
struct Ground {
    const world::LevelGrid& grid;
    const SurfaceTable& surfaces;
    std::uint16_t level_flags{}; // $24B8: bit 11 = ice never times out ("Ice timeout off")
    world::Switches& switches;   // switch pads write the push-button state
};

// The explorer's own tick ($12B7C): steering, friction, gravity, moving,
// the top below it, contact and walls. Objects tick after it and may move
// it (lifts); `settle` then finishes the tick.
void move_explorer(Explorer& explorer, std::uint8_t joystick, Ground& ground, TickEvents& events);

// The rest of the tick after the objects ($12BFC): landing impact, the
// respawn point, falling out of the world.
void settle_explorer(Explorer& explorer, TickEvents& events);

} // namespace spindizzy::play
