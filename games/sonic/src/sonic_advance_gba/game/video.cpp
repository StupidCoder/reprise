#include "sonic_advance_gba/game/video.hpp"
#include "sonic_advance_gba/game/math.hpp"

namespace sonic_advance_gba {

namespace {

// After each frame every sprite table entry is filled with the halfword 0200
// (08000C54): hidden, and every rotation parameter 2.0.
gba::Sprite blank_sprite() {
    gba::Sprite s;
    s.set_attributes(0x200, 0x200, 0x200);
    return s;
}

} // namespace

namespace {

void use_line_scroll(VideoState& v, u8 layer) {
    v.line_effect_on = true;
    v.line_target = VideoState::LineTarget::scroll;
    v.line_layer = layer;
    v.line_bytes = 4;
}

void put_scroll(VideoState& v, unsigned line, u16 x, u16 y) {
    auto& t = v.filling();
    t.put_halfword(line * 4, x);
    t.put_halfword(line * 4 + 2, y);
}

} // namespace

void line_scroll_fill(VideoState& v, u8 layer, u8 first, u8 last, u16 x, u16 y) {
    use_line_scroll(v, layer);
    for (unsigned n = first; n < last; ++n) {
        put_scroll(v, n, u16(x & 0x1FF), u16(y & 0x1FF));
    }
}

void line_scroll_stretch(VideoState& v, u8 layer, u8 first, u8 last, u16 x, u16 y) {
    use_line_scroll(v, layer);
    u16 vy = u16((y - first) & 0x1FF);
    for (unsigned n = first; n < last; n = (n + 1) & 0xFF) {
        put_scroll(v, n, u16(x & 0x1FF), vy);
        vy = u16(vy - 1);
    }
}

void line_scroll_interleave(VideoState& v, u8 layer, u8 first, u8 last, s16 offset, s16 step, s16 x,
                            u16 y) {
    use_line_scroll(v, layer);
    for (unsigned n = first; n < last; n = (n + 1) & 0xFF) {
        put_scroll(v, n, u16((x + offset) & 0x1FF), y);
        // The step is taken as a signed byte and flips sign every line.
        offset = s16(-(offset + s8(step)));
        step = s16(-s8(step));
    }
}

void rotate_layer(gba::Background& bg, const Assets& a, u16 angle, s16 scale_x, s16 scale_y, s32 cx,
                  s32 cy, s32 sx, s32 sy) {
    const s32 c = sine(a, angle + 0x100u) >> 6, s = sine(a, angle) >> 6;
    const s16 kx = s16(divide(0x10000, scale_x)), ky = s16(divide(0x10000, scale_y));
    bg.pa = s16(u32(c * kx) >> 8);
    bg.pb = s16(u32(s * kx) >> 8);
    bg.pc = s16(u32((-s32(sine(a, angle)) >> 6) * ky) >> 8);
    bg.pd = s16(u32(c * ky) >> 8);
    const s32 mx = s16(-sx), my = s16(-sy);
    bg.origin_x = mx * bg.pa + bg.pb * my + ((cx << 16) >> 8);
    bg.origin_y = mx * bg.pc + bg.pd * my + ((cy << 16) >> 8);
}

bool queue_sprite(VideoState& v, unsigned layer, const gba::Sprite& s) {
    if (v.queued >= 128) {
        return false;
    }
    if (layer > 31) {
        layer = 31;
    }
    v.layers[layer].push_back(s);
    v.queued_entries[v.queued] = s;
    ++v.queued;
    return true;
}

namespace {

// The water's split. Its interrupt at line L - 1 (0804C9A0) copies the
// underwater colours and turns layer 0 off while that line is drawn, and
// starts the per-line copy into layer 3's scroll, entry 0 at line L. The
// next vertical blank copies the ordinary colours back.
void apply_water(VideoState& v, gba::Display& d) {
    VideoState::WaterSplit& w = v.water;
    std::array<u16, 512> normal{};
    std::copy(v.bg_colors.begin(), v.bg_colors.end(), normal.begin());
    std::copy(v.sprite_colors.begin(), v.sprite_colors.end(), normal.begin() + 256);
    const auto under = [&](std::array<u16, 512>& c) {
        std::copy(w.bg.begin(), w.bg.begin() + 0xE8, c.begin());
        std::copy(w.sprites.begin(), w.sprites.end(), c.begin() + 256);
    };
    d.colors = normal;
    if (w.line == 0 && !w.late) {
        under(d.colors);
    }
    if (w.line == 0) {
        // 0804C92C: all under water from the vertical blank on.
        gba::Display::ColorChange cc{0, normal};
        under(cc.colors);
        d.color_changes.insert(d.color_changes.begin(), cc);
        d.settings.set_control(d.settings.control() & 0xFEFF);
        for (auto& c : d.setting_changes) {
            c.settings.set_control(c.settings.control() & 0xFEFF);
        }
        return;
    }
    if (d.colors != normal) {
        d.color_changes.insert(d.color_changes.begin(), {0, normal});
    }
    if (u8(w.line - 1) >= 0x9F) {
        return;
    }
    const int first = w.line - 1;
    gba::Display::ColorChange cc{first, normal};
    under(cc.colors);
    d.color_changes.push_back(cc);
    // From the interrupt's line on: layer 0 off; layer 3's scroll per line
    // from the table being shown.
    std::vector<gba::Display::SettingsChange> changes;
    gba::Settings s = d.settings;
    std::size_t ci = 0;
    const auto& t = v.line_tables[v.line_table];
    for (int n = 0; n < gba::screen_height; ++n) {
        while (ci < d.setting_changes.size() && d.setting_changes[ci].line == n) {
            s = d.setting_changes[ci].settings;
            ++ci;
        }
        if (n < first) {
            if (ci > 0 && d.setting_changes[ci - 1].line == n) {
                changes.push_back({n, s});
            }
            continue;
        }
        gba::Settings next = s;
        next.set_control(next.control() & 0xFEFF);
        if (n >= w.line) {
            const unsigned at = unsigned(n - w.line) * 4;
            next.bg[3].scroll_x = u16(t.halfword(at) & 0x1FF);
            next.bg[3].scroll_y = u16(t.halfword(at + 2) & 0x1FF);
        }
        // (At line 1 the interrupt comes at line 0: the vertical blank's
        // settings stay the frame's own.)
        changes.push_back({n, next});
    }
    d.setting_changes = std::move(changes);
}

} // namespace

void commit_video(VideoState& v, gba::Display& d, SpriteHold hold, bool renew) {
    d.settings = v.settings;
    if (v.bg_colors_changed) {
        std::copy(v.bg_colors.begin(), v.bg_colors.end(), d.colors.begin());
        v.bg_colors_changed = false;
    }
    if (v.sprite_colors_changed) {
        std::copy(v.sprite_colors.begin(), v.sprite_colors.end(), d.colors.begin() + 256);
        v.sprite_colors_changed = false;
    }
    // The per-line values: line n gets table entry n.
    d.setting_changes.clear();
    d.color_changes.clear();
    const auto& t = v.line_tables[v.line_table];
    const auto line_entry = [&](gba::Settings& next, int n) {
        const unsigned at = unsigned(n) * v.line_bytes;
        switch (v.line_target) {
        case VideoState::LineTarget::scroll: {
            auto& bg = next.bg[v.line_layer & 3];
            bg.scroll_x = u16(t.halfword(at) & 0x1FF);
            bg.scroll_y = u16(t.halfword(at + 2) & 0x1FF);
            break;
        }
        case VideoState::LineTarget::window0:
        case VideoState::LineTarget::window1:
        case VideoState::LineTarget::windows: {
            const auto set = [&](unsigned w, u16 h) {
                next.windows[w].left = u8(h >> 8);
                next.windows[w].right = u8(h);
            };
            if (v.line_target == VideoState::LineTarget::window1) {
                set(1, t.halfword(at));
            } else {
                set(0, t.halfword(at));
                if (v.line_target == VideoState::LineTarget::windows) {
                    set(1, t.halfword(at + 2));
                }
            }
            break;
        }
        }
    };
    if (v.line_effect_on || !v.hblank.empty()) {
        // Line by line: the per-line copy's values (its transfer comes
        // first in the blank), then the handlers' changes.
        gba::Settings s = d.settings;
        if (v.line_effect_on) {
            line_entry(s, 0);
        }
        d.settings = s;
        for (int n = 1; n < gba::screen_height; ++n) {
            gba::Settings next = s;
            if (v.line_effect_on) {
                line_entry(next, n);
            }
            for (const auto& handler : v.hblank) {
                handler(n - 1, next);
            }
            if (next != s) {
                d.setting_changes.push_back({n, next});
            }
            s = next;
        }
    }
    v.hblank.clear();
    if (v.water.on) {
        apply_water(v, d);
    }
    // 08004894: the sprite table in layer order, then hidden entries.
    std::vector<gba::Sprite> built;
    for (auto& layer : v.layers) {
        for (const auto& s : layer) {
            built.push_back(s);
        }
        layer.clear();
    }
    const gba::Sprite blank = blank_sprite();
    switch (hold) {
    case SpriteHold::none:
        for (std::size_t n = 0; n < d.sprites.size(); ++n) {
            d.sprites[n] = n < built.size() ? built[n] : blank;
        }
        v.kept_from = 0;
        break;
    case SpriteHold::starting: {
        // The frame's entries move to the end; hidden ones before them.
        const std::size_t first = d.sprites.size() - built.size();
        for (std::size_t n = 0; n < d.sprites.size(); ++n) {
            d.sprites[n] = n < first ? blank : built[n - first];
        }
        v.kept_from = u8(first);
        break;
    }
    case SpriteHold::held:
        for (std::size_t n = 0; n < built.size(); ++n) {
            d.sprites[n] = built[n];
        }
        for (std::size_t n = built.size(); n < v.kept_from; ++n) {
            d.sprites[n] = blank;
        }
        break;
    }
    if (hold == SpriteHold::none) {
        for (const auto& [entry, attribute] : v.cleared_attributes) {
            if (entry >= built.size()) {
                auto at = d.sprites[entry].attributes();
                at[attribute] = 0;
                d.sprites[entry].set_attributes(at[0], at[1], at[2]);
            }
        }
    }
    v.cleared_attributes.clear();
    d.rotations = v.rotations;
    v.queued = 0;
    if (renew) {
        // 08000C54: the next frame fills the other line table, starts with
        // blank rotation sets and has to ask for its line effects again.
        v.rotations = VideoState::blank_rotations();
        v.line_table ^= 1;
        v.line_effect_on = false;
        v.water.on = false;
        v.water.late = false;
    }
}

void set_rotation(VideoState& v, const Assets& a, u16 angle, s16 scale_x, s16 scale_y,
                  unsigned set) {
    const s32 cos = sine(a, angle + 0x100u) >> 6;
    const s32 sin = sine(a, angle) >> 6;
    const s32 ix = s16(divide(0x10000, scale_x)), iy = s16(divide(0x10000, scale_y));
    gba::Rotation& r = v.rotations.at(set);
    r.pa = s16((ix * cos) >> 8);
    r.pb = s16((sin * ix) >> 8);
    r.pc = s16(((-s32(sine(a, angle)) >> 6) * iy) >> 8);
    r.pd = s16((iy * cos) >> 8);
}

} // namespace sonic_advance_gba
