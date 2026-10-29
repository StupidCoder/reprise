#pragma once
#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/object_types.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The basic moving things of an act: Sonic and the objects share Body, the
// position/velocity/collision-box record the original code uses for both.
namespace sonic1_gg {

// The controller state the game reads each update. Button 1 and 2 both jump.
// Positional initialisation ({left, right, jump, down, up}) is used in places.
struct Input {
    bool left{}, right{}, jump{}, down{}, up{};
};

// Bits of Body::flags. For Sonic this is RAM byte D415; objects use the same
// layout for the bits the shared movement code reads (0x02, 0x20-0x80), while
// bits 0x01-0x10 of an object are private to its update routine (most use 0x01
// for "initialised" and 0x02 for "facing left").
namespace body_flag {
inline constexpr int rolling = 0x01;       // Sonic: curled up on the ground.
inline constexpr int facing_left = 0x02;   // Sprite is mirrored.
inline constexpr int jumping = 0x04;       // Sonic: curled up in the air.
inline constexpr int jump_released = 0x08; // Sonic: jump button released since landing.
inline constexpr int underwater = 0x10;    // Sonic: water physics apply.
inline constexpr int no_terrain = 0x20;    // Skip terrain collision after moving.
inline constexpr int wall_contact = 0x40;  // Last move hit a wall.
inline constexpr int grounded = 0x80;      // Last move landed on a floor.
// Curled either way: the state in which Sonic damages enemies.
inline constexpr int curled = rolling | jumping;
} // namespace body_flag

// Anything that moves through the level: Sonic and every object.
//
// Positions are 24-bit fixed point in 1/256 pixel (see arithmetic.hpp). (x, y)
// is the top-left corner of the collision box. The RAM addresses are Sonic's.
struct Body {
    int x{};             // D3FE (24-bit)
    int y{};             // D401 (24-bit)
    int vx{};            // D404, 1/256 pixel per update
    int vy{};            // D407, positive is down
    int width{16};       // D40A, collision box in pixels
    int height{32};      // D40B
    int flags{8};        // D415, see body_flag
    int contact_class{}; // D416, attribute of the last terrain block touched

    // Whole-pixel position (the fraction byte is dropped).
    int pixel_x() const { return pixel(x); }

    int pixel_y() const { return pixel(y); }

    // Sets the whole-pixel position, keeping the 1/256 fraction.
    void set_pixel_x(int whole) { set_pixel(x, whole); }

    void set_pixel_y(int whole) { set_pixel(y, whole); }

    bool grounded() const { return (flags & body_flag::grounded) != 0; }

    bool facing_left() const { return (flags & body_flag::facing_left) != 0; }
};

// One of the 32 object slots (slot 0 is unused; Sonic is separate).
struct Object : Body {
    int type{object_type::none};
    int mapping{}; // ROM address of the sprite layout to draw (0 = invisible)

    // The seven type-specific state bytes at object offsets 17..23. Their
    // meaning differs per type; each handler names the ones it uses. Bytes 5
    // and 6 are often an animation timer and position (Game::animate_object),
    // and pairs of bytes are read as little-endian words (state_word).
    std::array<std::uint8_t, 7> state{};

    bool active{}; // Within its activation distance of the camera this scan.

    // Widescreen enhancement only (widescreen.cpp): drawn in the larger view
    // while its original activation is still pending. Never set in the original
    // format; the paired bytes hold its idle animation counters.
    bool staged{};
    std::array<std::uint8_t, 2> staged_animation{};

    // Engine flame of a boss craft, submitted before the object moves (7A36).
    int flame_tile{-1}, flame_x{}, flame_y{};
    // Seesaw pose (plank tilt, weight offset) captured before contact changes it.
    std::array<int, 2> bridge_render{};
    // Extra hardware sprites the routine submits directly: tile, world X, world Y.
    std::vector<std::array<int, 3>> extra_sprites;
    // Background tile frames of the two flower animation phases.
    std::array<int, 2> flower_frames{0, 1};

    // Flag 0x01 of most objects marks that their first-update set-up has run.
    // Returns true, once, on that first update.
    bool first_update() {
        if (flags & 0x01) {
            return false;
        }
        flags |= 0x01;
        return true;
    }

    // Little-endian 16-bit value stored in two consecutive state bytes.
    int state_word(int offset) const { return state.at(offset) | (state.at(offset + 1) << 8); }

    void set_state_word(int offset, int value) {
        state.at(offset) = value;
        state.at(offset + 1) = value >> 8;
    }
};

// The camera is the top-left corner of the 256x192 area the VDP renders; the
// Game Gear LCD shows the 160x144 window at offset (48, 24) inside it.
struct Camera {
    int x{}, y{}; // D254 / D257, in pixels

    // Dead zone: Sonic may move freely while his position relative to the camera
    // stays within [left_gap, right_gap] x [top_gap, bottom_gap]. The horizontal
    // gaps shift with Sonic's facing so that he sees further ahead.
    int left_gap{80}, right_gap{88}; // D259 / D25B
    int top_gap{64}, bottom_gap{96}; // D25D / D25F
    int vertical_bias{};             // D2B8: looking up (+) or down (-)
};

} // namespace sonic1_gg
