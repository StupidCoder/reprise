#pragma once
#include "triple_trouble_gg/game/routines.hpp"
#include "triple_trouble_gg/game/types.hpp"
#include <array>
#include <cstdint>

// One of the 24 object slots (64 bytes each, D500-DAFF). Slot 0 is the
// player, slot 1 the second player.
//
// The fields keep the offsets of the original's 64-byte records in their
// comments. Most have the same meaning for every object; `counter`, `timer`
// and `step` belong to the object types, which name their use.
namespace triple_trouble_gg {

// Bits of Object::display (offset 4).
namespace display_flag {
inline constexpr u8 mirrored = 0x10;  // Faces left: sprite mirrored.
inline constexpr u8 flicker = 0x20;   // Blinks (hidden every other 2 frames).
inline constexpr u8 offscreen = 0x40; // Not drawn (outside the update window).
inline constexpr u8 hidden = 0x80;    // Not drawn.
} // namespace display_flag

// Where an object is in its script (scripts.hpp): step `step` of the script
// started for (`type`, `state`). A type of 0: no script (yet).
struct ScriptPosition {
    u8 type{}, state{};
    std::uint16_t step{};
    u8 bank{}; // the bank of its scripts when not the type's own (Tails as the partner: 16)

    bool running() const { return type != 0; }
};

struct Object {
    u8 type{};       // 00: 0 free; 1 Sonic
    u8 state{};      // 01: current script
    u8 next_state{}; // 02: requested script
    u8 flags{}; // 03: type-specific flag bits (bit 3: state changes wait for the script's restart)
    u8 display{};                   // 04: display_flag bits
    u8 sprite_count{};              // 05: hardware sprites of the current frame
    u8 frame{};                     // 06: sprite frame number
    u8 frame_ticks{};               // 07: updates left on the current script step
    u8 tile_base{};                 // 08: added to frame tile numbers
    u8 tile_base_mirrored{};        // 09: the same when mirrored
    u8 angle{};                     // 0A
    u8 speed{};                     // 0B
    Routine routine{Routine::none}; // 0C: run on every update of the current script step
    ScriptPosition script;          // 0E: the position in the script
    u8 x_sub{};                     // 10: X fraction (1/256 pixel)
    std::uint16_t x{};              // 11: X in world pixels
    u8 y_sub{};                     // 13
    std::uint16_t y{};              // 14
    std::uint16_t vx{};             // 16: 1/256 pixel per update
    std::uint16_t vy{};             // 18
    std::uint16_t screen_x{};       // 1A: sprite position
    std::uint16_t screen_y{};       // 1C
    u8 counter{};                   // 1E
    u8 timer{};                     // 1F
    u8 touch{};                     // 20
    u8 touch_sides{};               // 21
    u8 terrain{};                   // 22: terrain contact bits
    u8 contact{};                   // 23: terrain and object contact bits combined (5A3B)
    u8 status{};                    // 24
    u8 path{};         // 25: nonzero: blocks with an alternative terrain record use it (6FA6)
    u8 status_ticks{}; // 26
    u8 floor_off{};    // 27: nonzero: no floor landing (5FCF)
    // 28-2D: the frame record (5AED): two bytes the collision code reads,
    // the address of the frame's sprite offsets (bank 0D) and the address of
    // its header (bank 0F: Y and X origin, tile list).
    u8 frame_width{};             // 28
    u8 frame_height{};            // 29
    std::uint16_t layout{};       // 2A
    std::uint16_t frame_header{}; // 2C
    u8 flicker_ticks{};           // 2E
    u8 anim_step{};               // 2F: the player's animation step (AC6E, ACF0)
    u8 surface{};                 // 30: platforms: the height of their top above the origin (5783)
    u8 slot_tag{};                // 31: set by 52CA (slot number + 1 of the spawner's ...)
    u8 power_up{};                // 32
    u8 loop_count{};              // 33: script loop counter (commands 14/15)
    // 34-39: bytes each type uses its own way (named where they are used).
    std::array<u8, 6> scratch{};
    std::uint16_t origin_x{}; // 3A: spawn position
    std::uint16_t origin_y{}; // 3C
    u8 placement{};           // 3E: 1-based index in the placement table, 0 if spawned
    u8 param{};               // 3F: spawn parameter
};

} // namespace triple_trouble_gg
