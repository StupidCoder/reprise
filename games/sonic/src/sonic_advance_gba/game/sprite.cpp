#include "sonic_advance_gba/game/sprite.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/tables.hpp"
#include <algorithm>
#include <utility>

namespace sonic_advance_gba {

namespace {

// The animation tables (080D868C).
struct AnimationTables {
    const Assets& a;

    u32 scripts() const { return a.u32(table::animations + 0x00); }

    u32 frames() const { return a.u32(table::animations + 0x04); }

    u32 entries() const { return a.u32(table::animations + 0x08); }

    u32 colors() const { return a.u32(table::animations + 0x0C); }

    u32 tiles_4bpp() const { return a.u32(table::animations + 0x10); }

    u32 tiles_8bpp() const { return a.u32(table::animations + 0x14); }

    // The script of an animation's variant.
    u32 script(u16 animation, u8 variant) const {
        return a.u32(a.u32(scripts() + 4u * animation) + 4u * variant);
    }
};

// Sprite sizes by shape and size (table 0809BE28: width, height in pixels).
constexpr u8 entry_size[12][2] = {
    {8, 8},   {16, 16}, {32, 32}, {64, 64}, {16, 8},  {32, 8},
    {32, 16}, {64, 32}, {8, 16},  {8, 32},  {16, 32}, {32, 64},
};

// What a command returns: continue with the next word, restart the script
// (a new animation was set), or stop this frame with a value.
enum class Next { go_on, restart, stop };

struct CommandResult {
    Next next;
    u32 value; // for stop
};

CommandResult run_command(Game& g, Sprite& s, u32 at, u32 op, bool bg) {
    const Assets& a = g.assets();
    const AnimationTables t{a};
    switch (op) {
    case 0: { // FFFFFFFF 080035F4: load tiles (index, count; negative index: 8 bits per pixel)
        s.position = u16(s.position + 3);
        if (!(s.flags & sprite_flag::keep_tiles)) {
            const s32 index = a.s32(at + 4);
            const u32 count = a.u32(at + 8);
            if (index < 0) {
                s.tiles.from = t.tiles_8bpp() + u32(index) * 0x40;
                s.tiles.bytes = u16(count << 6);
            } else {
                s.tiles.from = t.tiles_4bpp() + u32(index) * 0x20;
                s.tiles.bytes = u16(count << 5);
            }
            g.backgrounds().copies.push(s.tiles);
        }
        return {Next::go_on, 1};
    }
    case 1: { // FFFFFFFE 08004A84 (08003288): load colours (index, count, first colour)
        s.position = u16(s.position + 3);
        if (!(s.flags & sprite_flag::keep_colors)) {
            const u32 from = t.colors() + a.u32(at + 4) * 0x20;
            const unsigned count = a.u16(at + 8);
            const unsigned first = s.palette * 16u + a.u16(at + 10);
            // A 32-bit copy of count/2 words: count colours, rounded down to even.
            const unsigned n = (count >> 1) * 2;
            auto& colors = bg ? g.video().bg_colors : g.video().sprite_colors;
            // Beyond the palette the original copies into the memory after its
            // colour buffer (met with palette FEh: the third X-Zone boss's arm
            // while it blinks, written to 03003AA0, which nothing reads).
            const unsigned kept = first >= 256 ? 0 : std::min(n, 256 - first);
            if (kept != n && first < 256) {
                throw Unrecovered("animation colours partly beyond the palette");
            }
            for (unsigned i = 0; i < kept; ++i) {
                colors[first + i] = a.u16(from + 2 * i);
            }
            (bg ? g.video().bg_colors_changed : g.video().sprite_colors_changed) = true;
        }
        return {Next::go_on, 1};
    }
    case 2: // FFFFFFFD 08004AF0: jump back
        s.position = u16(s.position - a.u32(at + 4));
        return {Next::go_on, 1};
    case 3: // FFFFFFFC 08004AFC: end
        s.flags |= sprite_flag::ended;
        return {Next::stop, 0};
    case 4: // FFFFFFFB 08004B0C: play a sound
        s.position = u16(s.position + 2);
        g.sound().start(a.u16(at + 4));
        return {Next::go_on, 1};
    case 5: { // FFFFFFFA 08003660: set hitbox n (index bits 0-3)
        s.position = u16(s.position + 3);
        const u32 n = a.u32(at + 4) & 0xF;
        if (n >= s.hitboxes.size()) {
            throw Unrecovered("sprite hitbox beyond the third");
        }
        Hitbox& h = s.hitboxes[n];
        h.index = a.u32(at + 4);
        h.left = a.s8(at + 8);
        h.top = a.s8(at + 9);
        h.right = a.s8(at + 10);
        h.bottom = a.s8(at + 11);
        if (a.u32(at + 8) == 0) {
            h.index = 0xFFFFFFFF;
        } else if (bg) {
            // 080027E4 swaps the edges without negating them.
            if (s.flags & sprite_flag::flip_y) {
                std::swap(h.top, h.bottom);
            }
            if (s.flags & sprite_flag::flip_x) {
                std::swap(h.left, h.right);
            }
        } else {
            if (s.flags & sprite_flag::flip_y) {
                const s8 top = h.top;
                h.top = s8(-h.bottom);
                h.bottom = s8(-top);
            }
            if (s.flags & sprite_flag::flip_x) {
                const s8 left = h.left;
                h.left = s8(-h.right);
                h.right = s8(-left);
            }
        }
        return {Next::go_on, 1};
    }
    case 6: // FFFFFFF9 08004B20: move by (dx, dy)
        s.position = u16(s.position + 2);
        s.x = s16(s.x + a.s16(at + 4));
        s.y = s16(s.y + a.s16(at + 6));
        return {Next::go_on, 1};
    case 7: // FFFFFFF8 08004B3C: nothing (three words)
        s.position = u16(s.position + 3);
        return {Next::go_on, 1};
    case 8: // FFFFFFF7 08004B48: change animation and variant
        s.position = u16(s.position + 2);
        s.animation = a.u16(at + 4);
        s.previous_variant = 0xFF;
        s.variant = u8(a.u16(at + 6));
        return {Next::restart, 0};
    case 9: // FFFFFFF6 08004B68: skip four words and keep the frame (the
            // handler returns its argument pointer, which is never 1 or -1)
        s.position = u16(s.position + 4);
        return {Next::stop, at};
    case 10: // FFFFFFF5 08004B70: entries' priority
        s.position = u16(s.position + 2);
        if (bg) {
            return {Next::go_on, 1};
        }
        s.flags = (s.flags & ~sprite_flag::priority) | (a.u32(at + 4) << 12);
        return {Next::go_on, 1};
    case 11: // FFFFFFF4 08004B8C: sprite table layer
        s.position = u16(s.position + 2);
        if (bg) {
            return {Next::go_on, 1};
        }
        s.layer = u16(a.u32(at + 4) << 6);
        return {Next::go_on, 1};
    default:
        throw Unrecovered("animation command " + std::to_string(op));
    }
}

} // namespace

namespace {

u32 run_script(Game& g, Sprite& s, bool bg) {
    const Assets& a = g.assets();
    const AnimationTables t{a};
    u32 script = t.script(s.animation, s.variant);
    u32 at = script + 4u * s.position;
    u32 word = a.u32(at);
    while (s32(word) < 0) {
        const auto r = run_command(g, s, at, ~word, bg);
        if (r.next == Next::stop) {
            return r.value;
        }
        if (r.next == Next::restart) {
            script = t.script(s.animation, s.variant);
            s.position = 0;
        }
        at = script + 4u * s.position;
        word = a.u32(at);
    }
    // A frame: duration (low 16 bits used), then the frame number.
    s.timer = s16(s16(word) * 0x100 + s.timer);
    s.timer = s16(s.timer - s.speed * 0x10);
    const u32 frame = a.u32(at + 4);
    s.frame = frame == 0xFFFFFFFF ? 0xFFFFFFFF : a.u32(t.frames() + 4u * s.animation) + frame * 12;
    s.position = u16(s.position + 2);
    return 1;
}

void restart_if_changed(Sprite& s) {
    if (s.variant != s.previous_variant || s.animation != s.previous_animation) {
        s.tiles.bytes = 0;
        s.previous_variant = s.variant;
        s.previous_animation = s.animation;
        s.position = 0;
        s.timer = 0;
        s.flags &= ~sprite_flag::ended;
    }
}

} // namespace

u32 animate(Game& g, Sprite& s) {
    restart_if_changed(s);
    if (s.flags & sprite_flag::ended) {
        return 0;
    }
    if (s.timer > 0) {
        s.timer = s16(s.timer - s.speed * 0x10);
        return 1;
    }
    return run_script(g, s, false);
}

u32 animate_background(Game& g, Sprite& s) {
    if (s.flags & sprite_flag::ended) {
        return 0;
    }
    restart_if_changed(s);
    if (s.timer > 0) {
        s.timer = s16(s.timer - s.speed * 0x10);
        return 1;
    }
    return run_script(g, s, true);
}

void draw(Game& g, Sprite& s) {
    if (s.frame == 0xFFFFFFFF) {
        return;
    }
    const Assets& a = g.assets();
    const AnimationTables t{a};
    const u32 f = s.frame;
    const u8 frame_flags = a.u8(f + 0);
    const u8 first = a.u8(f + 1);
    const u16 count = a.u16(f + 2);
    s.entries = u8(count);
    int x = s.x, y = s.y;
    if (s.flags & sprite_flag::view_relative) {
        x -= g.view_offset_x;
        y -= g.view_offset_y;
    }
    int w = a.u16(f + 4), h = a.u16(f + 6);
    if (!(s.flags & sprite_flag::rotated)) {
        y -= (s.flags & sprite_flag::flip_y) ? h - a.s16(f + 10) : a.s16(f + 10);
        x -= (s.flags & sprite_flag::flip_x) ? w - a.s16(f + 8) : a.s16(f + 8);
    } else if (s.flags & sprite_flag::double_size) {
        x -= a.u16(f + 4) >> 1;
        y -= a.u16(f + 6) >> 1;
        w <<= 1;
        h <<= 1;
    }
    if (x + w < 0 || x > 240 || y + h < 0 || y > 160 || count == 0) {
        return;
    }
    const u32 list = a.u32(t.entries() + 4u * s.animation);
    auto& v = g.video();
    for (unsigned i = 0; i < count; ++i) {
        const u32 e = list + (first + i) * 6u;
        const u16 a0 = a.u16(e), a1 = a.u16(e + 2), a2 = a.u16(e + 4);
        u16 n0 = a0 & 0xFE00, n1 = a1 & 0xFE00;
        u16 n2 = u16(a2 + s.palette * 0x1000);
        unsigned ex = a1 & 0x1FF, ey = a0 & 0xFF;
        if (!(s.flags & sprite_flag::rotated)) {
            const unsigned shape_size = ((a0 & 0xC000) >> 12) | (a1 >> 14);
            if (((frame_flags >> 1) ^ (s.flags >> 11)) & 1) {
                n1 ^= 0x2000;
                ey = unsigned(h - entry_size[shape_size][1]) - ey;
            }
            if (((s.flags >> 10) & 1) != (frame_flags & 1u)) {
                n1 ^= 0x1000;
                ex = unsigned(w - entry_size[shape_size][0]) - ex;
            }
        } else {
            n0 = u16(n0 | 0x100);
            if (s.flags & sprite_flag::double_size) {
                n0 = u16(n0 | 0x300);
            }
            n1 = u16(n1 | ((s.flags & sprite_flag::rotation_set) << 9));
        }
        n0 = u16(n0 | ((s.flags & sprite_flag::mode) << 3));
        const u16 a2_before = n2;
        n2 = u16(n2 | ((s.flags & sprite_flag::priority) >> 2));
        n0 = u16(n0 + ((y + ey) & 0xFF));
        n1 = u16(n1 + ((x + ex) & 0x1FF));
        if (n0 & 0x2000) {
            // 256-colour entries count their tile number in 32-byte units.
            n2 = u16(n2 + (a2_before & 0x3FF));
        }
        n2 = u16(n2 + ((s.tiles.to - 0x10000) >> 5));
        gba::Sprite entry;
        entry.set_attributes(n0, n1, n2);
        if (!queue_sprite(v, (s.layer & 0x7C0) >> 6, entry)) {
            return; // the sprite table is full
        }
        if (i == 0) {
            s.first_entry = u8(v.queued - 1);
        }
    }
}

void draw_into_map(Game& g, Sprite& s) {
    g.backgrounds().queue_sprite_map(s);
    if (s.frame == 0xFFFFFFFF) {
        return;
    }
    const Assets& a = g.assets();
    auto& settings = g.video().settings;
    const unsigned layer = (s.flags & 0x18000) >> 15;
    const s32 fine_x = remainder(a.s16(s.frame + 8) - s.x, 16);
    const s32 fine_y = remainder(a.s16(s.frame + 10) - s.y, 8);
    if (layer >= 2 && (settings.mode & 3) != 0) {
        // A rotated layer: its reference point (20.8).
        settings.bg[layer].origin_x = fine_x << 8;
        settings.bg[layer].origin_y = fine_y << 8;
        return;
    }
    settings.bg[layer].scroll_x = u16(fine_x);
    settings.bg[layer].scroll_y = u16(fine_y);
}

namespace {

// 08003D84 and 08003A2C: with `with_depth`, the scene's depth factor
// scales the position too.
void place_in_scene(Game& g, Sprite& s, u16 angle, s16 scale_x, s16 scale_y, s16 x, s16 y,
                    bool with_depth) {
    if (s.frame == 0xFFFFFFFF) {
        return;
    }
    const Assets& a = g.assets();
    const SceneTransform& v = g.scene;
    const unsigned turn = (angle + v.angle) & 0x3FFu;
    const s32 cos = sine(a, turn + 0x100) >> 6;
    const s32 sin = sine(a, turn) >> 6;
    s32 sx = s16((scale_x * s32(u16(v.scale_x))) >> 8);
    s32 sy = s16((scale_y * s32(u16(v.scale_y))) >> 8);
    gba::Rotation& r = g.video().rotations[s.flags & sprite_flag::rotation_set];
    r.pa = s16((cos * s16(divide(0x10000, sx))) >> 8);
    r.pb = s16((sin * s16(divide(0x10000, sx))) >> 8);
    r.pc = s16((-sin * s16(divide(0x10000, sy))) >> 8);
    r.pd = s16((cos * s16(divide(0x10000, sy))) >> 8);
    if (scale_x < 0) {
        sx = s16((-scale_x * s32(u16(v.scale_x))) >> 8);
    }
    if (scale_y < 0) {
        sy = s16((-scale_y * s32(u16(v.scale_y))) >> 8);
    }
    // The sprite's own matrix and the scene's (with the depth).
    const s32 m00 = s16((cos * sx) >> 8), m01 = s16((-sin * sx) >> 8);
    const s32 m10 = s16((sin * sy) >> 8), m11 = s16((cos * sy) >> 8);
    const s32 scene_cos = sine(a, v.angle + 0x100u) >> 6;
    const s32 scene_sin = sine(a, v.angle) >> 6;
    const s32 scene_minus_sin = (-s32(sine(a, v.angle))) >> 6;
    s32 g00, g01, g10, g11;
    if (with_depth) {
        const auto product = [](s32 p, s32 q) {
            return s16((s32(s16((p) >> 8)) * s32(s16((q) >> 8))) >> 8);
        };
        g00 = product(scene_cos * u16(v.scale_x), sx * v.depth);
        g01 = product(scene_minus_sin * u16(v.scale_x), sx * v.depth);
        g10 = product(scene_sin * u16(v.scale_y), sy * v.depth);
        g11 = product(scene_cos * u16(v.scale_y), sy * v.depth);
    } else {
        g00 = s16((u16(v.scale_x) * scene_cos) >> 8);
        g01 = s16((u16(v.scale_x) * scene_minus_sin) >> 8);
        g10 = s16((u16(v.scale_y) * scene_sin) >> 8);
        g11 = s16((u16(v.scale_y) * scene_cos) >> 8);
    }
    const s32 px = (g00 * x + g01 * y + v.focus_x * 0x100) >> 8;
    const s32 py = (g10 * x + g11 * y + v.focus_y * 0x100) >> 8;
    const u32 f = s.frame;
    const u16 w = a.u16(f + 4), h = a.u16(f + 6);
    const s16 ox = scale_x > 0 ? a.s16(f + 8) : s16(w - a.u16(f + 8));
    const s16 oy = scale_y > 0 ? a.s16(f + 10) : s16(h - a.u16(f + 10));
    const s32 dx = ox - (w >> 1), dy = oy - (h >> 1);
    s.x = s16(px - ((m00 * dx + m01 * dy + (w >> 1) * 0x100) >> 8));
    s.y = s16(py - ((m10 * dx + m11 * dy + (h >> 1) * 0x100) >> 8));
}

} // namespace

void transform_in_scene(Game& g, Sprite& s, u16 angle, s16 scale_x, s16 scale_y, s16 x, s16 y) {
    place_in_scene(g, s, angle, scale_x, scale_y, x, y, true);
}

void transform_in_view(Game& g, Sprite& s, u16 angle, s16 scale_x, s16 scale_y, s16 x, s16 y) {
    place_in_scene(g, s, angle, scale_x, scale_y, x, y, false);
}

void transform_matrix(Game& g, unsigned set, u16 angle, s16 scale_x, s16 scale_y) {
    const Assets& a = g.assets();
    const s32 cos = sine(a, (angle & 0x3FFu) + 0x100) >> 6;
    const s32 sin = sine(a, angle & 0x3FFu) >> 6;
    // The hardware's parameters map picture to frame: the inverse scale
    // (BIOS division), 8.8.
    gba::Rotation& r = g.video().rotations[set];
    const s32 inv_x = s16(divide(0x10000, scale_x)), inv_y = s16(divide(0x10000, scale_y));
    r.pa = s16((cos * inv_x) >> 8);
    r.pb = s16((sin * inv_x) >> 8);
    r.pc = s16((-sin * inv_y) >> 8);
    r.pd = s16((cos * inv_y) >> 8);
}

void transform(Game& g, TransformedSprite& t) {
    transform(g, t.sprite, t.angle, t.scale_x, t.scale_y, t.x, t.y);
}

void transform(Game& g, Sprite& s, u16 angle, s16 scale_x, s16 scale_y, s16 x, s16 y) {
    const struct {
        u16 angle;
        s16 scale_x, scale_y, x, y;
    } t{angle, scale_x, scale_y, x, y};

    if (s.frame == 0xFFFFFFFF) {
        return;
    }
    const Assets& a = g.assets();
    const s32 cos = sine(a, (t.angle & 0x3FFu) + 0x100) >> 6;
    const s32 sin = sine(a, t.angle & 0x3FFu) >> 6;
    transform_matrix(g, s.flags & sprite_flag::rotation_set, t.angle, t.scale_x, t.scale_y);
    // The forward matrix with the scales' sizes (mirroring is in the
    // frame's offset below).
    const s32 sx = t.scale_x < 0 ? s16(-t.scale_x) : t.scale_x;
    const s32 sy = t.scale_y < 0 ? s16(-t.scale_y) : t.scale_y;
    const s32 m00 = s16((cos * sx) >> 8), m01 = s16((-sin * sx) >> 8);
    const s32 m10 = s16((sin * sy) >> 8), m11 = s16((cos * sy) >> 8);
    const u32 f = s.frame;
    const u16 w = a.u16(f + 4), h = a.u16(f + 6);
    const s16 ox = t.scale_x > 0 ? a.s16(f + 8) : s16(w - a.u16(f + 8));
    const s16 oy = t.scale_y > 0 ? a.s16(f + 10) : s16(h - a.u16(f + 10));
    const s32 dx = ox - (w >> 1), dy = oy - (h >> 1);
    s.x = s16(t.x - ((m00 * dx + m01 * dy + (w >> 1) * 0x100) >> 8));
    s.y = s16(t.y - ((m10 * dx + m11 * dy + (h >> 1) * 0x100) >> 8));
}

} // namespace sonic_advance_gba
