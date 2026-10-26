#include "wizball_c64/game/play.hpp"

// The play screen, laid out the way the original's raster interrupts set up
// the VIC down the frame (play list $B3E2; a handler's changes count from
// the line after its interrupt):
//   line 20  $89BB  the token bar: seven sprites from $B4C6
//   line 48  $8A52  score lines (font charset), playfield sprites, sprite 0
//                   above the wiz (shield top)
//   line 65  $8A25  playfield: multicolour, 38 columns, fine scroll
//   y - 2    $8B0F  sprite 0 shows the wiz
//   y + 20   $8B22  sprite 0 below the wiz (shield bottom)
//   line 232 $8BCB  playfield sprites off
//   line 238 $8B46  the panel: five sprites from $B505
namespace wizball::game {
namespace {
constexpr int hud_row_text = 0x71E2;
} // namespace

Charset Play::play_charset() const {
    Charset cs = assets_.play_charset;
    for (int code = 1; code <= Playfield::sky_codes; ++code) cs[std::size_t(code)] = playfield_.sky_glyph(code);
    const auto& chars = assets_.places[std::size_t(charset_place_)].characters; // as $77C6 last loaded them
    for (std::size_t c = 0; c < chars.size(); ++c) cs[0xA0 + c] = chars[c];
    if (font_in_field_)
        for (int c = 0; c < 0x40; ++c) cs[std::size_t(0xA0 + c)] = field_font_[std::size_t(c)];
    return cs;
}

SpriteShape Play::sprite_shape(int pointer) const {
    SpriteShape shape{};
    if (pointer == 5) return shape; // cleared at the start of play ($720B)
    if (pointer < 0x80) return assets_.sprites[std::size_t(pointer)].rows;
    if (pointer == 0xA0) return place_digit_;
    if (pointer >= 0xB4 && pointer <= 0xB8) { // drawn at run time ($ED00)
        const auto at = work_sprites_.begin() + (pointer - 0xB4) * 0x40;
        std::copy(at, at + 63, shape.begin());
        return shape;
    }
    if (pointer >= 0xB9 && pointer <= 0xBC) {
        const auto& c = cauldrons_[std::size_t(pointer - 0xB9)];
        for (int r = 0; r < 21; ++r)
            for (int b = 0; b < 3; ++b) shape[std::size_t(r * 3 + b)] = c.rows[std::size_t(r)][std::size_t(b)];
    }
    return shape;
}

void Play::put_side_column(Display& d, int column) const {
    const auto cells = playfield_.side_column(column);
    for (int r = 0; r < Playfield::rows; ++r) d.set_side(r + 3, column, cells[std::size_t(r)], field_colour());
}

SpriteSettings Play::catellite_sprite_settings() const {
    const auto& c = shown_sprites_.catellite;
    SpriteSettings s;
    s.enabled = shown_sprites_.mask >> 1 & 1;
    s.x = c.x;
    s.y = c.y;
    s.shape = sprite_shape(c.pointer);
    s.colour = std::uint8_t(c.colour & 15);
    s.multicolour = true;
    return s;
}

Display Play::picture(int extension, int blackout_fade) const {
    Display d;
    d.extension = extension;
    static thread_local Charset font, field;
    font = assets_.front_charset;
    if (letters_inverted_)
        for (int c = 0; c < 0x40; ++c)
            for (auto& b : font[std::size_t(c)]) b = std::uint8_t(~b);
    field = play_charset();

    // Character matrices: the score lines and the playfield.
    d.screens.assign(2, {});
    auto& hud = d.screens[0].codes;
    for (int c = 0; c < 40; ++c) hud[std::size_t(c)] = image_.byte(std::uint16_t(hud_row_text + c)) & 0x3F;
    auto digits = [&](int column, const std::array<std::uint8_t, 7>& s) {
        for (int i = 0; i < 6; ++i) hud[std::size_t(40 + column + i)] = std::uint8_t(0x30 + s[std::size_t(i + 1)]);
    };
    digits(0, scores_[1].digits);
    digits(34, scores_[2].digits);
    for (int i = 0; i < 6; ++i) hud[std::size_t(40 + 17 + i)] = std::uint8_t(0x30 + high_score_[std::size_t(i)]);
    const int lives_column = image_.byte(std::uint16_t(0x71DF + player_));
    hud[std::size_t(40 + lives_column)] = std::uint8_t(std::min<int>(lives_[std::size_t(player_)], 10) + 0x2F);
    const int aliens_column = image_.byte(std::uint16_t(0x71DD + player_));
    hud[std::size_t(40 + aliens_column)] = std::uint8_t(0x30 + aliens_left_ / 10);
    hud[std::size_t(40 + aliens_column + 1)] = std::uint8_t(0x30 + aliens_left_ % 10);
    if (hud_text_shown_) // the lab's words over the score lines ($B01B, $B0EB)
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 16; ++c) hud[std::size_t(r * 40 + 12 + c)] = hud_text_[std::size_t(r)][std::size_t(c)];
    auto& field_codes = d.screens[1].codes;
    // Screen 0 ($E000) is the score lines' screen too: shown, its rows 0-1
    // carry them (line 66, the score's last, already uses the playfield's
    // settings).
    if (playfield_.shown_screen() == 0)
        for (int i = 0; i < 80; ++i) field_codes[std::size_t(i)] = hud[std::size_t(i)];
    for (int r = 0; r < Playfield::rows; ++r)
        for (int c = 0; c < 40; ++c) field_codes[std::size_t((r + 3) * 40 + c)] = playfield_.code(c, r);

    // Colour RAM.
    for (int i = 0; i < 80; ++i) d.colours[std::size_t(i)] = 0x0E;
    const int y0 = hud_flash_ & 7;
    const auto flash = image_.byte(std::uint16_t(0xB5CB + y0 + 1));
    const int player_column = image_.byte(std::uint16_t(0x71DB + player_));
    for (int i = 0; i < 6; ++i) {
        d.colours[std::size_t(player_column + i)] = flash;
        d.colours[std::size_t(40 + player_column + i)] = flash;
        if (new_high_) {
            d.colours[std::size_t(17 + i)] = flash;
            d.colours[std::size_t(40 + 17 + i)] = flash;
        }
    }
    if (hud_band_colour_)
        for (int c = 12; c < 28; ++c) d.colours[std::size_t(c)] = d.colours[std::size_t(40 + c)] = *hud_band_colour_;
    const auto aliens_colour = cleared_at_loop_top_ ? 0 : image_.byte(std::uint16_t(0xB5DF + y0));
    d.colours[std::size_t(40 + aliens_column)] = aliens_colour;
    d.colours[std::size_t(40 + aliens_column + 1)] = aliens_colour;
    d.colours[std::size_t(40 + lives_column)] = image_.byte(std::uint16_t(0xB5D7 + y0));
    for (int i = 80; i < 120; ++i) d.colours[std::size_t(i)] = cell_colours_[0]; // row 2 is filled with the field ($7D1A)
    for (int r = 0; r < Playfield::rows; ++r)
        for (int c = 0; c < 40; ++c) d.colours[std::size_t((r + 3) * 40 + c)] = cell_colour(c, r);

    // Lines.
    LineSettings panel{};
    panel.charset = &font;
    panel.screen = 0;
    d.set_lines_from(0, panel);
    LineSettings score = panel;
    d.set_lines_from(49, score);
    LineSettings play{};
    play.multicolour = true;
    play.columns38 = !columns40_; // $AB9B: the lab in 40 columns
    play.xscroll = std::uint8_t(playfield_.fine_scroll() ^ 7);
    play.charset = &field;
    play.screen = 1;
    play.background = std::uint8_t(flash_background_ & 0x7F);
    // The C64's colours one brightness step darker (for the blackout's fade).
    static constexpr std::array<std::uint8_t, 16> darker{0, 15, 9, 12, 2, 8, 0, 13, 11, 0, 8, 6, 11, 5, 4, 12};
    auto faded = [&](std::uint8_t colour) {
        if (blackout_fade <= 0) return std::uint8_t(0);
        colour &= 15;
        for (int k = 0; k < 4 - blackout_fade; ++k) colour = darker[colour];
        return colour;
    };
    play.shared1 = field_dark_ ? faded(shared1_) : shared1_;
    play.shared2 = field_dark_ ? faded(shared2_) : shared2_;
    d.set_lines_from(66, play);
    // Widescreen: the landscape runs on to the sides while a place scrolls.
    const bool wide = extension > 0 &&
                      (stage_ == Stage::play || stage_ == Stage::vanish || stage_ == Stage::bonus || stage_ == Stage::flight ||
                       stage_ == Stage::game_over) &&
                      !in_lab_;
    if (wide) {
        for (int r = 66; r < 233; ++r) d.line(r).wide = true;
        for (int c = -extension; c < 0; ++c) put_side_column(d, c);
        for (int c = 40; c < 40 + extension; ++c) put_side_column(d, c);
    }
    LineSettings after = play;
    after.background = 0; // $8BCB
    d.set_lines_from(233, after);
    d.set_lines_from(239, panel);
    for (int r = 0; r < Display::raster_lines; ++r) {
        auto& l = d.line(r);
        if (r >= 21 && r < 49) {
            l.sprite_shared1 = weapon_select_ ? 2 : 6; // $89D2
            l.sprite_shared2 = 1;
        } else if (r >= 49 && r < 239) {
            l.sprite_shared1 = sprite_shared_[0]; // $E7E9/$E7EA
            l.sprite_shared2 = sprite_shared_[1];
        } else {
            l.sprite_shared1 = 0x0F;
            l.sprite_shared2 = 0x0B;
        }
    }

    // Sprites: the panel's (they run on into the next frame's top), the
    // token bar, the playfield's.
    auto panel_sprites = [&](int line) {
        static constexpr int slots[] = {0, 1, 2, 4, 5};
        const int xs[] = {image_.byte(0xB505), image_.byte(0xB507), image_.byte(0xB509), image_.byte(0xB50D),
                          image_.byte(0xB50F) | 0x100};
        const int ys[] = {image_.byte(0xB506), image_.byte(0xB508), image_.byte(0xB50A), image_.byte(0xB50E),
                          image_.byte(0xB510)};
        const int colours[] = {panel_colour_shown_, image_.byte(0xB516), image_.byte(0xB517), image_.byte(0xB519), panel_extra_};
        const int pointers[] = {image_.byte(0xB51D), panel_slot1_, image_.byte(0xB51F), image_.byte(0xB521),
                                image_.byte(0xB522)};
        for (int k = 0; k < 5; ++k) {
            SpriteSettings s;
            s.enabled = true;
            s.x = xs[k];
            s.y = ys[k];
            s.shape = sprite_shape(pointers[k]);
            s.colour = std::uint8_t(colours[k] & 15);
            s.multicolour = (0x36 >> slots[k]) & 1;
            d.set_sprite(slots[k], line, s);
        }
        SpriteSettings off;
        d.set_sprite(3, line, off);
        d.set_sprite(6, line, off);
        d.set_sprite(7, line, off);
    };
    panel_sprites(0);
    for (int n = 0; n < 8; ++n) { // line 20: the token bar
        SpriteSettings s;
        s.enabled = n < 7;
        s.x = image_.byte(std::uint16_t(0xB4C6 + 2 * n)) | (n == 6 ? 0x100 : 0);
        s.y = image_.byte(std::uint16_t(0xB4C7 + 2 * n));
        s.shape = sprite_shape(token_pointer_[std::size_t(n)]);
        s.colour = n < 7 ? std::uint8_t(token_colour_shown_[std::size_t(n)] & 15) : 0;
        s.multicolour = true;
        d.set_sprite(n, 21, s);
    }
    // Line 48: the playfield's sprites; sprite 0 above the wiz.
    const int wx = wiz_.x(), wy = wiz_.y();
    SpriteSettings top;
    top.enabled = (arrival_ ? sprite_mask_ : shown_sprites_.mask) & 1;
    top.x = wx;
    top.y = (wy - 0x17) & 0xFF;
    top.shape = sprite_shape(spray_top_); // $B8DE, $B8E0: the spray's ring above
    top.colour = std::uint8_t(spray_tint_ & 15);
    top.multicolour = true;
    top.behind = wiz_.behind_landscape();
    d.set_sprite(0, 49, top);
    // Sprite 1 is the catellite; sprites 2-7 are the objects' ($A29C:
    // $E7C8 on, colours $E7ED on, pointers $B527 on). During a place
    // change, sprites 1-3 are the tunnel exit and the wiz ($827E-$82DE).
    if (flying_text_) {
        // $AE5F: the six text sprites from the table at $B4E5.
        for (int n = 1; n < 8; ++n) {
            SpriteSettings s;
            if (n >= 2) {
                s.enabled = sprite_mask_ >> n & 1;
                s.x = image_.byte(std::uint16_t(0xB4E5 + 2 * n));
                s.y = image_.byte(std::uint16_t(0xB4E6 + 2 * n));
                s.shape = sprite_shape(0xB2 + n);
                s.colour = std::uint8_t(flight_colour_ & 15);
                s.multicolour = false; // $AE81
            }
            d.set_sprite(n, 49, s);
        }
    } else if (in_lab_) {
        d.set_sprite(1, 49, catellite_sprite_settings());
        for (int k = 0; k < 3; ++k) {
            const auto& l = lab_sprites_[std::size_t(k)];
            SpriteSettings s;
            s.enabled = shown_sprites_.mask >> (k + 2) & 1;
            s.x = l.x;
            s.y = l.y;
            s.shape = sprite_shape(l.pointer);
            s.colour = std::uint8_t(l.colour & 15);
            s.multicolour = true; // $AB1A: all multicolour
            d.set_sprite(k + 2, 49, s);
        }
        for (int k = 5; k < 8; ++k) d.set_sprite(k, 49, SpriteSettings{});
    } else if (arrival_) {
        const int xs[] = {0xD0, 0xE8, arrival_->slide_x};
        const int ys[] = {0x46, 0x46, wy};
        const int pointers[] = {0xB5, 0xB7, 0x10};
        const int colours[] = {3, 3, 5};
        for (int n = 1; n < 8; ++n) {
            SpriteSettings s;
            if (n <= 3) {
                s.enabled = sprite_mask_ >> n & 1;
                s.x = xs[n - 1];
                s.y = ys[n - 1];
                s.shape = sprite_shape(pointers[n - 1]);
                s.colour = std::uint8_t(colours[n - 1]);
                s.multicolour = true;
                s.expand_y = n < 3;
            }
            d.set_sprite(n, 49, s);
        }
    } else {
        d.set_sprite(1, 49, catellite_sprite_settings());
        for (int k = 0; k < int(object_sprites_.size()); ++k) {
            const auto& o = shown_sprites_.objects[std::size_t(k)];
            SpriteSettings t;
            t.enabled = shown_sprites_.mask >> (k + 2) & 1;
            t.x = o.x;
            t.y = o.y;
            t.shape = sprite_shape(o.pointer);
            t.colour = std::uint8_t(o.colour & 15);
            t.multicolour = o.multicolour;
            d.set_sprite(k + 2, 49, t);
        }
    }
    SpriteSettings body = top;
    body.y = wy;
    body.shape = sprite_shape(wiz_.frame());
    body.colour = wiz_colour_;
    d.set_sprite(0, wy - 1, body);
    SpriteSettings bottom_part = body;
    bottom_part.y = (wy + 0x17) & 0xFF;
    bottom_part.shape = sprite_shape(spray_bottom_);
    bottom_part.colour = std::uint8_t(spray_tint_ & 15);
    d.set_sprite(0, wy + 21, bottom_part);
    SpriteSettings cleared = bottom_part;
    cleared.x = 0;
    cleared.behind = false;
    d.set_sprite(0, 233, cleared);
    panel_sprites(239);
    return d;
}

} // namespace wizball::game
