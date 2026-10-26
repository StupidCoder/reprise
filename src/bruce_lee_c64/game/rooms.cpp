// The rooms' own rules: what each room sets up when Bruce enters it
// ($33A0 through the table at $4EA9) and what it does every pass ($3A5C
// through the table at $4ED1). Most of it is one-shot changes once
// lanterns are taken (walls open, floors close, things stop), plus the
// moving machinery: movers, sliders, the lift glyphs, crumbling floor
// cells, room 13's moving pieces, room 18's fireballs and room 19's end.
//
// Texts are the game's strings ($43AB), printed at a row index (screen
// rows 2n-1 and 2n) and a column; the scripts' numbers are those of the
// original's code.
#include "bruce_lee_c64/game/world.hpp"

namespace bruce_lee::game {
namespace {

constexpr std::uint8_t sound_blink = 0x81, sound_fireball = 0x86, sound_slider = 0x89, sound_open = 0x8A,
                       sound_flash = 0x8B, sound_crumble = 0x8D;

bool negative(std::uint8_t v) {
    return v & 0x80;
}

// The texts the column helpers print from the top down ($3A77, $3A8D,
// $3A97, $3AA1): two cleared cells and a closing one.
constexpr std::array<std::uint8_t, 3> open_column{0x13, 0x13, 0x14};
constexpr std::array<std::uint8_t, 3> open_column_floor{0x13, 0x13, 0x17};
constexpr std::array<std::uint8_t, 3> open_column_ledge{0x13, 0x13, 0x2D};
constexpr std::array<std::uint8_t, 3> close_column{0x63, 0x63, 0x64};

// First texts of the two mover figures ($4073 family: $1F, $4091 family: $26).
constexpr int figure_a = 0x1F, figure_b = 0x26;

} // namespace

bool World::hanging(int r, int lantern) const {
    return progress[std::size_t(r)].lanterns[std::size_t(lantern)];
}

bool World::once(int counter) {
    auto& c = room_counters[std::size_t(counter)];
    if (negative(c)) {
        return false;
    }
    --c;
    return true;
}

void World::set_colour(int column, int row, std::uint8_t colour) {
    field.at(column, row).colour = colour & 15;
}

// $3A77: a sound, then three texts down one column; the $3A8D/$3A97/$3AA1
// variants differ in the texts.
void World::print_column(int row, int column, std::array<std::uint8_t, 3> texts) {
    request_sound(sound_open);
    for (int i = 0; i < 3; ++i) {
        print(texts[std::size_t(i)], row + i, column);
    }
}

void World::print_at_mover(int mover, int text) {
    print(text, movers[std::size_t(mover)].row, movers[std::size_t(mover)].column);
}

// The pattern at $37FE: frames 0-6 of a figure; at frame 3 it steps a
// column on (its old place gets frame 6, the new one frame 2) until the
// limit; after frame 6 it goes back and waits.
void World::step_mover(int i, int first_text, std::uint8_t limit, std::uint8_t restart_column, std::uint8_t wait) {
    Mover& m = movers[std::size_t(i)];
    ++m.frame;
    if (negative(m.frame)) {
        return;
    }
    if (m.frame == 7) {
        m.frame = wait;
        m.column = restart_column;
        return;
    }
    if (m.frame == 3 && m.column < limit) {
        print_at_mover(i, first_text + 6);
        ++m.column;
        m.frame = 2;
    }
    print_at_mover(i, first_text + m.frame);
}

// The slider steps compare the place before the step.
void World::slide_right(std::uint8_t last, std::uint8_t restart) {
    slider.was = slider.at;
    ++slider.at;
    if (slider.was >= last) {
        slider.at = restart;
    }
}

void World::slide_left(std::uint8_t first, std::uint8_t restart) {
    slider.was = slider.at;
    --slider.at;
    if (slider.was < first) {
        slider.at = restart;
    }
}

// $403B / $4050
void World::draw_slider(int erase_text, int piece_text) {
    request_sound(sound_slider);
    print(erase_text, slider.row, slider.was);
    print(piece_text, slider.row, slider.at);
}

// $2C5D: the lift. A step comes every few passes (the speed table); 16
// steps per speed, the speed running down to the pause at index 0, then
// back at 7 in the other direction. A step scrolls the glyphs' pattern:
// pattern byte k (from the phase on, wrapping) fills two rows of the
// upper glyph (k < 4) or of its lower partner (glyph + $80).
void World::step_lift() {
    Lift& l = lift;
    const auto& speeds = assets.lift_speeds;
    bool next_speed = false;
    if (!negative(l.timer)) {
        if (--l.timer != 0) {
            return;
        }
        l.timer = speeds[l.speed];
        next_speed = negative(--l.rounds);
    } else {
        const std::uint8_t left = std::uint8_t((l.timer & 0x7F) - 1);
        if (left != 0) {
            l.timer = left | 0x80;
            return;
        }
        // L2C72: a pause: no step until its rounds are over.
        l.timer = speeds[l.speed];
        if (!negative(--l.rounds)) {
            return;
        }
        next_speed = true;
    }
    if (next_speed) {
        if (negative(--l.speed)) {
            l.speed = 7;
            l.down ^= 1;
        }
        l.timer = speeds[l.speed];
        l.rounds = 0x10;
    }
    if (!l.down) {
        if (++l.phase >= 8) {
            l.phase = 0;
        }
    } else if (negative(--l.phase)) {
        l.phase = 7;
    }
    const std::size_t set = room < 10 ? 0 : 1; // $7C: the charset at $A000 or $A800
    const auto& patterns = assets.lift_patterns[set];
    auto& charset = charsets[set];
    for (std::size_t g = 0; g < 3; ++g) {
        std::size_t k = l.phase;
        for (std::size_t y = 0; y < 8; ++y) {
            const auto value = patterns[g * 8 + k];
            auto& glyph = charset[0x61 + g + (y < 4 ? 0 : 0x80)];
            glyph[2 * (y & 3)] = value;
            glyph[2 * (y & 3) + 1] = value;
            k = (k + 1) & 7;
        }
    }
}

// $3215: a stepped-on floor cell cracks after 8 passes, opens at 12,
// cracks again at 16, mends at 20 (its colour back to $F8) and is free
// again at 24.
void World::step_crumbles() {
    for (int i = 2; i >= 0; --i) {
        Crumble& c = crumbles[std::size_t(i)];
        if (c.timer == 0) {
            continue;
        }
        const auto t = c.timer++;
        switch (t) {
        case 8:
            request_sound(sound_crumble);
            [[fallthrough]];
        case 16:
            print(0x2F, c.row, c.column);
            print(0x30, c.row + 1, c.column);
            break;
        case 12:
            print(0x31, c.row, c.column);
            print(0x32, c.row + 1, c.column);
            break;
        case 20: {
            print(0x00, c.row, c.column);
            // The printer leaves its column after the text; two back and
            // two screen rows down is the stepped-on cell.
            const int text_end = c.column + int(assets.strings[0].size());
            const int top = c.row < 2 ? c.row : 2 * c.row - 1;
            set_colour(text_end - 2, top + 2, 0xF8);
            print(0x33, c.row + 1, c.column);
            break;
        }
        case 24:
            c.timer = 0;
            break;
        default:
            break;
        }
    }
}

// $34B0: room 13's pieces; later rounds ($40) move them over other spans.
void World::setup_slides() {
    slides_set = true;
    const bool later = flag40;
    const auto pick = [&](std::uint8_t first_round, std::uint8_t later_rounds) {
        return later ? later_rounds : first_round;
    };
    constexpr std::uint8_t left = 0x02, right = 0x82;
    Slide unused = slides[3];
    unused.mode = 0; // $0112 = 0; its other values stay
    slides = {{
        {left, 0x21, 0x07, pick(0x14, 0x12), 0x21},
        {left, pick(0x13, 0x15), 0x0A, 0x06, pick(0x13, 0x15)},
        {left, 0x02, 0x0C, 0x02, pick(0x21, 0x25)},
        unused,
        {left, 0x16, 0x0C, 0x02, pick(0x21, 0x25)},
        {right, 0x06, 0x05, 0x06, pick(0x22, 0x26)},
        {right, 0x16, 0x05, 0x06, pick(0x22, 0x26)},
        {right, pick(0x06, 0x02), 0x07, pick(0x06, 0x02), pick(0x14, 0x12)},
        {right, pick(0x14, 0x16), 0x0A, pick(0x14, 0x16), pick(0x22, 0x26)},
    }};
}

// $1042: each piece a column on, wrapping; rows 5 and 10 use other texts.
// The slider's variables serve as the drawing's arguments.
void World::step_slides() {
    for (int i = 8; i >= 0; --i) {
        Slide& s = slides[std::size_t(i)];
        if (s.mode == 0) {
            continue;
        }
        slider.was = s.at;
        slider.row = s.row;
        if (!negative(s.mode)) {
            --s.at;
            if (s.at < s.low) {
                s.at = s.high;
            }
        } else {
            ++s.at;
            if (s.at >= s.high) {
                s.at = s.low;
            }
        }
        slider.at = s.at;
        if (s.row == 0x05 || s.row == 0x0A) {
            draw_slider(0x55, 0x54);
        } else {
            draw_slider(0x1C, 0x1B);
        }
    }
}

// $41EE / $420D
void World::start_fireball(int i) {
    Fireball& f = fireballs[std::size_t(i)];
    f.active = true;
    f.x = i == 0 ? 0x50 : 0x5E;
    f.y = 0x37;
    sprite_pointers[6 + std::size_t(i)] = i == 0 ? 0x0D : 0x0E; // $41F0 / $420F
    request_sound(sound_fireball);
}

// $4120
void World::step_fireballs() {
    for (std::size_t row = 0; row < 4; ++row) {
        const auto mask = assets.fireball_masks[3 - row];
        fireballs[0].shape[row] = draw(0) & mask;
        fireballs[1].shape[row] = draw(0) & mask;
    }
    bool started = false;
    if (!fireballs[0].active) {
        if (fireballs[0].wait == 0) {
            start_fireball(0);
            started = true; // the other one is not looked at this pass
        } else {
            --fireballs[0].wait;
        }
    }
    if (!started && !fireballs[1].active) {
        if (fireballs[1].wait == 0) {
            start_fireball(1);
        } else {
            --fireballs[1].wait;
        }
    }
    for (int i = 1; i >= 0; --i) {
        Fireball& f = fireballs[std::size_t(i)];
        if (f.active) {
            f.y = std::uint8_t(f.y + 5);
            if (f.y >= 0xE0) {
                f.active = false;
                f.wait = 2;
            }
        }
    }
    const std::uint8_t bruce_x = actors.slots[0].x;
    for (int i = 1; i >= 0; --i) {
        Fireball& f = fireballs[std::size_t(i)];
        if (!f.active) {
            continue;
        }
        // Three steps towards Bruce's x high up, two near the floor.
        const int steps = f.y >= 0xA8 ? 2 : 3;
        for (int s = 0; s < steps; ++s) {
            if (f.x < bruce_x) {
                ++f.x;
            } else if (f.x != bruce_x) {
                --f.x;
            }
        }
        f.colour = (draw(0) | 0xF0) & 15;
    }
}

// $2FEC: room 16's extra fall is drawn over.
void World::erase_extra_fall() {
    print(0x56, 0x0A, 0x24);
    print(0x56, 0x0B, 0x24);
}

// $33A0: the counters cleared, then the room's setup.
void World::setup_room() {
    room_counters = {};
    switch (room) {
    case 1:
    case 3:
        room_counters[0] = 3;
        break;
    case 4:
        room_counters[3] = 0x1F;
        movers[0] = {0xFF, 0x01, 0x0B};
        movers[1] = {0xFF, 0x0B, 0x07};
        movers[2] = {0xF4, 0x1A, 0x07};
        break;
    case 5:
        for (int c = 34; c <= 36; ++c) {
            set_colour(c, 3, 0xFC);
            set_colour(c, 4, 0xFC);
        }
        movers[0] = {0xFF, 0x1B, 0x09};
        movers[1] = {0xFC, 0x1B, 0x0A};
        movers[2] = {0xF8, 0x01, 0x05};
        break;
    case 7:
        slider.at = slider.was = 0x05;
        slider.row = 0x0B;
        movers[0].column = 0x22;
        movers[0].row = 0x04;
        movers[0].frame = 0xFF;
        break;
    case 8:
        movers[0].column = 0x06;
        movers[1].column = 0x1B;
        movers[0].row = movers[1].row = 0x05;
        movers[0].frame = 0xFF;
        movers[1].frame = 0xF2;
        break;
    case 9:
        for (int c = 35; c <= 38; ++c) {
            set_colour(c, 3, 0xFA);
            set_colour(c, 4, 0xFA);
        }
        slider.at = slider.was = 0x04;
        slider.row = 0x0C;
        movers[0].column = movers[1].column = 0x10;
        movers[0].row = 0x05;
        movers[1].row = 0x06;
        movers[0].frame = 0xFC;
        movers[1].frame = 0xFF;
        break;
    case 12:
    case 14:
        room_counters[0] = 1;
        break;
    case 13:
        setup_slides();
        break;
    case 15:
        room_counters[0] = room_counters[1] = room_counters[2] = 1;
        break;
    case 16:
        for (int row = 19; row <= 22; ++row) {
            set_colour(36, row, 0xFF);
            set_colour(37, row, 0xFF);
        }
        if (extra_falls[std::size_t(player)] == 0) {
            erase_extra_fall();
        }
        break;
    case 17:
        set_colour(38, 19, 0xFF);
        room_counters[0] = room_counters[1] = 1;
        movers[0] = {0xF8, 0x08, 0x03};
        movers[1] = {0xF1, 0x08, 0x07};
        movers[2] = {0xEA, 0x08, 0x09};
        slider.at = 0x0B;
        slider.row = 0x05;
        break;
    case 18:
        for (int row = 3; row < Playfield::rows; ++row) {
            for (int c = 0; c < Playfield::columns; ++c) {
                set_colour(c, row, 0xFE);
            }
        }
        room_counters[0] = room_counters[1] = 1;
        fireballs = {};
        fireballs[1].wait = 0x10;
        break;
    case 19:
        countdown = 0x0458;
        for (int c = 0; c < Playfield::columns; ++c) {
            field.at(c, 0).code = 0x00;
        }
        break;
    default:
        break; // rooms 0, 2, 6, 10, 11: nothing
    }
}

void World::room_script(PassEvents& events) {
    switch (room) {
    case 0: script_room_0(); break;
    case 1: script_room_1(); break;
    case 2: script_room_2(); break;
    case 3: script_room_3(); break;
    case 4: script_room_4(); break;
    case 5: script_room_5(); break;
    case 6: script_room_6(); break;
    case 7: script_room_7(); break;
    case 8: script_room_8(); break;
    case 9: script_room_9(); break;
    case 10: script_room_10(); break;
    case 11: script_room_11(); break;
    case 12: script_room_12(); break;
    case 13: script_room_13(); break;
    case 14: script_room_14(); break;
    case 15: script_room_15(); break;
    case 16: script_room_16(); break;
    case 17: script_room_17(); break;
    case 18: script_room_18(); break;
    case 19: script_room_19(events); break;
    default: break;
    }
}

// $362F: once room 6 is cleared, something at (9, 7) blinks.
void World::script_room_0() {
    if (!cleared(6) || off_beat) {
        return;
    }
    if (!negative(--room_counters[0])) {
        return;
    }
    if (room_counters[1] == 0) {
        request_sound(sound_blink);
        print(0x1D, 9, 0x07);
        ++room_counters[1];
    } else {
        print(0x1E, 9, 0x07);
        --room_counters[1];
        room_counters[0] = 5;
    }
}

// $3666: with rooms 0-2 cleared (and room 6 not yet), the floor at
// (12, $11) opens in four steps.
void World::script_room_1() {
    if (off_beat || negative(room_counters[0]) || cleared(6) ||
        !(cleared(0) && cleared(1) && cleared(2))) {
        return;
    }
    const std::uint8_t step = --room_counters[0];
    if (step == 2) {
        request_sound(sound_open);
    }
    print(std::uint8_t(step + 0x10), 0x0C, 0x11);
}

// $3693: once room 6 is cleared, a passage at (11, $25) opens and the
// sign at (9, $21) blinks.
void World::script_room_2() {
    if (!cleared(6)) {
        return;
    }
    if (once(1)) {
        request_sound(sound_open);
        print(0x1A, 0x0B, 0x25);
        print(0x07, 0x0C, 0x25);
    }
    if (off_beat || !negative(--room_counters[2])) {
        return;
    }
    if (room_counters[0] == 0) {
        request_sound(sound_blink);
        print(0x1D, 9, 0x21);
        ++room_counters[0];
    } else {
        print(0x1E, 9, 0x21);
        --room_counters[0];
        room_counters[2] = 5;
    }
}

// $36EF: room 3 cleared opens the floor at (12, $1D); two platforms in row
// 6 take turns every $21 passes.
void World::script_room_3() {
    if (!off_beat && !negative(room_counters[0]) && cleared(3)) {
        const std::uint8_t step = --room_counters[0];
        if (step == 2) {
            request_sound(sound_open);
        }
        print(std::uint8_t(step + 0x10), 0x0C, 0x1D);
    }
    if (!negative(--room_counters[2])) {
        return;
    }
    room_counters[2] = 0x20;
    if (room_counters[1] != 0) {
        --room_counters[1];
        print(0x06, 6, 0x09);
        print(0x60, 6, 0x15);
    } else {
        ++room_counters[1];
        print(0x60, 6, 0x09);
        print(0x06, 6, 0x15);
    }
}

// $374D: two lanterns blink while they hang; taking one clears its wall
// and stops its mover (the second also resets the lift).
void World::script_room_4() {
    if (hanging(4, 0)) {
        if (++room_counters[2] == 0x20) {
            print(0x34, 4, 0x0B);
        } else if (room_counters[2] == 0x40) {
            room_counters[2] = 0;
            print(0x36, 4, 0x0B);
        }
    }
    if (!hanging(4, 0) && once(0)) {
        request_sound(sound_open);
        print(0x16, 8, 0x0B);
        print(0x16, 9, 0x0B);
        print_at_mover(1, figure_a + 6);
    }
    if (hanging(4, 1)) {
        if (++room_counters[3] == 0x20) {
            print(0x35, 4, 0x1C);
        } else if (room_counters[3] == 0x40) {
            room_counters[3] = 0;
            print(0x37, 4, 0x1C);
        }
    }
    if (!hanging(4, 1) && once(1)) {
        lift.down = 0;
        lift.speed = 7;
        request_sound(sound_open);
        print(0x16, 8, 0x19);
        print(0x16, 9, 0x19);
        print_at_mover(2, figure_a + 6);
    }
    step_mover(0, figure_b, 0x03, 0x01, 0xA0);
    if (hanging(4, 0)) {
        step_mover(1, figure_a, 0x0C, 0x0B, 0xE4);
    }
    if (hanging(4, 1)) {
        step_mover(2, figure_a, 0x1B, 0x1A, 0xE4);
    }
    step_lift();
}

// $3898
void World::script_room_5() {
    if (!hanging(5, 8) && once(2)) {
        print_column(0x0A, 0x19, open_column);
        print_column(0x0A, 0x1A, open_column);
        print_at_mover(1, figure_a + 6);
    }
    if (!hanging(5, 6) && once(3)) {
        print_column(0x0A, 0x00, open_column);
        print(0x15, 0x0B, 0x00);
    }
    const bool upper = cleared(6) && (!hanging(5, 2) || !hanging(5, 4));
    if (upper && once(4)) {
        print(0x19, 2, 0x22);
        print_column(3, 0x1E, open_column);
        print_column(6, 0x20, open_column);
    }
    step_mover(0, figure_a, 0x1C, 0x1B, 0xE0);
    if (hanging(5, 8)) {
        step_mover(1, figure_a, 0x1C, 0x1B, 0xE0);
    }
    step_mover(2, figure_b, 0x02, 0x01, 0xE2);
    step_lift();
}

// $3997
void World::script_room_6() {
    if (cleared(6) && once(0)) {
        print_column(5, 0x01, open_column_floor);
    }
    step_crumbles();
    step_lift();
}

// $39B0: an opening drawn ends the pass's script.
void World::script_room_7() {
    step_crumbles();
    if (!hanging(7, 0) && !hanging(7, 1) && !hanging(7, 2) && !hanging(7, 3) && !hanging(7, 4) && once(0)) {
        print_column(2, 0x21, open_column_floor);
        return;
    }
    bool lower_row_taken = true;
    for (int i = 5; i <= 10; ++i) {
        lower_row_taken = lower_row_taken && !hanging(7, i);
    }
    if (lower_row_taken && once(1)) {
        print_column(5, 0x26, open_column_floor);
        return;
    }
    step_mover(0, figure_b, 0x24, 0x22, 0xE0);
    if (hanging(7, 11) || hanging(7, 12)) {
        slide_right(0x23, 0x05);
    } else {
        slide_left(0x06, 0x24);
    }
    draw_slider(0x1C, 0x1B);
}

// $3AC1
void World::script_room_8() {
    if (cleared(7) && !hanging(8, 2) && once(2)) {
        print_column(7, 0x0E, open_column);
    }
    if (!hanging(8, 0) && !hanging(8, 1) && once(3)) {
        if (hanging(8, 3)) {
            lift.down = 1;
            lift.speed = 7;
        }
        print_column(7, 0x19, open_column);
    }
    if (cleared(8) && !hanging(9, 2) && !hanging(9, 3) && once(4)) {
        print_column(2, 0x26, open_column_ledge);
    }
    if (!hanging(8, 2) && once(0)) {
        print_column(0x0A, 0x01, open_column);
    }
    if (cleared(7) && cleared(8) && once(1)) {
        print_column(0x0A, 0x26, open_column);
    }
    step_mover(0, figure_a, 0x0B, 0x06, 0xDA);
    step_mover(1, figure_a, 0x20, 0x1B, 0xDA);
    step_lift();
}

// $3B9F
void World::script_room_9() {
    if (!negative(room_counters[2]) && !hanging(9, 0)) {
        --room_counters[2];
        lift.down = 1;
        lift.speed = 7;
    }
    if (!hanging(9, 1) && once(0)) {
        print_column(7, 0x17, open_column);
        lift.down = 0;
        lift.speed = 7;
    }
    if (cleared(9) && once(1)) {
        lift.down = 0;
        lift.speed = 7;
        request_sound(sound_open);
        print(0x18, 2, 0x23);
    }
    if (!hanging(9, 2) && !hanging(9, 3)) {
        slide_left(0x05, 0x18);
    } else {
        slide_right(0x18, 0x04);
    }
    draw_slider(0x1C, 0x1B);
    step_mover(0, figure_b, 0x14, 0x10, 0xC0);
    step_mover(1, figure_b, 0x14, 0x10, 0xC0);
    step_lift();
}

// $3C7D (the check of the protection byte at $00 left out)
void World::script_room_10() {
    if (!negative(room_counters[0]) && !hanging(10, 0) && !hanging(10, 1)) {
        --room_counters[0];
        print_column(4, 0x26, close_column);
    }
    step_crumbles();
}

// $3CAD
void World::script_room_11() {
    if (!negative(room_counters[0]) && !hanging(11, 2)) {
        --room_counters[0];
        lift.down = 0;
        lift.speed = 7;
    }
    step_crumbles();
    step_lift();
}

// $3E39 (the first counter starts at 1: the column is drawn twice)
void World::script_room_12() {
    if (!negative(room_counters[0]) && !hanging(12, 2)) {
        --room_counters[0];
        print_column(0x0A, 0x26, close_column);
    }
    if (!negative(room_counters[1]) && cleared(12) && !hanging(14, 2) && !hanging(14, 3)) {
        --room_counters[1];
        print_column(7, 0x26, close_column);
    }
    step_crumbles();
}

// $3E69: falling through the room scores 500 at each of three heights,
// once per height until the next round; otherwise the pieces move every
// other pass. (The sprite mask at the end, $D015 AND 3, hides sprites
// that the quiet room never shows.)
void World::script_room_13() {
    const Actor& bruce = actors.slots[0];
    if (bruce.y >= 0x64) {
        const int height = bruce.y < 0x8A ? 0 : bruce.y < 0xB6 ? 1 : 2;
        auto& bonus = drop_bonus[std::size_t(player)][std::size_t(height)];
        if (bonus != 0) {
            --bonus;
            add_score(0x1F4); // 500
            return;
        }
    }
    if (!off_beat) {
        step_slides();
    }
}

// $3EB9
void World::script_room_14() {
    if (!negative(room_counters[0]) && !hanging(14, 0) && !hanging(14, 1)) {
        --room_counters[0];
        print_column(4, 0x26, close_column);
    }
    step_crumbles();
}

// $3ED3 (the counters start at 1: each change is drawn twice)
void World::script_room_15() {
    if (!negative(room_counters[0]) && !hanging(15, 0)) {
        print(0x57, 3, 0x11);
        print(0x58, 4, 0x11);
        --room_counters[0];
        request_sound(sound_open);
    }
    if (!negative(room_counters[1]) && !hanging(15, 1) && !hanging(15, 2) && !hanging(15, 4)) {
        print(0x57, 3, 0x17);
        print(0x58, 4, 0x17);
        --room_counters[1];
        request_sound(sound_open);
    }
    if (!negative(room_counters[2]) && cleared(15)) {
        print(0x59, 9, 0x25);
        print(0x5A, 8, 0x25);
        print(0x5B, 7, 0x25);
        request_sound(sound_open);
        --room_counters[2];
    }
}

// $3F48
void World::script_room_16() {
    if (!negative(room_counters[0]) && cleared(16)) {
        --room_counters[0];
        print_column(7, 0x01, close_column);
    }
    step_crumbles();
}

// $3F5E: row 3 flashes every 11 passes, the slider moves every other
// pass, the lanterns open two passages, three movers run along.
void World::script_room_17() {
    if (negative(--room_counters[2])) {
        room_counters[2] = 0x0A;
        const std::uint8_t colour = std::uint8_t(((field.at(8, 3).colour & 7) ^ 2) | 0xF8);
        for (int c = 8; c <= 8 + 0x1B; ++c) {
            set_colour(c, 3, colour);
        }
    }
    --room_counters[3];
    if (room_counters[3] != 0) {
        if (negative(room_counters[3])) {
            room_counters[3] = 1;
        }
        slide_right(0x1B, 0x0B);
        draw_slider(0x1C, 0x1B);
    }
    if (room_counters[0] != 0 && !hanging(17, 0)) {
        --room_counters[0];
        print(0x5D, 0x0A, 0x25);
    }
    if (room_counters[1] != 0 && !hanging(17, 1)) {
        --room_counters[1];
        print(0x5C, 0x0A, 0x18);
    }
    step_crumbles();
    // $3FC5: the movers from the last; the first stops earlier and waits
    // less long.
    for (int i = 2; i >= 0; --i) {
        step_mover(i, figure_a, i == 0 ? 0x23 : 0x25, 0x08, i == 0 ? 0xC8 : 0xCA);
    }
}

// $3CC9: the last fight. While the lantern hangs, fireballs fall towards
// Bruce (touching one is death) and the face at (2, $10) turns to him
// every 8 passes, its mouth working. Taking the lantern scores 3000 and
// sets off $50 passes of flashing; then the way out at (11, $25) opens.
void World::script_room_18() {
    Actor& bruce = actors.slots[0];
    int face = 1;
    if (room_counters[0] == 0) {
        // L3D9C
        if (room_counters[1] == 0) {
            print(0x5E, 0x0B, 0x25);
            print(0x5F, 0x0C, 0x26);
            return;
        }
        const std::uint8_t left = room_counters[1];
        std::uint8_t colour = std::uint8_t((draw(0) & 0x0F) | 0x80); // drawn, then replaced
        colour = left == 1 ? 0 : std::uint8_t(draw(0) & 7);
        colour |= 0xF8;
        for (int x = 0x11; x >= 0; --x) {
            for (int row = 5; row <= 8; ++row) {
                set_colour(11 + x, row, colour);
            }
            if (x < 0x0C) {
                for (int row : {3, 4, 9, 10, 11, 12}) {
                    set_colour(14 + x, row, colour);
                }
            }
        }
        --room_counters[1];
        // $3DE5: the room shows charset 3 from now on, early in the
        // flashing now and then still its own (1 in 16).
        shown_charset = 3;
        if (left >= 0x11 && draw(0) < 0x10) {
            shown_charset = 2;
        }
        request_sound(sound_flash);
        room_counters[3] ^= 1;
        const int f = room_counters[3];
        print(0x47 + f, 3, 0x0C);
        print(0x49 + f, 3, 0x19);
        print(0x4B + f, 6, 0x0E);
        print(0x4D + f, 6, 0x17);
        return;
    }
    if (!hanging(18, 0)) {
        room_counters[0] = 0;
        for (auto& f : fireballs) {
            f.shape = {};   // $422C
            f.active = false; // $D015 AND $1F: the sprites go off
        }
        print(0x61, 0x0B, 0x00);
        add_score(0xBB8); // 3000
        room_counters[3] = 0;
        room_counters[1] = 0x50;
    } else {
        step_fireballs();
        const std::uint8_t collisions = collision_latch; // $D01E
        collision_latch = 0;
        if ((collisions & 0x03) && (collisions & 0xC0) && bruce.move != move::dying) {
            bruce.move = move::dying; // $406A
            bruce.step = 0x10;
        }
        if (--room_counters[1] != 0) {
            return;
        }
        room_counters[1] = 8;
        face = 2;
        while (face != 0 && bruce.x < assets.face_thresholds[std::size_t(face)]) {
            --face;
        }
    }
    room_counters[2] = std::uint8_t(face);
    print(0x38 + face, 2, 0x10);
    const int mouth = room_counters[3] == 0 ? 6 : 0;
    if (room_counters[3] == 0) {
        ++room_counters[3];
    } else {
        --room_counters[3];
    }
    print(0x3B + mouth + face, 3, 0x12);
    print(0x3E + mouth + face, 4, 0x12);
}

// $425F: the end. The bottom rows flash and the message shows until the
// countdown is over; then the next round starts in room 0 with all
// lanterns back.
void World::script_room_19(PassEvents& events) {
    if (countdown != 0) {
        const std::uint8_t colour = std::uint8_t((draw(0) & 7) | 0xF8);
        for (int x = 0x1C; x >= 0; --x) {
            for (int row = 17; row <= 24; ++row) {
                set_colour(4 + x, row, colour);
            }
        }
        print(4, 0, 0x0A);
        return;
    }
    flag40 = true; // INC $40
    drop_bonus[std::size_t(player)] = {1, 1, 1};
    for (std::size_t r = 0; r < progress.size() && r < assets.rooms.size(); ++r) {
        progress[r].remaining = assets.lanterns_per_room[r];   // $10BC
        progress[r].lanterns.assign(assets.rooms[r].lanterns.size(), true); // $33B7
        first_visit[r][std::size_t(player)] = r != 0;          // $10D7
    }
    // L3204: into room 0 at its start.
    Actor& bruce = actors.slots[0];
    bruce.x = assets.start_x[0];
    bruce.y = assets.start_y[0];
    events.room_change = 0;
}

} // namespace bruce_lee::game
