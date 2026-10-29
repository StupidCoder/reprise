// The sprite table (1821-1978) and Sonic's tile uploads (10F3, 12A8).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
constexpr unsigned sprite_bank = 0x1F; // frame records and layouts
} // namespace

// 1821: builds the sprite table shadow from the objects (bank 1F tables). On
// the title screen and in the intro (D293 bit 7) only the first 16 slots are
// drawn and the rest of the table is left alone (1979).
void Game::build_sprites() {
    const bool front_end = flow.game_flags & game_flag::scene;
    sprites.next_y = 0;
    sprites.next_x = 0;
    const int count = front_end ? 16 : 20;
    for (int i = 0; i < count; ++i) {
        Object& o = objects[i];
        if (!o.state || u8(o.type - 1) >= 0xEF) {
            continue;
        }
        const u8 display = o.display;
        if (display & display_flag::flicker) {
            // 1965: hidden on two of every four frames (from the next frame).
            ++o.flicker_ticks;
            if (o.flicker_ticks & 2) {
                o.display |= display_flag::hidden;
            } else {
                o.display &= u8(~display_flag::hidden);
            }
        }
        if (display & (display_flag::hidden | display_flag::offscreen)) {
            continue;
        }
        update_screen_position(o); // 3A4C
        add_sprite_y(o);           // 1884
        add_sprite_x(o);           // 18D8
    }
    if (!front_end) {
        // 186B: unused entries are moved off screen (Y = E0). The count is
        // 33 minus the cursor, but LD (HL),E0 / LDIR writes one more, up to
        // DB33; the last twelve (HUD) entries are fixed. Nothing is cleared
        // once the cursor passes DB32.
        if (sprites.next_y <= 0x32) {
            for (unsigned i = sprites.next_y; i <= 0x33; ++i) {
                sprites.table[i].y = 0xE0;
            }
        }
    }
    sprites.dirty = 0xFF;
}

// 3A4C: the object's position relative to the camera.
void Game::update_screen_position(Object& o) {
    o.screen_x = unsigned(o.x - camera.x);
    o.screen_y = unsigned(o.y - camera.y);
}

// 1884: the Y entries. The frame record (at `tiles`) starts with a Y offset;
// the layout gives each sprite's offset (4 bytes per sprite: dy, dx). Sprites
// outside Y -16..191 are hidden with Y = E0.
void Game::add_sprite_y(Object& o) {
    const unsigned base = (o.screen_y + data_word(sprite_bank, o.tiles)) & 0xFFFF;
    unsigned layout = o.layout;
    for (int s = 0; s < o.sprite_count; ++s) {
        const unsigned y = (base + data_word(sprite_bank, layout)) & 0xFFFF;
        u8 value = u8(y);
        if (((y + 0x40) & 0xFF00) || ((y + 0x40) & 0xFF) < 0x30) {
            value = 0xE0;
        }
        auto& slot = sprites.table.at(sprites.next_y++);
        slot.y = value;
        slot.full_y = std::int16_t(y);
        slot.placed_y = value;
        slot.owner = o.type;
        layout += 4;
    }
}

// 18D8: the X and tile entries. The record's second word is the X offset
// (negated when mirrored), the third points to the tile numbers. Mirrored
// objects other than Sonic use a second layout 046C bytes on. Sprites whose X
// leaves 0-255 are placed at X = 0, under the blanked left column.
void Game::add_sprite_x(Object& o) {
    const bool mirrored = o.display & display_flag::mirrored;
    unsigned dx = data_word(sprite_bank, o.tiles + 2);
    if (mirrored) {
        dx = (0x10000 - dx) & 0xFFFF;
    }
    unsigned tile = data_word(sprite_bank, o.tiles + 4);
    const unsigned base = (o.screen_x + dx) & 0xFFFF;
    unsigned layout = o.layout + 2;
    if (o.type != 1 && mirrored) {
        layout += 0x046C;
    }
    for (int s = 0; s < o.sprite_count; ++s) {
        const unsigned x = (base + data_word(sprite_bank, layout)) & 0xFFFF;
        auto& slot = sprites.table.at(sprites.next_x++);
        slot.x = (x & 0xFF00) ? 0 : u8(x);
        slot.full_x = std::int16_t(x);
        slot.placed_x = slot.x;
        slot.placed = true;
        slot.repeat = false;
        layout += 4;
        slot.tile =
            u8(data_byte(sprite_bank, tile) + (mirrored ? o.tile_base_mirrored : o.tile_base));
        ++tile;
    }
}

// 12A8: notes whether Sonic's frame or facing changed, so that the frame
// interrupt uploads his tiles (bit 7 of D34E).
void Game::check_player_tiles() {
    const Object& sonic = objects[0];
    const u8 d504 = sonic.display;
    tiles.player_mode = u8((u8((d504 << 2) | (d504 >> 6)) & 0x40) | 0x20);
    if (tiles.player_mirror != tiles.player_mode) {
        tiles.player_mirror = tiles.player_mode;
        tiles.player_mode |= 0x80;
    }
    if (tiles.player_frame != sonic.frame) {
        tiles.player_frame = sonic.frame;
        tiles.player_mode |= 0x80;
    }
}

// 10F3: copies Sonic's tiles for the current frame to tiles 0-15. The table
// at bank 1F, BCAB (BE37 for the mirrored art) gives per frame the bank, the
// address and the number of 64-byte (two-tile) chunks. Frame 0 clears 16
// tiles instead (and leaves the request pending).
void Game::upload_player_tiles() {
    if ((tiles.player_mode & 0xA0) != 0xA0) {
        return;
    }
    const unsigned frame = tiles.player_frame;
    if (!frame) {
        display.fill_art(0, 16 * 32, 0); // 1278: tiles 0-15
        return;
    }
    const auto entry =
        ((tiles.player_mode & 0x40) ? rom::sonic_tiles_mirrored : rom::sonic_tiles) + frame * 4;
    const unsigned chunks = rom.byte(entry, 3);
    display.put_art(0, rom.bytes({rom.byte(entry), rom.word(entry, 1)}, chunks * 64));
    tiles.player_mode &= 0x7F;
}

} // namespace sonic2_gg
