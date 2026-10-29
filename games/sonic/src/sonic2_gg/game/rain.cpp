// Sky High Zone act 2's rain (681A): eight drops drawn with sprites 2C-33,
// moving up and right and placed again (from a table of 16 positions,
// picked with LD A,R) when they leave the area around the screen. Map
// columns set by table 6A76 start and stop it. While it rains the glider
// climbs by itself (see glider.cpp).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
constexpr u8 first_sprite = 0x2C;
constexpr u8 hidden_tile = 0xFE;
} // namespace

void Game::update_rain() {
    if (flow.zone != 1 || flow.act != 1) {
        return;
    }
    rain_trigger(); // 6A25
    switch (rain.mode & 0x03) {
    case 0: // 6841: the sprites off screen
        if (sprites.table[first_sprite].y == 0xE0) {
            return;
        }
        for (int i = 0; i < 8; ++i) {
            sprites.table[first_sprite + i].y = 0xE0;
        }
        return;
    case 1: // 6853: the drops at the first eight table positions
        for (int i = 0; i < 8; ++i) {
            rain.y[i] = camera.y + rom.byte(rom::rain_positions, i * 2u);
            rain.x[i] = camera.x + rom.byte(rom::rain_positions, i * 2u + 1);
        }
        for (int b = 8, i = 0; b >= 1; --b, ++i) {
            sprites.table[first_sprite + i].tile = u8(0xF6 + b);
        }
        rain.mode = 2;
        rain.drops_shown = 8;
        rain.stop_ticks = 0x0078;
        return;
    case 2:
        rain_fall(); // 68B9
        return;
    case 3: // 6A0B: two more seconds, drops leaving the left edge hidden
        rain.drops_shown = 0;
        rain.stop_ticks = rain.stop_ticks - 1u;
        if (!(hi(rain.stop_ticks) & 0x80) && rain.stop_ticks) {
            rain_fall();
            return;
        }
        rain.mode = 0;
        return;
    }
}

// 68B9: moves the drops; every other frame each one's tile steps through
// F6-F8 (an animation), then the sprites are placed.
void Game::rain_fall() {
    ++rain.ticks;
    for (int i = 0; i < 8; ++i) {
        rain_move_drop(i); // 6942
        if (!(rain.ticks & 0x01)) {
            // 69DB
            u8& tile = sprites.table[first_sprite + i].tile;
            if (tile != hidden_tile) {
                u8 next = u8(tile + 1);
                if (next >= 0xF9) {
                    next = 0xF6;
                }
                tile = next;
            }
        }
    }
    // 68E9
    for (int i = 0; i < 8; ++i) {
        auto& slot = sprites.table[first_sprite + i];
        slot.y = u8(unsigned(rain.y[i]) - camera.y);
        // Larger views: the drops are repeated across the view.
        slot.full_y = std::int16_t(unsigned(rain.y[i]) - camera.y);
        slot.placed_y = slot.y;
        slot.owner = 0;
    }
    for (int b = 8, i = 0; b >= 1; --b, ++i) {
        const u8 x = u8(unsigned(rain.x[i]) - camera.x);
        auto& slot = sprites.table[first_sprite + i];
        slot.x = x;
        slot.full_x = std::int16_t(unsigned(rain.x[i]) - camera.x);
        slot.placed_x = x;
        slot.placed = true;
        slot.repeat = true;
        if (x < 0x08 && rain.drops_shown < b) {
            slot.tile = hidden_tile;
        }
    }
}

// 6942: up 4 and right 5 pixels per frame; outside the area from 48
// above the camera and 64 left of it to 256 right, it starts again.
void Game::rain_move_drop(int i) {
    rain.y[i] = rain.y[i] + 0xFFFCu;
    const unsigned y = (unsigned(rain.y[i]) + 0x0040u) & 0xFFFF;
    if (y >= camera.y && ((y - camera.y) & 0xFFFF) >= 0x0010) {
        rain.x[i] = rain.x[i] + 0x0005u;
        const unsigned x = (unsigned(rain.x[i]) + 0x0040u) & 0xFFFF;
        if (x >= camera.x && ((x - camera.x) & 0xFFFF) < 0x0140) {
            return;
        }
    }
    // 6991: a new position from the table, picked by the frame, the drop,
    // LD A,R, Sonic's X, the clock's seconds and frames.
    u8 c = u8(rain.ticks + i * 2);
    c = u8(c + refresh_register());
    c = u8(c + lo(objects[0].x));
    c = u8(c + clock.seconds);
    c = u8(c + clock.ticks);
    const auto entry = rom::rain_positions + (c & 0x0F) * 2u;
    rain.y[i] = camera.y + rom.byte(entry);
    rain.x[i] = camera.x + rom.byte(entry, 1);
}

// 6A25: the map column the camera is in (64 pixels each) selects, from
// table 6A76: 00 stop, 01 start (unless raining), 03 begin stopping
// (if raining); others nothing.
void Game::rain_trigger() {
    const unsigned column = (camera.x >> 6) & 0x3F;
    switch (u8(rom.byte(rom::rain_columns, column) + 1) & 0x07) {
    case 1:
        rain.mode = 0;
        return;
    case 2:
        if (rain.mode != 2) {
            rain.mode = 1;
        }
        return;
    case 4:
        if (rain.mode) {
            rain.mode = 3;
        }
        return;
    default:
        return;
    }
}

} // namespace sonic2_gg
