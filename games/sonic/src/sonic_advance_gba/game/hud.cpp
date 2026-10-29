#include "sonic_advance_gba/game/hud.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <array>

namespace sonic_advance_gba {

namespace {

constexpr u32 pictures = 0x08680A38;        // 7 words each (see title_card.cpp)
constexpr u32 text_ring_panel = 0x08658074; // one glyph
constexpr u32 text_no_rings = 0x08658070;   // the flashing zero, 4 phases

// The clock's tables: multiples of 60 (by seconds), of 3600 (by minutes),
// and two-character strings 00-59 for seconds and for hundredths.
constexpr u32 sixty_multiples = 0x08657FE0, hour_multiples = 0x0865805A;
constexpr u32 hundredths_text = 0x08657EF0, seconds_text = 0x08657F68;

// Glyphs (character - 20h) of the digits picture: 0-9, then ':' (2Ah) and
// the "9" of maxed counters (29h); the same set again 0Bh glyphs on in red.
constexpr u8 digit_zero = 0x20, digit_nine_max = 0x29, colon = 0x2A, red = 0xB;

class Hud final : public Process {
public:
    explicit Hud(u8 leftover) : Process(0x2180), blink_(leftover) {
        time_[1] = colon;
        time_[4] = colon;
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        const u8 panel = a.u8(text_ring_panel);
        // The ring counter's panel, and the lives icon.
        SpriteText t{.layer = 0x18,
                     .x = -2,
                     .y = -1,
                     .count = 1,
                     .slot = 3,
                     .palette = 6,
                     .single_row = true};
        draw_text(g, &panel, t, false);
        if (g.game_mode == 0) {
            t.y = 0x90;
            t.x = 2;
            t.slot = 1;
            t.palette = u16(s16(s8(s.character)));
            draw_text(g, &panel, t, false);
        }
        if (s.rings < 1000) {
            u32 v = s.rings;
            for (int i = 0; i < 3; ++i) {
                const u32 q = u32(divide(s32(v), 10));
                rings_[std::size_t(2 - i)] = u8(v - q * 10 + digit_zero);
                v = q;
            }
        } else {
            rings_.fill(digit_nine_max);
        }
        shown_rings_ = s.rings;
        score_[9] = 0;
        u32 v = s.score;
        for (int i = 8; i >= 0; --i) {
            const u32 q = u32(divide(s32(v), 10));
            score_[std::size_t(i)] = u8(v - q * 10 + digit_zero);
            v = q;
        }
        set_time(g, u32(s.clock));
        lives_ = s.lives > 9 ? digit_nine_max : s.lives == 0 ? u8(0x20) : u8(s.lives + 0x1F);
        draw_counters(g);
        // The clock's warning beeps in its last minute.
        if (g.save().time_limit_off != 1 || g.game_mode != 0) {
            if (!(s.flags & stage_flag::countdown)) {
                if (s.clock > 0x87F0 && remainder(s.clock, 0x3C) == 0 && s.clock < 36000) {
                    g.sound().start(0x8B);
                }
            } else if (u32(s.clock) < 0x4B0 && remainder(s.clock, 0x3C) == 0 && s.clock != 0) {
                g.sound().start(0x8B);
            }
        }
    }

private:
    // 08053A5C: the clock (frames) as m:ss:hh, red and flashing near the
    // limit.
    void set_time(Game& g, u32 clock) {
        const StageState& s = g.stage();
        const Assets& a = g.assets();
        u8 colour = 0;
        if (!(s.flags & stage_flag::countdown) ? s.clock >= 0x7E91 : s.clock <= 0xE0F) {
            if (s.ticks & 0x10) {
                colour = red;
            }
        }
        if (g.save().time_limit_off == 1 && g.game_mode == 0) {
            colour = 0;
        }
        if (clock < 36000) {
            const s32 seconds = divide(s32(clock), 0x3C);
            const s32 minutes2 = divide(seconds, 0x3C) * 2;
            const s32 second2 = (seconds - a.s16(sixty_multiples + u32(minutes2))) * 2;
            const s32 frame2 = ((s32(clock) - a.s16(sixty_multiples + u32(second2))) -
                                a.u16(hour_multiples + u32(minutes2))) *
                               2;
            time_[6] = u8(colour + a.u8(hundredths_text + 1 + u32(frame2)));
            time_[5] = u8(colour + a.u8(hundredths_text + u32(frame2)));
            time_[3] = u8(colour + a.u8(seconds_text + 1 + u32(second2)));
            time_[2] = u8(colour + a.u8(seconds_text + u32(second2)));
            time_[0] = u8(colour + a.u8(seconds_text + 1 + u32(minutes2)));
        } else {
            const u8 nine = u8(colour + digit_nine_max);
            time_[6] = time_[5] = time_[3] = nine;
            time_[2] = u8(colour + 0x25);
            time_[0] = nine;
        }
        time_[1] = u8(colour + colon);
        time_[4] = u8(colour + colon);
    }

    // 08053B78
    void draw_counters(Game& g) {
        const StageState& s = g.stage();
        const Assets& a = g.assets();
        SpriteText t{.layer = 0x10, .y = 5, .slot = 0, .palette = 6, .single_row = true};
        if (s.rings < 10) {
            t.x = 9;
            t.count = 1;
            if (s.rings == 0) {
                const u8 c = a.u8(text_no_rings + (blink_ >> 3));
                draw_text(g, &c, t, false);
                blink_ = u8((blink_ + 1) & 0x1F);
            } else {
                draw_text(g, &rings_[2], t, false);
            }
        } else if (s.rings < 100) {
            t.x = 5;
            t.count = 2;
            draw_text(g, &rings_[1], t, false);
        } else {
            t.x = 1;
            t.count = 3;
            draw_text(g, &rings_[0], t, false);
        }
        t.y = -2;
        unsigned lead = 0;
        while (score_[lead] == 0x20 && ++lead < 10) {
        }
        if (s.score == 0) {
            t.count = 1;
            t.x = 0x49;
            const u8 panel = a.u8(text_ring_panel);
            draw_text(g, &panel, t, false);
        } else {
            t.count = s16(9 - lead);
            t.x = s16(0x51 - t.count * 8);
            draw_text(g, &score_[lead], t, false);
        }
        t.x = 0x19;
        t.y = 0xC;
        t.count = 7;
        draw_text(g, time_.data(), t, false);
        if (g.game_mode == 0) {
            t.x = 0x16;
            t.y = 0x8F;
            t.count = 1;
            draw_text(g, &lives_, t, false);
        }
    }

    std::array<u8, 3> rings_{};  // +00
    std::array<u8, 10> score_{}; // +10
    std::array<u8, 7> time_{};   // +20
    u8 lives_{};                 // +30
    u16 shown_rings_{};          // +44
    // +48: the zero's flashing phase. The original never sets it: it starts
    // with whatever the task memory held. The HUD's memory is the one the
    // title card's second text just freed, whose byte there is the card's
    // "picture shown" flag; the port starts from that flag.
    u8 blink_;
};

} // namespace

// 08053888: the HUD's pictures.
void load_hud_pictures(Game& g) {
    const Assets& a = g.assets();
    const StageState& s = g.stage();
    GraphicsLoad l;
    l.options = 0;
    l.slot = 0;
    l.tiles_from = 0x08659C78; // the red digits
    l.colors_from = 0x0865A078;
    l.tiles_to = 0x10D40;
    l.tile_bytes = 0x2C0;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.parts = load::sprite | load::colors | load::tiles;
    l.key = 2;
    l.extra = {0, 2, 1, 2};
    load_graphics(g, l);

    l.tiles_from = 0x08658478; // the digits
    l.tiles_to = 0x10A80;
    l.parts = load::sprite | load::tiles;
    load_graphics(g, l);

    const u32 e = pictures + (s.character + 1u) * 28;
    l.options = u8(s.character + 1);
    l.slot = 1; // the lives icon
    l.tiles_from = a.u32(e);
    l.colors_from = a.u32(e + 4);
    l.tile_bytes = 0x80;
    l.color_bytes = 0x20;
    l.palette = s.character;
    l.tiles_to = 0x10800;
    l.parts = load::sprite | load::tiles;
    l.key = a.u32(e + 8);
    l.extra = {u8(a.u32(e + 12)), u8(a.u32(e + 16)), u8(a.u32(e + 20)), u8(a.u32(e + 24))};
    load_graphics(g, l);

    l.options = 5;
    l.slot = 3; // the ring panel
    l.tiles_from = 0x0865A318;
    l.colors_from = 0x0865A518;
    l.tile_bytes = 0x200;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.tiles_to = 0x10880;
    l.parts = load::sprite | load::tiles;
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    load_graphics(g, l);
}

void start_hud(Game& g, u8 leftover) {
    g.scheduler().create<Hud>(leftover);
    if (!g.screen_graphics) {
        throw Unrecovered("the HUD without the screen graphics process");
    }
    g.screen_graphics->reset_pool();
    load_hud_pictures(g);
}

} // namespace sonic_advance_gba
