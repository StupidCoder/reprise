#include "fort_apocalypse_c64/game/front_end.hpp"

namespace fort_apocalypse::game {
namespace {

// Cell colours the init gives the screen ($AFA0 called with $0C, then
// $89D5-$8A9D), which the title shows and the options screen inherits.
std::uint8_t title_colour(int column, int row) {
    const int at = row * 40 + column;
    if (row >= 9 && row <= 11) {
        return 0x0F; // $8A90: $D968-$D9DF
    }
    if (at >= 761 && at <= 880) {
        return 0x0B; // $8A99: $DAF9-$DB70 (row 19, column 1 to row 22, column 0)
    }
    if ((row == 0 && column >= 19 && column <= 30) ||
        (row == 3 && (column <= 11 || (column >= 27 && column <= 38)))) {
        return 0x0F; // $AFCA
    }
    if (row == 0 || row == 5) {
        return 0x0B;
    }
    return 0x0C;
}

// The flashing frame of the title ($89E7-$8A24): rows 6 and 24 and the two
// side columns alternate between characters $3D and $3C. The side loop runs
// after the rows and writes column 39 of rows 6-23 together with column 0 of
// the row below, so the corners at (39, 6) and (0, 24) take its values.
std::uint8_t frame_character(int column, int row) {
    std::uint8_t code = 0;
    if (row == 6 || row == 24) {
        code = column % 2 == 0 ? 0x3D : 0x3C;
    }
    if (column == 39 && row >= 6 && row <= 23) {
        code = (row - 6) % 2 == 0 ? 0x3D : 0x3C;
    }
    if (column == 0 && row >= 7 && row <= 24) {
        code = (row - 7) % 2 == 0 ? 0x3D : 0x3C;
    }
    return code;
}

// Positions the code passes to the text routine; text row r is screen row
// 6 + r ($9B35 counts from $44F0).
constexpr int text_row(int r) {
    return 6 + r;
}

} // namespace

FrontEnd::FrontEnd(const data::Assets& assets) : assets_(assets) {}

FrontEnd::Outcome FrontEnd::tick(const Controls& controls) {
    if (miss_interrupt_) {
        miss_interrupt_ = false;
        return Outcome::none;
    }
    ++frame_;
    return screen_ == Screen::title ? title_tick(controls) : options_tick(controls);
}

void FrontEnd::read_space(const Controls& controls) {
    // $AFED skips the keyboard while joystick 2 is in use.
    if (controls.up || controls.down || controls.left || controls.right || controls.fire) {
        return;
    }
    if (controls.space != space_down_) {
        space_down_ = controls.space;
        space_event_ = controls.space;
    }
}

FrontEnd::Outcome FrontEnd::title_tick(const Controls& controls) {
    read_space(controls);
    if ((frame_ & 3) == 0) {
        colours_swapped_ = !colours_swapped_; // $8ABA: swap $D022 and $D023
    }
    if ((frame_ & 7) == 0) {
        ++title_timer_;
    }
    // The title also plays a rising two-voice tone from the timer ($8AD0);
    // sound is not rebuilt yet.
    if (title_timer_ == 0xD3) {
        settings_.gravity = 0; // $8B1E: the attract game always uses weak gravity
        return Outcome::attract_game;
    }
    if (controls.fire || controls.key == FunctionKey::f7) {
        selected_ = 0;
        return Outcome::new_game;
    }
    if (controls.key == FunctionKey::f3 || controls.key == FunctionKey::f5) {
        enter_options();
    }
    return Outcome::none;
}

void FrontEnd::enter_options() {
    // $8B10: mode 9, option 0; $8B97 copies the HUD and clears the rest of
    // the screen. The interrupt draws the counters from the next frame on,
    // the main loop the option texts once it sees a new key.
    screen_ = Screen::options;
    selected_ = 0;
    hud_interrupt_ran_ = false;
    options_drawn_ = false;
}

void FrontEnd::enter_title() {
    // $89C7: the title is drawn afresh; its timer and colours restart, the
    // settings stay.
    screen_ = Screen::title;
    title_timer_ = 0;
    colours_swapped_ = false;
    miss_interrupt_ = true; // observed: $15 does not advance in the next frame
}

void FrontEnd::use_key(FunctionKey key) {
    // $9351: F3 selects the next option, F5 the next value of the selected
    // one; then the whole options screen is drawn again.
    if (key == FunctionKey::f3) {
        selected_ = (selected_ + 1) % 3;
    } else if (key == FunctionKey::f5) {
        auto& value = settings_.value(selected_);
        value = (value + 1) % 3;
    }
    options_drawn_ = true;
}

FrontEnd::Outcome FrontEnd::options_tick(const Controls& controls) {
    // The interrupt: input, and the counters in the HUD.
    read_space(controls);
    hud_interrupt_ran_ = true;

    // The main loop pass after it.
    if (paused_) {
        // $9333: wait for space again, a function key or fire.
        if (space_event_ || controls.key != FunctionKey::none || controls.fire) {
            paused_ = false;
            space_event_ = false;
        } else {
            return Outcome::none;
        }
    } else if (controls.key != last_key_) { // $92E2
        last_key_ = controls.key;
        idle_ = 0;
        if (controls.key == FunctionKey::f7) {
            return Outcome::new_game; // $92F2
        }
        use_key(controls.key);
    } else if (space_event_) { // $9322
        space_event_ = false;
        paused_ = true;
        return Outcome::none;
    }
    if (frame_ & 4) { // $8BF8
        if (--idle_ == 0) {
            enter_title();
        }
    }
    return Outcome::none;
}

void FrontEnd::draw(c64::TextFrame& frame) const {
    frame.clear();
    frame.set_idle_pattern(assets_.idle_pattern);
    if (screen_ == Screen::title) {
        draw_title(frame);
    } else {
        draw_options(frame);
    }
}

void FrontEnd::draw_title(c64::TextFrame& frame) const {
    for (int row = 0; row < c64::TextFrame::rows; ++row) {
        for (int column = 0; column < c64::TextFrame::columns; ++column) {
            const auto code = frame_character(column, row);
            frame.set(column, row, code ? hud_glyph(assets_, code) : nullptr,
                      title_colour(column, row));
        }
    }
    const auto& t = assets_.texts;
    draw_text(frame, assets_, t.title[0], 5, text_row(3));
    draw_text(frame, assets_, t.title[1], 6, text_row(6));
    draw_text(frame, assets_, t.title[2], 2, text_row(8));
    draw_text(frame, assets_, t.title[3], 6, text_row(10));
    draw_text(frame, assets_, t.copyright, 5, text_row(13));
    for (std::size_t i = 0; i < t.year.size(); ++i) {
        frame.set_glyph(26 + int(i), 19, hud_glyph(assets_, t.year[i])); // $8A76: to $4712
    }
    draw_text(frame, assets_, t.title[4], 4, text_row(15));

    // $D022/$D023 start as 2 and 1 ($89CD) and swap every 4 frames.
    const std::uint8_t a = colours_swapped_ ? 1 : 2, b = colours_swapped_ ? 2 : 1;
    frame.set_lines({0, a, b, 0, true, false, false, 0, 3}); // $D011 = $1B, $D016 = $18
}

void FrontEnd::draw_options(c64::TextFrame& frame) const {
    for (int row = 0; row < c64::TextFrame::rows; ++row) {
        for (int column = 0; column < c64::TextFrame::columns; ++column) {
            std::uint8_t colour = title_colour(column, row);
            if (row >= 13 && row <= 17) {
                colour = 0x0D; // $93E1
            }
            if (row >= 13 + 2 * selected_ && row <= 14 + 2 * selected_) {
                colour = 0x0C; // $93F9: the selected option's two rows
            }
            frame.set(column, row, nullptr, colour);
        }
    }
    draw_hud(frame, assets_, counters_, hud_interrupt_ran_);
    if (options_drawn_) {
        const auto& t = assets_.texts;
        draw_text(frame, assets_, t.options, 13, text_row(1));
        draw_text(frame, assets_, t.option, 0, text_row(3));
        draw_text(frame, assets_, t.select, 28, text_row(3));
        for (int s = 0; s < 3; ++s) {
            draw_text(frame, assets_, t.setting_names[std::size_t(s)], 0, text_row(7 + 2 * s));
            draw_text(frame, assets_,
                      t.setting_values[std::size_t(s)][std::size_t(settings_.value(s))], 28,
                      text_row(7 + 2 * s));
        }
    }
    // Only the HUD interrupt runs here ($9AB3 sets $12 = 1, so $9C2E does not
    // install the playfield part): the whole screen has its setup, vertical
    // scroll 7 ($D011 = $1F), 40 columns, black. Nothing sets the shared
    // colours, so they stay as the title's flashing left them.
    const std::uint8_t a = colours_swapped_ ? 1 : 2, b = colours_swapped_ ? 2 : 1;
    frame.set_lines({0, a, b, 0, true, false, false, 0, std::uint8_t(hud_interrupt_ran_ ? 7 : 3)});
}

} // namespace fort_apocalypse::game
