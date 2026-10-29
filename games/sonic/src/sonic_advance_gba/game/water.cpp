#include "sonic_advance_gba/game/water.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

namespace {

// 08006680 with the water's settings: the first 32 lines of the line table
// wave layer 3's scroll (two sines, 2 pixels across and 10 down), the rest
// repeats them.
void wave_far_layer(Game& g, u8 line) {
    StageState& s = g.stage();
    auto& v = g.video();
    const s16 base_x = s16(v.settings.bg[3].scroll_x), base_y = s16(v.settings.bg[3].scroll_y);
    const u32 row = u32(s32(base_y) + line) & 0x1F;
    u32 a1 = (s.ticks * 4 + row * 0x40) & 0x3FF;
    u32 a2 = (s.ticks * 2 + row * 0x20 + 0x100) & 0x3FF;
    v.line_effect_on = true;
    v.line_target = VideoState::LineTarget::scroll;
    v.line_layer = 3;
    v.line_bytes = 4;
    auto& t = v.filling();
    const Assets& a = g.assets();
    for (unsigned n = 0; n < 0x20; ++n) {
        t.put_halfword(n * 4, u16(((2 * sine(a, a1 & 0x3FF)) >> 14) + base_x) & 0x1FF);
        t.put_halfword(n * 4 + 2, u16(((10 * sine(a, a2 & 0x3FF)) >> 14) + base_y) & 0x1FF);
        a1 = (a1 & 0xFFFF) + 0x40;
        a2 = (a2 & 0xFFFF) + 0x20;
    }
    for (unsigned n = 0x20; n < 0xA0; ++n) {
        t.put_halfword(n * 4, t.halfword((n & 0x1F) * 4));
        t.put_halfword(n * 4 + 2, t.halfword((n & 0x1F) * 4 + 2));
    }
}

// 0804C634: the water's process (last of all, FFFEh). The level moves a
// pixel every other frame towards its target; the water's line on the
// screen is worked out for the split; the surface is a row of 17 waves
// (animation 233h, fixed tiles) shown every other frame pair.
class WaterProcess final : public Process {
public:
    explicit WaterProcess(Game& g) : Process(0xFFFE), game_(g) {
        surface_.start(0x12660, 0x233, 0, 0, 0, 0, 0);
        animate(g, surface_);
    }

    // 0804C8DC: the split ends; the underwater colours are cleared.
    void on_destroy() override {
        WaterState& w = game_.stage().water;
        w.process = nullptr;
        w.under_sprites.fill(0);
        std::fill(w.under_bg.begin(), w.under_bg.begin() + 0xE0, u16(0));
        // The ordinary colours come back (the last 0804C92C asks for them
        // to be copied again). (When the screen changes after the game
        // over, the save written in that frame makes it overrun; the
        // original then repeats the last frame's 0804C92C, showing the
        // cleared colours for one picture: not reproduced.)
        auto& v = game_.video();
        v.water.on = false;
        v.bg_colors_changed = v.sprite_colors_changed = true;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        WaterState& w = s.water;
        if ((s.ticks & 1) && s.water_level != w.target) {
            s.water_level = s16(s.water_level + (s.water_level < w.target ? 1 : -1));
        }
        const s16 cy = s.camera.y;
        if (cy < s.water_level) {
            w.line = s.water_level < cy + 0xA0 ? u8(s.water_level - cy) : 0xFF;
        } else {
            w.line = 0;
        }
        auto& v = g.video();
        if (u8(w.shown_line - 1) < 0x9F) {
            surface_.x = s16(-((s32((s.ticks + 1) >> 2) + s.camera.x) & 0xF));
            surface_.y = s16(w.line + 1);
            surface_.flags |= sprite_flag::keep_colors | sprite_flag::keep_tiles;
            animate(g, surface_);
            if ((s.ticks & 2) && u16(surface_.y - 1) < 0xEF) {
                for (int i = 0; i < 17; ++i) {
                    draw(g, surface_);
                    surface_.x = s16(surface_.x + 0x10);
                }
            }
        }
        v.water.on = true;
        v.water.late = first_frame_;
        v.water.line = w.line;
        v.water.bg = w.under_bg;
        v.water.sprites = w.under_sprites;
        if (w.line < 0xA0) {
            wave_far_layer(g, w.line);
            if (w.line != 0) {
                v.line_effect_on = false;
            }
        } else {
            v.line_effect_on = false;
        }
        // 0804C92C (in the vertical blank): this picture's line. (On the
        // act's first frame the callback comes a frame late, together with
        // the next frame's.)
        if (!first_frame_) {
            w.shown_line = w.line;
        }
        first_frame_ = false;
    }

private:
    Game& game_;
    Sprite surface_;          // 03006094
    bool first_frame_ = true; // made at the act's start (see WaterSplit::late)
};

u32 mix(u32 pair, u32 tint) {
    return ((pair & 0x7BDE7BDE) + (((pair & 0x739C739C) + tint) >> 1)) >> 1;
}

void mix_colors(const std::array<u16, 256>& from, std::array<u16, 256>& to, u32 tint) {
    for (unsigned i = 0; i < 256; i += 2) {
        const u32 m = mix(u32(from[i]) | (u32(from[i + 1]) << 16), tint);
        to[i] = u16(m);
        to[i + 1] = u16(m >> 16);
    }
}

} // namespace

void set_water(Game& g, s16 level, u32 tint, u32 options) {
    StageState& s = g.stage();
    WaterState& w = s.water;
    s.water_level = level;
    w.line = 0xFF;
    w.shown_line = 0xFF;
    w.options = u16(options & 0x100);
    w.target = level;
    w.tint = tint;
    if (level >= 0) {
        w.process = &g.scheduler().create<WaterProcess>(g);
    }
}

void build_underwater_colors(Game& g) {
    WaterState& w = g.stage().water;
    const auto& v = g.video();
    const u32 tint = w.tint & 0x739C739C;
    mix_colors(v.sprite_colors, w.under_sprites, tint);
    mix_colors(v.bg_colors, w.under_bg, tint);
}

void load_water_colors(Game& g) {
    Sprite s;
    s.start(TileCopy::nowhere, 0x354, 0, 0, 0, 0, 0);
    animate(g, s);
}

} // namespace sonic_advance_gba
