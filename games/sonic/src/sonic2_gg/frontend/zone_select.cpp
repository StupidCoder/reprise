// The zone select (0A61): Start with the second controller's code B6 on
// the title screen. A list of the 21 acts; up/down (repeating after 40
// frames held) move the arrows, button 2 starts the chosen act.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
constexpr unsigned rows = 21;
} // namespace

// 13FA: `count` characters from bank 0 into the name table, each with the
// attribute byte D2C7 (the font is at tiles 100 + character).
void Game::draw_text(Display::Cell at, Table text, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        display.entry({at.column + int(i), at.row}) =
            std::uint16_t(rom.byte(text, i) | (video.text_attribute << 8));
    }
}

// 0A61
Flow Game::zone_select() {
    load_tiles(288, rom::font); // 75A2
    clear_map_state();          // 53B6
    clear_level_state();        // 223C
    co_await next_frame();      // 17EE
    clear_screen();
    video.text_attribute = 0x01;
    draw_text({6, 4}, rom::zone_select_title, 19); // "  - ZONE SELECT -  "
    // 0A94-0DB1: one line per act (19 characters, 26 bytes apart).
    for (unsigned k = 0; k < rows; ++k) {
        video.text_attribute = 0x01;
        draw_text({6, 6 + int(k)}, rom::zone_select_lines + k * 0x26, 19);
    }
    palette.fades[0] = 0x80;
    palette.fades[1] = 0x32;
    palette.fades[2] = 0x80;
    palette.fades[3] = 0x32;
    flow.zone_select_cursor = 0;
    // 0E0D
    do {
        co_await wait_frame(); // 05C5
        zone_select_input();   // 0E29
        // 0E70 / 0E90: the arrows either side of the line.
        const int row = 5 + flow.zone_select_cursor;
        draw_rect({19, row}, rom::zone_select_arrow_left, 3, 1);
        draw_rect({25, row}, rom::zone_select_arrow_right, 3, 1);
        zone_select_scroll(); // 0EB0
    } while (!(input.pressed[0] & 0x20));
    flow.zone_select_used = 0xFF;
    // 0DCE: table 0DE3 (zone, act).
    flow.zone = rom.byte(rom::zone_select_acts, flow.zone_select_cursor * 2u);
    flow.act = rom.byte(rom::zone_select_acts, flow.zone_select_cursor * 2u + 1);
}

// 0E29: a press moves the cursor; holding up or down repeats it every
// other frame after 40 frames (D445 counts here).
void Game::zone_select_input() {
    auto up = [this] {
        if (flow.zone_select_cursor) {
            --flow.zone_select_cursor;
        }
    };
    auto down = [this] {
        if (flow.zone_select_cursor < 0x14) {
            ++flow.zone_select_cursor;
        }
    };
    const u8 pressed = input.pressed[0];
    if (pressed & 0x01) {
        up();
        return;
    }
    if (pressed & 0x02) {
        down();
        return;
    }
    const u8 held = input.held[0] & 0x03;
    if (!held) {
        boss.slot = 0;
        return;
    }
    if (++boss.slot < 0x28) {
        return;
    }
    boss.slot = 0x26;
    if (held & 0x01) {
        up();
    } else {
        down();
    }
}

// 0EB0: from the thirteenth line the screen scrolls down 48 pixels, one
// pixel per frame (the target is kept in D27E).
void Game::zone_select_scroll() {
    camera.max_y = with_lo(camera.max_y, flow.zone_select_cursor >= 0x0C ? 0x30 : 0x00);
    const u8 target = lo(camera.max_y);
    if (camera.scroll_y == target) {
        return;
    }
    camera.scroll_y = camera.scroll_y < target ? u8(camera.scroll_y + 1) : u8(camera.scroll_y - 1);
}

} // namespace sonic2_gg
