#pragma once
#include "sonic2_gg/game/routines.hpp"
#include "sonic2_gg/game/types.hpp"

// One of the 20 object slots (64 bytes each, D500-D9FF). Slot 0 is Sonic.
//
// The fields keep the offsets of the original's 64-byte records in their
// comments. Most have the same meaning for every object; `counter`, `timer`,
// `hits` and `step` belong to the object types, which name their use.
namespace sonic2_gg {

// Bits of Object::display (offset 4).
namespace display_flag {
inline constexpr u8 keep_on_screen = 0x02; // Never removed for being off screen.
inline constexpr u8 mirrored = 0x10;       // Faces left: sprite mirrored.
inline constexpr u8 flicker = 0x20;        // Blinks (hidden every other 2 frames).
inline constexpr u8 offscreen = 0x40;      // Outside the update window this frame.
inline constexpr u8 hidden = 0x80;         // Not drawn.
} // namespace display_flag

struct Object {
    u8 type{};               // 00: 0 free; 1 Sonic; FE/FF being removed
    u8 state{};              // 01: current animation/behaviour script
    u8 next_state{};         // 02: requested script; switched on the next update
    u8 flags{};              // 03: type-specific flag bits
    u8 display{};            // 04: display_flag bits
    u8 sprite_count{};       // 05: hardware sprites of the current frame
    u8 frame{};              // 06: sprite frame number
    u8 frame_ticks{};        // 07: updates left on the current script frame
    u8 tile_base{};          // 08: added to frame tile numbers
    u8 tile_base_mirrored{}; // 09: the same when mirrored
    u8 angle{};              // 0A: direction for angle movement (sine table 0330)
    u8 speed{};              // 0B
    // 0C/0E: the routine run on every update of the current script frame,
    // and the position in the script: step `script_step` of the script of
    // type `script_type`'s state `script_state` (scripts.hpp). A script type
    // of 0: no script yet (it starts from the object's state).
    Routine routine{Routine::none};
    u8 script_type{}, script_state{};
    std::uint16_t script_step{};
    u8 x_sub{};               // 10: X fraction (1/256 pixel)
    std::uint16_t x{};        // 11: X in world pixels
    u8 y_sub{};               // 13
    std::uint16_t y{};        // 14
    std::uint16_t vx{};       // 16: 1/256 pixel per update
    std::uint16_t vy{};       // 18
    std::uint16_t screen_x{}; // 1A: sprite position
    std::uint16_t screen_y{}; // 1C
    // Two bytes each type uses its own way: mostly counters and timers,
    // sometimes direction or phase bits (named where they are used).
    u8 counter{};           // 1E
    u8 timer{};             // 1F
    u8 touch{};             // 20: 1 when touching Sonic this update
    u8 touch_sides{};       // 21: sides of the contact with Sonic
    u8 terrain{};           // 22: terrain contact bits
    u8 contact{};           // 23: terrain and object contact bits combined
    u8 hits{};              // 24: hits a boss can still take (a monitor: its previous display bits)
    std::uint16_t layout{}; // 28: ROM address of the frame's sprite offsets
    std::uint16_t tiles{};  // 2A: ROM address of the frame's tile list
    u8 width{};             // 2C: collision half sizes
    u8 height{};            // 2D
    u8 flicker_ticks{};     // 2E
    u8 step{};              // 2F: a scripted sequence's position (Sonic, the continue screen)
    u8 power_up{};          // 32: Sonic's power-up: 1 speed shoes, 2 invincibility
    std::uint16_t origin_x{}; // 3A: spawn position
    std::uint16_t origin_y{}; // 3C
    u8 placement{};           // 3E: 1-based index in the placement table, 0 if spawned
    u8 param{};               // 3F: spawn parameter

    // The script starts again from the object's state on its next update.
    void restart_script() { script_type = 0; }

    // `counter` and `timer` read as one 16-bit count (the intro's objects).
    std::uint16_t long_count() const { return std::uint16_t(counter | (timer << 8)); }

    void set_long_count(unsigned v) {
        counter = u8(v);
        timer = u8(v >> 8);
    }

    // X with its fraction, as the 16-bit value the code adds speeds to.
    std::uint16_t x_low16() const { return std::uint16_t(x_sub | ((x & 0xFF) << 8)); }
};

} // namespace sonic2_gg
