#include "wizball_c64/game/front_end.hpp"

// The front end's screens as their raster lists build them: hi-res text in
// 40 columns from the screen at $EC00 with the font at $F000 (the letters
// inverted in memory on most screens, so they take the background colour
// over black cells), and handlers that change the background or sprite
// colours every line or two. A handler's direct writes count from the line
// after its interrupt; a sprite colour written after a wait for line Y
// shows from line Y + 1 (the reference emulator takes a sprite's colour
// for a line before the line starts).
namespace wizball::game {
namespace {
// The title's colour steps for the table's rows ($936A-$93C5): the
// handler's line, the line it re-places the solid sprites at, and its X.
struct RowBars {
    int line, y, x;
};
constexpr RowBars title_rows[] = {{160, 0xA2, 8}, {173, 0xB2, 9}, {192, 0xC2, 0x0A},
                                  {208, 0xD2, 0x0B}, {224, 0xE2, 0x0C}, {240, 0xF2, 0x0D}};
} // namespace

FrontEnd::StarSprites FrontEnd::star_sprites() const {
    StarSprites out;
    if (screen_ != Screen::menu && screen_ != Screen::player) return out;
    const bool menu = screen_ == Screen::menu;
    out.shape = sprite_shape(menu ? 5 : 0x0C);
    out.colour = menu ? 1 : sprite_colour_;
    for (const auto& s : stars_)
        for (int side = 0; side < 2; ++side) {
            const int x = (side == 0 ? s.x : std::uint8_t(0xB0 - s.x)) * 2;
            out.sprites.push_back({x, std::uint8_t(0xFF - s.y), 1});
            out.sprites.push_back({x, s.y, 0});
        }
    return out;
}

SpriteShape FrontEnd::sprite_shape(int pointer) const {
    SpriteShape shape{};
    if (pointer >= 0x80 && pointer < 0x90) {
        std::copy_n(sprites_.begin() + (pointer - 0x80) * 64, 63, shape.begin());
        return shape;
    }
    if (pointer == 5) { // cleared at the start of play; the menu's dot ($903D)
        if (dot_sprite_) shape[33] = 0xC0;
        return shape;
    }
    if (pointer < 0x80) return assets_.sprites[std::size_t(pointer)].rows;
    return shape;
}

Display FrontEnd::picture(int extension, int blackout_fade) const {
    if ((screen_ == Screen::play || screen_ == Screen::game_over) && play_) return play_->picture(extension, blackout_fade);
    Display d = screen_picture();
    d.extension = extension; // the front end's screens keep their border at the sides
    return d;
}

Display FrontEnd::screen_picture() const {
    if (screen_ == Screen::loading || (screen_ == Screen::scores && scores_wait_ != 0)) {
        // $8C44: the screen blanked while the place is set up.
        Display d;
        d.vertical_border_open = false;
        d.set_lines_from(0, LineSettings{});
        return d;
    }

    Display d;
    d.vertical_border_open = false;
    static thread_local Charset font;
    font = assets_.front_charset;
    for (int c = 0; c < 256; ++c) {
        const bool inverted = c < 0x40 ? letters_inverted_ : big_inverted_;
        if (inverted)
            for (auto& b : font[std::size_t(c)]) b = std::uint8_t(~b);
    }
    d.screens.assign(1, {});
    d.screens[0].codes = codes_;
    d.colours = cells_;
    LineSettings base{};
    base.charset = &font;
    base.screen = 0;
    base.border = screen_ == Screen::title || screen_ == Screen::menu ? border_ : 0;
    auto background = [&](int from, std::uint8_t colour) {
        for (int r = from; r < Display::raster_lines; ++r) d.line(r).background = std::uint8_t(colour & 15);
    };
    d.set_lines_from(0, base);
    auto at = [&](int address) { return colours_.at(address); };

    // Sprites shared by the menu, the player screen: the four stars,
    // mirrored into the top half until about line 136 ($915F, $9163).
    auto stars = [&](int pointer, std::uint8_t colour) {
        for (int half = 0; half < 2; ++half) {
            const int line = half == 0 ? 0 : 136;
            for (int k = 0; k < 4; ++k) {
                const auto& s = stars_[std::size_t(k)];
                for (int side = 0; side < 2; ++side) {
                    SpriteSettings sp;
                    sp.enabled = true;
                    sp.y = half == 0 ? std::uint8_t(0xFF - s.y) : s.y;
                    sp.x = (side == 0 ? s.x : std::uint8_t(0xB0 - s.x)) * 2;
                    sp.shape = sprite_shape(pointer);
                    sp.colour = colour;
                    d.set_sprite(k + 4 * side, line, sp);
                }
            }
        }
    };

    switch (screen_) {
    case Screen::title: {
        // Before $922C the background is the last frame's ($EBF1).
        background(0, at(0xEBF1));
        background(36, at(0xEBD1)); // $922C
        // The logo: sprites 0-4 ($80-$84) from $B571, doubled in width,
        // coloured line by line from the logo's colours ($EBD0 down).
        for (int n = 0; n < 5; ++n) {
            SpriteSettings s;
            s.enabled = true;
            s.x = image_.byte(std::uint16_t(0xB571 + 2 * n)) | (n == 4 ? 0x100 : 0);
            s.y = image_.byte(std::uint16_t(0xB572 + 2 * n));
            s.shape = sprite_shape(0x80 + n);
            s.expand_x = true;
            s.colour = std::uint8_t(at(0xEBC1) & 15); // the last frame's last
            d.set_sprite(n, 36, s);
            for (int i = 0; i < 16; ++i) {
                s.colour = std::uint8_t(at(0xEBD0 - i) & 15);
                d.set_sprite(n, 0x3E + 1 + i, s);
            }
        }
        for (int n = 5; n < 8; ++n) d.set_sprite(n, 36, SpriteSettings{});
        // $92A2: the top score's line in sprites 0-2 ($85-$87), four colour
        // steps; the top name's colour.
        background(108, 0);
        for (int n = 0; n < 4; ++n) {
            SpriteSettings s;
            s.enabled = n < 3;
            s.x = image_.byte(std::uint16_t(0xB57B + 2 * n));
            s.y = image_.byte(std::uint16_t(0xB57C + 2 * n));
            s.shape = sprite_shape(0x85 + n);
            s.expand_x = true;
            for (int i = 0; i < 4; ++i) {
                s.colour = std::uint8_t(at(0xEBD5 - i) & 15);
                d.set_sprite(n, i == 0 ? 108 : 0x76 + 1 + 2 * i, s);
            }
        }
        for (int r = 8; r < 10; ++r)
            for (int c = 20; c < 40; ++c) d.colours[std::size_t(r * 40 + c)] = std::uint8_t(at(0xEBF2) & 15);
        background(141, at(0xEBD6));
        // $9327: four solid sprites ($8C), tall and wide, behind the text.
        const int xs[] = {0xE8, 0x30, 0x00, 0xB8};
        SpriteSettings solid[4];
        for (int n = 0; n < 4; ++n) {
            solid[n].enabled = true;
            solid[n].x = xs[n];
            solid[n].y = 0xA2;
            solid[n].shape = sprite_shape(0x8C);
            solid[n].expand_x = solid[n].expand_y = true;
            solid[n].behind = true;
            solid[n].colour = std::uint8_t(at(0xEBD2) & 15);
            d.set_sprite(n, 148, solid[n]);
        }
        background(161, at(0xEBF1)); // $936A
        for (const auto& rb : title_rows) {
            for (int n = 0; n < 4; ++n) {
                solid[n].y = rb.y;
                d.set_sprite(n, rb.line + 1, solid[n]);
            }
            for (int i = 0; i < 4; ++i) {
                const int x = rb.x - i;
                for (int n = 0; n < 4; ++n) {
                    solid[n].colour = std::uint8_t(at((n & 1 ? 0xEBD6 : 0xEBE3) + x) & 15);
                    d.set_sprite(n, rb.y + 1 + 2 * i, solid[n]);
                }
            }
        }
        break;
    }
    case Screen::menu: {
        // $93CC-$93F7: the chosen mode's row in four colour steps.
        background(0, at(0xEBD6));
        const int rows[] = {0x4B, 0x63, 0x7B, 0x93, 0xAB};
        const int lines[] = {72, 96, 120, 144, 168};
        for (int m = 0; m < 5; ++m) {
            if (m == mode_) {
                for (int i = 0; i < 4; ++i) background(rows[m] + 1 + 2 * i, at(0xEBDA - i));
                background(rows[m] + 9, 6);
            } else {
                background(lines[m] + 1, 6);
            }
        }
        background(231, at(0xEBD6)); // $9419
        stars(5, 1);
        break;
    }
    case Screen::player: {
        background(0, player_background_top_);
        background(155, player_background_);
        stars(0x0C, sprite_colour_);
        break;
    }
    case Screen::scores: {
        background(0, at(0xEBD1));
        background(45, at(0xEBD2)); // $6DD5
        background(79, at(0xEBF1)); // $6DDC
        for (int k = 0; k < 2; ++k)  // $6DE3: $973B twice
            for (int i = 0; i < 4; ++i) background(0x90 + 8 * k + 1 + 2 * i, at(0xEBE4 + i));
        background(167, at(0xEBD6)); // $6DEB
        background(183, at(0xEBE4)); // $6DF2
        background(208, at(0xEBD1)); // $6DF9
        for (int k = 0; k < 2; ++k) { // $6E00: $9401 twice
            for (int i = 0; i < 4; ++i) background(0xE8 + 8 * k + 1 + 2 * i, at(0xEBDA - i));
            background(0xE8 + 8 * k + 9, 6);
        }
        break;
    }
    case Screen::tips: tips_picture(d); break;
    default: break;
    }
    return d;
}

void FrontEnd::tips_picture(Display& d) const {
    auto at = [&](int address) { return colours_.at(address); };
    auto background = [&](int from, std::uint8_t colour) {
        for (int r = from; r < Display::raster_lines; ++r) d.line(r).background = std::uint8_t(colour & 15);
    };
    // $94EA: sprites from a table of records (x, y, x's ninth bit, colour,
    // pointer), with the pointers and colours the handlers patch in.
    auto records = [&](std::uint16_t table, int count, int line, auto&& patch) {
        for (int k = 0; k < count; ++k) {
            const auto r = std::uint16_t(table + 5 * k);
            SpriteSettings s;
            s.enabled = true;
            s.x = image_.byte(r) | (image_.byte(std::uint16_t(r + 2)) ? 0x100 : 0);
            s.y = image_.byte(std::uint16_t(r + 1));
            std::uint8_t colour = image_.byte(std::uint16_t(r + 3)), pointer = image_.byte(std::uint16_t(r + 4));
            patch(k, colour, pointer);
            s.colour = std::uint8_t(colour & 15);
            s.shape = sprite_shape(pointer);
            s.multicolour = true;
            // Each record takes about two raster lines to write (measured
            // $94F6 at 191, 193, 196, 198, 200, 201.5 for the list at 190),
            // so a sprite still being shown above keeps its last record.
            d.set_sprite(k & 7, line + (21 * k + 5) / 10, s);
        }
    };
    auto shared = [&](int from, std::uint8_t c1, std::uint8_t c2) {
        for (int r = from; r < Display::raster_lines; ++r) {
            d.line(r).sprite_shared1 = c1;
            d.line(r).sprite_shared2 = c2;
        }
    };
    switch (tips_page_) {
    case 0:
        // $9681 at line 1: eight sprites (the token bar and the wiz),
        // three of them animated, token 1 flashing.
        records(image_.word(0x9677), 8, 2, [&](int k, std::uint8_t& colour, std::uint8_t& pointer) {
            if (k < 3) pointer = image_.byte(std::uint16_t(0x96F7 + 4 * k + tips_phase_[std::size_t(k)]));
            if (k == 4) colour = tips_token_colour_;
        });
        shared(2, 0x0B, 0x0D);
        background(2, at(0xEBD2));
        background(66, std::uint8_t(at(0xEBD2) ^ 8)); // $9703
        background(81, at(0xEBF2));                    // $970C
        shared(152, 6, 1);                            // $972F
        for (int i = 0; i < 4; ++i) background(0x99 + 1 + 2 * i, at(0xEBE4 + i));
        background(181, at(0xEBD8)); // $971A
        records(image_.word(0x9679), 6, 191, [](int, std::uint8_t&, std::uint8_t&) {}); // $9750
        background(206, at(0xEBD9)); // $9721
        background(231, at(0xEBDA)); // $9728
        break;
    case 1: {
        // $9762: the cat's frames step round the records.
        // $9762: records 2-9 get the frames from the phase on, one each;
        // records 0 and 1 keep the last ones written.
        const int phase = tips_cat_phase_ & 3;
        auto cat = [&](int record, std::uint8_t& pointer) {
            if (record == 0) pointer = image_.byte(std::uint16_t(0x97B8 + ((phase + 7) & 3)));
            else if (record == 1) pointer = image_.byte(std::uint16_t(0x97BC + ((phase + 7) & 3)));
            else pointer = image_.byte(std::uint16_t(0x97BC + ((phase + record - 2) & 3)));
        };
        records(image_.word(0x967B), 8, 2, [&](int k, std::uint8_t&, std::uint8_t& pointer) { cat(k, pointer); });
        shared(2, 0x0B, 0x0D);
        background(2, at(0xEBD2));
        background(66, std::uint8_t(at(0xEBD2) ^ 8)); // $97C2
        background(81, at(0xEBF1));                    // $97CB
        for (int i = 0; i < 4; ++i) background(0x81 + 1 + 2 * i, at(0xEBE4 + i)); // $97D2
        background(151, std::uint8_t(at(0xEBD1) ^ 8)); // $97D7
        records(image_.word(0x967D), 2, 151, [&](int k, std::uint8_t&, std::uint8_t& pointer) { cat(8 + k, pointer); });
        break;
    }
    default: {
        // $97F0: no sprites; stripes from two small lists by $EBB8.
        background(2, at(0xEBD2));
        background(66, std::uint8_t(at(0xEBD2) ^ 8));
        const auto phase = colours_.right_index() & 3;
        const auto a = image_.byte(std::uint16_t(0x9821 + phase)), b = image_.byte(std::uint16_t(0x9825 + (phase ^ 3)));
        const int lines[] = {94, 118, 142, 166, 198, 214, 238};
        for (int i = 0; i < 7; ++i) background(lines[i] + 1, i % 2 == 0 ? a : b);
        break;
    }
    }
}

} // namespace wizball::game
