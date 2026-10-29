// Mode 19 (1BFE): the title's third choice, a sound test (the other one,
// mode 16's list in select.cpp, picks a zone and act). Sonic (state 45,
// bank 0C 8B9E-8C8E) stands under one of two numbers (type 5F, bank 1C
// B650-B7C7): songs 80-A2 on the left, effects B0-DD on the right. Up and
// Down change the number under him, a button plays it, Left and Right walk
// him over to the other one; Start goes back to the SEGA logo. Observed in
// the original: trace rp_boot19 (the random player from power-on).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// The choices in the shared work bytes (DBEE, DBEF) with their ranges, and
// their digits' tile numbers (DBE6-DBE7, DBEA-DBEB) that the numbers' frames
// (type 5F frames 2 and 3) take from RAM.
constexpr unsigned song_choice = 0x08, effect_choice = 0x09;
constexpr unsigned song_digits = 0x00, effect_digits = 0x04;
constexpr u8 first_song = 0x80, last_song = 0xA2, first_effect = 0xB0, last_effect = 0xDD;
// 1C:B7B8: a digit's tile number (0-F).
constexpr Table digit_tiles{0x1C, 0xB7B8};
// The player's script parts (bank 0C): standing under the songs or the
// effects, walking right or left.
constexpr unsigned at_songs = 0x8BB4, walk_right = 0x8BC2, at_effects = 0x8BD4, walk_left = 0x8BA2;
} // namespace

// 1BFE
Flow Game::title_sound_test() {
    flow.frame_work = 0;
    co_await clear_screen(); // 273B
    clear_sprites();         // 2757
    clear_game_state();      // 327E
    clear_map_state();       // 428B
    co_await busy(sprite_clear_clocks() + 59677 + 12074);
    co_await clear_screen(); // 273B
    // 76E3: the letters (0B:B9C0 at tile 80); 76F6: the digits (1D:BD18 in
    // this mode).
    di();
    unsigned clocks = 17 + 85 + load_tiles(0x1000, 0x0B, 0xB9C0);
    clocks += 17 + 85 + load_tiles(0x05C0, 0x1D, 0xBD18);
    co_await busy(clocks);
    fill_white(); // 340C
    palette.fade_period = 3;
    palette.fades[0] = {0x90, 0x31};
    palette.fades[1] = {0x90, 0x32};
    // The text (type 5F, parameter 1, which places the rest) and Sonic
    // (state 45: parameter 44). 50E1 tags its spawner (IX) as the title's
    // name table animations (228E, records from D43F) left it, at D47F: the
    // tag (52CA of D47F: FE) lands 49 past it, in D4B0.
    for (unsigned i = 2; i < 18; ++i) {
        if (!objects[i].type) {
            objects[i].type = 0x5F;
            objects[i].param = 0x01;
            animation.opening = 0xFE;
            break;
        }
    }
    objects[0].type = 0x01;
    objects[0].param = 0x44;
    act.work[song_choice] = first_song;
    act.work[effect_choice] = first_effect;
    flow.frame_work = frame_work::build_sprites;
    do {
        co_await next_frame();               // 084C
        co_await update_player();            // 3431
        co_await update_alternate_objects(); // 4F9C
    } while (!(input.pressed & button::start));
    stop_music(); // 2288
    flow.mode = mode::sega_logo;
}

bool Game::run_sound_test_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::sound_test_place: // 8BE2
        o.x = 0x0064;
        o.y = 0x00A0;
        return true;
    case Routine::sound_test_go_left: // 8C81
        o.vx = 0xFE00;
        return true;
    case Routine::sound_test_go_right: // 8C88
        o.vx = 0x0200;
        return true;
    case Routine::sound_test_walk_left: // 8BEF: under the songs at X 64
        move_object(o);                 // 5359 (0381)
        if (lo(o.x) < 0x64) {
            jump_script(o, at_songs);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::sound_test_walk_right: // 8C38: under the effects at X A4
        move_object(o);
        if (lo(o.x) >= 0xA4) {
            jump_script(o, at_effects);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::sound_test_songs: // 8C03
        if (sound_test_choose(song_choice, first_song, last_song)) {
            return true;
        }
        if (input.pressed & button::right) {
            jump_script(o, walk_right);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::sound_test_effects: // 8C4C
        if (sound_test_choose(effect_choice, first_effect, last_effect)) {
            return true;
        }
        if (input.pressed & button::left) {
            jump_script(o, walk_left);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::sound_test_text_start: // 1C:B66E: the state is the parameter; the first
                                         // one clears the digits (DBE6-DBED) and DBF0
        o.display |= 0x02;
        o.next_state = o.param;
        if (o.param == 0x01) {
            std::fill_n(act.work.begin(), 8, u8(0));
            act.work[0x0A] = 0;
        }
        return true;
    case Routine::sound_test_numbers: // 1C:B76B: the two numbers (parameters 0A, 0B)
        spawn_object_at(o, 0x5F, 0x0A, 0x0064, 0x0078); // 510A (0357)
        spawn_object_at(o, 0x5F, 0x0B, 0x00A4, 0x0078);
        return true;
    case Routine::sound_test_song_digits: // 1C:B786
        sound_test_digits(song_choice, song_digits);
        return true;
    case Routine::sound_test_effect_digits: // 1C:B7AF
        sound_test_digits(effect_choice, effect_digits);
        return true;
    default:
        return false;
    }
}

// 8C03 / 8C4C: Up and Down step the choice within its range; then a button
// requests it (as a song: the driver's second request byte, DE06) and the
// walk is not looked at.
bool Game::sound_test_choose(unsigned choice, u8 first, u8 last) {
    const u8 pressed = input.pressed;
    u8& v = act.work[choice];
    if (pressed & button::up) {
        if (v != first) {
            --v;
        }
    } else if (pressed & button::down) {
        if (v != last) {
            ++v;
        }
    }
    if (pressed & button::jump) {
        sound_.request(v, 2);
        return true;
    }
    return false;
}

// 1C:B786 / B7AF: the choice's two hex digits as tile numbers.
void Game::sound_test_digits(unsigned choice, unsigned digits) {
    const u8 v = act.work[choice];
    act.work[digits] = rom.byte(digit_tiles, v >> 4);
    act.work[digits + 1] = rom.byte(digit_tiles, v & 0x0F);
}

} // namespace triple_trouble_gg
