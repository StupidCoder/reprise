// The character select (080597A8-0805B950): the four characters stand on a
// turning ring; Left and Right turn it, A chooses, B goes back to the main
// menu. A banner scrolls "SELECT A CHARACTER" across the top, the
// character's name and a coloured band rise from the bottom, and the ring
// behind is a sprite animation drawn into background layer 1's map.
//
// The screen is a main process and six part processes. The parts start
// idle (the original's empty task routine 0805B94C holding their data) and
// the main process moves them from one phase to the next.

#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/graphics.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/sprite.hpp"
#include <array>

namespace sonic_advance_gba {

namespace {

constexpr u16 select_song = 3;
constexpr u16 sound_turn = 0x67, sound_choose = 0x6A, sound_back = 0x6B, sound_tails_follows = 0x75;

// Pictures (7 words each: tiles, colours, then the slot's glyph layout:
// tiles per glyph and four layout bytes).
constexpr u32 pictures = 0x08680A38;
constexpr u32 band_tiles = 0x08670B78;
constexpr u32 band_colors = 0x08670F78; // per character (6 bytes apart, 8 read)
constexpr u32 name_tiles = 0x086713B8;  // per character, 0xC00 bytes
constexpr u32 chosen_banner_tiles = 0x08674D18;

// Tables of the screen.
constexpr u32 button_code = 0x08681514;    // halfwords, ending with 0
constexpr u32 ring_variants = 0x08681474;  // by quarter of the turn between two characters
constexpr u32 fade_delays = 0x08681478;    // by character: frames before the fade out
constexpr u32 name_x = 0x0868146C;         // by character
constexpr u32 name_sway = 0x086814CA;      // 16 halfwords: x offsets while turning
constexpr u32 arrow_left_x = 0x086814EA;   // 7 bytes, then the right arrow's 7
constexpr u32 carousel_order = 0x086814FE; // 2 x 3 bytes: neighbours while turning
constexpr u32 band_rows = 0x08681504;      // 2 rows of 8 glyphs

const u8 banner_text[] = " !\"#$%&' !\"#$%&' !\"#$%&'";
const u8 chosen_banner_text[] = " ! ! ! ! ! ! ! ! ! ! ! !";
const u8 spaces[] = "        ";
const u8 eight_glyphs[] = " !\"#$%&'";
const u8 left_arrow[] = " ", right_arrow[] = "!";

// The loader record for the screen's sprite pictures.
GraphicsLoad sprite_picture(const Assets& a, u8 slot, u8 picture, u32 tiles_to, u32 tile_bytes,
                            u8 parts) {
    GraphicsLoad l;
    const u32 p = pictures + picture * 28u;
    l.tiles_from = a.u32(p);
    l.colors_from = a.u32(p + 4);
    l.key = a.u32(p + 8);
    l.extra = {a.u8(p + 12), a.u8(p + 16), a.u8(p + 20), a.u8(p + 24)};
    l.tiles_to = tiles_to;
    l.tile_bytes = tile_bytes;
    l.color_bytes = 0x20;
    l.palette = 0;
    l.parts = parts;
    l.slot = slot;
    l.options = picture;
    return l;
}

// The band's colours (slot 0's palette 15) for `character`.
void load_band_colors(Game& g, unsigned character, bool with_tiles) {
    GraphicsLoad l;
    l.options = 0x27;
    l.slot = 0;
    l.colors_from = band_colors + character * 6u;
    l.tiles_to = 0x10020;
    l.color_bytes = 8;
    l.palette = 0xF;
    l.key = 0x10;
    l.extra = {2, 0, 4, 4};
    l.parts = with_tiles ? load::tiles | load::colors | load::sprite : load::colors | load::sprite;
    if (with_tiles) {
        l.tiles_from = band_tiles;
        l.tile_bytes = 0x400;
    }
    load_graphics(g, l);
}

// The rise of a part from below: s8 of the sine's high byte.
s16 sine_high(const Assets& a, s32 index) {
    return s8(u16(sine(a, unsigned(index))) >> 8);
}

// A single row of sprite text.
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

enum class Phase { waiting, entering, idle, chosen };

// The ring behind the characters (0x34 bytes, priority 2020): animation 2D3
// written into layer 1's map, its variant by how far the turn has gone.
class Ring final : public Process {
public:
    explicit Ring(Game& g) : Process(0x2020) {
        sprite_.tiles.to = 0x40 + g.video().settings.bg[1].tile_block * 0x4000u;
        sprite_.tiles.bytes = 0;
        sprite_.flags = 0x8000; // layer 1
        sprite_.x = sprite_.y = 0;
        sprite_.layer = 0;
        sprite_.timer = 0;
        sprite_.previous_variant = 0xFF;
        sprite_.speed = 0x10;
        sprite_.palette = 0;
        sprite_.animation = 0x2D3;
        sprite_.variant = 0;
        animate_background(g, sprite_);
        draw_into_map(g, sprite_);
    }

    // 0805B950
    void update(Game& g) override {
        sprite_.previous_variant = 0xFF;
        sprite_.animation = 0x2D3;
        sprite_.variant = g.assets().u8(ring_variants + ((angle & 0xFF) >> 6));
        animate_background(g, sprite_);
        draw_into_map(g, sprite_);
    }

    u16 angle{}; // +32

private:
    Sprite sprite_; // +00
};

// The characters on the ring (0xCC bytes, priority 2010): the one in front
// and its neighbours left and right, as variants of animation 2D2.
class Carousel final : public Process {
public:
    explicit Carousel(Game& g) : Process(0x2010) {
        sprites_[1].start(0x12520, 0x2D2, 0, 0x118, 0x6E, 0x7C0, 0);
        sprites_[0].start(0x13920, 0x2D2, 6, -50, 0x6E, 0x7C0, 0);
        sprites_[2].start(0x14D20, 0x2D2, 2, 0x122, 0x6E, 0x7C0, 0);
        lock_.start(0x16120, 0x2DA, 0, 0x122, 0x6E, 0x700, 0);
        animate(g, lock_);
    }

    void update(Game& g) override {
        switch (phase) {
        case Phase::waiting:
            break;
        case Phase::entering:
            enter(g);
            break;
        case Phase::idle:
            idle(g);
            break;
        case Phase::chosen:
            chosen(g);
            break;
        }
    }

    Phase phase = Phase::waiting;
    u16 angle{};                    // +C4
    u8 turning{};                   // +C6 1: to the next character, 2: to the previous
    u8 locked{};                    // +C8 characters that cannot be chosen (multiplayer)
    std::array<Sprite, 3> sprites_; // +00 left, +30 front, +60 right

private:
    unsigned character() const { return (angle & 0x300) >> 8; }

    void animate_and_draw(Game& g) {
        for (Sprite& s : sprites_) {
            animate(g, s);
            draw(g, s);
        }
    }

    // 0805B1AC: the characters slide in; window 1 opens on the right.
    void enter(Game& g) {
        const unsigned c = character();
        const u32 before = clock_++;
        auto& w = g.video().settings.windows[1];
        if (clock_ > 0x11) {
            const s32 i = s32(before) - 0x10;
            sprites_[1].x = s16(0x100 - divide(i * 0x80, 0xB));
            sprites_[2].x = s16(0x11C - divide(i * 0x20, 0xB));
            sprites_[0].x = s16(divide(i * 0x20, 0xB) - 0x20);
            sprites_[1].y = 0x6E;
            if (i < 5) {
                w.left = 0xF0;
                w.right = 0xF0;
            } else {
                w.left = u8((s16(before) - 0x15) * -5 + 0xF0);
                w.right = 0xF0;
            }
        }
        if (clock_ > 0x1C) {
            clock_ = 0;
            w.left = 0xC8;
            w.right = 0xF0;
            phase = Phase::idle;
        }
        sprites_[1].variant = u8(c << 1);
        sprites_[2].variant = u8(((c + 1) & 3) << 1);
        sprites_[0].variant = u8(((c + 3) & 3) << 1);
        animate_and_draw(g);
    }

    // 0805B2F0: standing, or turning: the three slide by 16 pixels a
    // frame, and a quarter of the way the next character takes a place.
    void idle(Game& g) {
        const Assets& a = g.assets();
        const u8 step = u8(angle & 0xFF) >> 4;
        const unsigned c = character();
        s16 progress = step;
        s32 sign = 0;
        if (turning == 0) {
            sprites_[0].x = -8;
            sprites_[1].x = 0x78;
            sprites_[2].x = 0xF8;
        } else {
            const bool back = (turning & 1) == 0;
            sign = back ? 1 : -1;
            progress = back ? s16(0x10 - step) : s16(step);
            const s16 move = s16(sign * 0x100);
            if (progress == 4) {
                // The table lists front, right, left.
                const unsigned at = back ? 0 : 3;
                sprites_[1].variant = u8(((a.u8(carousel_order + at) + c) & 3) << 1);
                sprites_[2].variant = u8(((a.u8(carousel_order + at + 1) + c) & 3) << 1);
                sprites_[0].variant = u8(((a.u8(carousel_order + at + 2) + c) & 3) << 1);
                for (Sprite& s : sprites_) {
                    s.x = s16(s.x - (move >> 1));
                }
            }
            if (progress < 9) {
                for (Sprite& s : sprites_) {
                    s.x = s16(s.x + (move >> 4));
                }
            }
        }
        // Which character stands where for the locks.
        s32 shift = 0;
        if (sign == 1) {
            shift = progress < 4 ? 1 : 0;
        } else if (sign == -1) {
            shift = progress < 4 ? 0 : 1;
        }
        for (unsigned i = 0; i < 3; ++i) {
            if ((locked >> ((shift - 1 + s32(i) + s32(c)) & 3)) & 1) {
                lock_.x = sprites_[i].x;
                lock_.y = sprites_[i].y;
                draw(g, lock_);
            }
            animate(g, sprites_[i]);
            draw(g, sprites_[i]);
        }
    }

    // 0805B88C: the neighbours move away.
    void chosen(Game& g) {
        if (clock_ < 5) {
            sprites_[0].x = s16(sprites_[0].x - 0x14);
            sprites_[2].x = s16(sprites_[2].x + 0x14);
        }
        ++clock_;
        animate_and_draw(g);
    }

    Sprite lock_; // +90
    u32 clock_{}; // +C0
};

// The coloured band at the bottom (0x20 bytes, priority 2030): two rows of
// eight 32-pixel glyphs (slot 0) in the character's colours, rising in;
// while the ring turns it dips and takes the next character's colours.
class Band final : public Process {
public:
    Band(u16 angle_) : Process(0x2030), angle(angle_), last_(angle_) {
        text_ = text_row(0x5A, 0x100, 0x40, 8, 0, 0xF);
    }

    void update(Game& g) override {
        const Assets& a = g.assets();
        switch (phase) {
        case Phase::waiting:
            return;
        case Phase::entering: {
            // 0805B4F8
            const s16 i = s16(++clock_);
            if (i < 0x22) {
                if (i < 0xE) {
                    if (i < 0xB) {
                        text_.x = s16(0x100 - divide(i << 8, 10));
                        if (i == 1) {
                            load_band_colors(g, (angle & 0x300) >> 8, false);
                        }
                    } else {
                        text_.x = 0;
                    }
                } else {
                    text_.y = s16(sine_high(a, (divide(i * 0x67, 0x6E) - 10) * 0x11) + 0x40);
                }
            } else {
                clock_ = 0;
                text_.y = 0x70;
                phase = Phase::idle;
            }
            draw_rows(g, false);
            return;
        }
        case Phase::idle: {
            // 0805B660
            text_.x = 0;
            const u8 between = u8(angle & 0xFF);
            if (between == 0) {
                text_.y = 0x70;
            } else {
                const s32 d = between >= 0x7F ? between - 0x7F : 0x7F - between;
                const s16 dip = s16((u32(d) & 0x1FFFF) >> 1);
                text_.y = s16(0xA8 - dip);
                if (last_ == 0) {
                    unsigned c = (angle & 0x300) >> 8;
                    if ((angle & 0xFF) > 0x80) {
                        ++c;
                    }
                    load_band_colors(g, c & 3, false);
                }
                last_ = u16(dip);
            }
            if (text_.y < 0x70) {
                text_.y = 0x70;
            }
            draw_rows(g, false);
            return;
        }
        case Phase::chosen:
            // 0805B8FC
            ++clock_;
            text_.x = 0;
            draw_rows(g, true);
            return;
        }
    }

    Phase phase = Phase::waiting;
    u16 angle{}; // +1C

private:
    void draw_rows(Game& g, bool layered) {
        std::array<u8, 16> rows;
        for (unsigned i = 0; i < 16; ++i) {
            rows[i] = g.assets().u8(band_rows + i);
        }
        for (unsigned r = 0; r < 2; ++r) {
            if (layered) {
                draw_text_layered(g, rows.data() + 8 * r, text_);
            } else {
                draw_text(g, rows.data() + 8 * r, text_, true);
            }
            text_.y = s16(text_.y + (r == 0 ? 0x20 : -0x20));
        }
    }

    SpriteText text_; // +08
    u32 clock_{};     // +18
    u16 last_{};      // +1E the last dip (0: at the bottom)
};

// The arrows left and right of the front character (0x20 bytes, priority
// 2040; slot 1): they slide in, then bounce outwards.
class Arrows final : public Process {
public:
    Arrows() : Process(0x2040) { text_ = text_row(0, 0x100, 0x28, 1, 1, 0); }

    // 0805B0E8
    void update(Game& g) override {
        if (!shown) {
            return;
        }
        const Assets& a = g.assets();
        const u32 before = clock_++;
        const s16 i = s16(clock_);
        if (i < 0x27) {
            if (clock_ > 0x1E) {
                text_.x = s16((s16(before) - 0x1D) * 6 - 0x24);
                draw_text(g, left_arrow, text_, true);
                text_.x = s16((s16(clock_) - 0x1E) * -6 + 0xF0);
                draw_text(g, right_arrow, text_, true);
            }
            return;
        }
        if (i > 0x62) {
            clock_ = before - 0x3B;
        }
        const s32 phase = divide(remainder(i, 0x3C), 10);
        text_.x = a.u8(arrow_left_x + phase);
        draw_text(g, left_arrow, text_, true);
        text_.x = a.u8(arrow_left_x + 7 + phase);
        draw_text(g, right_arrow, text_, true);
    }

    bool shown = false;

private:
    SpriteText text_; // +08
    u32 clock_{};     // +18
};

// The character's name (0x20 bytes, priority 2030; slot 2): six 32-pixel
// glyphs that slide in and rise; while turning it sways and is reloaded
// with the next name.
class CharacterName final : public Process {
public:
    CharacterName() : Process(0x2030) { text_ = text_row(0x14, 0, 0x54, 6, 2, 0); }

    void update(Game& g) override {
        const Assets& a = g.assets();
        const unsigned c = (angle & 0x300) >> 8;
        switch (phase) {
        case Phase::waiting:
            return;
        case Phase::entering: {
            // 0805AEF0
            const s16 i = s16(++clock_);
            if (i < 0x22) {
                if (i < 0xE) {
                    if (i < 0xB) {
                        text_.x = s16(0x119 - divide(i << 8, 10));
                    } else {
                        text_.x = 0x19;
                    }
                } else {
                    text_.y = s16(sine_high(a, (divide(i * 0x67, 0x6E) - 10) * 0x11) + 0x54);
                }
            } else {
                text_.y = 0x84;
                clock_ = 0;
                phase = Phase::idle;
            }
            draw_text(g, eight_glyphs, text_, true);
            return;
        }
        case Phase::idle: {
            // 0805AF90
            const u8 between = u8(angle & 0xFF);
            if (between == 0) {
                text_.x = a.s16(name_x + 2 * c);
            } else {
                unsigned n = c;
                const s32 d = between >= 0x7F ? between - 0x7F : 0x7F - between;
                if (last_ == 0) {
                    n = c + ((angle & 0xFF) > 0x80 ? 1 : 0);
                    n &= 3;
                    g.screen_graphics->reset_pool();
                    GraphicsLoad l;
                    l.options = 0x29;
                    l.slot = 2;
                    l.tiles_from = name_tiles + n * 0xC00;
                    l.tile_bytes = 0xC00;
                    l.tiles_to = 0x11120;
                    l.parts = load::tiles | load::sprite;
                    l.key = 0x10;
                    l.extra = {2, 0, 4, 4};
                    load_graphics(g, l);
                }
                last_ = u16((u32(d) & 0x1FFFF) >> 1);
                text_.x = s16(a.s16(name_sway + 2u * (between >> 4)) + a.s16(name_x + 2 * n));
            }
            draw_text(g, eight_glyphs, text_, true);
            return;
        }
        case Phase::chosen:
            // 0805B84C
            ++clock_;
            draw_text_layered(g, eight_glyphs, text_);
            text_.x = a.s16(name_x + 2 * c);
            return;
        }
    }

    Phase phase = Phase::waiting;
    u16 angle{}; // +1C

private:
    SpriteText text_; // +08
    u32 clock_{};     // +18
    u16 last_{};      // +1E
};

// The banner at the top (0x44 bytes, priority 2030): a black bar (slot 3)
// and the scrolling words (slot 4) behind window 0.
class Banner final : public Process {
public:
    Banner() : Process(0x2030) {
        bar_ = text_row(0x78, 0, 0x38, 8, 3, 0);
        words_ = text_row(100, 0x118, 0x3A, 9, 4, 0);
    }

    void update(Game& g) override {
        const Assets& a = g.assets();
        switch (phase) {
        case Phase::waiting:
            return;
        case Phase::entering: {
            // 0805ACA4
            const s32 i = s16(screen_clock);
            if (i < 0x21) {
                if (i < 0xD) {
                    if (i < 10) {
                        bar_.x = s16(0x100 - divide(i << 8, 10));
                        position_ -= 0x640;
                    } else {
                        bar_.x = 0;
                        position_ -= 0xB4;
                    }
                } else {
                    const s32 j = s16(i - 0xD);
                    bar_.y = s16(0x38 - sine_high(a, j * (divide(j, 10) + 0x11)));
                    position_ -= 0xB4;
                }
            } else {
                bar_.y = 6;
                screen_clock = 0;
                position_ -= 0xB4;
                phase = Phase::idle;
            }
            draw_text(g, spaces, bar_, true);
            set_word_count();
            scroll(g, bar_.y, false);
            return;
        }
        case Phase::idle:
            // 0805ADBC
            bar_.x = 0;
            draw_text(g, spaces, bar_, true);
            ++clock_;
            words_.y = 6;
            position_ -= 0xB4;
            set_word_count();
            scroll(g, words_.y, false);
            return;
        case Phase::chosen:
            // 0805AE50
            ++clock_;
            bar_.x = 0;
            draw_text_layered(g, spaces, bar_);
            words_.y = 6;
            position_ -= speed;
            for (int k = 0; k < 3; ++k) {
                if (speed != 0) {
                    --speed;
                }
            }
            scroll(g, words_.y, true);
            return;
        }
    }

    Phase phase = Phase::waiting;
    u32 screen_clock{}; // +38 the main process's clock
    s16 speed{};        // +40 the words' speed once chosen

private:
    // Fewer words are drawn while they are still coming in from the right.
    void set_word_count() {
        words_.count = position_ < 1 ? 9 : s16(9 - divide(position_ >> 6, 0x20));
        if (position_ > 0x3C00) {
            words_.count = 0;
        }
    }

    void scroll(Game& g, s16 y, bool chosen) {
        if (position_ < -0x7C0) {
            position_ += 0x800;
            glyph_ = (glyph_ + 1) & 7;
        }
        words_.x = s16(position_ >> 6);
        words_.y = y;
        if (chosen) {
            draw_text_layered(g, chosen_banner_text + glyph_, words_);
        } else {
            draw_text(g, banner_text + glyph_, words_, true);
        }
    }

    SpriteText bar_;        // +08
    SpriteText words_;      // +20
    u32 glyph_{};           // +30 the first glyph shown
    u32 clock_{};           // +34
    s32 position_ = 0x4600; // +3C the words' x (in 64ths)
};

// The main process (task 08059F68, priority 2000, 0x3C bytes).
class CharacterSelect final : public Process {
public:
    CharacterSelect(Game& g, u8 character) : Process(0x2000) {
        if (g.game_mode > 1) {
            throw Unrecovered("the multiplayer character select (080597A8)");
        }
        auto& v = g.video();
        g.stage().tails_follows = 0;
        ensure_screen_graphics(g);
        v.settings.set_control(0x1240);
        v.settings.bg[1].set_control(0x1907);
        for (int i = 1; i <= 2; ++i) {
            v.settings.bg[std::size_t(i)].scroll_x = 0;
            v.settings.bg[std::size_t(i)].scroll_y = 0;
        }
        angle_ = u16((character & 3) * 0x100 + 0x2000);
        shown_ = target_ = s8((character & 3) + 0x20);
        fade_ = {0, 0, 2, 0, 0x200, 1};
        auto& s = g.scheduler();
        band_ = &s.create<Band>(angle_);
        arrows_ = &s.create<Arrows>();
        name_ = &s.create<CharacterName>();
        banner_ = &s.create<Banner>();
        ring_ = &s.create<Ring>(g);
        const Assets& a = g.assets();
        load_band_colors(g, character, true);
        load_graphics(g, sprite_picture(a, 1, 0x28, 0x10420, 0x400,
                                        load::tiles | load::colors | load::sprite));
        GraphicsLoad name = sprite_picture(a, 2, 0x29, 0x11120, 0xC00, load::tiles | load::sprite);
        name.tiles_from += character * 0xC00u;
        load_graphics(g, name);
        load_graphics(g, sprite_picture(a, 3, 0x2B, 0x10820, 0x100, load::tiles | load::sprite));
        load_graphics(g, sprite_picture(a, 4, 0x2A, 0x10920, 0x800, load::tiles | load::sprite));
        carousel_ = &s.create<Carousel>(g);
        auto& q = g.backgrounds();
        q.clear_value[1] = 0;
        q.clears[1] = {0, 0, 0xFF, 0x20};
        g.sound().start_or_continue(select_song);
        step_fade(v, fade_);
    }

    void update(Game& g) override {
        switch (state_) {
        case State::appear:
            appear(g);
            break;
        case State::choose:
            choose(g);
            break;
        case State::chosen:
            chosen(g);
            break;
        case State::back:
            back(g);
            break;
        }
    }

private:
    enum class State { appear, choose, chosen, back };

    // 08059F68: after the fade in, the windows and the half-transparent
    // look are set and the parts come in.
    void appear(Game& g) {
        auto& v = g.video();
        if (!step_fade(v, fade_)) {
            return;
        }
        fade_ = {0, 1, 1, 0, 0x200, 1};
        auto& s = v.settings;
        s.window0 = s.window1 = true;
        s.windows[0] = {0, 0x28, 0, 0xF0};
        s.windows[1] = {0xC8, 0xF0, 0, 0xF0};
        s.inside[0] = s.inside[1] = 0x3F;
        s.outside = 0x1F;
        s.blend.set_control(0x1290);
        s.blend.eva = 0x10;
        s.blend.evb = 0;
        s.blend.evy = 8;
        carousel_->phase = Phase::entering;
        band_->phase = Phase::entering;
        arrows_->shown = true;
        name_->phase = Phase::entering;
        banner_->phase = Phase::entering;
        state_ = State::choose;
    }

    void start_turn(Game& g, u8 direction) {
        turning_ = direction;
        steps_ = 0x10;
        g.sound().start(sound_turn);
    }

    // 0805A02C: turning and choosing (ready after 22h frames).
    void choose(Game& g) {
        const Controller& c = g.controller();
        s32 target = target_;
        check_code(g);
        const bool ready = clock_ >= 0x22;
        const bool locked = (locked_ >> (target & 3)) & 1;
        if ((c.pressed_once & button::b) && ready && target_ == shown_) {
            // 0805B824
            g.sound().start(sound_back);
            clock_ = 0;
            g.video().settings.blend.evy = 0;
            state_ = State::back;
        } else if ((c.pressed_once & button::a) && target_ == shown_ && ready && !locked) {
            g.sound().start(sound_choose);
            pick(g);
        } else if ((c.pressed & button::left) && ready) {
            target = s16(target - 1);
            if (turning_ == 0) {
                start_turn(g, 2);
            }
        } else if ((c.pressed & button::right) && ready) {
            target = s16(target + 1);
            if (turning_ == 0) {
                start_turn(g, 1);
            }
        } else if (shown_ < target) {
            if (turning_ == 0) {
                start_turn(g, 1);
            }
        } else if (target < shown_) {
            if (turning_ == 0) {
                start_turn(g, 2);
            }
        } else if (locked && ready) {
            // A character that cannot be chosen is passed over.
            if (last_turn_ == 0) {
                start_turn(g, 1);
            } else {
                target += (last_turn_ & 1) ? 1 : -1;
            }
        }
        last_turn_ = turning_;
        if (turning_ != 0 && steps_ != 0) {
            angle_ = u16(angle_ + ((turning_ & 1) ? 0x10 : -0x10));
            if (--steps_ == 0) {
                turning_ = 0;
            }
        }
        angle_ &= 0x3FFF;
        target_ = s8(target & 0x3F);
        // Turning back, the character in front changes as soon as the ring
        // has left it.
        const s8 before = shown_;
        shown_ = s8(angle_ >> 8);
        if ((target & 0x3F) < before && (angle_ & 0xFF) != 0) {
            shown_ = s8(shown_ + 1);
        }
        shown_ = s8(shown_ & 0x3F);
        // The numbers stay within 1C-24 (the ring has four places).
        if (shown_ >= 0x25 && target_ >= 0x25 && s16(angle_) > 0x2400) {
            do {
                shown_ = s8(shown_ - 4);
            } while (shown_ > 0x24);
            while (target_ > 0x24) {
                target_ = s8(target_ - 4);
            }
            while (s16(angle_) > 0x2400) {
                angle_ = u16(angle_ - 0x400);
            }
        } else if (shown_ < 0x1C && target_ < 0x1C && s16(angle_) < 0x1C00) {
            do {
                shown_ = s8(shown_ + 4);
            } while (shown_ < 0x1C);
            while (target_ < 0x1C) {
                target_ = s8(target_ + 4);
            }
            while (s16(angle_) < 0x1C00) {
                angle_ = u16(angle_ + 0x400);
            }
        }
        const u16 a = angle_ & 0x3FF;
        name_->angle = a;
        band_->angle = a;
        carousel_->angle = a;
        carousel_->turning = turning_;
        if (ring_) {
            ring_->angle = a;
        }
        carousel_->locked = locked_;
        ++clock_;
        banner_->screen_clock = clock_;
    }

    // 0805B7B0: Up on Sonic, then Right, Down, Right, L, Right, R, Right
    // and A: Tails follows Sonic.
    void check_code(Game& g) {
        const u16 pressed = g.controller().pressed_once;
        if (pressed == 0) {
            return;
        }
        if ((pressed & button::up) && (angle_ & 0x300) == 0) {
            code_ = u8((angle_ >> 8) & 3);
            return;
        }
        if (code_ != 0xFF) {
            code_ = (g.assets().u16(button_code + 2u * code_) & pressed) ? u8(code_ + 1) : 0xFF;
        }
    }

    // 0805A970: a character is chosen.
    void pick(Game& g) {
        g.stage().character = u8((angle_ >> 8) & 3);
        carousel_->sprites_[1].variant = u8(((angle_ & 0x300) >> 7) + 1);
        banner_->phase = Phase::chosen;
        carousel_->phase = Phase::chosen;
        name_->phase = Phase::chosen;
        band_->phase = Phase::chosen;
        g.scheduler().destroy(*arrows_);
        g.scheduler().destroy(*ring_);
        arrows_ = nullptr;
        ring_ = nullptr;
        clock_ = 0;
        banner_->speed = 0xB4;
        g.video().settings.blend.evy = 0;
        GraphicsLoad l;
        l.options = 0x2C;
        l.slot = 4;
        l.tiles_from = chosen_banner_tiles;
        l.tile_bytes = 0x200;
        l.tiles_to = 0x10920;
        l.color_bytes = 0x20;
        l.palette = 0;
        l.parts = load::tiles | load::sprite;
        l.key = 8;
        l.extra = {2, 1, 4, 2};
        load_graphics(g, l);
        state_ = State::chosen;
    }

    void destroy_parts(Game& g) {
        auto& s = g.scheduler();
        for (Process* p :
             std::array<Process*, 6>{name_, band_, carousel_, banner_, arrows_, ring_}) {
            if (p) {
                s.destroy(*p);
            }
        }
        name_ = nullptr;
        band_ = nullptr;
        carousel_ = nullptr;
        banner_ = nullptr;
        arrows_ = nullptr;
        ring_ = nullptr;
    }

    static void screen_off(Game& g) {
        auto& s = g.video().settings;
        s.set_control(s.control() & 0x0FFF);
        s.blend.set_control(0);
        s.blend.evy = 0;
    }

    // 0805AAC4: a pause, the fade out, then the act.
    void chosen(Game& g) {
        StageState& st = g.stage();
        ++clock_;
        if (clock_ < 0x39) {
            if (clock_ > g.assets().u8(fade_delays + st.character)) {
                step_fade(g.video(), fade_);
            }
            return;
        }
        const u8 code = code_;
        destroy_parts(g);
        g.scheduler().destroy(*this);
        screen_off(g);
        if (g.game_mode == 0) {
            if (code == 8) {
                g.sound().start(sound_tails_follows);
                st.tails_follows = 1;
            }
            if (g.save().zones_reached[st.character] == 0) {
                st.zone = 0;
                st.unknown_5124 = 0;
                start_game_act(g);
                return;
            }
        }
        throw Unrecovered("the act select (08061914)");
    }

    // 0805ABCC: back to the main menu.
    void back(Game& g) {
        ++clock_;
        fade_.speed = 0x180;
        fade_.blend = 0x10;
        if (!step_fade(g.video(), fade_)) {
            if (clock_ == 0x10) {
                destroy_parts(g);
                auto& q = g.backgrounds();
                for (unsigned layer = 0; layer < 3; ++layer) {
                    q.clear_value[layer] = 0;
                    q.clears[layer] = {0, 0, 0xFF, 0x14};
                }
            }
            return;
        }
        g.sound().stop(select_song);
        screen_off(g);
        g.scheduler().destroy(*this);
        if (g.game_mode == 1) {
            throw Unrecovered("the time attack menu (08010078)");
        }
        g.start_main_menu(true);
    }

    State state_ = State::appear;
    ScreenFade fade_;       // +00
    Carousel* carousel_{};  // +0C
    Band* band_{};          // +10
    Banner* banner_{};      // +14
    CharacterName* name_{}; // +18
    Arrows* arrows_{};      // +1C
    Ring* ring_{};          // +20
    u32 clock_{};           // +24
    u16 angle_{};           // +28 the ring's turn: character in bits 8-9, 1C00-24FF
    s8 shown_{};            // +2A the character in front (20h + n, kept within 1C-24)
    s8 target_{};           // +2B the character asked for
    u8 turning_{};          // +2C 1 next, 2 previous
    u8 last_turn_{};        // +2D
    u8 steps_{};            // +2E steps of 10h left in the turn
    u8 locked_{};           // +2F
    u8 code_ = 0xFF;        // +39 the button code's progress
};

} // namespace

void Game::start_character_select(u8 character) {
    scheduler_.create<CharacterSelect>(*this, character);
}

} // namespace sonic_advance_gba
