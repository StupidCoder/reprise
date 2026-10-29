// Screen routines of bank 0 (1352-1A70, 17EE, 7700, 76C6): clearing,
// tile art, drawing into the background map, fades.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 132E: the power-on settings: the picture off, no water line, no scroll
// (and the settings this game never changes, see gg_engine/video/display.hpp).
void Game::init_display() {
    display.shown = false;
    display.line_interrupt = false;
    display.scroll_x = 0;
    display.scroll_y = 0;
}

// 13C2 / 13D0: the picture on or off (the backdrop colour only).
void Game::display_on() {
    display.shown = true;
}

void Game::display_off() {
    display.shown = false;
}

// 17EE: waits for a frame, then blanks the first sprite tile, fills the name
// table with tile 256 and hides all sprites (interrupts off from here on).
// Callers do the wait (EI; HALT) first.
void Game::clear_screen() {
    di();
    spend(4 + 30 + fill_clocks(0x20) + 30 + fill_clocks(0x380));
    display.fill_art(0x2000, 0x40, 0);
    display.map.fill(0x0100);
    display.level_shown = false;
    clear_sprites();
}

// 13A3: CPU clocks of a fill of `words` name table words, call to RET.
unsigned Game::fill_clocks(unsigned words) {
    return 17 + 77 * words + 90;
}

// 1809: every sprite at Y = F0 (off screen), X and tile 0.
void Game::clear_sprites() {
    spend(3571);
    for (int i = 0; i < 64; ++i) {
        sprites.table[i].y = 0xF0;
        sprites.table[i].x = 0;
        sprites.table[i].tile = 0;
    }
    sprites.dirty = 0xFF;
}

// 76C6: 16 KiB of zeros (with the display off meanwhile).
void Game::clear_vram() {
    di();
    spend(211221); // 1024 x 16 OUTs
    display.tiles = {};
    display.map = {};
    display.sprites = {};
    display.level_shown = false;
    display_on();
}

// 7700: 128 zero writes to CRAM (which wraps every 64 bytes).
void Game::clear_cram() {
    di();
    spend(4 + 30 + fill_clocks(0x40) + 10);
    display.colors.fill(0);
}

// 1B00, called with the destination set (136A) and the bank mapped.
void Game::load_tiles(unsigned first_tile, Table block, bool mirrored) {
    // The bank switch, address and call around 1B00, then the decoder.
    spend(186 + decode_clocks(rom, block.bank, block.address, mirrored));
    display.put_art(first_tile * 32, rom.tiles(block, mirrored));
}

// 1A3F: a background cell moved by the camera position, so that drawing
// lines up with the scrolled background: the column by camera X / 8 (within
// the 32 columns) and the row by camera Y / 8 (within the 28 rows; table
// 5860).
Display::Cell Game::scrolled(Display::Cell c) const {
    const int column = (c.column + (lo(camera.x) >> 3)) % Display::map_columns;
    const int shift = rom.word(rom::background_rows, ((lo(camera.y) >> 3) & 0x1F) * 2u) / 0x40;
    return {column, (c.row + shift) % Display::map_rows};
}

// 19C1: map entries row by row from the scrolled cell; each row wraps
// within its 32 columns and the rows wrap from the bottom to the top. It ends
// with EI.
void Game::draw_rect(Display::Cell at, Table entries, int rows, int columns) {
    Display::Cell row_start = scrolled(at);
    unsigned clocks = 17 + 260; // the call and 1A3F
    for (int r = 0; r < rows; ++r) {
        Display::Cell c = row_start;
        clocks += 11 + 11 + 4 + 85;
        for (int k = 0; k < columns; ++k) {
            display.entry(c) = rom.word(entries);
            entries = entries + 2;
            clocks += 102;
            if (++c.column == Display::map_columns) {
                c.column = 0;
                clocks += 135;
            }
        }
        clocks += 10 + 10 + 11 + 4 + 7 + 10 + 10 + 13 - 5;
        if (++row_start.row == Display::map_rows) {
            row_start.row = 0;
            clocks += 7;
        }
    }
    spend(clocks + 4 + 10);
    ei(); // 19F9
}

// 12FC: fade both palettes out.
void Game::start_fade_out() {
    palette.fades[0] = 0x40;
    palette.fades[2] = 0x40;
}

// 223C: clears the act's state: Sonic's movement, the objects, water, the
// palettes, the sprite table and more (the original clears D300-DBE4).
void Game::clear_level_state() {
    spend(47841);
    tiles = {};
    const u8 block_attribute = probe.block_attribute; // D100, outside the block
    probe = {};
    probe.block_attribute = block_attribute;
    player = {};
    const u8 dirty = sprites.dirty; // D134, outside the block
    sprites = {};
    sprites.dirty = dirty;
    level.animated_tiles_source = 0;
    level.animated_tiles_vram = 0;
    flow.title_card_ticks = 0;
    boss = {};
    spawner = {};
    water = {};
    ending = {};
    clock.time_over = 0;
    video.irq_colors_only = 0;
    const u8 fade_speed = palette.fade_speed, fade_counter = palette.fade_counter; // D2C8/9
    palette = {};
    palette.fade_speed = fade_speed;
    palette.fade_counter = fade_counter;
    objects = {};
    rain = {};
}

} // namespace sonic2_gg
