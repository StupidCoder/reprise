// The character select (mode 18, 1AD0) and the step after it (mode 16,
// 1A27): a new game's lives and flags; left and right choose Sonic or
// Tails (each with its own palettes), a button or Start confirms.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 77D0: the screen's art (16:9DF4 at tile C0) and its two halves of
// map: Sonic's (11:BDEE at 38CC) and Tails' (03:BC7A at 38E0), 18 rows of
// 10 entries each.
constexpr Table select_tiles{0x16, 0x9DF4};
constexpr Table sonic_side{0x11, 0xBDEE};
constexpr Table tails_side{0x03, 0xBC7A};
// The palettes (table 0E:B6FC) showing each choice: background, sprites.
constexpr u8 sonic_palettes[2] = {0x07, 0x0A};
constexpr u8 tails_palettes[2] = {0x09, 0x08};
constexpr u8 select_song = 0x87;
// 0064: bit 2 set: mode 16 opens the sound test instead (a development
// switch; clear in this cartridge).
constexpr Table debug_switch{0x00, 0x0064};
} // namespace

// 1AD0
Flow Game::character_select() {
    flow.frame_work = 0;
    co_await clear_screen();       // 273B
    clear_game_state();            // 327E
    fill_white();                  // 340C
    co_await load_select_screen(); // 77D0
    sound_.request(select_song, 1);
    score.lives = 0x03;
    score.point_rewards = 0x01;
    flow.emeralds = 0;
    flow.restart_point = 0;
    flow.special_entry = 0;
    flow.demo = 0;
    if (!flow.character) {
        flow.character = 1;
    }
    // From white to the current choice's palettes.
    const u8* p = flow.character == 1 ? sonic_palettes : tails_palettes;
    set_fades(1, 0x90, p[0], 0x90, p[1]);
    co_await wait_frames(0x1E); // 252C
    co_await next_frame();
    // 1B52: the choice's palettes at once; left and right change it (sound
    // C0), a button or Start confirms.
    for (bool changed = true;;) {
        if (changed) {
            const u8* q = flow.character == 1 ? sonic_palettes : tails_palettes;
            palette.fades[0].mode |= 0x20;
            palette.fades[0].palette = q[0];
            palette.fades[1].mode |= 0x20;
            palette.fades[1].palette = q[1];
            changed = false;
        }
        co_await next_frame();
        const u8 pressed = input.pressed;
        if (pressed & 0xB0) {
            break;
        }
        const u8 choice = (pressed & 0x04) ? 1 : (pressed & 0x08) ? 2 : 0;
        if (choice && choice != flow.character) {
            flow.character = choice;
            sound_.request(0xC0, 1);
            changed = true;
        }
    }
    // 1BB7
    co_await wait_frames(0x14); // 21BA
    stop_music();               // 2288
    set_fades(3, 0x50, palette.fades[0].palette, 0x50, palette.fades[1].palette);
    co_await wait_frames(0x5A);
    switch (flow.next_mode_hint) {
    case 0x01:
        flow.mode = 0x11;
        break;
    case 0x03:
        flow.mode = 0x0D;
        break;
    default:
        flow.mode = 0x16;
        break;
    }
}

// 77D0: video memory cleared, the art and both halves of the map.
Flow Game::load_select_screen() {
    clear_vram(); // 7BCF
    di();
    unsigned clocks = 211242 + 17 + 53 + 85;
    clocks += load_tiles(0x1800, select_tiles.bank, select_tiles.address);
    clocks += 53 + 30 + draw_rect(0x38CC, sonic_side.bank, sonic_side.address, 0x12, 0x0A);
    clocks += 53 + 30 + draw_rect(0x38E0, tails_side.bank, tails_side.address, 0x12, 0x0A);
    co_await busy(clocks);
    ei(); // 2C73
}

// 1A27: mode 16: the act's title card (mode 4), or with the intro's
// button code (or the cartridge's switch) the sound test first, which
// chooses the zone and act; zone 8 then goes to mode 0D, zone 6 to 9.
Flow Game::after_select() {
    if (!flow.intro_secret && !(rom.byte(debug_switch) & 0x04)) {
        flow.mode = 0x04;
        co_return;
    }
    flow.frame_work = 0;
    clear_rings_and_time();  // 32C3
    co_await clear_screen(); // 273B
    // 76D0: its letters (03:B7EE at tile 120).
    di();
    co_await busy(17 + 4 + 53 + load_tiles(0x2400, 0x03, 0xB7EE));
    fill_white();               // 340C
    co_await wait_frames(0x30); // 252C
    co_await clear_screen();
    set_fades(0, 0x90, 0x02, 0x90, 0x02);
    co_await sound_test(); // 10:BCCC
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x78);
    if (flow.zone == 0x08) {
        flow.mode = 0x0D;
        co_return;
    }
    if (flow.zone == 0x06) {
        palette.fades[0].mode = 0x40;
        palette.fades[1].mode = 0x40;
        co_await wait_frames(0x78);
        flow.mode = 0x09;
        co_return;
    }
    flow.mode = 0x04;
}

// 10:BCCC: the list (10:BDCE, one byte per entry, 20 rows of 19 at 390C);
// Up and Down choose (held: repeating after 40 frames, every other frame
// after), two arrows mark the choice, the list scrolls to show the last
// rows; button 2 takes it (sound DD, D168 FF).
Flow Game::sound_test() {
    flow.zone = 0;
    flow.act = 0;
    act.menu_choice = 0;
    di();
    act.entry_high = 0x01;
    co_await busy(draw_byte_rect(0x390C, 0x10, 0xBDCE, 0x14, 0x13, act.entry_high)); // 2C75 (0504)
    ei();
    for (;;) {
        co_await next_frame(); // 0300
        sound_test_input();    // BD22
        const unsigned row = act.menu_choice * 0x40u;
        unsigned clocks = draw_rect(0x3966 + row, 0x10, 0xBD81, 0x03, 0x01); // BD69: the left arrow
        co_await busy(clocks);
        ei();
        co_await busy(draw_rect(0x3972 + row, 0x10, 0xBDA0, 0x03, 0x01)); // BD87: the right one
        ei();
        // BDA6: the scroll toward 18 (choices 0C on) or 0, a line per frame.
        camera.max_y = act.menu_choice >= 0x0C ? 0x18 : 0x00; // D3A4 serves as the target
        const u8 target = u8(camera.max_y);
        if (camera.scroll_y > target) {
            --camera.scroll_y;
        } else if (camera.scroll_y < target) {
            ++camera.scroll_y;
        }
        if (input.pressed & 0x20) {
            sound_.request(0xDD, 1);
            flow.next_mode_hint = 0xFF;
            break;
        }
    }
    const Table choice{0x10, std::uint16_t(0xBF4A + act.menu_choice * 2)};
    flow.zone = rom.byte(choice, 0);
    flow.act = rom.byte(choice, 1);
}

// 10:BD22
void Game::sound_test_input() {
    const auto up = [this] {
        if (!act.menu_choice) {
            return;
        }
        --act.menu_choice;
        sound_.request(0xD8, 1);
    };
    const auto down = [this] {
        if (act.menu_choice >= 0x11) {
            return;
        }
        ++act.menu_choice;
        sound_.request(0xD8, 1);
    };
    if (input.pressed & 0x01) {
        up();
        return;
    }
    if (input.pressed & 0x02) {
        down();
        return;
    }
    if (!(input.held & 0x03)) {
        input.repeat_ticks = 0;
        return;
    }
    if (++input.repeat_ticks < 0x28) {
        return;
    }
    input.repeat_ticks = 0x26;
    if (input.held & 0x01) {
        up();
    } else if (input.held & 0x02) {
        down();
    }
}

} // namespace triple_trouble_gg
