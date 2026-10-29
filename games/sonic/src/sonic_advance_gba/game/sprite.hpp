#pragma once
#include "sonic_advance_gba/game/tile_copy.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <array>

// Animated sprites (the original's 0x30-byte sprite record).
//
// A sprite shows one animation from the cartridge's animation tables
// (03002034 = 080D868C): for each animation a list of variants, each a
// script of 32-bit words. A non-negative word is a frame's duration (in
// frames, 8.8 fixed point with the sprite's speed subtracted each frame)
// followed by the frame's number; negative words are commands (load tiles,
// load colours, loop, end, play a sound, set a hitbox, move, change
// animation, ...). A frame record (12 bytes) names its sprite table entries
// (6-byte attribute lists), its size and its offset from the sprite's
// position. 080034CC runs the script, 08004158 queues the frame's entries
// for the sprite table.
namespace sonic_advance_gba {

class Game;

// Bits of Sprite::flags (+10).
namespace sprite_flag {
inline constexpr u32 rotation_set = 0x1F; // bits 0-4: the rotation/scaling set when rotated
inline constexpr u32 rotated = 0x20;
inline constexpr u32 double_size = 0x40;
inline constexpr u32 mode = 0x180; // bits 7-8: entries' mode (semi-transparent, window)
inline constexpr u32 flip_x = 0x400;
inline constexpr u32 flip_y = 0x800;
inline constexpr u32 priority = 0x3000; // bits 12-13: the entries' priority against backgrounds
inline constexpr u32 ended = 0x4000;    // the script reached its end command
inline constexpr u32 view_relative = 0x20000; // drawn relative to 030010B4/B6
inline constexpr u32 keep_colors = 0x40000;   // colour commands do nothing
inline constexpr u32 keep_tiles = 0x80000;    // tile commands do nothing
} // namespace sprite_flag

// A hitbox set by an animation command (+28, +30, ...): `index` FFFFFFFF
// when off. The usual sprite record has room for the first; the player's
// sprites carry a second one.
struct Hitbox {
    u32 index = 0xFFFFFFFF;
    s8 left{}, top{}, right{}, bottom{};
};

struct Sprite {
    TileCopy tiles;             // +00 the frame's tiles: tiles.to is where they go (+04)
    u16 animation{};            // +0A
    u32 frame = 0xFFFFFFFF;     // +0C the frame record's cartridge address (FFFFFFFF: none)
    u32 flags{};                // +10 sprite_flag bits
    u16 position{};             // +14 the script position (in words)
    s16 x{}, y{};               // +16, +18
    u16 layer{};                // +1A bits 6-10: the sprite table layer (0 in front)
    s16 timer{};                // +1C frame time left (8.8)
    u16 previous_animation{};   // +1E
    u8 variant{};               // +20
    u8 previous_variant = 0xFF; // +21
    u8 speed = 0x10;            // +22 in sixteenths
    u8 first_entry{};           // +23 the first sprite table entry queued (set by draw)
    u8 entries{};               // +24 the frame's entry count
    u8 palette{};               // +25 the 16-colour palette added to the entries
    // +28, +30; animation scripts may set a third (+38), which lands in
    // whatever follows the record (the Secret Base boss leaves room).
    std::array<Hitbox, 3> hitboxes{};

    // The usual set-up of the original's screens: animation, variant and
    // tile destination, position, layer bits (+1A) and flags; everything
    // else as after a reset (+08 0, script at its start, speed 16, no frame,
    // palette 0, no hitbox).
    void start(u32 tiles_to, u16 animation_, u8 variant_, s16 x_, s16 y_, u16 layer_, u32 flags_) {
        tiles.to = tiles_to;
        animation = animation_;
        variant = variant_;
        x = x_;
        y = y_;
        layer = layer_;
        tiles.bytes = 0;
        position = 0;
        timer = 0;
        previous_variant = 0xFF;
        speed = 0x10;
        palette = 0;
        hitboxes[0].index = 0xFFFFFFFF;
        flags = flags_;
    }
};

// A sprite drawn rotated and scaled, with its transform in front (0x0C
// bytes before the sprite record): angle 0-3FF, scale (8.8; negative
// mirrors) and the picture position the frame turns around.
struct TransformedSprite {
    u16 angle{};         // +00
    s16 scale_x = 0x100; // +02
    s16 scale_y = 0x100; // +04
    s16 x{}, y{};        // +06, +08
    Sprite sprite;       // +0C
};

// 080034CC: runs the animation script up to the next frame. Returns 0 once
// the script has ended (the end command), otherwise nonzero.
u32 animate(Game& g, Sprite& s);
// 08002658: the same for animations that cycle background colours: colour
// commands load background colours, priority and layer commands do nothing,
// hitboxes are not mirrored. An ended script returns 0 even when the
// animation was changed.
u32 animate_background(Game& g, Sprite& s);
// 08004158: queues the current frame's entries for the sprite table (when
// any part of it is on screen).
void draw(Game& g, Sprite& s);
// 08003D84: as transform(), for a sprite turned by `angle` and scaled at
// (x, y) of the game's scene transform: the scene's turn and scales apply
// to the position (times its depth, from its focus) and add to the sprite's
// own.
void transform_in_scene(Game& g, Sprite& s, u16 angle, s16 scale_x, s16 scale_y, s16 x, s16 y);
// 08003A2C: the same without the scene's depth factor.
void transform_in_view(Game& g, Sprite& s, u16 angle, s16 scale_x, s16 scale_y, s16 x, s16 y);
// 08002888: the current frame goes into the map of a background layer
// (sprite flag bits 15-16) in the next vertical blank instead of the sprite
// table; the layer's fine scroll is set for the frame's offset.
void draw_into_map(Game& g, Sprite& s);
// 080037D0: sets the sprite's rotation set (sprite flag bits 0-4) for the
// transform and places the current frame so that its centre turns around
// the transform's position.
void transform(Game& g, TransformedSprite& t);
// Only the rotation/scaling parameter set `set` of such a transform.
void transform_matrix(Game& g, unsigned set, u16 angle, s16 scale_x, s16 scale_y);
// The same with the transform given separately.
void transform(Game& g, Sprite& s, u16 angle, s16 scale_x, s16 scale_y, s16 x, s16 y);

} // namespace sonic_advance_gba
