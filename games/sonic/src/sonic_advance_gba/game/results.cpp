// The results after an act (080573A0-08058310): "<character> GOT THROUGH
// ACT n", the ring bonus (100 points a ring) and the time bonus count into
// the score; then the next act starts (0803D6F0).

#include "sonic_advance_gba/game/results.hpp"
#include "sonic_advance_gba/game/ending.hpp"
#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>
#include <array>

namespace sonic_advance_gba {

namespace {

constexpr u32 pictures = 0x08680A38; // 7 words each (see the character select)
constexpr u32 ring_label = 0x08681300, time_label = 0x0868130B; // 11 glyph codes each

// The time bonus by the clock (frames).
u32 time_bonus(u32 clock) {
    if (clock >= 0x5460) {
        return 0;
    }
    if (clock >= 18000) {
        return 500;
    }
    if (clock >= 0x3840) {
        return 1000;
    }
    if (clock >= 0x2A30) {
        return 2000;
    }
    if (clock >= 0x1C20) {
        return 3000;
    }
    if (clock >= 0x1518) {
        return 4000;
    }
    if (clock >= 0xE10) {
        return 5000;
    }
    if (clock >= 3000) {
        return 10000;
    }
    return clock > 0x707 ? 50000 : 80000;
}

SpriteText text_row(u16 layer, s16 x, s16 y, s16 count, u16 slot, u16 palette) {
    SpriteText t;
    t.layer = layer;
    t.x = x;
    t.y = y;
    t.count = count;
    t.slot = slot;
    t.palette = palette;
    t.single_row = true;
    return t;
}

// The parts read the tally's clock (+1C) and when the outro starts (+30).
class ResultsPart : public Process {
public:
    explicit ResultsPart(u32 delay) : Process(0x2120), delay_(delay) {}

    u16 clock{}; // +1C

protected:
    bool leaving() const { return u32(s16(clock)) > delay_ + 0xE6; }

    u32 delay_; // +30
};

// 08057B40: the title ("<character> GOT THROUGH"): slides in from the
// right, rises; it leaves left after the tally (second acts).
class ResultsTitle final : public ResultsPart {
public:
    explicit ResultsTitle(u32 delay) : ResultsPart(delay) {
        text_ = text_row(10, 0xF0, 0x7E, 8, 8, 6);
    }

    void update(Game& g) override {
        const s32 c = s16(clock);
        const s8 zone = g.stage().zone;
        if (leaving()) {
            if ((zone & 1) || zone > 0xB) {
                if (text_.x < -0x20) {
                    text_.x = s16(text_.x + 0x20);
                    if (text_.count != 0) {
                        --text_.count;
                    }
                }
                text_.x = s16(text_.x - 0x1A);
            }
        } else if (c < 0x1A) {
            if (c > 0xF) {
                text_.x = s16(text_.x - 0x1A);
                if (text_.x < -0x10) {
                    text_.x = -0x10;
                }
            }
        } else if (c < 0x24) {
            text_.x = -0x10;
        } else if (c < 0x32) {
            text_.y = s16(text_.y - 6);
        } else {
            text_.count = 9;
            text_.y = 0x2A;
        }
        static const u8 blanks[] = "         ";
        draw_text(g, blanks, text_, u32(clock) > delay_ + 0xF0);
    }

private:
    SpriteText text_;
};

// 08057C08: "ACT n" scrolling in a band from the right.
class ResultsBand final : public ResultsPart {
public:
    explicit ResultsBand(u32 delay) : ResultsPart(delay) {
        text_ = text_row(0, 0x100, 0x80, 9, 2, 0xC);
    }

    void update(Game& g) override {
        const s32 c = s16(clock);
        if (leaving()) {
            text_.x = s16(text_.x - 0x1C);
            if (text_.x <= -0x20) {
                if (text_.count != 0) {
                    --text_.count;
                }
                text_.x = s16(text_.x + 0x20);
                next_glyph();
            }
        } else if (c < 0x1A) {
            if (c < 0x10) {
                return;
            }
            const s16 before = text_.x;
            text_.x = s16(text_.x - 0x1C);
            if (text_.x > 0xEF) {
                return;
            }
            text_.count = s16(9 - (text_.x >> 5));
            if (text_.x < -0x1F) {
                text_.x = s16(before + 4);
                next_glyph();
            }
        } else {
            if (c >= 0x32) {
                text_.y = 0x2C;
            } else if (c >= 0x24) {
                text_.y = s16(text_.y - 6);
            }
            const s16 before = text_.x;
            text_.x = s16(text_.x - 2);
            if (c < 0x24) {
                text_.count = 9;
            }
            if (text_.x <= -0x20) {
                text_.x = s16(before + 0x1E);
                next_glyph();
            }
        }
        static const u8 band[] = " !\"#$%&' !\"#$%&' !\"#$%&'";
        draw_text(g, band + glyph_, text_, false);
    }

private:
    void next_glyph() { glyph_ = u16(remainder(glyph_ + 1, 8)); }

    SpriteText text_;
    u16 glyph_{}; // +1E
};

// 08057CFC (rings) / 08058008 (time): a bonus line: the label's glyphs and
// the number's digits zoom in one after another, then the number counts
// down; after the tally the line slides out left.
class ResultsBonus final : public ResultsPart {
public:
    ResultsBonus(const Assets& a, u32 delay, u32 value, bool time)
        : ResultsPart(delay), value(value), time_(time) {
        for (unsigned i = 0; i < 11; ++i) {
            label_[i] = a.u8((time ? time_label : ring_label) + i);
        }
        text_ = text_row(0, 0x100, row(), 0x10, 4, 6);
        compose(value);
    }

    void update(Game& g) override {
        const u16 c = clock;
        const s32 start = time_ ? 0x3C : 0x5A;
        const s16 s = c <= start ? 0 : s16(c - start);
        if (c > 0x85) {
            compose(value);
        }
        const s16 out = leaving() ? s16(s32(u32((c - 0xE6) - delay_) * 0x1C0000) >> 16) : 0;
        if (s >= (time_ ? 0xB5 : 0x97)) {
            set(4, 10, s16(0x32 - out));
            if (text_.x > -0x51) {
                draw_text(g, string_.data(), text_, false);
            }
            set(0, 1, 0);
            for (unsigned k = 0xB; k < 0x10; ++k) {
                if (string_[k] != '+') {
                    text_.x = s16(k * 8 - (out - 0x32));
                    if (text_.x > -9) {
                        draw_text(g, string_.data() + k, text_, false);
                    }
                }
            }
            return;
        }
        if (s >= 0x2D) {
            set(4, 10, 0x32);
            draw_text(g, string_.data(), text_, false);
            const unsigned k = first_digit();
            set(0, s16(0x10 - k), s16(k * 8 - (out - 0x32)));
            draw_text(g, string_.data() + k, text_, false);
            return;
        }
        const unsigned n = std::min<unsigned>(u8(divide(s, 2)), 0x10);
        if (n == 0) {
            return;
        }
        u8 landed = 0;
        for (unsigned i = 0; i < n; ++i) {
            const s16 t = s16(s - 2 * i);
            text_.slot = i < 0xB ? 4 : 0;
            text_.palette = 6;
            if (t < (time_ ? 0xC : 0xD)) {
                if (i < 0xB || string_[i] != '+') {
                    // Zooming in (rotation sets 14h-1Fh, set by the tally).
                    text_.count = 1;
                    text_.y = s16(divide(t * -0x15 + 0x100, 0x23) + (time_ ? 0x48 : 0x5E));
                    const u8 set = u8(!time_ && t == 0xC ? 0x1F : t + 0x14);
                    text_.x = s16(i * 8 + 0x2E);
                    draw_text_rotated(g, string_.data() + i, text_, set);
                }
            } else {
                ++landed;
            }
        }
        if (landed > 0xB) {
            const unsigned k = first_digit();
            text_.slot = 0;
            text_.palette = 6;
            text_.count = s16(landed - k);
            text_.y = row();
            text_.x = s16(k * 8 + 0x32);
            draw_text(g, string_.data() + k, text_, false);
            landed = 0xB;
        }
        set(4, landed, 0x32);
        draw_text(g, string_.data(), text_, false);
    }

    u32 value; // +18

private:
    s16 row() const { return time_ ? 0x50 : 0x66; }

    void set(u16 slot, s16 count, s16 x) {
        text_.slot = slot;
        text_.palette = 6;
        text_.count = count;
        text_.x = x;
        text_.y = row();
    }

    // The label, then the number right-aligned with '+' (a blank glyph)
    // before it.
    void compose(u32 v) {
        for (s32 i = 0xF; i >= 0; --i) {
            const u32 q = u32(divide(s32(v), 10));
            string_[std::size_t(i)] = u8(v - q * 10 + ' ');
            v = q;
        }
        for (unsigned i = 0xB; i < 0xF && string_[i] == ' '; ++i) {
            string_[i] = '+';
        }
        for (unsigned i = 0; i < 0xB; ++i) {
            string_[i] = label_[i];
        }
    }

    unsigned first_digit() const {
        unsigned k = 0xB;
        while (string_[k] == '+' && k < 0xF) {
            ++k;
        }
        return k;
    }

    std::array<u8, 11> label_{};
    bool time_;
    SpriteText text_;
    std::array<u8, 16> string_{}; // +20
};

// 08057854: the tally (priority 2100). Its tiles are freed with it
// (08058310).
class Results final : public Process {
public:
    Results(Game& g, u32 rings, u32 time, u32 delay, bool special)
        : Process(0x2100), game_(g), delay_(delay), ring_bonus_(rings), time_bonus_(time),
          special_(special) {
        title_ = &g.scheduler().create<ResultsTitle>(delay);
        band_ = &g.scheduler().create<ResultsBand>(delay);
        ring_line_ = &g.scheduler().create<ResultsBonus>(g.assets(), delay, rings, false);
        time_line_ = &g.scheduler().create<ResultsBonus>(g.assets(), delay, time, true);
        fade_ = {0, 0, 1, 0, 0x100, 1};
        tiles_ = g.sprite_tiles.allocate(0x80);
        number_tiles_ = g.sprite_tiles.allocate(0x1E);
        load(g);
    }

    void on_destroy() override {
        game_.sprite_tiles.free(tiles_);
        game_.sprite_tiles.free(number_tiles_);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        for (ResultsPart* p : std::array<ResultsPart*, 4>{title_, time_line_, band_, ring_line_}) {
            p->clock = u16(clock_);
        }
        if (clock_ < 0x87) {
            // The bonus lines' zoom: rotation sets 14h-1Fh, from flat to
            // full height.
            for (unsigned k = 0; k < 0xC; ++k) {
                set_rotation(g.video(), g.assets(), 0, 0x100,
                             s16(k == 0 ? 1 : divide(s32(k) << 8, 0xC)), k + 0x14);
            }
        }
        if (clock_ < 0xB5) {
            ++clock_;
            return;
        }
        if (ring_bonus_ == 0 && time_bonus_ == 0) {
            finish(g, s);
            return;
        }
        if (ring_bonus_ != 0) {
            ring_bonus_ -= 100;
            add_points(g);
            ring_line_->value = ring_bonus_;
        }
        if (time_bonus_ != 0) {
            time_bonus_ -= 100;
            add_points(g);
            time_line_->value = time_bonus_;
        }
        if (ring_bonus_ != 0 || time_bonus_ != 0) {
            if (remainder(s32(s.ticks), 4) == 0) {
                g.sound().start(0x8C);
            }
            ++clock_;
            return;
        }
        // Counted: the player stops looking (act 1) and the wait starts.
        if ((s.zone & 1) == 0) {
            s.player[0].flags &= ~player_flag::keep_state;
        }
        s.camera.max_step_x = s.camera.max_step_y = 0;
        const bool long_wait = s.character == 0 ? (!special_ && s.zone > 0xB) : s.zone == 0xC;
        wait_ = long_wait ? 0x87 : 0x2D;
        g.sound().start(0x8D);
        ++clock_;
    }

private:
    static void add_points(Game& g) {
        StageState& s = g.stage();
        const u32 before = s.score;
        s.score += 100;
        if (divide(s32(s.score), 50000) != divide(s32(before), 50000) && g.game_mode == 0) {
            ++s.lives;
        }
    }

    void finish(Game& g, StageState& s) {
        s.camera.max_step_x = s.camera.max_step_y = 0;
        Player& p = s.player[0];
        if (u32((p.x >> 8) - s.camera.x + 0x40) > 0x170) {
            p.speed_x = 0;
            p.ground_speed = 0;
            if (s.players == 2) {
                throw Unrecovered("the partner after the results (08057854)");
            }
        }
        if (delay_ + 0xF0 < u32(s32(s16(clock_)))) {
            if (u8(s.zone - 0xC) > 1) {
                step_fade(g.video(), fade_);
            }
            if (wait_ == 0 || --wait_ == 0) {
                wait_ = 0;
                const bool special = special_;
                auto& sch = g.scheduler();
                sch.destroy(*ring_line_);
                sch.destroy(*band_);
                sch.destroy(*title_);
                sch.destroy(*time_line_);
                sch.destroy(*this);
                if (s.character == 0) {
                    if (special) {
                        return;
                    }
                    if (s.zone > 0xB) {
                        if (s.zone == 0xD) {
                            return;
                        }
                        start_ending(g);
                        return;
                    }
                } else if (s.zone == 0xC) {
                    start_ending(g);
                    return;
                }
                next_act(g);
                return;
            }
        }
        ++clock_;
    }

    // 08056FB0: the pictures (slot 8 the title, 2 the band and the
    // character's name, 4 the label glyphs).
    void load(Game& g) {
        const Assets& a = g.assets();
        const s8 zone = g.stage().zone;
        GraphicsLoad l;
        l.options = 0x10;
        l.slot = 8;
        l.tiles_from = 0x0865F078;
        l.colors_from = 0x0865F278;
        l.tile_bytes = 0x200;
        l.color_bytes = 0x20;
        l.palette = 6;
        l.tiles_to = 0x11000;
        l.parts = load::tiles | load::colors | load::sprite;
        l.key = 0x10;
        l.extra = {2, 0, 4, 4};
        load_graphics(g, l);
        const auto picture = [&](u8 index) {
            const u32 p = pictures + index * 28u;
            l.options = index;
            l.tiles_from = a.u32(p);
            l.colors_from = a.u32(p + 4);
            l.key = a.u32(p + 8);
            l.extra = {a.u8(p + 12), a.u8(p + 16), a.u8(p + 20), a.u8(p + 24)};
        };
        picture(0x22);
        l.slot = 2;
        l.tiles_from += zone == 0xD ? 0x1E00 : zone == 0xC ? 0x1400 : (zone & 1) == 0 ? 0 : 0xA00;
        l.color_bytes = 0x20;
        l.palette = 0xC;
        l.tile_bytes = 0xA00;
        l.tiles_to = tiles_ + 0x600;
        load_graphics(g, l);
        picture(u8(g.stage().character + 0xB));
        l.slot = 2;
        l.tile_bytes = 0x600;
        l.color_bytes = 0xC;
        l.palette = 0xC;
        l.tiles_to = tiles_;
        load_graphics(g, l);
        picture(0);
        l.slot = 4;
        l.color_bytes = 0x20;
        l.palette = 6;
        const u32 font = l.tiles_from;

        static constexpr struct {
            u32 from, to, bytes;
        } pieces[] = {{0x9C0, 0x240, 0x40}, {0xB40, 0x100, 0xC0}, {0x880, 0x1C0, 0x40},
                      {0x940, 0x200, 0x40}, {0xA40, 0x280, 0x40}, {0x1240, 0x2C0, 0x40},
                      {0xC80, 0, 0x100}};

        for (const auto& piece : pieces) {
            l.tiles_from = font + piece.from;
            l.tiles_to = number_tiles_ + piece.to;
            l.tile_bytes = piece.bytes;
            load_graphics(g, l);
        }
    }

    Game& game_;
    ScreenFade fade_;              // +00
    ResultsBonus* ring_line_{};    // +0C
    ResultsBand* band_{};          // +10
    ResultsTitle* title_{};        // +14
    ResultsBonus* time_line_{};    // +18
    u32 tiles_{}, number_tiles_{}; // +1C, +20
    s32 clock_{};                  // +24
    u32 delay_;                    // +28
    u32 ring_bonus_, time_bonus_;  // +2C, +30
    u16 wait_ = 0x2D;              // +34
    bool special_;                 // +36
};

} // namespace

u32 start_results(Game& g, u16 rings, u32 clock) {
    StageState& s = g.stage();
    const u32 ring_bonus = rings * 100u;
    s.total_rings = u16(s.total_rings + rings);
    s.flags |= stage_flag::title_card;
    if (s.zone == 10) {
        s.camera.locks |= camera_lock::far_still;
    }
    if (g.game_mode == 1) {
        throw Unrecovered("the time attack results (08069C60)");
    }
    SaveData& save = g.save();
    const bool special = s.character == 0 && s.zone > 0xB && save.emeralds == 0x7F &&
                         std::all_of(save.zones_reached.begin(), save.zones_reached.end(),
                                     [](u16 z) { return z > 0xC; }) &&
                         (save.zones_reached[0] != 0xF || s.unknown_5124 != 0xC);
    if (!special) {
        u16& reached = save.zones_reached[s.player[0].character & 3];
        reached = u16(std::max<s32>(s.zone + 1, reached));
    }
    if (s.zone == 10) {
        clock = s.frames <= 0x2A30 ? 1 : u32(s.frames - 0x2A30);
    }
    g.sound().start(s.zone == 0xD ? 0x24 : s.zone == 0xC ? 0x23 : (s.zone & 1) == 0 ? 0x21 : 0x22);
    s.music_off = 0xFF;
    const u32 time = time_bonus(clock);
    const u32 longer = (time <= ring_bonus && ring_bonus != time) ? ring_bonus : time;
    const s32 base = divide(s32(longer), 100);
    const u32 delay = u32(base + 0x4B);
    g.screen_graphics->reset_pool();
    g.scheduler().create<Results>(g, ring_bonus, time, delay, special);
    return u32(base + 0x168);
}

void next_act(Game& g) {
    // 0803D6F0: everything stops, the save is written (its Flash write
    // runs with the interrupts off), the next act starts.
    StageState& s = g.stage();
    g.clear_screen_processes();
    g.sound().stop_all();
    g.sound().restart_output();
    s.zone = s8(s.zone + 1);
    if (s.zone < 0x12) {
        start_act(g);
    }
}

} // namespace sonic_advance_gba
