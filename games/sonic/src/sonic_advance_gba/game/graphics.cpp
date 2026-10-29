#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/video.hpp"

namespace sonic_advance_gba {

ScreenGraphics::ScreenGraphics(Game& g) : Process(0x1800), game_(g) {}

void ScreenGraphics::on_destroy() {
    // 080536A4
    if (game_.screen_graphics == this) {
        game_.screen_graphics = nullptr;
    }
}

TileCopy* ScreenGraphics::take() {
    if (used_ >= 15) {
        // The 16th node's link is uninitialised memory in the original.
        throw Unrecovered("more than 15 picture copies between pool resets");
    }
    return &pool_[used_++];
}

void ensure_screen_graphics(Game& g) {
    if (!g.screen_graphics) {
        g.screen_graphics = &g.scheduler().create<ScreenGraphics>(g);
    }
    g.screen_graphics->reset_pool();
}

void load_graphics(Game& g, const GraphicsLoad& l) {
    ScreenGraphics* m = g.screen_graphics;
    if (!m) {
        throw Unrecovered("picture loaded without the screen graphics process");
    }
    auto& slot = m->slots.at(l.slot);
    slot.key = l.key;
    slot.extra = l.extra;
    auto& v = g.video();
    const auto queue = [&](u32 from, u32 to, u32 bytes) {
        if (TileCopy* c = m->take()) {
            c->from = from;
            c->to = to;
            c->bytes = u16(bytes);
            g.backgrounds().copies.push(*c);
        }
    };
    const auto copy_colors = [&](std::array<u16, 256>& colors, bool& changed) {
        const unsigned first = l.palette * 16u;
        const unsigned n = l.color_bytes >> 1;
        if (first + n > 256) {
            throw Unrecovered("picture colours beyond the palette");
        }
        for (unsigned i = 0; i < n; ++i) {
            colors[first + i] = g.assets().u16(l.colors_from + 2 * i);
        }
        changed = true;
    };
    if (!(l.parts & load::sprite)) {
        const auto& bg = v.settings.bg[l.layer & 3];
        if (l.parts & load::tiles) {
            const u32 start = (l.options & 0x80) ? 0 : 0x20;
            queue(l.tiles_from, start + bg.tile_block * 0x4000u, l.tile_bytes);
        }
        if (l.parts & load::map) {
            queue(l.map_from, bg.map_block * 0x800u, l.map_bytes);
        }
        if (l.parts & load::colors) {
            copy_colors(v.bg_colors, v.bg_colors_changed);
        }
    } else {
        if (l.parts & load::tiles) {
            queue(l.tiles_from, l.tiles_to, l.tile_bytes);
        }
        if (l.parts & load::colors) {
            copy_colors(v.sprite_colors, v.sprite_colors_changed);
        }
        slot.first_tile = (l.tiles_to - 0x10000) >> 5;
    }
}

void set_slot_layout(Game& g, u8 slot, u32 key, std::array<u8, 4> extra) {
    if (!g.screen_graphics) {
        throw Unrecovered("picture layout set without the screen graphics process");
    }
    auto& s = g.screen_graphics->slots.at(slot);
    s.key = key;
    s.extra = extra;
}

namespace {

void put_text(Game& g, const u8* chars, const SpriteText& t, bool semi_transparent, bool layered) {
    if (!g.screen_graphics) {
        throw Unrecovered("sprite text without the screen graphics process");
    }
    const auto& s = g.screen_graphics->slots.at(t.slot);
    const u8 size = s.extra[0], shape = s.extra[1], width = s.extra[2], height = s.extra[3];
    u16 a0 = u16(shape * 0x4000 + u8(t.y) + (semi_transparent ? 0x400 : 0));
    const u16 a2 = u16((t.palette << 12) | (s.first_tile & 0x3FF));
    auto& v = g.video();
    const auto put = [&](u16 a1, s32 i) {
        const unsigned layer = ((t.layer + (layered ? i : 0)) >> 3) & 0xFF;
        gba::Sprite e;
        e.set_attributes(a0, a1, u16(u16(s.key) * (chars[i] - 0x20) + a2));
        return queue_sprite(v, layer, e);
    };
    if (t.single_row) {
        u16 a1 = u16(size * 0x4000 + (t.x & 0x1FF));
        for (s32 i = 0; i < t.count; ++i, a1 = u16(a1 + width * 8)) {
            if (!put(a1, i)) {
                return;
            }
        }
        return;
    }
    for (unsigned row = 0; row < t.rows; ++row) {
        u16 a1 = u16(size * 0x4000 + (t.x & 0x1FF));
        // Glyphs left of the picture are skipped in steps of 128 pixels.
        for (s32 i = s32((-t.x) & 0x1FF) >> 7; i < t.count; ++i, a1 = u16(a1 + width * 8)) {
            if (!put(a1, i)) {
                break;
            }
        }
        a0 = u16(a0 + height * 8);
    }
}

} // namespace

void draw_text_scaled(Game& g, const u8* chars, const SpriteText& t, s16 scale_x, s16 scale_y,
                      u16 angle, u8 rotation_set) {
    if (!g.screen_graphics) {
        throw Unrecovered("sprite text without the screen graphics process");
    }
    const auto& s = g.screen_graphics->slots.at(t.slot);
    auto& v = g.video();
    u16 mode = 0, set = 0;
    if (angle != 0 || scale_x != 0x100 || scale_y != 0x100) {
        set_rotation(v, g.assets(), angle, scale_x, scale_y, rotation_set);
        set = rotation_set;
        mode = 3;
    }
    for (s32 i = 0; i < t.count; ++i) {
        gba::Sprite e;
        e.set_attributes(u16(s.extra[1] << 14 | u8(t.y) | mode << 8 | 0x400),
                         u16(s.extra[0] << 14 | set << 9 | ((i * s.extra[2] * 8 + t.x) & 0x1FF)),
                         u16(t.palette * 0x1000 + s.first_tile + (chars[i] - 0x20) * u16(s.key)));
        if (!queue_sprite(v, (t.layer >> 3) & 0xFF, e)) {
            return;
        }
    }
}

void draw_credits_text(Game& g, const u8* chars, const SpriteText& t) {
    if (!g.screen_graphics) {
        throw Unrecovered("sprite text without the screen graphics process");
    }
    const auto& s = g.screen_graphics->slots.at(t.slot);
    const u8 size = s.extra[0], shape = s.extra[1], width = s.extra[2];
    u16 a1 = u16(size * 0x4000 + (t.x & 0x1FF));
    auto& v = g.video();
    for (s32 i = 0; i < t.count; ++i, a1 = u16(a1 + width * 8)) {
        const u8 c = chars[i];
        if (c == 0x20) {
            continue;
        }
        u16 a0 = u16(shape * 0x4000 + u8(t.y));
        s32 glyph = c - 0x20;
        if (c >= 0x61) {
            a0 = u16(a0 + 8);
            glyph = c - 0x11;
        }
        const u16 a2 =
            u16((u16(s.key) * glyph + ((t.palette << 12) | (s.first_tile & 0x3FF))) & 0xF3FF);
        gba::Sprite e;
        e.set_attributes(a0, a1, a2);
        if (!queue_sprite(v, ((t.layer + i) >> 3) & 0xFF, e)) {
            return;
        }
    }
}

void draw_text(Game& g, const u8* chars, const SpriteText& t, bool semi_transparent) {
    put_text(g, chars, t, semi_transparent, false);
}

void draw_text_layered(Game& g, const u8* chars, const SpriteText& t) {
    put_text(g, chars, t, false, true);
}

void draw_text_rotated(Game& g, const u8* chars, const SpriteText& t, u8 rotation_set,
                       s16 spacing) {
    if (!g.screen_graphics) {
        throw Unrecovered("sprite text without the screen graphics process");
    }
    const auto& s = g.screen_graphics->slots.at(t.slot);
    auto& v = g.video();
    for (s32 i = 0; i < t.count; ++i) {
        gba::Sprite e;
        e.set_attributes(
            u16(s.extra[1] * 0x4000 + 0x300 + (t.y & 0x1FF)),
            u16(s.extra[0] * 0x4000 + rotation_set * 0x200 + ((i * spacing) >> 5) + t.x),
            u16(t.palette * 0x1000 + s.first_tile + (chars[i] - 0x20) * u16(s.key)));
        if (!queue_sprite(v, (t.layer >> 3) & 0xFF, e)) {
            return;
        }
    }
}

} // namespace sonic_advance_gba
