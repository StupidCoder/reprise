#include "sonic_advance_gba/game/background.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/tables.hpp"
#include "sonic_advance_gba/game/video.hpp"
#include <algorithm>

namespace sonic_advance_gba {

Background::~Background() {
    if (queues_) {
        queues_->forget(*this);
    }
}

void Background::set_from_template(const Assets& a, u32 at) {
    tiles.from = a.u32(at + 0x00);
    tiles.to = a.u32(at + 0x04) - 0x06000000;
    tiles.bytes = a.u16(at + 0x08);
    map_to = a.u32(at + 0x0C) - 0x06000000;
    map_from = a.u32(at + 0x10);
    map_width = a.u16(at + 0x14);
    map_height = a.u16(at + 0x16);
    asset = a.u16(at + 0x1C);
    source_x = a.u16(at + 0x1E);
    source_y = a.u16(at + 0x20);
    x = a.u16(at + 0x22);
    y = a.u16(at + 0x24);
    width = a.u16(at + 0x26);
    height = a.u16(at + 0x28);
    palette = a.u8(at + 0x2A);
    tile_frame = a.u8(at + 0x2B);
    tile_timer = a.u8(at + 0x2C);
    const u16 f = a.u16(at + 0x2E);
    layer = u8(f & 3);
    flags_bit2 = f & 0x4;
    tiles_queued = f & 0x8;
    keep_colors = f & 0x10;
    scrolling = f & 0x20;
    tiled_map = f & 0x40;
    flip_y = f & 0x80;
    flip_x = f & 0x100;
    tiles_relative = f & 0x200;
    if (f & 0xFC00) {
        throw Unrecovered("background template flags beyond bit 9");
    }
    scroll_x = a.u16(at + 0x30);
    scroll_y = a.u16(at + 0x32);
    last_scroll_x = a.u16(at + 0x34);
    last_scroll_y = a.u16(at + 0x36);
    layout = a.u32(at + 0x38);
    layout_width = a.u16(at + 0x3C);
    layout_height = a.u16(at + 0x3E);
}

void BackgroundQueues::queue_draw(Background& b) {
    if (draws_.size() >= 16) {
        throw Unrecovered("background draw queue overflow (16 entries)");
    }
    draws_.push_back(&b);
    b.queues_ = this;
}

void BackgroundQueues::forget(Background& b) {
    // The original would copy from the freed record; nothing is drawn here.
    std::erase(draws_, &b);
}

void load_background(const Assets& assets, VideoState& video, BackgroundQueues& queues,
                     Background& b) {
    const u32 a = assets.u32(table::backgrounds + 4u * b.asset);
    b.map_width = assets.u16(a + 0);
    b.map_height = assets.u16(a + 2);
    b.tiles.from = assets.u32(a + 8);
    b.tiles.bytes = assets.u16(a + 12);
    if (!b.tiles_queued) {
        queues.copies.push(b.tiles);
        b.tiles_queued = true;
    }
    const u32 colors_from = assets.u32(a + 16);
    b.palette = assets.u8(a + 20);
    const u16 count = assets.u16(a + 22);
    if (!b.keep_colors) {
        if (b.palette + count > 256) {
            throw Unrecovered("background colours beyond the background palette");
        }
        for (unsigned i = 0; i < count; ++i) {
            video.bg_colors[b.palette + i] = assets.u16(colors_from + 2 * i);
        }
        video.bg_colors_changed = true;
        b.keep_colors = true; // bit 4 is set once the colours are in
    }
    b.map_from = assets.u32(a + 24);
    if (b.tiled_map) {
        b.layout = assets.u32(a + 28);
        b.layout_width = assets.u16(a + 32);
        b.layout_height = assets.u16(a + 34);
    }
    queues.queue_draw(b);
}

void animate_background_tiles(const Assets& assets, BackgroundQueues& queues, Background& b) {
    const u32 a = assets.u32(table::backgrounds + 4u * b.asset);
    const u8 frames = assets.u8(a + 6);
    if (frames == 0) {
        return;
    }
    if (++b.tile_timer < assets.u8(a + 7)) {
        return;
    }
    b.tile_timer = 0;
    if (++b.tile_frame >= frames) {
        b.tile_frame = 0;
    }
    const u16 frame_bytes = assets.u16(a + 4);
    if (b.tiles_relative) {
        throw Unrecovered("animated tiles relative to their destination (080025B0)");
    }
    // Frame 0 is the start of the tile set; the others follow the whole set.
    b.tiles.from = b.tile_frame == 0
                       ? assets.u32(a + 8)
                       : assets.u32(a + 8) + assets.u32(a + 12) + frame_bytes * (b.tile_frame - 1u);
    b.tiles.bytes = frame_bytes;
    queues.copies.push(b.tiles);
}

namespace {

// The map drawn from a layout of blocks (tiled maps, +2E bit 6): the
// window of width x height entries whose top left is map entry (ox, oy) of
// the whole layout, at the start of the layer's map. `e`: bytes per entry,
// `row`: bytes per map row.
void draw_tiled(const Assets& assets, const Background& b, u32 ox, u32 oy, u32 e, u32 row,
                gba::Display& d) {
    const u32 bw = b.map_width, bh = b.map_height;
    for (u32 col = 0; col < b.width;) {
        const u32 sx = ox + col;
        const u32 bcol = u32(divide(s32(sx), s32(bw)));
        const u32 in_x = sx - bw * bcol;
        const u32 chunk_w = std::min<u32>(b.width - col, bw - in_x);
        u32 rows_left = b.height;
        for (u32 r = 0; r < b.height;) {
            const u32 sy = oy + r;
            const u32 brow = u32(divide(s32(sy), s32(bh)));
            const u32 in_y = sy - bh * brow;
            const u32 chunk_h = bh - in_y;
            const u32 block = assets.u8(b.layout + brow * b.layout_width + bcol);
            u32 from = b.map_from + e * (bh * bw * block + bw * in_y + in_x);
            u32 to = b.map_to + b.y + row * r + b.x + e * col;
            r += chunk_h;
            const u32 n = std::min(chunk_h, rows_left);
            rows_left -= n;
            for (u32 k = 0; k < n; ++k, to += row, from += e * bw) {
                for (u32 i = 0; i < (e * chunk_w) / 2; ++i) {
                    if (to + 2 * i + 1 >= 0x10000) {
                        throw Unrecovered("background map beyond background memory");
                    }
                    d.put_halfword((to & ~1u) + 2 * i, assets.u16((from & ~1u) + 2 * i));
                }
            }
        }
        col += chunk_w;
    }
}

// 08001AA4: draws a background's map part into its layer's map.
void draw_map(const Assets& assets, gba::Settings& s, Background& b, gba::Display& d) {
    if (b.scrolling || b.tiled_map) {
        const unsigned layer = b.layer & 3;
        const bool text = layer < 2 || (s.mode & 3) == 0;
        const u32 e = text ? 2 : 1;
        const u32 row = e * (text ? 0x20 : (16u << s.bg[layer].size));
        if (!b.scrolling) {
            draw_tiled(assets, b, b.source_x, b.source_y, e, row, d);
            return;
        }
        if (!b.tiled_map) {
            throw Unrecovered("scrolling background maps without a layout (08001AA4)");
        }
        // The fine scroll goes to the next frame's settings; the window is
        // redrawn when the scroll crosses a tile.
        s.bg[layer].scroll_x = u16(b.scroll_x & 7);
        s.bg[layer].scroll_y = u16(b.scroll_y & 7);
        if ((b.last_scroll_x >> 3) != (b.scroll_x >> 3) ||
            (b.last_scroll_y >> 3) != (b.scroll_y >> 3)) {
            draw_tiled(assets, b, u16((b.scroll_x >> 3) + b.source_x),
                       u16((b.scroll_y >> 3) + b.source_y), e, row, d);
        }
        return;
    }
    const unsigned layer = b.layer & 3;
    const unsigned size = s.bg[layer].size;
    // Text layers: 32 two-byte entries per map row; rotated layers (2, 3 in
    // modes 1 and 2): 16 << size one-byte entries.
    const bool text = layer < 2 || (s.mode & 3) == 0;
    if (!text) {
        // Rotated layers: one byte per entry, rows of 16 << size entries,
        // from a byte map. The copy moves halfwords, so an odd width leaves
        // its last entry out.
        if (b.flip_x || b.flip_y) {
            throw Unrecovered("mirrored background map drawn into a rotated layer");
        }
        const u32 row = 16u << size;
        u32 to = b.map_to + row * b.y + b.x;
        u32 from = b.map_from + u32(b.map_width) * b.source_y + b.source_x;
        for (unsigned r = 0; r < b.height; ++r, to += row, from += b.map_width) {
            for (unsigned k = 0; k < unsigned(b.width >> 1); ++k) {
                const u32 at = (to & ~1u) + 2 * k;
                if (at + 1 >= 0x10000) {
                    throw Unrecovered("background map beyond background memory");
                }
                d.put_halfword(at, assets.u16((from & ~1u) + 2 * k));
            }
        }
        return;
    }
    const u32 row_bytes = 64;
    const bool wide = size == 1 || size == 3;
    u32 to = b.map_to + row_bytes * b.y + 2u * b.x;
    const auto put = [&](u32 at, u16 v) {
        if (at + 1 >= 0x10000) {
            throw Unrecovered("background map beyond background memory");
        }
        d.put_halfword(at, v);
    };
    const auto entry = [&](unsigned col, unsigned row) {
        return assets.u16(b.map_from + 2u * (u32(b.map_width) * row + col));
    };
    if (!b.flip_x && !b.flip_y) {
        // A part crossing column 32 of a 512-pixel wide layer continues in
        // the next 2 KiB map block, at the same column it started in (the
        // second copy keeps the destination's column offset).
        const int first = 0x20 - b.x;
        const int rest = int(b.width) + b.x - 0x20;
        const bool split = wide && first >= 1 && rest >= 1;
        for (unsigned row = 0; row < b.height; ++row, to += row_bytes) {
            const unsigned sr = b.source_y + row;
            if (!split) {
                for (unsigned c = 0; c < b.width; ++c) {
                    put(to + 2 * c, entry(b.source_x + c, sr));
                }
            } else {
                for (int c = 0; c < first; ++c) {
                    put(to + 2u * c, entry(b.source_x + c, sr));
                }
                for (int c = 0; c < rest; ++c) {
                    put(to + 0x800 + 2u * c, entry(b.source_x + first + c, sr));
                }
            }
        }
        return;
    }
    // Mirrored parts are copied entry by entry with the flip bits toggled.
    const u16 toggle = u16((b.flip_x ? 0x400 : 0) | (b.flip_y ? 0x800 : 0));
    for (unsigned row = 0; row < b.height; ++row, to += row_bytes) {
        const unsigned sr = b.flip_y ? b.source_y + b.height - 1 - row : b.source_y + row;
        for (unsigned c = 0; c < b.width; ++c) {
            const unsigned sc = b.flip_x ? b.source_x + b.width - 1 - c : b.source_x + c;
            put(to + 2 * c, u16(entry(sc, sr) ^ toggle));
        }
    }
}

// 08002F84: a pending map clear of layer `layer`.
void clear_map(const gba::Settings& s, unsigned layer, MapClear& c, u8 value, gba::Display& d) {
    if (c.top == c.bottom && c.left == c.right) {
        return;
    }
    const auto& bg = s.bg[layer];
    u32 map = u32(bg.map_block) * 0x800;
    const bool text = layer < 2 || (s.mode & 3) == 0;
    const auto fill16 = [&](u32 at, u32 count, u16 v) {
        for (u32 i = 0; i < count; ++i) {
            if (at + 2 * i + 1 >= 0x10000) {
                throw Unrecovered("map clear beyond background memory");
            }
            d.put_halfword(at + 2 * i, v);
        }
    };
    if (text) {
        map += c.left * 2u;
        // Entries per map row: 32, or 64 for the tall layer sizes 2 and 3.
        const u32 row = (bg.size == 2 || bg.size == 3) ? 0x40 : 0x20;
        if (c.right == 0xFF) {
            fill16(map + c.top * row * 2, row * u32(c.bottom - c.top), value);
        } else {
            for (unsigned r = c.top; r <= c.bottom; r = (r + 1) & 0xFF) {
                fill16(map + row * 2 * r, u32(c.right - c.left + 1), value);
            }
        }
    } else {
        // Rotated layers (one byte per entry): here FF in the bottom value
        // selects whole rows.
        map += c.left;
        const u32 row = 16u << bg.size;
        const u16 v = u16(value | (value << 8));
        if (c.bottom == 0xFF) {
            fill16(map + row * c.top, (row * u32(c.bottom - c.top)) / 2, v);
        } else {
            for (unsigned r = c.top; r <= c.bottom; r = (r + 1) & 0xFF) {
                const int n = c.right - c.left + 1;
                fill16(map + row * r, u32((n / 2 - (n >> 31)) >> 1), v);
            }
        }
    }
    c = {};
}

} // namespace

void BackgroundQueues::run(const Assets& assets, gba::Settings& settings, gba::Display& d) {
    copies.run(assets, d);
    for (unsigned layer = 0; layer < 4; ++layer) {
        clear_map(settings, layer, clears[layer], clear_value[layer], d);
    }
    draw_sprite_maps(assets, settings, d);
    while (!draws_.empty()) {
        Background* b = draws_.front();
        draws_.pop_front();
        if (b->scrolling && b->scroll_x == b->last_scroll_x && b->scroll_y == b->last_scroll_y) {
            continue; // nothing new to show
        }
        draw_map(assets, settings, *b, d);
        // 08002570: the position drawn.
        b->last_scroll_x = b->scroll_x;
        b->last_scroll_y = b->scroll_y;
    }
}

// 08002958's part for rotated layers: one byte per map entry (no flips),
// rows of 16 << size entries. Video memory takes halfwords only: an entry
// at an odd place is written together with the one before it (the byte
// kept from the last even place, which carries over from one row and one
// sprite to the next); a row ending on an even place writes that entry
// with a zero after it (the original reads the halfword back from the odd
// address, which the hardware returns rotated, and keeps its upper byte:
// that is 0).
void BackgroundQueues::draw_rotated_sprite_map(const Assets& assets, const SpriteMap& s,
                                               const gba::Background& bg, u32 tile_base,
                                               u32 map_base, unsigned shift, gba::Display& d) {
    static constexpr u8 sizes[12][2] = {
        {8, 8},   {16, 16}, {32, 32}, {64, 64}, {16, 8},  {32, 8},
        {32, 16}, {64, 32}, {8, 16},  {8, 32},  {16, 32}, {32, 64},
    };
    const u32 f = s.frame;
    const u8 first = assets.u8(f + 1);
    const u16 count = assets.u16(f + 2);
    const s16 offset_x = assets.s16(f + 8), offset_y = assets.s16(f + 10);
    const u32 list = assets.u32(assets.u32(table::animations + 8) + 4u * s.animation) + first * 6u;
    const s32 row_bytes = 0x10 << bg.size;
    const s32 left = (s.x - offset_x) & ~0xF;
    for (unsigned i = 0; i < count; ++i) {
        const u32 e = list + i * 6;
        const u16 a0 = assets.u16(e), a1 = assets.u16(e + 2), a2 = assets.u16(e + 4);
        const u8* size = sizes[((a0 & 0xC000) >> 12) | (a1 >> 14)];
        s32 row = s32(map_base) + row_bytes * ((s.y - offset_y + (a0 & 0xFF)) >> 3);
        u32 tile = (((s.tiles_to - tile_base) >> shift) + (a2 & 0x3FF)) & 0xFF;
        for (unsigned r = 0; r < size[1] / 8u; ++r, row += row_bytes) {
            s32 at = row + (((a1 & 0x1FF) + left) >> 3);
            for (unsigned c = 0; c < size[0] / 8u; ++c, ++at, tile = (tile + 1) & 0xFFFF) {
                if (at & 1) {
                    rotated_map_byte_ = (rotated_map_byte_ | (tile << 8)) & 0xFFFF;
                    put_map_halfword(d, at & ~1, u16(rotated_map_byte_));
                } else {
                    rotated_map_byte_ = tile;
                }
            }
            if (at & 1) {
                put_map_halfword(d, at & ~1, u16(rotated_map_byte_));
            }
        }
    }
}

// 08002958: the queued sprites' frames written into their layers' maps.
// Each sprite table entry of the frame becomes a block of map entries
// numbering its tiles in a row, at the entry's place relative to the
// sprite's position; positions are taken in steps of 16 pixels across and 8
// down (08002888 sets the layer's fine scroll for the rest).
void BackgroundQueues::draw_sprite_maps(const Assets& assets, const gba::Settings& settings,
                                        gba::Display& d) {
    // Entry sizes in pixels by shape and size (hardware).
    static constexpr u8 sizes[12][2] = {
        {8, 8},   {16, 16}, {32, 32}, {64, 64}, {16, 8},  {32, 8},
        {32, 16}, {64, 32}, {8, 16},  {8, 32},  {16, 32}, {32, 64},
    };
    for (const SpriteMap& s : sprite_maps_) {
        const u32 f = s.frame;
        if (f == 0xFFFFFFFF) {
            continue;
        }
        const unsigned layer = (s.flags & 0x18000) >> 15;
        const gba::Background& bg = settings.bg[layer];
        const u32 tile_base = bg.tile_block * 0x4000u, map_base = bg.map_block * 0x800u;
        const unsigned shift = bg.colors_256 ? 6 : 5;
        if (layer >= 2 && (settings.mode & 3) != 0) {
            draw_rotated_sprite_map(assets, s, bg, tile_base, map_base, shift, d);
            continue;
        }
        // Map entries per row (the original takes 64 for the two tall sizes).
        const s32 row_entries = (bg.size == 2 || bg.size == 3) ? 0x40 : 0x20;
        if (row_entries != 0x20) {
            throw Unrecovered("a sprite frame written into a 64-entry-wide map (08002958)");
        }
        const u8 frame_flags = assets.u8(f);
        const u8 first = assets.u8(f + 1);
        const u16 count = assets.u16(f + 2);
        const u16 width = assets.u16(f + 4), height = assets.u16(f + 6);
        const s16 offset_x = assets.s16(f + 8), offset_y = assets.s16(f + 10);
        const u32 list =
            assets.u32(assets.u32(table::animations + 8) + 4u * s.animation) + first * 6u;
        for (unsigned i = 0; i < count; ++i) {
            const u32 e = list + i * 6;
            const u16 a0 = assets.u16(e);
            u16 a1 = assets.u16(e + 2);
            const u16 a2 = u16(assets.u16(e + 4) + s.palette * 0x1000);
            const u8* size = sizes[((a0 & 0xC000) >> 12) | (a1 >> 14)];
            u16 ex = a1 & 0x1FF;
            s16 ey = s16(a0 & 0xFF);
            s32 top, left;
            if ((((frame_flags >> 1) ^ (s.flags >> 11)) & 1) == 0) {
                top = s.y - offset_y;
            } else {
                a1 ^= 0x2000; // drawn upside down
                // (The original picks the frame's offset by its bit 0 here.)
                top = s.y + ((frame_flags & 1) == 0 ? height - offset_y : offset_y) - 8;
                ey = s16(-ey);
            }
            if (((s.flags >> 10) & 1) == (frame_flags & 1u)) {
                left = s.x - offset_x;
            } else {
                a1 ^= 0x1000; // mirrored
                left = s.x + ((frame_flags & 1) == 0 ? width - offset_x : offset_x) - 8;
                ex = u16(-ex);
            }
            const s32 column_step = (a1 & 0x1000) ? -2 : 2;
            const s32 row_step = (a1 & 0x2000) ? -2 * row_entries : 2 * row_entries;
            s32 row = s32(map_base) + row_entries * (s32(u32(u32(top) & ~7u) + u32(s32(ey))) >> 2);
            u16 tile =
                u16(((s.tiles_to - tile_base) >> shift) + (a2 & 0xF3FF) + ((a1 & 0x3000) >> 2));
            for (unsigned r = 0; r < size[1] / 8u; ++r, row += row_step) {
                s32 at = row + (s32(u32(s32(s16(ex))) + (u32(left) & ~0xFu)) >> 2);
                for (unsigned c = 0; c < size[0] / 8u; ++c, at += column_step) {
                    if (at < 0 || at + 1 >= 0x10000) {
                        throw Unrecovered(
                            "a sprite frame written beyond background memory (08002958)");
                    }
                    d.put_halfword(u32(at), tile++);
                }
            }
        }
    }
    sprite_maps_.clear();
}

void BackgroundQueues::put_map_halfword(gba::Display& d, s32 at, u16 value) {
    if (at < 0 || at + 1 >= 0x10000) {
        throw Unrecovered("a sprite frame written beyond background memory (08002958)");
    }
    d.put_halfword(u32(at), value);
}

} // namespace sonic_advance_gba
