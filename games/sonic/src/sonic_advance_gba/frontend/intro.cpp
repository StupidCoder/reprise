// The intro after the logos (080638E4-08065850): a Chaos Emerald falls
// through a shower of sparkles, searchlights sweep over Dr. Robotnik, then
// Sonic, Tails, Knuckles and Amy are presented with their names sliding in.
// A, B or Start goes to the title screen.

#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/searchlight.hpp"
#include "sonic_advance_gba/game/sprite.hpp"
#include <array>

namespace sonic_advance_gba {

namespace {

constexpr u16 skip_buttons = button::a | button::b | button::start;
constexpr u16 intro_song = 1;

// Tables of the presentation scenes.
constexpr u32 presenter_animations[4] = {0x086842B4, 0x086842BC, 0x086842C4, 0x086842CC};
constexpr u32 sonic_letters = 0x08684188, tails_letters = 0x086841D0;
constexpr u32 knuckles_letters = 0x08684218, amy_letters = 0x08684284;
constexpr u32 emerald_colors = 0x0869EC10;

// The emerald (task 080657E0, priority 2120, 0x44 bytes). The director sets
// its clock every frame.
class Emerald final : public Process {
public:
    explicit Emerald(Game& g) : Process(0x2120) {
        sprite.start(0x10020, 0x306, 0, 0x78, -0x24, 0xC0, 0x1000);
        animate(g, sprite);
    }

    void update(Game& g) override {
        // The fall slows to a stop before frame 90, holds until frame 233,
        // then continues faster.
        if (u16(clock - 0x5A) > 0x8F) {
            y += fall;
        }
        if (clock < 0x5A) {
            fall -= 6;
            if (fall < 0) {
                fall = 0;
            }
        }
        if (clock == 0xE9) {
            fall = 0x140;
        }
        if (y > 0xB400) {
            y = 0xB400;
        }
        sprite.y = s16(u32(y) >> 8);
        animate(g, sprite);
        draw(g, sprite);
    }

    Sprite sprite;          // +00
    s16 clock{};            // +30 set by the director
    s32 unused{};           // +34
    s32 unused_y = -0x2400; // +38
    s32 y = -0x2400;        // +3C 8.8
    s32 fall = 0x240;       // +40
};

// A sparkle around the falling emerald (tasks 08065024 and 08065128,
// priorities 6800-680F). Each falls with the emerald on a spiral and starts
// over from the top every 47 frames; the two sets turn opposite ways.
class Sparkle final : public Process {
public:
    Sparkle(Game& g, u8 index, bool second_set)
        : Process(u16(0x6800 | (index * 2) | (second_set ? 1 : 0))), index_(index),
          second_(second_set) {
        clock_ = second_set ? u16(index * 6) : u16(0x2F - index * 6);
        phase_ = s32(clock_);
        const u32 tiles = second_set ? 0x10BE0 + index * 0x80u : 0x103E0 + index * 0x80u;
        const s16 x = s16(divide(sine(g.assets(), index * 0x40u), 0x28A) + 0x78);
        sprite.start(tiles, 0x307, second_set ? 1 : 0, x, -0x24, second_set ? 0x180 : 0, 0x1000);
        animate(g, sprite);
    }

    void update(Game& g) override {
        const Assets& a = g.assets();
        clock_ = u16(clock_ + 1);
        if (u16(clock_ - 0x5A) > 0x8F) {
            target_ += fall_;
        }
        if (s16(clock_) < 0x5A) {
            fall_ -= 6;
            if (fall_ < 0) {
                fall_ = 0;
            }
        }
        if (s16(clock_) == 0xE9) {
            fall_ = 0x140;
        }
        const s32 bottom = second_ ? 0xD400 : 0xB400;
        if (target_ > bottom) {
            target_ = bottom;
        }
        if (phase_ == 0) {
            sprite.previous_variant = 0xFF;
            phase_ = 0x2F;
            y_ = target_;
        }
        if (!second_) {
            y_ -= divide(sine(a, u32(phase_ * -6 + 0x100) & 0xFF), 0x18);
            sprite.x =
                s16(divide(sine(a, u32(phase_ * 0xC + s16(clock_) * 0x20) & 0x3FF), 0x28A) + 0x78);
        } else {
            y_ -= divide(sine(a, u32(phase_ * 6)), 0x18);
            sprite.x =
                s16(0x78 - divide(sine(a, u32(phase_ * 0xC + s16(clock_) * 0x20) & 0x3FF), 0x28A));
        }
        sprite.y = s16(u32(y_) >> 8);
        --phase_;
        sprite.layer = (index_ & 1) ? 0 : 0x180;
        animate(g, sprite);
        draw(g, sprite);
    }

    Sprite sprite; // +00

private:
    u16 clock_{}; // +30
    u8 index_{};  // +32
    bool second_{};
    s32 phase_{};          // +34 frames to the next start from the top
    s32 y_ = -0x2400;      // +38 8.8
    s32 target_ = -0x2400; // +3C the emerald-like fall the sparkle follows
    s32 fall_ = 0x240;     // +40
};

// A character's picture (tasks 08065224, 080652F4, 08065410, 08065504; 200
// bytes): its first sprite changes animation at two moments taken from a
// table, and the sprite stops (flags cleared) at the end.
class Presenter final : public Process {
public:
    Presenter(Game& g, u8 who, u16 priority) : Process(priority), who_(who) {
        // Sonic and Amy: two sprites, both started; Tails and Knuckles three,
        // only the first started and drawn.
        static constexpr u32 tiles[4][3] = {{0x11440, 0x14000, 0},
                                            {0x10400, 0x11980, 0x13280},
                                            {0x10080, 0x11980, 0x13280},
                                            {0x11440, 0x16400, 0}};
        static constexpr s16 pos[4][2] = {{0xB4, 0x82}, {0x30, 0x6C}, {0x30, 0x82}, {0xB4, 0x6C}};
        static constexpr u16 layers[4] = {0x3C0, 0x300, 0x3C0, 0x180};
        const unsigned count = (who == 0 || who == 3) ? 2 : 3;
        for (unsigned i = 0; i < count; ++i) {
            sprites[i].start(tiles[who][i], 0x301, who, pos[who][0], pos[who][1], layers[who],
                             0x80);
        }
        if (who == 0) {
            animate(g, sprites[0]);
            animate(g, sprites[1]);
        } else if (who == 3) {
            animate(g, sprites[0]);
            animate(g, sprites[1]);
            draw(g, sprites[0]);
        } else {
            animate(g, sprites[0]);
            draw(g, sprites[0]);
        }
    }

    void update(Game& g) override {
        const Assets& a = g.assets();
        const u32 table = presenter_animations[who_];
        Sprite& s = sprites[0];
        const auto change = [&](unsigned which) {
            s.previous_variant = 0xFF;
            s.animation = a.u16(table + which * 4);
            s.variant = u8(a.u16(table + which * 4 + 2));
        };
        const s16 c = clock;
        switch (who_) {
        case 0: // 08065224
            if (c == 0x1E) {
                change(0);
                animate(g, s);
            }
            if (c > 0) {
                if (c > 0x161) {
                    s.flags = 0;
                    animate(g, s);
                } else if (c > 0xBD) {
                    if (c == 0xBE) {
                        change(1);
                    }
                    animate(g, s);
                }
                draw(g, s);
            }
            break;
        case 1: // 080652F4
            if (c == 0x1E) {
                s.tiles.to = 0x10080;
                change(0);
                animate(g, s);
            }
            if (c >= 0) {
                if (c <= 0x1D) {
                    animate(g, s);
                }
                if (c > 0x161) {
                    s.flags = 0;
                    animate(g, s);
                } else if (c > 0xBD) {
                    if (c == 0xBE) {
                        change(1);
                    }
                    animate(g, s);
                }
                draw(g, s);
            }
            break;
        case 2: // 08065410: the tiles go to the same place every frame
            s.tiles.to = 0x10080;
            if (c == 0x1E) {
                change(0);
            }
            if (c >= 0) {
                if (c > 0x161) {
                    s.flags = 0;
                    animate(g, s);
                } else if (c == 0xEB) {
                    change(1);
                }
                animate(g, s);
                draw(g, s);
            }
            break;
        default: // 08065504
            if (c > 0x19A) {
                break;
            }
            if (u16(c) == 0x161) {
                s.flags = 0;
            }
            if (c == 0x1E) {
                change(0);
                animate(g, s);
            }
            if (c >= 0) {
                if (c > 0x161) {
                    s.flags = 0;
                    animate(g, s);
                } else if (c > 0xA4) {
                    if (c == 0xA5) {
                        change(1);
                    }
                    animate(g, s);
                }
                draw(g, s);
            }
            break;
        }
    }

    std::array<Sprite, 3> sprites; // +00, +30, +60
    s16 clock{};                   // +B4 set by the director

private:
    u8 who_{};
};

// A letter of a character's name (tasks 080655F8, 08065670, 080656E8,
// 08065760; priority 6820): from frame 90 on, letter n slides in from the
// right 15n frames after the first.
class NameLetter final : public Process {
public:
    NameLetter(Game& g, u8 who, u32 index, u32 table, s32 x, bool drawn_at_start, u32 flags)
        : Process(0x6820), who_(who), index_(index), x_(x) {
        const Assets& a = g.assets();
        const u32 e = table + index * 12;
        sprite.start(a.u32(e + 8) - 0x06000000, u16(a.u32(e)), u8(a.u32(e + 4)), 0x118, 0x46, 0x180,
                     flags);
        animate(g, sprite);
        if (drawn_at_start) {
            draw(g, sprite);
        }
    }

    void update(Game& g) override {
        static constexpr s32 stop[4] = {0x14, 0x5C, 0x58, 0x1C};
        static constexpr s16 line[4] = {0x70, 0x5A, 0x70, 0x5A};
        clock_ = s16(clock_ + 1);
        if (who_ == 3 && clock_ >= 0x19B) {
            return;
        }
        if (clock_ == 0x161) {
            sprite.flags = 0;
        }
        if (clock_ > 0x5A && s32(index_ * 0xF) < s16(clock_ - 0x5A)) {
            x_ -= 0x10;
            if (x_ < stop[who_]) {
                x_ = stop[who_];
            }
            sprite.x = s16(x_);
            sprite.y = line[who_];
            animate(g, sprite);
            draw(g, sprite);
        }
    }

    Sprite sprite; // +00

private:
    s16 clock_{}; // +30
    u8 who_{};
    u32 index_{}; // +34
    s32 x_{};     // +3C
};

// 08063B58, 08063C18, 08063CD8, 08063D98: a presentation's backgrounds
// (layer 1: the frame, layer 0: the character's backdrop).
void load_presentation_backgrounds(Game& g, unsigned who) {
    struct Parts {
        u32 frame_colors, frame_color_bytes;
        u8 frame_parts;
        u32 tiles, tile_bytes, colors, map;
    };

    static constexpr Parts parts[4] = {
        {0x086AA990, 0x200, 0x15, 0x086ABAD0, 0x1E00, 0x086AB8D0, 0x086AD8D0},
        {0x086AA9B0, 0x20, 0x14, 0x086AE2D0, 0x1B80, 0x086AE0D0, 0x086AFE50},
        {0x086AA9D0, 0x20, 0x14, 0x086B0850, 0x1880, 0x086B0650, 0x086B20D0},
        {0x086AA9F0, 0x20, 0x14, 0x086B2AD0, 0x1840, 0x086B28D0, 0x086B4310},
    };
    const Parts& p = parts[who];
    g.screen_graphics->reset_pool();
    GraphicsLoad frame;
    frame.options = 0x80;
    frame.slot = 0;
    frame.tiles_from = 0x086AAB90;
    frame.tile_bytes = 0x540;
    frame.colors_from = p.frame_colors;
    frame.color_bytes = p.frame_color_bytes;
    frame.map_from = 0x086AB0D0;
    frame.map_bytes = 0x800;
    frame.palette = 0;
    frame.layer = 1;
    frame.parts = p.frame_parts;
    load_graphics(g, frame);
    GraphicsLoad backdrop;
    backdrop.options = 0x80;
    backdrop.slot = 1;
    backdrop.tiles_from = p.tiles;
    backdrop.tile_bytes = p.tile_bytes;
    backdrop.colors_from = p.colors;
    backdrop.color_bytes = 0x200;
    backdrop.map_from = p.map;
    backdrop.map_bytes = 0x800;
    backdrop.palette = 0;
    backdrop.layer = 0;
    backdrop.parts = load::tiles | load::map;
    load_graphics(g, backdrop);
}

// The director (task 08064094, priority 2100, 0x54 bytes).
class IntroDirector final : public Process {
public:
    IntroDirector(Game& g, Emerald& emerald) : Process(0x2100), emerald_(&emerald) {
        fade_ = {0, 1, 2, 0, 0x80, 1};
        step_fade(g.video(), fade_);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::emerald:
            emerald_scene(g);
            break;
        case State::searchlights:
            searchlight_scene(g);
            break;
        case State::sonic:
        case State::tails:
        case State::knuckles:
        case State::amy:
            presentation(g);
            break;
        case State::to_title:
            to_title(g, false);
            break;
        case State::skip:
            to_title(g, true);
            break;
        }
    }

private:
    enum class State { emerald, searchlights, sonic, tails, knuckles, amy, to_title, skip };

    auto& scroll(Game& g, unsigned layer) { return g.video().settings.bg[layer]; }

    bool skipped(Game& g) {
        if (g.controller().pressed_once & skip_buttons) {
            state_ = State::skip;
            return true;
        }
        return false;
    }

    // 08064094: the emerald falls; the backdrop brightens and scrolls.
    void emerald_scene(Game& g) {
        emerald_->clock = clock_;
        if (skipped(g)) {
            return;
        }
        auto& v = g.video();
        const Assets& a = g.assets();
        if (clock_ > 0x97) {
            u32 k = (u32(clock_ - 0x98) & 0x1FFFF) >> 1;
            if (k > 0x28) {
                k = 0x28;
            }
            for (unsigned i = 0; i < 0x20; ++i) {
                const u32 c = a.u16(emerald_colors + 2 * i);
                const u32 r = u32(divide(s32(k * (c & 0x1F)), 0x28));
                const u32 gr = u32(divide(s32(k * ((c & 0x3E0) >> 5)), 0x28));
                const u32 b = u32(divide(s32(k * ((c & 0x7C00) >> 10)), 0x28));
                v.bg_colors[i] = u16((r & 0x1F) | ((gr & 0x1F) << 5) | ((b & 0x1F) << 10));
                v.bg_colors_changed = true;
            }
        }
        if (clock_ > 0xB5) {
            auto& y0 = scroll(g, 0).scroll_y;
            y0 = u16(y0 + 2);
            if (s16(y0) > 100) {
                y0 = 100;
            }
            if (s16(y0) > 0x50) {
                scroll(g, 2).scroll_y = u16(y0 - 0x50);
            }
        }
        scroll(g, 1).scroll_y = u16(scroll(g, 1).scroll_y + 10);
        scene_graphics(g, clock_);
        if (clock_ < 0x140) {
            step_fade(v, fade_);
        } else {
            clock_ = 0;
            state_ = State::searchlights;
        }
        ++clock_;
    }

    // 08063E58: what the emerald scene loads on its first frames.
    void scene_graphics(Game& g, s16 c) {
        auto& v = g.video();
        GraphicsLoad l;
        l.options = 0x80;
        l.palette = 0;
        switch (c) {
        case 0:
            for (u8 i = 0; i < 8; ++i) {
                g.scheduler().create<Sparkle>(g, i, true);
            }
            return;
        case 1:
            v.settings.set_control(0x1240);
            l.slot = 0;
            l.tiles_from = 0x0869EE10;
            l.tile_bytes = 0x2F00;
            l.colors_from = 0x0869EC10;
            l.color_bytes = 0x200;
            l.map_from = 0x086A1D10;
            l.map_bytes = 0x800;
            l.layer = 1;
            l.parts = 0x15;
            break;
        case 2:
            v.settings.set_control(0x1340);
            l.slot = 1;
            l.tiles_from = 0x086A2710;
            l.tile_bytes = 0x2840;
            l.map_from = 0x086A4F50;
            l.map_bytes = 0x8C0;
            l.layer = 0;
            l.parts = 0x11;
            break;
        case 3:
            v.settings.set_control(0x1740);
            l.slot = 0;
            l.tiles_from = 0x086A8E90;
            l.tile_bytes = 0x1440;
            l.map_from = 0x086AA2D0;
            l.map_bytes = 0x6C0;
            l.layer = 2;
            l.parts = 0x11;
            break;
        case 0x136:
            l.slot = 1;
            l.tiles_from = 0x086A5A10;
            l.tile_bytes = 0x2840;
            l.map_from = 0x086A83D0;
            l.map_bytes = 0x8C0;
            l.layer = 0;
            l.parts = 0x11;
            break;
        default:
            return;
        }
        load_graphics(g, l);
    }

    // 08064210: the sparkles go out, Robotnik is lit by two searchlights.
    void searchlight_scene(Game& g) {
        if (skipped(g)) {
            return;
        }
        auto& v = g.video();
        const u32 c = u32(s32(clock_));
        if (c < 0x60) {
            g.scheduler().destroy_range(0x6800, u16((c >> 2) + 0x6802));
        }
        if (c == 0x3C) {
            fade_ = {0, 1, 1, 0, 0x40, 1};
            apply_fade(v, fade_);
        }
        scroll(g, 1).scroll_y = u16(scroll(g, 1).scroll_y + 10);
        const s16 n = ++clock_;
        if (n < 0x1FA) {
            if (n < 0xE1) {
                if (c - 0x1E < 0x5A) {
                    step_fade(v, fade_);
                }
            } else if (n == 0xE1) {
                for (int i = 0; i < 2; ++i) {
                    Searchlight& l = g.scheduler().create<Searchlight>(g.video());
                    l.x = 0x78;
                    l.y = 0xFA;
                    l.angle = 0;
                    l.turn_speed = 0;
                    l.swing = 0;
                    l.window = u8(2 + i);
                    l.width = 6;
                    lights_[i] = &l;
                }
                v.settings.window0 = true;
                v.settings.window1 = true;
                v.settings.inside = {0x1F, 0x1F};
                v.settings.outside = 0x3F;
                v.settings.inside_sprite_window = 0;
                v.settings.blend.set_control(0x3FDF);
                v.settings.blend.evy = 8;
            } else {
                const u32 k = u32(n - 0xE1);
                for (int i = 0; i < 2; ++i) {
                    lights_[i]->angle = u16((0x18 - remainder(s32(k), 0x19)) * 6 + (i ? 0x370 : 0));
                    if (k < 0x40 && remainder(s32(k), 6) == 0) {
                        ++lights_[i]->width;
                    }
                }
            }
            return;
        }
        // The presentations begin.
        g.scheduler().destroy(*emerald_);
        emerald_ = nullptr;
        scroll(g, 1).scroll_x = 0;
        scroll(g, 1).scroll_y = 0;
        pan_x_ = 0x4000;
        pan_y_ = 0;
        clock_ = 0;
        for (auto*& l : lights_) {
            g.scheduler().destroy(*l);
            l = nullptr;
        }
        v.settings.set_control(0x1340);
        v.settings.bg[0].set_control(0x1D86);
        v.settings.bg[1].set_control(0x1883);
        v.settings.blend.set_control(0);
        v.settings.blend.evy = 0;
        presenter_ = &g.scheduler().create<Presenter>(g, 0, 0x2210);
        presenter_->clock = 0;
        for (u32 i = 0; i < 6; ++i) {
            g.scheduler().create<NameLetter>(g, 0, i, sonic_letters, 300, false, 0x80);
        }
        state_ = State::sonic;
    }

    // 080645B0, 080648A0, 08064B80, 08064E50: one character's presentation.
    void presentation(Game& g) {
        const unsigned who = unsigned(state_) - unsigned(State::sonic);
        auto& v = g.video();
        presenter_->clock = clock_;
        // The backdrop pans (8.8): Sonic left/down, Tails right/up, Knuckles
        // down, Amy left/up.
        switch (who) {
        case 0:
            pan_x_ = u16(pan_x_ - 0x20);
            pan_y_ = u16(pan_y_ + 0x20);
            break;
        case 1:
            pan_x_ = u16(pan_x_ + 0x20);
            pan_y_ = u16(pan_y_ - 0x20);
            break;
        case 2:
            pan_y_ = u16(pan_y_ + 0x20);
            break;
        default:
            pan_x_ = u16(pan_x_ - 0x20);
            pan_y_ = u16(pan_y_ - 0x20);
            break;
        }
        scroll(g, 0).scroll_x = u16(pan_x_ >> 8);
        scroll(g, 0).scroll_y = u16(pan_y_ >> 8);
        if (skipped(g)) {
            return;
        }
        const u16 c = u16(clock_);
        bool apply = true;
        if (c < 0x1E) {
            if (c == 1) {
                load_presentation_backgrounds(g, who);
            }
            if (who == 0) {
                fade_ = {0, 1, 2, 0, 0, 1};
            }
        } else if (c <= 0x29) {
            fade_ = {0, 1, 8, 0, 0x100, 1};
        } else {
            if (c < 0x16A) {
                step_fade(v, fade_);
            }
            apply = false;
        }
        if (apply) {
            apply_fade(v, fade_);
        }
        const s16 n = ++clock_;
        if (n < 0x19E) {
            if (n > 0x160) {
                if (n == 0x161) {
                    fade_.kind = 4;
                    fade_.level = 0;
                    fade_.speed = 0x180;
                }
                step_fade(v, fade_);
            }
            return;
        }
        if (who == 3) {
            // 08064E50: the last one fades out into the title screen.
            state_ = State::to_title;
            step_fade(v, fade_);
            return;
        }
        g.scheduler().destroy_range(0x6820, 0x6821);
        static constexpr u16 next_pan[3][2] = {
            {0xE000, 0x6000}, {0xE000, 0x1000}, {0x3000, 0x5000}};
        pan_x_ = next_pan[who][0];
        pan_y_ = next_pan[who][1];
        g.scheduler().destroy(*presenter_);
        clock_ = 0;
        fade_ = {0, 1, 2, 0, 0, 1};
        apply_fade(v, fade_);
        if (who == 0) {
            presenter_ = &g.scheduler().create<Presenter>(g, 1, 0x2280);
            presenter_->clock = 0;
            for (u32 i = 0; i < 6; ++i) {
                g.scheduler().create<NameLetter>(g, 1, i, tails_letters, 0x174, true, 0x80);
            }
            state_ = State::tails;
        } else if (who == 1) {
            presenter_ = &g.scheduler().create<Presenter>(g, 2, 0x2230);
            presenter_->clock = 0;
            for (u32 i = 0; i < 9; ++i) {
                g.scheduler().create<NameLetter>(g, 2, i, knuckles_letters, 0x170, false, 0x40080);
            }
            state_ = State::knuckles;
        } else {
            presenter_ = &g.scheduler().create<Presenter>(g, 3, 0x2230);
            presenter_->clock = 0;
            for (u32 i = 0; i < 4; ++i) {
                g.scheduler().create<NameLetter>(g, 3, i, amy_letters, 0x134, false, 0x40080);
            }
            state_ = State::amy;
        }
    }

    // 08064F6C (the end) and 08064FC4 (skipped): everything goes, the title
    // screen starts.
    void to_title(Game& g, bool skipped) {
        g.clear_screen_processes();
        if (skipped) {
            g.sound().stop(intro_song);
        }
        for (auto& bg : g.video().settings.bg) {
            bg.scroll_x = 0;
            bg.scroll_y = 0;
        }
        g.start_title(skipped);
    }

    State state_ = State::emerald;
    ScreenFade fade_;                      // +00
    Emerald* emerald_;                     // +0C
    std::array<Searchlight*, 2> lights_{}; // +10, +14
    Presenter* presenter_{};               // +18-+24
    s16 clock_{};                          // +4C
    u16 pan_x_{}, pan_y_{};                // +50, +52
};

} // namespace

void Game::start_intro() {
    // 080638E4
    clear_screen_processes();
    ensure_screen_graphics(*this);
    screen_graphics->reset_pool();
    auto& s = video_.settings;
    s.set_control(0x1040);
    s.bg[0].set_control(0x9D86);
    s.bg[1].set_control(0x1B8B);
    s.bg[2].set_control(0x9880);
    for (int i = 0; i < 3; ++i) {
        s.bg[std::size_t(i)].scroll_x = 0;
        s.bg[std::size_t(i)].scroll_y = 0;
    }
    Emerald& emerald = scheduler_.create<Emerald>(*this);
    for (u8 i = 0; i < 8; ++i) {
        scheduler_.create<Sparkle>(*this, i, false);
    }
    scheduler_.create<IntroDirector>(*this, emerald);
    sound_.start(intro_song);
}

} // namespace sonic_advance_gba
