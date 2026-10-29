// The act's backgrounds and the camera (0803E0B0-0803F6DC).

#include "sonic_advance_gba/game/camera.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>

namespace sonic_advance_gba {

namespace {

constexpr u32 layer_templates[3] = {0x084A6A60, 0x084A6AA0, 0x084A6AE0}; // layers 1, 2, 3
constexpr u32 far_layer_params = 0x084A6BF0;   // per act: width, height, tile block, map block
constexpr u32 stage_maps = 0x084A6DA4;         // per act: the collision map's table
constexpr u32 far_layer_routines = 0x084A6BA8; // per act: the far background's routine
constexpr u32 far_layer_starts = 0x084A6B60;   // per act: an extra start routine (0: none)

// 0803E758: acts 1 and 2 of the first zone: the far background stays,
// its top lines drift with time (clouds) and its bottom lines move with
// the camera, more the lower they are.
// 0803E844: the far background in bands: rows of 64 lines repeating
// down the view at a quarter of the view's speed, with three strips of 16
// lines (at half speed) and one of 24 lines (two thirds) laid over them.
// (x, y) is the view's position without the last wrap.
void banded_far_layer(Game& g, s32 x, s32 y) {
    StageState& s = g.stage();
    auto& v = g.video();
    const u16 base_x = u16((x >> 2) & 0xFF);
    v.settings.bg[3].scroll_x = base_x;
    v.settings.bg[3].scroll_y = u16((y >> 2) & 0xFF);
    v.line_effect_on = true;
    v.line_target = VideoState::LineTarget::scroll;
    v.line_layer = 3;
    v.line_bytes = 4;
    s.camera.parallax_x = s16(v.settings.bg[3].scroll_x);
    s.camera.parallax_y = s16(v.settings.bg[3].scroll_y);
    auto& t = v.filling();
    const auto put = [&](s32 line, u16 sx, s32 sy) {
        if (line >= 0 && line < 0xA0) {
            t.put_halfword(unsigned(line) * 4, sx);
            t.put_halfword(unsigned(line) * 4 + 2, u16(sy));
        }
    };
    // The bands: line l shows row (l + offset) mod 64.
    const s32 offset = (y >> 2) & 0x3F;
    for (s32 line = 0; line < 0xA0; ++line) {
        put(line, base_x, offset - ((line + offset) & ~0x3F));
    }
    // The strips (each at the line where its row reaches the view).
    for (s32 k = 0; k < 3; ++k) {
        const s32 at = -(k * 0x40 + (y >> 1)) & 0xFF;
        for (s32 line = at - 0x10; line < at; ++line) {
            put(line, u16(((x >> 1) + k * 0x20) & 0xFF), 0x50 - at);
        }
    }
    const s32 at = -((y * 2) / 3 + 3 * 0x60) & 0xFF;
    for (s32 line = at - 0x18; line < at; ++line) {
        put(line, u16(((x * 2) / 3 + 3 * 0x20) & 0xFF), 0x68 - at);
    }
}

// 0803E9F0: a sparkle of the Casino Paradise far background, drawn four
// times mirrored about its middle, moving with the far background; gone
// when its animation ends. Its tiles are its own (08040454).
class CasinoSparkle final : public Process {
public:
    CasinoSparkle(Game& g, s32 x, s32 y, u32 kind) : Process(0x4001), game_(g), x_(x), y_(y) {
        const Assets& a = g.assets();
        sprite_.start(0, 0, 0, 0, 0, 0, 0);
        sprite_.tiles.to = g.sprite_tiles.allocate(a.u8(sparkles + kind + 3));
        sprite_.animation = a.u16(sparkles + kind);
        sprite_.variant = a.u8(sparkles + kind + 2);
        sprite_.layer = 0x7C0;
        sprite_.flags = 0x3000;
    }

    void on_destroy() override { game_.sprite_tiles.free(sprite_.tiles.to); }

    void update(Game& g) override {
        if (sprite_.flags & sprite_flag::ended) {
            g.scheduler().destroy(*this);
            return;
        }
        const auto& c = g.stage().camera;
        sprite_.x = s16(x_ - c.parallax_x);
        sprite_.y = s16(y_ - c.parallax_y);
        sprite_.flags &= ~(sprite_flag::keep_colors | sprite_flag::keep_tiles);
        animate(g, sprite_);
        sprite_.flags = (sprite_.flags & ~(sprite_flag::flip_x | sprite_flag::flip_y)) |
                        sprite_flag::keep_colors | sprite_flag::keep_tiles;
        draw(g, sprite_);
        sprite_.flags ^= sprite_flag::flip_x;
        draw(g, sprite_);
        sprite_.flags ^= sprite_flag::flip_x | sprite_flag::flip_y;
        draw(g, sprite_);
        sprite_.flags ^= sprite_flag::flip_x;
        draw(g, sprite_);
    }

    static constexpr u32 sparkles = 0x084A6C38; // 2 x (animation, variant, tiles)

private:
    Game& game_;
    Sprite sprite_; // +18
    s32 x_, y_;     // +00, +04
};

// 0803EA8C: Casino Paradise: the far background scrolls across the act in
// proportion; its lower part in bands moving faster the lower they are;
// now and then a sparkle (every fourth frame, one chance in eight).
void casino_far_layer(Game& g, s32 x, s32 y) {
    StageState& s = g.stage();
    auto& v = g.video();
    x -= s.camera.offset_x;
    const s32 sx = divide(x * 0x10, s.map.width - 0xF0);
    const s32 sy = divide((y - s.camera.offset_y) * 0x60, s.map.height - 0xA0);
    v.settings.bg[3].scroll_x = u16(sx);
    s.camera.parallax_x = s16(sx);
    v.settings.bg[3].scroll_y = u16(sy);
    v.line_effect_on = true;
    v.line_target = VideoState::LineTarget::scroll;
    v.line_layer = 3;
    v.line_bytes = 4;
    s.camera.parallax_y = s16(sy);
    auto& t = v.filling();
    const u32 fill = (u32(sy) << 16) | u32(sx);
    for (unsigned line = 0; line < 0xA0; ++line) {
        t.put_halfword(line * 4, u16(fill));
        t.put_halfword(line * 4 + 2, u16(fill >> 16));
    }

    // Bands: (first line before the scroll, lines, the speed's shift).
    const struct {
        s32 first, lines, shift;
    } bands[] = {{0x86, 6, 6},  {0x86, 6, 5},  {0x8C, 12, 4},
                 {0x98, 16, 3}, {0xA8, 24, 2}, {0xC0, 64, 1}};

    for (const auto& b : bands) {
        for (s32 i = 0; i < b.lines; ++i) {
            const s32 line = b.first - sy + i;
            if (line < 0) {
                continue;
            }
            if (line > 0x9F) {
                break;
            }
            t.put_halfword(unsigned(line) * 4, u16((sx + (x >> b.shift)) & 0xFF));
            t.put_halfword(unsigned(line) * 4 + 2, u16(sy));
        }
    }
    if ((s.ticks & 3) == 0 && (s.next_random() & 0x700) == 0) {
        const u32 r2 = s.next_random(), r3 = s.next_random(), r4 = s.next_random();
        const u32 kind = (r4 & 0x100) >> 6;
        g.scheduler().create<CasinoSparkle>(g, s32(((r2 & 0xFF00) * 0x100 - 0x80000)) >> 16,
                                            s32((((r3 & 0x7F00) >> 8) - 0x20) << 16) >> 16, kind);
    }
}

// 0803F6E0: Ice Paradise's mist in front (layer 0, template 084A6B20,
// asset 47h: tiles at 0C000h, map at 0D800h, 32 x 32 tiles), blended half
// and half.
void start_mist_layer(Game& g) {
    StageState& s = g.stage();
    auto& v = g.video().settings;
    v.blend.eva = 0x10;
    v.blend.evb = 0x10;
    v.bg[0].set_control(0x1B0C);
    Background& b = s.front_layer;
    b.set_from_template(g.assets(), 0x084A6B20);
    b.asset = 0x47;
    b.tiles.to = 0xC000;
    b.map_to = 0xD800;
    b.width = 0x20;
    b.height = 0x20;
    load_background(g.assets(), g.video(), g.backgrounds(), b);
}

// 0803F73C: acts 8 and 9: layer 0 gets the template's front layer with
// picture 48h (its places and size as the template has them).
void start_front_layer_8(Game& g) {
    StageState& s = g.stage();
    g.video().settings.bg[0].set_control(0x1C0F);
    Background& b = s.front_layer;
    b.set_from_template(g.assets(), 0x084A6B20);
    b.asset = 0x48;
    load_background(g.assets(), g.video(), g.backgrounds(), b);
}

// 0803F7D4: act 13: layer 2 (turned and scaled, 256 colours, tiles at
// 4000h, map at E000h) starts empty: its tiles and map are cleared at once.
void start_moon_layer(Game& g) {
    auto& memory = g.video_memory_now().memory;
    std::fill(memory.begin() + 0x4000, memory.begin() + 0x8000, u8(0));
    std::fill(memory.begin() + 0xE000, memory.begin() + 0xE800, u8(0));
    g.video().settings.bg[2].set_control(0x5C85);
}

// 0803F88C: acts 8 and 9: layer 0 drifts left a pixel every four frames,
// blended over the rest (C/16 of it, all of what is behind).
void drifting_front_layer(Game& g) {
    StageState& s = g.stage();
    auto& v = g.video().settings;
    v.set_control(v.control() | 0x100);
    v.blend.set_control(0x3F41);
    v.blend.eva = 0xC;
    v.blend.evb = 0x10;
    v.bg[0].scroll_x = u16((0x100 - (s.ticks >> 2)) & 0xFF);
    v.bg[0].scroll_y = 0;
    load_background(g.assets(), g.video(), g.backgrounds(), s.front_layer);
    animate_background_tiles(g.assets(), g.backgrounds(), s.front_layer);
}

// 0803ED50: Ice Paradise: the mist drifts (across at 5/4 of the view's
// speed plus a quarter pixel a frame, and up a pixel a frame); the far
// layer scrolls with the act; under water (the whole picture) the mist is
// off.
void ice_far_layer(Game& g, s32 x, s32 y) {
    StageState& s = g.stage();
    auto& v = g.video().settings;
    v.blend.set_control(0x3F41);
    v.blend.eva = 0x10;
    v.blend.evb = 0x10;
    x -= s.camera.offset_x;
    const s32 dy = y - s.camera.offset_y;
    v.bg[0].scroll_x = u16((x + (x >> 2) + s32(s.ticks >> 2)) & 0xFF);
    v.bg[0].scroll_y = u16((dy + 0x100 - s32(s.ticks)) & 0xFF);
    load_background(g.assets(), g.video(), g.backgrounds(), s.front_layer);
    animate_background_tiles(g.assets(), g.backgrounds(), s.front_layer);
    v.bg[3].scroll_x = u16(divide(x * 0x10, s.map.width - 0xF0));
    s.camera.parallax_x = s16(v.bg[3].scroll_x);
    s.camera.parallax_y = s16(divide(dy * 0x160, s.map.height - 0xA0));
    v.bg[3].scroll_y = u16(s.camera.parallax_y);
    if (s.water.shown_line == 0) {
        v.set_control(v.control() & 0xFEFF);
        auto& now = g.video_memory_now().settings;
        now.set_control(now.control() & 0xFEFF);
    } else {
        v.set_control(v.control() | 0x100);
    }
}

// 0803F9E8: act 10's horizontal blank handler: from line 101 layer 0
// shows the top of its map (size 256x512, rows from A0h); from line 103
// the brightening (BLDY) fades out over 32 lines.
void rocket_sky_lines(int line, gba::Settings& s) {
    if (line == 0x64) {
        s.bg[0].size = 2;
        s.bg[0].scroll_x = 0;
        s.bg[0].scroll_y = 0xA0;
    } else if (u8(line - 0x66) <= 0x1F) {
        s.blend.evy = u8(0x10 - ((line - 0x65) >> 1));
    }
}

// 0803EE50: act 10, the rocket. Before take-off (checkpoint 0, and up to
// the moment the far layer's scroll meets the rising sky) the far picture
// (map block 1Ch, 256x512) scrolls across with the act; flying (checkpoints
// 1 and 2) it moves down two lines a frame. Past the third checkpoint it
// goes on through map blocks 1Ch, 1Bh and 1Ah up to space, where layer 0
// joins with the top of the picture (the sky's rows straight from the
// map, 0803E0B0) brightened and fading (the horizontal blank handler);
// after the act it stays (camera_lock::far_still).
void rocket_far_layer(Game& g, s32 x) {
    StageState& s = g.stage();
    auto& c = s.camera;
    if (c.locks & camera_lock::far_still) {
        return;
    }
    auto& vs = g.video();
    auto& v = vs.settings;
    auto& far = v.bg[3];
    auto& front = v.bg[0];
    const auto set_far = [&](u16 mask, u16 bits) {
        far.set_control(u16((far.control() & mask) | bits));
    };
    const u32 rise = u32(-s32(s.ticks) * 2); // the sky's lines: two a frame
    const auto ground_view = [&] {
        set_far(0x20FF, 0x9C00);
        far.scroll_x = u16(divide((x - c.offset_x) * 0x10, 0x330));
        c.parallax_x = s16(far.scroll_x);
    };
    // Layer 0 in front (map block 1Ah), brightened (fading per line in
    // single play).
    const auto sky_top = [&](u16 from_far, u16 scroll_y) {
        v.blend.set_control(0x81);
        if (g.game_mode < 2) {
            v.blend.evy = 0;
            front.set_control(u16((from_far & 0x20FF) | 0x1A00));
            front.scroll_y = scroll_y;
            vs.hblank.push_back(rocket_sky_lines);
        } else {
            front.set_control(u16((from_far & 0x20FF) | 0x9A00));
            front.scroll_y = 0xA0;
            v.blend.evy = 0;
        }
    };
    if (s.checkpoint == 0) {
        ground_view();
        c.parallax_y = 0x100;
        far.scroll_y = 0x100;
        return;
    }
    if (s.checkpoint < 3) {
        if (!(c.locks & camera_lock::rocket_up)) {
            ground_view();
            if ((rise & 0x1FF) == u32(s16(far.scroll_y))) {
                c.locks |= camera_lock::rocket_up;
            }
            return;
        }
        if ((rise & 0x1FF) == 0) {
            far.size = 0;
        }
        set_far(0xE0FF, 0x1C00);
        c.parallax_y = s16(far.size == 0 ? (rise & 0xFF) : (rise & 0x1FF));
        far.scroll_x = 0;
        far.scroll_y = u16(c.parallax_y);
        c.parallax_x = 0;
        return;
    }
    if (s.checkpoint != 3) {
        return;
    }
    const u16 block = u16(far.control() & 0x1F00);
    if (block == 0x1C00) {
        far.scroll_y = u16(rise & 0xFF);
        far.scroll_x = 0;
        c.parallax_x = 0;
        c.parallax_y = s16(far.scroll_y);
        if ((rise & 0x1FF) == 0x100 && (c.locks & camera_lock::rocket_space)) {
            c.parallax_y = 0x100;
            far.scroll_y = 0x100;
            set_far(0x20FF, 0x9B00);
        }
    } else if (block == 0x1B00) {
        far.scroll_y = u16(rise & 0x1FF);
        far.scroll_x = 0;
        c.parallax_x = 0;
        c.parallax_y = s16(far.scroll_y);
        if ((rise & 0x1FF) == 0) {
            c.parallax_y = s16((rise - 0x100) & 0x1FF);
            set_far(0x20FF, 0x9A00);
            far.scroll_y = u16(c.parallax_y);
        }
    } else if ((far.control() & 0xC000) == 0x8000) {
        const u32 y = (rise - 0x100) & 0x1FF;
        far.scroll_y = u16(y);
        far.scroll_x = 0;
        c.parallax_x = 0;
        c.parallax_y = s16(y);
        if (y < 0xA0) {
            v.set_control(v.control() | 0x100);
            front.set_control(u16((far.control() & 0x20FF) | 0x1A00));
            c.parallax_y = s16((s.ticks >> 3) & 0xFF);
            front.scroll_x = 0;
            sky_top(far.control(), u16(c.parallax_y));
        }
        if (y == 0) {
            far.scroll_y = u16(rise & 0xFF);
            set_far(0x20FF, 0x1A00);
            c.parallax_x = 0;
            front.scroll_x = 0;
            c.parallax_y = s16((s.ticks >> 1) & 0xFF);
            v.blend.set_control(0x81);
            v.blend.evy = 0;
            if (g.game_mode < 2) {
                front.scroll_y = u16(c.parallax_y);
                vs.hblank.push_back(rocket_sky_lines);
            } else {
                front.size = 2;
                front.scroll_y = 0xA0;
            }
        }
    } else {
        // In space: the far picture drifts slowly.
        set_far(0x20FF, 0x1A00);
        far.scroll_y = u16((s.ticks >> 6) & 0xFF);
        c.parallax_x = 0x74;
        front.scroll_x = 0x74;
        c.parallax_y = s16((s.ticks >> 3) & 0xFF);
        v.blend.set_control(0x81);
        if (g.game_mode < 2) {
            v.blend.evy = 0;
            front.set_control(u16((front.control() & 0x20FF) | 0x1A00));
            front.scroll_y = u16(c.parallax_y);
            far.scroll_x = 0;
            vs.hblank.push_back(rocket_sky_lines);
        } else {
            front.set_control(u16((front.control() & 0x20FF) | 0x9A00));
            front.scroll_y = 0xA0;
            v.blend.evy = 0;
            far.scroll_x = 0;
        }
        if (!(c.locks & camera_lock::rocket_up)) {
            // The planet below sinks out of view (layer 0 per line).
            line_scroll_fill(vs, 0, 0, 100, front.scroll_x, front.scroll_y);
            line_scroll_fill(vs, 0, 100, 0x68, 0, 0xA0);
            const u32 sink = (s.ticks >> 5) & 0x7F;
            if (0x80 - sink < 0x39) {
                line_scroll_fill(vs, 0, 0x68, u8(0xE8 - sink), 0, u16(sink + 0xA0));
                line_scroll_fill(vs, 0, u8(0xE8 - sink), 0xA0, 0, u16(sink + 0x20));
            } else {
                line_scroll_fill(vs, 0, 0x68, 0xA0, 0, u16(sink + 0xA0));
            }
        }
    }
}

// 0803F3CC: act 11 (and 17): over the act's whole width the far picture
// scrolls 110h pixels across, over its height 48h down; two 16-line bands
// (picture lines 78h and B0h) drift sideways with time, one each way.
void drifting_bands_far_layer(Game& g, s32 x, s32 y) {
    StageState& s = g.stage();
    auto& c = s.camera;
    auto& vs = g.video();
    auto& far = vs.settings.bg[3];
    const s32 sx = divide((x - c.offset_x) * 0x110, s.map.width - 0xF0);
    c.parallax_x = s16(sx);
    far.scroll_x = u16(sx);
    const s32 sy = divide((y - c.offset_y) * 0x48, s.map.height - 0xA0);
    c.parallax_y = s16(sy);
    far.scroll_y = u16(sy);
    vs.line_effect_on = true;
    vs.line_target = VideoState::LineTarget::scroll;
    vs.line_layer = 3;
    vs.line_bytes = 4;
    auto& t = vs.filling();
    for (unsigned line = 0; line < 0xA0; ++line) {
        t.put_halfword(line * 4, u16(sx));
        t.put_halfword(line * 4 + 2, u16(sy));
    }
    const u32 drift = s.ticks >> 1;
    const auto band = [&](s32 from, u32 band_x) {
        for (s32 i = 0; i < 0x10; ++i) {
            const s32 line = from + i;
            if (line < 0) {
                continue;
            }
            if (line > 0x9F) {
                return;
            }
            t.put_halfword(unsigned(line) * 4, u16(band_x & 0x1FF));
            t.put_halfword(unsigned(line) * 4 + 2, u16(sy));
        }
    };
    band(0x78 - sy, u32(sx) - drift);
    band(0xB0 - sy, drift + u32(sx));
}

// 0803F774: act 12: the far picture scrolls 38h pixels across the act's
// width and 18h down its height.
void x_zone_far_layer(Game& g, s32 x, s32 y) {
    StageState& s = g.stage();
    auto& c = s.camera;
    auto& far = g.video().settings.bg[3];
    far.scroll_x = u16(divide((x - c.offset_x) * 0x38, s.map.width - 0xF0));
    c.parallax_x = s16(far.scroll_x);
    c.parallax_y = s16(divide((y - c.offset_y) * 0x18, s.map.height - 0xA0));
    far.scroll_y = u16(c.parallax_y);
}

// 0803F8F8: act 13's horizontal blank handler: the far layer (layer 1)
// takes its scroll for the next line from the line values filled by the
// far routine (lines 5Ah-9Eh: the ground's lines); above them it stays.
// (0803F8EC, its vertical blank part, sets the scroll to 0 for line 0.)
struct MoonGroundLines {
    std::array<u16, 0x46> scroll_x{}; // for lines 5Ah-9Fh

    void operator()(int line, gba::Settings& s) const {
        s.bg[1].scroll_x = (line >= 0x5A && line != 0x9F) ? scroll_x[std::size_t(line - 0x5A)] : 0;
        s.bg[1].scroll_y = 0;
    }
};

// 0803F4F8: act 13: the far picture (earth and sky) stays; the lower lines,
// the moon's ground towards the horizon, slide sideways at speeds rising
// down the view (bands of 10, 5, 8 and 16 lines at 1/64, 1/32, 1/16 and
// 1/8 pixel a frame, then each line faster by 1/32 more).
void moon_far_layer(Game& g) {
    StageState& s = g.stage();
    auto& c = s.camera;
    auto& vs = g.video();
    auto& v = vs.settings;
    c.parallax_x = c.parallax_y = 0;
    v.bg[1].scroll_x = v.bg[1].scroll_y = 0;
    Background& far = s.layers[2];
    far.scroll_x = far.scroll_y = 0;
    load_background(g.assets(), vs, g.backgrounds(), far);
    const u32 t = s.ticks;
    MoonGroundLines lines;
    std::size_t n = 0;
    const auto band = [&](std::size_t count, u32 value) {
        for (std::size_t i = 0; i < count; ++i) {
            lines.scroll_x[n++] = u16(value & 0xFF);
        }
    };
    band(10, t >> 6);
    band(5, t >> 5);
    band(8, t >> 4);
    band(16, t >> 3);
    for (u32 i = 1; i < 0x20; ++i) {
        lines.scroll_x[n++] = u16(((t >> 3) + ((t * i) >> 5)) & 0xFF);
    }
    vs.hblank.push_back(lines);
}

// 0803F820: Secret Base act 1: a still picture scrolling slowly across for
// the first 5D0h pixels, then the bands.
void secret_base_1_far_layer(Game& g, s32 x, s32 y) {
    if (x >= 0x5D0) {
        banded_far_layer(g, x, y);
        return;
    }
    StageState& s = g.stage();
    auto& bg = g.video().settings.bg[3];
    bg.scroll_x = u16(divide(x * 0x10, 0x4E0));
    s.camera.parallax_y = 0x160;
    bg.scroll_y = 0x160;
    s.camera.parallax_x = s16(bg.scroll_x);
}

void neo_green_hill_far_layer(Game& g, s32 x) {
    StageState& s = g.stage();
    auto& v = g.video();
    const s32 dx = x - s.camera.offset_x;
    s.camera.parallax_x = 0;
    v.settings.bg[3].scroll_x = 0;
    s.camera.parallax_y = 0;
    v.settings.bg[3].scroll_y = 0;
    Background& far = s.layers[2];
    far.scroll_x = 0;
    far.scroll_y = 0;
    load_background(g.assets(), v, g.backgrounds(), far);
    v.line_effect_on = true;
    v.line_target = VideoState::LineTarget::scroll;
    v.line_layer = 3;
    v.line_bytes = 4;
    auto& t = v.filling();
    unsigned line = 0;
    const auto put = [&](u16 sx) {
        t.put_halfword(line * 4, sx);
        t.put_halfword(line * 4 + 2, 0);
        ++line;
    };
    while (line < 8) {
        put(u16((s.ticks >> 4) & 0xFF));
    }
    while (line < 0x18) {
        put(u16((s.ticks >> 5) & 0xFF));
    }
    while (line < 0x28) {
        put(u16((s.ticks >> 6) & 0xFF));
    }
    while (line < 0x58) {
        put(0);
    }
    s32 shift = dx * (0x58 - 0x56);
    while (line < 0xA0) {
        put(u16(u32(shift) >> 8));
        shift += dx;
    }
}

// 0803E690: the level layers follow the camera.
void scroll_layers(Game& g, s32 x, s32 y) {
    StageState& s = g.stage();
    auto& v = g.video();
    if (s.zone == 0xD) {
        // Act 13: only the ground (layer 0) moves, eight pixels a frame
        // ahead of the view, repeating every A80h pixels past the first 48h.
        x = moon_ground_x(s, x);
        auto& bg = v.settings.bg[0];
        bg.scroll_x = u16(x & 7);
        bg.scroll_y = u16(y & 7);
        Background& layer = s.layers[1];
        layer.scroll_x = u16(x);
        layer.scroll_y = u16(y);
        load_background(g.assets(), v, g.backgrounds(), layer);
        return;
    }
    for (unsigned i = 0; i < 2; ++i) {
        auto& bg = v.settings.bg[1 + i];
        bg.scroll_x = u16(x & 7);
        bg.scroll_y = u16(y & 7);
        Background& layer = s.layers[i];
        layer.scroll_x = u16(x);
        layer.scroll_y = u16(y);
        load_background(g.assets(), v, g.backgrounds(), layer);
        if (i == 0) {
            animate_background_tiles(g.assets(), g.backgrounds(), layer);
        }
    }
}

// 0803E3DC: the camera follows the player (a dead zone of 8 pixels to
// either side, at most max_step pixels a frame), within the act's limits.
void follow_player(Game& g) {
    StageState& s = g.stage();
    auto& c = s.camera;
    const auto clamp_x = [&](s32 v, s32 margin) {
        return v < c.min_x ? s32(c.min_x) : std::min(v, c.max_x - margin);
    };
    const auto clamp_y = [&](s32 v, s32 margin) {
        if (v < c.min_y) {
            return s32(c.min_y);
        }
        return v > c.max_y - margin ? c.max_y - margin : v;
    };
    s32 x = clamp_x(c.x, 0xF1);
    s32 y = clamp_y(c.y, 0xA1);
    if (c.hold != 0) {
        --c.hold;
    } else {
        if (g.game_mode >= 2) {
            throw Unrecovered("the multiplayer camera (0803E3DC)");
        }
        const Player& p = s.player[0];
        if (!(c.locks & 1)) {
            c.target_x = s16((p.x >> 8) + c.shift_x - 0x78);
        }
        if (!(c.locks & 2)) {
            s32 want = p.radius_y - 4;
            if (s.flags & stage_flag::upside_down) {
                want = -want;
            }
            const s32 have = c.vertical_offset;
            if (have < want) {
                c.vertical_offset = s16(std::min(have + 5, want));
            } else if (have > want) {
                c.vertical_offset = s16(std::max(have - 5, want));
            }
            c.target_y = s16(c.shift_y + s16(p.y >> 8) - 0x50 + c.look_y + c.vertical_offset);
        }
    }
    const s32 dx = c.target_x - x;
    if (dx > c.dead_zone_x) {
        x += std::min(dx - c.dead_zone_x, s32(c.max_step_x));
    } else if (dx + c.dead_zone_x < 0) {
        x += std::max(dx + c.dead_zone_x, -s32(c.max_step_x));
    }
    x = clamp_x(x, 0xF0);
    // In the air the camera lets the player move further up and down.
    const Player& p = s.player[0];
    if (!(p.flags & player_flag::in_air) || (p.character == 2 && p.special == 9)) {
        c.lead_y = u16(c.lead_y - 4);
        if (s16(c.lead_y) < 0) {
            c.lead_y = 0;
        }
    } else {
        c.lead_y = u16(c.lead_y + 4);
        if (s16(c.lead_y) > 0x18) {
            c.lead_y = 0x18;
        }
    }
    const s32 dy = c.target_y - y;
    const s32 lead = s16(c.lead_y);
    if (dy > lead) {
        y += std::min(dy - lead, s32(c.max_step_y));
    } else if (dy + lead < 0) {
        y += std::max(dy + lead, -s32(c.max_step_y));
    }
    y = clamp_y(y, 0xA0);
    x = clamp_x(x, 0xF0) + c.extra_x;
    y = clamp_y(y, 0xA0) + c.extra_y;
    c.x = s16(x);
    c.y = s16(y);
    scroll_layers(g, x, y);
    switch (c.zone_layer) {
    case 1:
        neo_green_hill_far_layer(g, x);
        break;
    case 2:
        secret_base_1_far_layer(g, x - c.offset_x, y - c.offset_y);
        break;
    case 3:
        banded_far_layer(g, x - c.offset_x, y - c.offset_y);
        break;
    case 4:
        casino_far_layer(g, x, y);
        break;
    case 5:
        ice_far_layer(g, x, y);
        break;
    case 6:
        drifting_front_layer(g);
        break;
    case 7:
        rocket_far_layer(g, x);
        break;
    case 8:
        drifting_bands_far_layer(g, x, y);
        break;
    case 9:
        x_zone_far_layer(g, x, y);
        break;
    case 10:
        moon_far_layer(g);
        break;
    default:
        break;
    }
}

// The camera's process (0803F690, then 0803F6CC).
class CameraProcess final : public Process {
public:
    CameraProcess() : Process(0x0F00) {}

    void update(Game& g) override {
        if (!started_) {
            // 0803F690: the level's layers appear.
            auto& v = g.video().settings;
            if (g.game_mode == 6) {
                v.set_control(v.control() & 0xDFFF);
            }
            v.set_control(v.control() | 0x1E00);
            started_ = true;
            return;
        }
        follow_player(g);
        g.stage().next_rotation_set = 4;
    }

    void on_destroy() override {
        // 0803F628
        if (game_) {
            auto& v = game_->video();
            for (auto& bg : v.settings.bg) {
                bg.scroll_x = bg.scroll_y = 0;
            }
            v.line_effect_on = false;
            // (In act 13 the original also puts back its usual interrupt
            // handler if one was swapped in, flag 03001100 bit 40h; nothing
            // recovered so far swaps it.)
        }
    }

    Game* game_{};

private:
    bool started_{};
};

} // namespace

s32 moon_ground_x(const StageState& s, s32 x) {
    x += s32(s.ticks * 8);
    if (x - 0x48 > 0xA7F) {
        x = remainder(x - 0x48, 0xA80) + 0x48;
    }
    return x;
}

void set_up_act_view(Game& g, s8 zone) {
    StageState& s = g.stage();
    const Assets& a = g.assets();
    if (g.game_mode == 6) {
        throw Unrecovered("the chao hunt backgrounds (0803E0B0)");
    }
    auto& v = g.video().settings;
    // Act 13: mode 1 with window 0; its ground in layer 0, the far picture
    // in layer 1 (layer 2 is turned by the far layer's start).
    const bool moon = zone == 0xD;
    v.set_control(moon ? 0x3741 : 0x3E40);
    v.bg[1].set_control(0x1E01);
    v.bg[2].set_control(0x1F02);
    const u32 p = far_layer_params + u32(zone) * 4;
    const s8 width = a.s8(p), height = a.s8(p + 1), tile_block = a.s8(p + 2),
             map_block = a.s8(p + 3);
    // Size from the far map's width and height (over 32 tiles: wide/tall).
    v.bg[3].set_control(u16((((width + 0x1F) >> 6) | (((height + 0x1F) >> 6) << 1)) << 14 |
                            map_block << 8 | 3 | tile_block << 2));
    if (moon) {
        v.bg[0].set_control(0x1F02);
        v.bg[1].set_control(0x1D0B);
    }
    for (unsigned i = 0; i < 3; ++i) {
        s.layers[i].set_from_template(a, layer_templates[i]);
        s.layers[i].asset = u16(zone * 3 + i);
    }
    Background& far = s.layers[2];
    if (moon) {
        s.layers[1].layer = 0;
        far.layer = 1;
    }
    far.tiles.to = u32(tile_block) * 0x4000;
    far.map_to = u32(map_block) * 0x800;
    far.width = u16(width);
    far.height = u16(height);
    // The level layers' maps are cleared first (rows 0-1F).
    auto& q = g.backgrounds();
    q.clear_value[1] = 0;
    q.clears[1] = {0, 0, 0xFF, 0x20};
    q.clear_value[2] = 0;
    q.clears[2] = {0, 0, 0xFF, 0x20};
    if (zone == 10) {
        // Act 10: the far picture's top 64 rows (the sky up to space) go
        // straight into its map block (1Ah, at once); the layer itself
        // shows the rows from 40h on, drawn into map block 1Ch.
        load_background(a, g.video(), q, far);
        auto& memory = g.video_memory_now().memory;
        for (u32 i = 0; i < 0x1000; ++i) {
            memory[far.map_to + i] = a.u8(far.map_from + i);
        }
        v.bg[3].map_block = 0x1C;
        far.map_to = 0xE000;
        far.source_y = 0x40;
    }
    for (auto& layer : s.layers) {
        // (Act 13 has no front layer.)
        if (!moon || &layer != &s.layers[0]) {
            load_background(a, g.video(), q, layer);
        }
    }
    s.map = StageMap::load(a, a.u32(stage_maps + u32(zone) * 4));
    auto& c = s.camera;
    c.min_y = 0;
    c.max_y = s16(s.map.height);
    c.min_x = 0;
    c.max_x = s16(s.map.width);
    const Player& pl = s.player[0];
    c.x = s16((pl.x >> 8) - 0x78);
    c.y = s16((pl.y >> 8) + c.look_y - 0x54);
    c.max_step_x = 0x10;
    c.max_step_y = 0xC;
    c.shift_x = c.shift_y = 0;
    c.offset_x = c.offset_y = 0;
    c.hold = 0;
    c.dead_zone_x = 8;
    c.lead_y = 0;
    c.look_y = 0;
    c.locks = 0;
    c.parallax_x = c.parallax_y = 0;
    c.extra_x = c.extra_y = 0;
    c.vertical_offset = s16(pl.radius_y - 4);
    c.target_x = c.x;
    c.target_y = c.y;
    auto& cam = g.scheduler().create<CameraProcess>();
    cam.game_ = &g;
    switch (a.u32(far_layer_routines + u32(zone) * 4)) {
    case 0x0803E759:
        c.zone_layer = 1;
        break;
    case 0x0803F821:
        c.zone_layer = 2;
        break;
    case 0x0803F86D:
        c.zone_layer = 3;
        break;
    case 0x0803EA8D:
        c.zone_layer = 4;
        break;
    case 0x0803ED51:
        c.zone_layer = 5;
        break;
    case 0x0803F88D:
        c.zone_layer = 6;
        break;
    case 0x0803EE51:
        c.zone_layer = 7;
        break;
    case 0x0803F3CD:
        c.zone_layer = 8;
        break;
    case 0x0803F775:
        c.zone_layer = 9;
        break;
    case 0x0803F4F9:
        c.zone_layer = 10;
        break;
    default:
        throw Unrecovered("the far background of act " + std::to_string(zone));
    }
    switch (a.u32(far_layer_starts + u32(zone) * 4)) {
    case 0:
        break;
    case 0x0803F6E1:
        start_mist_layer(g);
        break;
    case 0x0803F73D:
        start_front_layer_8(g);
        break;
    case 0x0803F7D5:
        start_moon_layer(g);
        break;
    default:
        throw Unrecovered("the far background's start of act " + std::to_string(zone));
    }
}

} // namespace sonic_advance_gba
