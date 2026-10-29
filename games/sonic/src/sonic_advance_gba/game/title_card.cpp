// The act's title card (080550C4-08055C18): six pieces of sprite text slide
// in over a fade from black, stay for a moment and slide out; then the
// player gets control and the HUD appears.

#include "sonic_advance_gba/game/title_card.hpp"
#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/hud.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/moon_zone.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include "sonic_advance_gba/game/water.hpp"
#include <vector>

namespace sonic_advance_gba {

namespace {

// Sprite pictures (7 words each: tiles, colours, tiles per glyph, entry
// size, shape, glyph width, height).
constexpr u32 pictures = 0x08680A38;
constexpr u32 name_glyphs = 0x08681274;   // per act: glyphs of the act's name part
constexpr u32 name_first = 0x08681282;    // per act: its first glyph
constexpr u32 zone_pictures = 0x08681294; // per act: the zone name picture (+13h)

// The texts' characters.
constexpr u32 text_banner = 0x086812A8, text_letters = 0x08681290, text_act = 0x086812AC;
constexpr u32 text_zone = 0x086812B4, text_number = 0x086812C0, text_name = 0x086812C8;

std::vector<u8> rom_text(const Assets& a, u32 at, std::size_t n) {
    std::vector<u8> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = a.u8(at + u32(i));
    }
    return v;
}

// A piece of the card (the six tasks at 2120, 0x24 bytes each).
class CardText : public Process {
public:
    CardText(Game& g, u32 tile_count) : Process(0x2120), game_(g) {
        if (tile_count) {
            tiles_ = g.sprite_tiles.allocate(tile_count);
        }
    }

    void on_destroy() override {
        if (tiles_) {
            game_.sprite_tiles.free(tiles_); // 08055C04
        }
    }

    s16 timer{};    // +18 the card's frame (set by the controller)
    bool shown{};   // +20 the fade has reached the full picture
    bool skipped{}; // +21

    u32 tiles() const { return tiles_; }

protected:
    // Half transparent while the picture fades in, or when skipped.
    bool faint() const { return u16(timer) < 0x6A || !shown || skipped; }

    void draw(Game& g, const u8* chars) { draw_text(g, chars, text, faint()); }

    SpriteText text;
    u16 aux{}; // +1A
    Game& game_;
    u32 tiles_{}; // +1C
};

// 080556FC: the coloured banner on the left: in from the left, out the
// same way.
class Banner final : public CardText {
public:
    Banner(Game& g) : CardText(g, 0x20) {
        text = {
            .layer = 0x18, .x = -0x50, .y = 0, .count = 3, .slot = 6, .palette = 0xF, .rows = 5};
        chars_ = rom_text(g.assets(), text_banner, 3);
    }

    void update(Game& g) override {
        if (timer >= 0xD8) {
            text.x = s16(text.x - 8);
        } else if (timer >= 0xB) {
            text.x = 0;
        } else if (timer >= 1) {
            text.x = s16(text.x + 8);
        }
        draw(g, chars_.data());
    }

private:
    std::vector<u8> chars_;
};

// 08055764: four letters of the banner, dropping in one after another.
class BannerLetters final : public CardText {
public:
    BannerLetters(Game& g) : CardText(g, 0) {
        text = {
            .layer = 0x10, .x = 0x14, .count = 1, .slot = 7, .palette = 0xF, .single_row = true};
        chars_ = rom_text(g.assets(), text_letters, 4);
    }

    void update(Game& g) override {
        if (timer < 0xD8) {
            if (timer >= 0x20) {
                for (int i = 0; i < 4; ++i) {
                    text.y = s16(i << 5);
                    draw(g, &chars_[std::size_t(i)]);
                }
            } else if (timer > 0x19) {
                const s16 s = s16(divide((u16(timer) - 0xD7) * 0x80, 6));
                const u16 start = u16((s - 0xB0) & 0x1FF);
                if (start > 0x1A5) {
                    u16 y = start;
                    for (int i = 0; i < 4; ++i, y = u16(y + 0x20)) {
                        text.y = s16(y);
                        draw(g, &chars_[std::size_t(i)]);
                    }
                }
            }
            return;
        }
        const s32 s = divide((u16(timer) - 0xD7) * 0x80, 10);
        s32 y = -s16(s);
        s32 shifted = s * -0x10000;
        for (int i = 0; i < 4; ++i, y += 0x20, shifted += 0x200000) {
            text.y = s16(y);
            if ((shifted >> 16) > -0x21) {
                draw(g, &chars_[std::size_t(i)]);
            }
        }
    }

private:
    std::vector<u8> chars_;
};

// 080558D0: "ACT" rising from the bottom (acts up to 0Bh).
class ActLabel final : public CardText {
public:
    ActLabel(Game& g) : CardText(g, 0xC) {
        text = {.layer = 0,
                .x = 0xB4,
                .y = 0xA0,
                .count = 6,
                .slot = 0xA,
                .palette = 6,
                .single_row = true};
        chars_ = rom_text(g.assets(), text_act, 6);
    }

    void update(Game& g) override {
        if (timer < 0xD8) {
            if (timer > 0x23) {
                text.x = 0xB4;
                text.y = 0x90;
                text.count = 6;
            } else if (timer >= 0x1F && text.y >= 0x91) {
                text.y = s16(text.y - 3);
            }
        } else if (text.y <= 0x9F) {
            text.y = s16(text.y + 2);
        }
        if (g.stage().zone < 0xC) {
            draw(g, chars_.data());
        }
    }

private:
    std::vector<u8> chars_;
};

// 08055964: "ZONE".
class ZoneLabel final : public CardText {
public:
    ZoneLabel(Game& g) : CardText(g, 0x10) {
        const StageState& s = g.stage();
        const bool first_act_shown =
            (s.zone & 1) == 0 || s.previous_zone == s.zone || s.zone == 0xD;
        text = {.layer = 8,
                .x = s16(first_act_shown ? 0xF0 : -0x10),
                .y = s16(first_act_shown ? 0x7E : 0x2A),
                .count = 8,
                .slot = 8,
                .palette = 6,
                .single_row = true};
        chars_ = rom_text(g.assets(), text_zone, 8);
    }

    void update(Game& g) override {
        const StageState& s = g.stage();
        if (timer < 0xD8) {
            if (timer >= 0x1A) {
                text.x = -0x10;
            } else if (timer >= 0x10) {
                if (s.zone == 0xD) {
                    text.y = 0x7E;
                    text.x = s16(text.x - 0x1A);
                    if (text.x < -0x10) {
                        text.x = -0x10;
                    }
                } else if (s.zone < 0xD && (s.zone & 1) && s.previous_zone != s.zone) {
                    text.x = -0x10;
                    text.y = s16(text.y + 9);
                    if (text.y > 0x7E) {
                        text.y = 0x7E;
                    }
                } else {
                    text.y = 0x7E;
                    text.x = s16(text.x - 0x1A);
                    if (text.x < -0x10) {
                        text.x = -0x10;
                    }
                }
            }
        } else {
            if (text.x < -0x20) {
                text.x = s16(text.x + 0x20);
                if (text.count != 0) {
                    --text.count;
                }
            }
            text.x = s16(text.x - 0x1A);
        }
        draw(g, chars_.data());
    }

private:
    std::vector<u8> chars_;
};

// 08055A6C: the act number.
class ActNumber final : public CardText {
public:
    ActNumber(Game& g) : CardText(g, 0x10) {
        text = {.layer = 0,
                .x = 0x168,
                .y = 0x72,
                .count = 4,
                .slot = 9,
                .palette = 6,
                .single_row = true};
        chars_ = rom_text(g.assets(), text_number, 4);
    }

    void update(Game& g) override {
        if (timer < 0xD8) {
            bool stay = timer < 0x1A && u16(timer) < 0x10;
            if (timer < 0x1A && !stay) {
                text.x = s16(text.x - 0x1A);
                stay = text.x > 0x6F;
            }
            if (!stay) {
                text.x = 0x70;
            }
        } else {
            text.x = s16(text.x - 0x1A);
        }
        draw(g, chars_.data());
    }

private:
    std::vector<u8> chars_;
};

// 08055AE4: the zone's name scrolling through, its letters cycling.
class ZoneName final : public CardText {
public:
    ZoneName(Game& g) : CardText(g, 0x38) {
        text = {.layer = 0,
                .x = 0x100,
                .y = 0x80,
                .count = 9,
                .slot = 2,
                .palette = 6,
                .single_row = true};
        chars_ = rom_text(g.assets(), text_name, 0x16);
        chars_.resize(0x16 + 5, 0);
    }

    void update(Game& g) override {
        auto next_letter = [&] {
            aux = u16(remainder(aux + 1, 7));
        };
        if (timer < 0xD8) {
            if (timer < 0x1A) {
                if (timer < 0x10) {
                    return;
                }
                text.y = 0x80;
                const s16 old = text.x;
                text.x = s16(old - 0x1C);
                if (text.x > 0xEF) {
                    return;
                }
                text.count = s16(9 - (text.x >> 5));
                if (text.x < -0x1F) {
                    text.x = s16(old + 4);
                    next_letter();
                }
            } else {
                const s16 old = text.x;
                text.x = s16(old - 2);
                text.count = 9;
                if (text.x < -0x1F) {
                    text.x = s16(old + 0x1E);
                    next_letter();
                }
            }
        } else {
            const s16 old = text.x;
            text.x = s16(old - 0x1C);
            if (text.x < -0x1F) {
                --text.count;
                text.x = s16(old + 4);
                next_letter();
            }
        }
        draw(g, chars_.data() + aux);
    }

private:
    std::vector<u8> chars_;
};

} // namespace

// 08055424: the card's clock, the fade, skipping, and the end.
class TitleCard final : public Process {
public:
    TitleCard(std::array<CardText*, 6> texts) : Process(0x2100), texts_(texts) {
        fade_ = {.delay = 0, .window = 0, .kind = 2, .level = 0, .speed = 0x60, .blend = 1};
    }

    ScreenFade& fade() { return fade_; }

    void leave_blending() { skipped_ = true; }

    void update(Game& g) override {
        StageState& s = g.stage();
        if ((g.controller().pressed_once & 0xF3) && g.game_mode < 2 && u16(timer_ - 0x24) < 0xB3) {
            // A button skips to the slide out.
            timer_ = 0xD7;
            fade_.speed = s16(divide(0x2000 - fade_.level, 10));
            if (!skipped_) {
                shown_ = step_fade(g.video(), fade_);
            }
        }
        for (CardText* t : texts_) {
            t->timer = timer_;
            t->shown = shown_;
            t->skipped = skipped_;
        }
        if (!skipped_ && (timer_ < 0x6A || !shown_)) {
            if (timer_ < 0x15) {
                apply_fade(g.video(), fade_);
            } else {
                shown_ = step_fade(g.video(), fade_);
            }
        }
        ++timer_;
        if (timer_ < 0xE2) {
            if (u8(s.zone - 6) < 2) {
                build_underwater_colors(g);
            }
            return;
        }
        if (!skipped_) {
            auto& v = g.video().settings;
            v.set_control(v.control() & 0x1FFF);
            v.blend.set_control(0);
            v.blend.evy = 0;
        }
        if (g.game_mode >= 2) {
            throw Unrecovered("the title card's end in multiplayer (08055424)");
        }
        s.player[0].flags &= ~player_flag::frozen;
        s.player[1].flags &= ~player_flag::frozen;
        s.previous_zone = s.zone;
        s.player[0].power &= 0x7F;
        s.flags &= ~u16(stage_flag::paused | stage_flag::title_card);
        if (u8(s.zone - 6) < 2) {
            load_water_colors(g);
            build_underwater_colors(g);
        }
        for (int i = 5; i >= 0; --i) {
            g.scheduler().destroy(*texts_[std::size_t(i)]);
        }
        g.scheduler().destroy(*this);
        if (g.game_mode < 4) {
            start_hud(g, shown_ ? 1 : 0);
        } else {
            throw Unrecovered("the multiplayer HUD (0801B544)");
        }
        if (s.zone == 0xD) {
            start_warning(g, u16(priority() + 1));
        }
    }

private:
    ScreenFade fade_;                // +00
    std::array<CardText*, 6> texts_; // +0C
    s16 timer_{};                    // +24
    bool shown_ = true;              // +26
    bool skipped_{};                 // +27 the blending is another effect's
};

namespace {

// 08054A4C: the card's pictures. `tiles`: the tile memory of the texts
// (zone label, banner, act number, zone name, act label).
void load_card_pictures(Game& g, u32 zone_tiles, u32 banner_tiles, u32 number_tiles, u32 name_tiles,
                        u32 act_tiles) {
    const Assets& a = g.assets();
    const StageState& s = g.stage();
    const auto picture = [&](GraphicsLoad& l, unsigned n) {
        const u32 e = pictures + n * 28;
        l.tiles_from = a.u32(e);
        l.colors_from = a.u32(e + 4);
        l.key = a.u32(e + 8);
        l.extra = {u8(a.u32(e + 12)), u8(a.u32(e + 16)), u8(a.u32(e + 20)), u8(a.u32(e + 24))};
    };
    const u8 sprite_with_colors = load::sprite | load::colors | load::tiles;
    GraphicsLoad l;
    l.slot = 6;
    picture(l, s.character + 6u);
    l.tile_bytes = 0x400;
    l.color_bytes = 0x20;
    l.palette = 0xF;
    l.tiles_to = banner_tiles;
    l.parts = sprite_with_colors;
    load_graphics(g, l);

    l.slot = 7;
    l.tiles_from = 0x0865B5B8 + s.character * 0x800u;
    l.colors_from = 0x0865D5B8;
    l.tile_bytes = 0x800;
    l.color_bytes = 0x20;
    l.palette = 0xF;
    l.tiles_to = 0x10800;
    l.parts = sprite_with_colors;
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    load_graphics(g, l);

    l.slot = 8;
    l.tiles_from = 0x0865F078;
    l.colors_from = 0x0865F278;
    l.tile_bytes = 0x200;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.tiles_to = zone_tiles;
    l.parts = sprite_with_colors;
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    load_graphics(g, l);

    if (s.zone < 0xE) {
        // The act number's glyphs: the act's own part between fixed ones.
        const u32 own = a.u8(name_glyphs + u32(s.zone));
        const u32 first = a.u8(name_first + u32(s.zone));
        const auto glyphs = [&](u32 from, u32 bytes, u32 to) {
            l.slot = 9;
            l.tiles_from = from;
            l.colors_from = 0x0865FD18;
            l.tile_bytes = bytes;
            l.color_bytes = 0x20;
            l.palette = 6;
            l.tiles_to = to;
            l.parts = sprite_with_colors;
            l.key = 1;
            l.extra = {0, 0, 1, 1};
            load_graphics(g, l);
        };
        glyphs(0x0865FBB8, (0xC - own) * 0x20, number_tiles);
        glyphs(0x0865F398, 0x80, number_tiles + 0x180);
        glyphs(0x0865F298 + first * 0x20, own << 5, number_tiles + (0xC - own) * 0x20);
        glyphs(0x0865F398, 0x20, number_tiles);
        set_slot_layout(g, 9, 4, {1, 1, 4, 1});
    } else {
        throw Unrecovered("the title card of the extra acts (08054A4C)");
    }

    l.slot = 10;
    l.tiles_from = 0x0865EF58 + (s.zone & 1) * 0x80u;
    l.tile_bytes = 0x80;
    l.tiles_to = act_tiles + 0x100;
    l.parts = load::sprite | load::tiles;
    l.key = 2;
    l.extra = {0, 2, 1, 2};
    load_graphics(g, l);

    l.slot = 10;
    l.tiles_from = 0x0865EE58;
    l.colors_from = 0x0865F058;
    l.tile_bytes = 0x100;
    l.tiles_to = act_tiles;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.parts = sprite_with_colors;
    l.key = 2;
    l.extra = {0, 2, 1, 2};
    load_graphics(g, l);

    l.slot = 2;
    picture(l, a.u8(zone_pictures + u32(s.zone)) + 0x13u);
    l.colors_from = 0x08660C58;
    l.tile_bytes = 0x700;
    l.color_bytes = 0x20;
    l.palette = 6;
    l.tiles_to = name_tiles;
    l.parts = sprite_with_colors;
    load_graphics(g, l);
    set_slot_layout(g, 2, 8, {2, 1, 4, 2});
}

} // namespace

void leave_blending_to_others(TitleCard& card) {
    card.leave_blending();
}

TitleCard& start_title_card(Game& g) {
    const StageState& s = g.stage();
    if (s.zone < 0xD && (s.zone & 1) && s.previous_zone != s.zone) {
        // A second act: a bar of eight entries covers the top for this frame.
        for (u16 i = 0; i < 8; ++i) {
            gba::Sprite e;
            e.set_attributes(0x042A, u16(i * 0x20 - 0x8000), 0x6080);
            if (!queue_sprite(g.video(), 1, e)) {
                break;
            }
        }
    }
    ensure_screen_graphics(g);
    g.screen_graphics->reset_pool();
    auto& sched = g.scheduler();
    auto& banner = sched.create<Banner>(g);
    auto& letters = sched.create<BannerLetters>(g);
    auto& act = sched.create<ActLabel>(g);
    auto& zone = sched.create<ZoneLabel>(g);
    auto& number = sched.create<ActNumber>(g);
    auto& name = sched.create<ZoneName>(g);
    auto& card = sched.create<TitleCard>(
        std::array<CardText*, 6>{&banner, &letters, &zone, &number, &act, &name});
    load_card_pictures(g, zone.tiles(), banner.tiles(), number.tiles(), name.tiles(), act.tiles());
    step_fade(g.video(), card.fade());
    return card;
}

} // namespace sonic_advance_gba
