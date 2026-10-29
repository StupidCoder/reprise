#pragma once
#include "gba/display.hpp"
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/sprite.hpp"
#include "sonic_advance_gba/game/tile_copy.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <array>
#include <deque>
#include <vector>

// Background pictures: a tile set, a colour set and a tile map from the
// cartridge's background table (084A6C40), drawn into a background layer's
// map (the original's 0x40-byte background record).
//
// Loading (080019C0) queues the tiles for copying and the map for drawing;
// both happen in the next vertical blank (workers 0800116C and 08001AA4).
// The colours go into the background colour buffer at once.
namespace sonic_advance_gba {

struct VideoState;
class BackgroundQueues;

struct Background {
    // +00 the tile set's copy (source and size from the asset; the owner
    // sets the destination, +04, before loading).
    TileCopy tiles;
    u32 map_to{};               // +0C video memory offset of the layer's map
    u16 asset{};                // +1C entry in the background table
    u16 source_x{}, source_y{}; // +1E, +20 the part of the map drawn (in tiles)
    u16 x{}, y{};               // +22, +24 where it goes in the layer's map
    u16 width{}, height{};      // +26, +28
    u8 layer{};                 // +2E bits 0-1
    bool scrolling{};           // +2E bit 5: drawn as a window into a larger map
    bool tiled_map{};           // +2E bit 6: the map is built from a layout of map blocks
    bool flip_y{};              // +2E bit 7
    bool flip_x{};              // +2E bit 8
    bool keep_colors{};         // +2E bit 4 preset: the colours are not loaded
    bool flags_bit2{}; // +2E bit 2: set by the title screens; not read by the routines recovered so
                       // far
    u16 scroll_x{}, scroll_y{};           // +30, +32 (scrolling maps)
    u16 last_scroll_x{}, last_scroll_y{}; // +34, +36 the position last drawn
    u8 tile_frame{};                      // +2B animated tiles: the frame shown
    u8 tile_timer{};                      // +2C animated tiles: frames into it
    bool tiles_relative{}; // +2E bit 9: animated tiles taken relative to the destination

    // Taken from the asset by load_background.
    u32 map_from{};                      // +10 cartridge address of the map (16-bit entries)
    u16 map_width{}, map_height{};       // +14, +16 (in tiles)
    u8 palette{};                        // +2A the first colour the asset's colours go to
    u32 layout{};                        // +38 tiled maps: the layout of map blocks
    u16 layout_width{}, layout_height{}; // +3C, +3E
    bool tiles_queued{};                 // +2E bit 3: the tile copy was queued once

    Background() = default;
    // The record as a cartridge template holds it (0x40 bytes at `at`).
    void set_from_template(const Assets& a, u32 at);
    Background(const Background&) = delete;
    Background& operator=(const Background&) = delete;
    ~Background();

private:
    friend class BackgroundQueues;
    BackgroundQueues* queues_{};
};

// A request to clear part of a layer's map (03001B40, four bytes per layer):
// columns left-right and rows top-bottom inclusive; a right of FF clears
// whole rows from top up to (not including) bottom. Pending while left !=
// right or top != bottom.
struct MapClear {
    u8 left{}, top{}, right{}, bottom{};
};

// The vertical blank's work lists: tile copies (03002040, 32 entries), map
// clears and map draws (030010C0, 16 entries).
class BackgroundQueues {
public:
    TileCopyQueue copies;
    std::array<MapClear, 4> clears{};
    std::array<u8, 4> clear_value{}; // 03004620: the map entry (byte) a clear writes

    void queue_draw(Background& b);

    // The screen changes: queued draws and copies are dropped (03002384 =
    // 0300211C, 030045F8 = 03002324; the sprite-to-map count 03004C30 = 0).
    void drop_pending() {
        draws_.clear();
        copies.clear();
        sprite_maps_.clear();
    }

    void forget(Background& b);

    // 08002888 (its list part): the sprite's current frame is written into
    // the map of its layer (sprite flag bits 15-16) in the vertical blank,
    // its entries as runs of tile numbers. The sprite is taken as it is now
    // (the original keeps a pointer; nothing changes it before the vertical
    // blank in the screens that use this).
    void queue_sprite_map(const Sprite& s) {
        sprite_maps_.push_back({s.frame, s.flags, s.x, s.y, s.animation, s.palette, s.tiles.to});
    }

    // 0800116C, 08002F84, 08002958, then 08001AA4. The map worker sets the
    // fine scroll of scrolling maps in the next frame's settings.
    void run(const Assets& assets, gba::Settings& settings, gba::Display& d);

private:
    void draw_sprite_maps(const Assets& assets, const gba::Settings& settings, gba::Display& d);

    std::deque<Background*> draws_;

    // What the worker reads of a queued sprite.
    struct SpriteMap {
        u32 frame, flags;
        s16 x, y;
        u16 animation;
        u8 palette;
        u32 tiles_to;
    };

    void draw_rotated_sprite_map(const Assets& assets, const SpriteMap& s,
                                 const gba::Background& bg, u32 tile_base, u32 map_base,
                                 unsigned shift, gba::Display& d);
    static void put_map_halfword(gba::Display& d, s32 at, u16 value);
    u32 rotated_map_byte_{}; // the stack word 08002958 keeps the even entry in (its first value
                             // unknown)
    std::vector<SpriteMap> sprite_maps_; // 030045B0 (count 03004C30)
};

// 080019C0: takes the tile set, colours and map of `b.asset`, queues the
// tile copy (once) and the map drawing, and loads the colours (unless
// keep_colors; once).
void load_background(const Assets& assets, VideoState& video, BackgroundQueues& queues,
                     Background& b);
// 080025B0: animated tile sets (frame count and time in the asset) copy
// their next frame over the first tiles when it is due.
void animate_background_tiles(const Assets& assets, BackgroundQueues& queues, Background& b);

} // namespace sonic_advance_gba
