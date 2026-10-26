// The screen: drawing into the character display, and turning it into a
// picture with the VIC's settings for each part of the frame.

#include "fort_apocalypse_c64/game/bcd.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <algorithm>

namespace fort_apocalypse::game {
namespace {
const c64::Glyph empty_glyph{};
} // namespace

void Game::set_cell(int index, u8 code) {
    if (index >= 0 && index < 1000) {
        screen_[std::size_t(index)] = code;
    }
}

void Game::clear_play_area() {
    // $9AB3.
    vic_.columns40 = true; // $D016 = $18
    vic_.xscroll = 0;
    vic_.yscroll = 3;      // $D011 = $1B
    vic_.rows25 = true;
    split_layout_ = false; // $12 = 1
    craft_ = Craft::inactive;
    enemy_.sprite = 1;
    if (enemy_.state == 4) {
        enemy_.state = 1;
    }
    for (auto& g : radar_glyphs_) {
        g = {};
    }
    for (int i = 240; i < 1000; ++i) {
        screen_[std::size_t(i)] = 0;
    }
    refuel_sound_ = 0;
    teleport_grace_ = 0;
    tracers_.clear();
    falling_.clear();
    born_.clear();
    crushed_ = false;
    missiles_active_ = 0;
    playfield_colour_ = 0;
    shot_sound_ = 0;
    explosion_sound_ = 0;
    engine_pitch_ = 0x14;
    enemy_.timer = 5;
    for (int i = 5; i >= 0; --i) {
        auto& m = missiles_[std::size_t(i)];
        if (m.state != 1) {
            m.state = 1;
            restore_missile_cell(i);
        }
    }
    for (int i = 2; i >= 0; --i) {
        auto& b = bullets_[std::size_t(i)];
        if (b.state == 7) {
            // The splash is restored through the objects' address routine
            // ($A7D8, column - 5) although bullets keep actor columns
            // (column - 1): the character lands four cells to the left.
            map_set(object_index(b.column, b.row), b.saved);
        }
        b.state = 0;
        sprite_regs_[std::size_t(2 + i)].pointer = 0;
    }
    sound_off();
}

void Game::clear_message_row() {
    for (int i = 0; i < 40; ++i) {
        screen_[std::size_t(240 + i)] = 0; // $9BBB
    }
}

void Game::copy_hud() {
    for (int i = 0; i < 240; ++i) {
        screen_[std::size_t(i)] = assets_.hud.cells[std::size_t(i / 40)][std::size_t(i % 40)] & 0x7F;
    }
}

void Game::fill_colours(u8 colour) {
    // $AFA0.
    colour_ram_.fill(colour);
    for (int i = 0; i < 40; ++i) {
        colour_ram_[std::size_t(i)] = 0x0B;
        colour_ram_[std::size_t(200 + i)] = 0x0B;
        colour_ram_[std::size_t(240 + i)] = 0x0C;
        colour_ram_[std::size_t(360 + i)] = 0x08;
    }
    for (int i = 0; i < 12; ++i) {
        colour_ram_[std::size_t(0x78 + i)] = 0x0F;
        colour_ram_[std::size_t(0x93 + i)] = 0x0F;
        colour_ram_[std::size_t(0x13 + i)] = 0x0F;
    }
}

void Game::draw_text(const data::Text& text, int column, int text_row) {
    // $9B56: text rows count from screen row 6.
    int at = 240 + text_row * 40 + column;
    for (const auto code : text) {
        set_cell(at++, code);
        if (code != 0) {
            set_cell(at++, u8(code + 0x20));
        }
    }
}

void Game::draw_number(u8 bcd, int& at, int& digits_left, bool& started) {
    // $A9D3: two digits of a BCD byte; leading zeros blank except the last
    // digit of the number.
    u8 value = bcd;
    if (!started && (value & 0xF0) == 0) {
        value |= 0xA0;
    } else {
        started = true;
    }
    if (!started && (value & 0x0F) == 0) {
        value |= 0x0A;
    } else {
        started = true;
    }
    for (int half = 0; half < 2; ++half) {
        u8 digit = half == 0 ? u8(value >> 4) : u8(value & 15);
        if (digit == 0x0A) {
            digit = digits_left == 0 ? 0 : 0x70;
        }
        const u8 left = u8(digit + 0x10);
        set_cell(at, left);
        set_cell(at + 1, u8(left == 0x10 ? 0x0A : (left & 0x0F)));
        at += 2;
        --digits_left;
    }
}

void Game::draw_counters() {
    // $A92F.
    if (mode_ == Mode::life_start || mode_ == Mode::debrief) {
        return;
    }
    int at = 0x13, digits = 5;
    bool started = false;
    draw_number(score_[2], at, digits, started);
    draw_number(score_[1], at, digits, started);
    draw_number(score_[0], at, digits, started);
    if (mode_ == Mode::playing && (bonus_[0] | bonus_[1]) && (frame_ & 7) == 0) {
        bool borrow = false;
        bonus_[0] = bcd_sub(bonus_[0], 1, borrow);
        bonus_[1] = bcd_sub(bonus_[1], 0, borrow);
    }
    at = 0x95;
    digits = 3;
    started = false;
    draw_number(bonus_[1], at, digits, started);
    draw_number(bonus_[0], at, digits, started);
    if (mode_ == Mode::playing && cheat(options_.infinite_fuel)) {
        fuel_ = {0x01, 0x20}; // the infinite-fuel cheat: full, as a refuel leaves it
        engine_ = engine_ == 9 ? 8 : engine_;
    } else if (mode_ == Mode::playing && engine_ == 8) {
        if ((fuel_[0] | fuel_[1]) == 0) {
            engine_ = 9;
        } else if ((frame_ & 0x0F) == 0) {
            bool borrow = false;
            fuel_[0] = bcd_sub(fuel_[0], 1, borrow);
            fuel_[1] = bcd_sub(fuel_[1], 0, borrow);
        }
    }
    at = 0x7A;
    digits = 3;
    started = false;
    draw_number(fuel_[1], at, digits, started);
    draw_number(fuel_[0], at, digits, started);
}

// --- the picture --------------------------------------------------------------------

void Game::render() {
    c64::TextFrame frame;
    frame.set_idle_pattern(assets_.idle_pattern);
    // Rows in the HUD part use the HUD set ($D018 = $14), rows of the
    // playfield part the playfield set ($D018 = $16, from raster $76).
    const int first_playfield_row = split_active_ ? 8 : 25;
    for (int i = 0; i < 1000; ++i) {
        const u8 code = screen_[std::size_t(i)];
        const c64::Glyph* glyph;
        if (i / 40 >= window_first_row && split_active_ && code < 128 && !carved_.empty()) {
            // A cell the carnage gun has carved (copy_window's cell).
            glyph = &cell_glyph_at((window_.row + i / 40 - window_first_row) * 256 + window_.column + i % 40,
                                   code);
        } else if (i / 40 >= first_playfield_row) {
            glyph = code < 128 ? &glyphs_[code] : &empty_glyph;
        } else if (code >= 0x5C && code < 0x80) {
            glyph = &radar_glyphs_[std::size_t(code - 0x5C)];
        } else {
            glyph = code < 128 ? &assets_.hud_charset[code] : &empty_glyph;
        }
        frame.set(i % 40, i / 40, glyph, colour_ram_[std::size_t(i)]);
    }
    auto setup = [](const Display& d) {
        c64::LineSetup line;
        line.background = d.background;
        line.shared1 = d.shared1;
        line.shared2 = d.shared2;
        line.border = d.border;
        line.multicolour = true;
        line.narrow = !d.columns40;
        line.xscroll = d.xscroll;
        line.yscroll = d.yscroll;
        line.sprites = d.sprites;
        return line;
    };
    frame.set_lines(setup(top_));
    if (split_active_) {
        // Observed: the playfield part's $D011/$D016 writes land in raster
        // $77 at cycle 7-11, its colours in raster $7F before the display
        // starts. (Its first write, $D022/$D023 = 0 at the end of raster $76,
        // only reaches the right part of that line, which is blank in play.)
        constexpr int split = hud_lines, colours = 0x7F - 51, bottom = 247 - 51;
        auto first = setup(split_);
        first.yscroll_before = top_.yscroll;
        frame.set_lines(split, split + 1, first);
        frame.set_lines(split + 1, colours, setup(split_));
        frame.set_lines(colours, c64::Picture::height, setup(lower_));
        // Observed: the background write lands mid-raster $7F, at pixel 84
        // to 124 or past the line's end as the interrupt's start jitters;
        // 92 is the most common (77 of 259 flashing frames measured).
        auto change = setup(lower_);
        change.background_from_x = 92;
        change.background_before = split_.background;
        change.border_before = split_.border;
        frame.set_lines(colours, colours + 1, change);
        if (!lower_.rows25) {
            auto covered = setup(lower_);
            covered.covered = true;
            frame.set_lines(bottom, c64::Picture::height, covered);
        }
    }
    for (int i = 0; i < 5; ++i) {
        const auto& r = sprite_regs_[std::size_t(i)];
        c64::Sprite s;
        s.shown = r.pointer != 0;
        s.x = r.x;
        s.y = r.y;
        s.expand_x = i < 2;
        s.behind_foreground = true; // $D01B = $1F
        s.colour = r.colour;
        if (r.pointer >= 1 && r.pointer <= 14) {
            const auto& shape = assets_.helicopter[std::size_t(r.pointer - 1)];
            for (int row = 0; row < 18; ++row) {
                s.data[std::size_t(row * 3)] = u8(shape.rows[std::size_t(row)] >> 8);
                s.data[std::size_t(row * 3 + 1)] = u8(shape.rows[std::size_t(row)]);
            }
        } else if (r.pointer == 0x20 || r.pointer == 0x21) {
            // $B0B0: block $20 holds the dot pattern twice, $21 once.
            for (int k = 0; k < 9; ++k) {
                s.data[std::size_t(k)] = assets_.bullet_dots[std::size_t(k)];
                if (r.pointer == 0x20) {
                    s.data[std::size_t(9 + k)] = assets_.bullet_dots[std::size_t(k)];
                }
            }
        } else {
            s.shown = false;
        }
        frame.set_sprite(i, s);
    }
    c64::Collisions hits;
    picture_ = frame.rasterise(&hits);
    shown_frame_ = frame;
    shown_screen_ = screen_;
    shown_window_ = window_;
    shown_wide_window_ = wide_window_;
    shown_wide_enemy_ = wide_enemy_;
    latched_sprite_hits_ |= hits.sprite_sprite;
    latched_background_hits_ |= hits.sprite_background;
    latched_graphics_hits_ |= hits.sprite_graphics;
}

} // namespace fort_apocalypse::game
