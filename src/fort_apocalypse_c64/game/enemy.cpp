// The enemy helicopter: one at a time, hunting the player in map
// coordinates and shown as sprite 1 while near the camera.

#include "fort_apocalypse_c64/game/bcd.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <algorithm>

namespace fort_apocalypse::game {

void Game::radar_blip(u8 column, u8 row) {
    // $A498: the radar picture has one pixel pair per 2 map columns; blips
    // are drawn and erased by XOR.
    if (row >= 40) {
        return; // the original would reach past the picture (into the mine tables)
    }
    const int offset = row * 40 + (column >> 3) + 3;
    radar_[std::size_t(offset / 40)][std::size_t(offset % 40)] ^= tables_.radar_mask[column & 7];
}

void Game::enemy_blip() {
    enemy_blip_on_ ^= 1; // $A488
    radar_blip(enemy_.column, enemy_.row);
}

void Game::enemy_interrupt() {
    // $9C52, from the playfield interrupt.
    if (enemy_.state == 1) {
        if (--enemy_.timer == 0) {
            enemy_try_spawn();
        }
        return;
    }
    if (enemy_.state == 4) {
        return;
    }
    if ((frame_ & enemy_mask_) == 0) {
        enemy_tick();
    }
}

void Game::enemy_try_spawn() {
    // $9C6B: a random patrol point, accepted only away from the player;
    // otherwise the timer runs out again (255 frames).
    enemy_.colour = tables_.levels[std::min<std::size_t>(level_, 2)].enemy_colour;
    sprite_regs_[1].colour = enemy_.colour;
    enemy_.bank = 8;
    u8 point = noise(0x9C77) & 7;
    if (level_ == 1) {
        point = u8(point + 8);
    }
    enemy_.column = tables_.enemy_column[point];
    enemy_.row = tables_.enemy_row[point];
    if (near_distance(u8(enemy_.column - column_)) < 0x22 &&
        near_distance(u8(enemy_.row - row_)) < 8) {
        return;
    }
    enemy_.state = 3;
    enemy_.sub_x = enemy_.sub_y = 0;
    enemy_.timer = 0;
    enemy_.reload = 1;
    enemy_blip();
}

void Game::enemy_tick() {
    // $9CDA.
    rotor_bit_ = enemy_.bank & 1;
    enemy_.bank &= 0xFE;
    enemy_blip();
    if (--enemy_.reload == 0) {
        enemy_.reload = 5;
        if (enemy_.sprite == 2 && bullets_[2].state == 0) {
            auto& b = bullets_[2];
            b.state = bank_direction(enemy_.bank);
            b.x = u8((enemy_.x & 3) + enemy_.x + 8);
            b.y = u8(enemy_.y + 8);
            shot_sound_ = 0x0F;
            sound_shot(0x81);
        }
    }
    // The X register the climb helper sees: $D7 at the right map edge, $31
    // otherwise ($9D3A-$9D40).
    u8 x_register = 0x31;
    if (enemy_.column == 0xD8 || enemy_.column == 0x30) {
        x_register = enemy_.column == 0xD8 ? 0xD7 : 0x31;
        // $9D46: at the map's edges it levels its bank, and while hidden it
        // is held just inside.
        if ((frame_ & 3) == 0 && (enemy_.bank < 4 || enemy_.bank >= 0x0E)) {
            enemy_.bank = enemy_.bank < 8 ? u8(enemy_.bank + 2) : u8(enemy_.bank - 2);
        }
        if (enemy_.sprite == 1) {
            enemy_.column = x_register;
        }
    } else {
        const u8 dx = u8(column_ - enemy_.column);
        if (dx != 0) {
            if (dx & 0x80) {
                enemy_left();
            } else {
                enemy_right();
            }
        }
    }
    const u8 dy = u8(row_ - enemy_.row);
    if (dy != 0) {
        if (dy & 0x80) {
            enemy_up(dy, x_register);
        } else {
            enemy_down();
        }
    }
    enemy_blip();
    if (enemy_.bank & 0x80) {
        enemy_.bank = 0;
    }
    if (enemy_.bank >= 0x12) {
        enemy_.bank = 0x10;
    }
    enemy_.bank |= rotor_bit_;
    enemy_.sub_x &= 3;
    enemy_.sub_y &= 7;
}

void Game::enemy_left() {
    // $9DB9: only into clear cells (its own and the two to the left).
    for (int k = 0; k < 3; ++k) {
        if (solid_at_player_column(u8(enemy_.column - k), enemy_.row)) {
            return;
        }
    }
    if (--enemy_.sub_x == 0xFF) {
        --enemy_.column;
    }
    if ((frame_ & 3) == 0) {
        enemy_.bank = u8(enemy_.bank - 2);
    }
}

void Game::enemy_right() {
    // $9DE9.
    for (int k = 0; k < 3; ++k) {
        if (solid_at_player_column(u8(enemy_.column + k), enemy_.row)) {
            return;
        }
    }
    if (++enemy_.sub_x == 4) {
        ++enemy_.column;
    }
    if ((frame_ & 3) == 0) {
        enemy_.bank = u8(enemy_.bank + 2);
    }
}

void Game::enemy_down() {
    // $9E19: its cell and the two below must be clear.
    for (int k = 0; k < 3; ++k) {
        if (solid_at_player_column(enemy_.column, u8(enemy_.row + k))) {
            return;
        }
    }
    if (++enemy_.sub_y == 8) {
        ++enemy_.row;
    }
}

u8 Game::zero_page(u8 address) const {
    // The two zero-page bytes the corrupted climb helper can reach.
    switch (address) {
    case 0xA7: return missiles_[1].column; // $A6 + 1
    case 0x4D: return teleport_grace_;
    default: return 0;
    }
}

void Game::enemy_up(u8 difference, u8 x_register) {
    // $A000. In the original the routine's first byte is $55 instead of $A5
    // (a stray byte of text that overlaps the code), so instead of
    // LDA $76 (its own column) it executes EOR $76,X: the row difference
    // still in A, combined with whatever zero-page byte X points at. The
    // probe column is then unrelated to the helicopter, which climbs into
    // rock it never checked. The fixed variant probes its own column.
    const u8 column = options_.fixed_enemy_climb
                          ? enemy_.column
                          : u8(difference ^ zero_page(u8(0x76 + x_register)));
    if (enemy_.row >= 3) {
        for (int k = 0; k < 4; ++k) {
            if (solid_at_player_column(column, u8(enemy_.row - k))) {
                return;
            }
        }
    }
    if (--enemy_.sub_y == 0xFF) {
        --enemy_.row;
    }
}

void Game::enemy_visibility() {
    // $A1B0: on screen within $30 columns and $13 rows of the camera.
    auto hide = [&] { enemy_.sprite = 1; };
    // For a widescreen view only: where it would be shown, left and right of
    // the columns the game shows it in.
    wide_enemy_ = {};
    if (enemy_.state != 1) {
        const u8 top = std::int8_t(camera_row_) < 0 ? 0 : camera_row_;
        if (enemy_.row >= top && u8(enemy_.row - camera_row_) < 0x13) {
            const int columns = ((int(enemy_.column) - int(camera_column_) + 128) & 255) - 128;
            const int x = columns * 4 + 0x16 + ((fine_x_ & 7) >> 1) + enemy_.sub_x;
            int y = u8(u8(enemy_.row - top) << 3) + 0x53 + (fine_y_ & 7);
            if (std::int8_t(camera_row_) < 0) {
                y += 8;
            }
            y += enemy_.sub_y;
            // As set_sprite_position ($B0CB) places it, without its 8 bits.
            wide_enemy_ = {true, (x - 0x24) * 2, y + 0x16};
        }
    }
    if (enemy_.state == 1) {
        hide();
        return;
    }
    if (enemy_.column < camera_column_ || u8(enemy_.column - camera_column_) >= 0x30) {
        hide();
        return;
    }
    const u8 top = std::int8_t(camera_row_) < 0 ? 0 : camera_row_;
    if (enemy_.row < top || u8(enemy_.row - camera_row_) >= 0x13) {
        hide();
        return;
    }
    enemy_.x = u8(u8((enemy_.column - camera_column_) << 2) + 0x16 + ((fine_x_ & 7) >> 1) +
                  enemy_.sub_x);
    u8 y = u8(u8(u8(enemy_.row - top) << 3) + 0x53 + (fine_y_ & 7));
    if (std::int8_t(camera_row_) < 0) {
        y = u8(y + 8);
    }
    enemy_.y = u8(y + enemy_.sub_y);
    u8 state = 3;
    // The VIC counts the rock's pair-01 pixels (most of it) as background,
    // so in the original the enemy flies through much of the rock unharmed;
    // the fixed pilot crashes on any of it.
    const u8 s = sprite_hits_, b = options_.fixed_enemy_climb ? graphics_hits_ : background_hits_;
    const bool shot = (s & 2) && (s & 0x0C) && !(s & 1);
    if (((b & 2) || shot) && enemy_.state != 4) {
        state = 4;
        enemy_.timer = 0x14;
        sound_explosion();
    }
    if (enemy_.state != 4) {
        enemy_.state = state;
    }
    enemy_.sprite = 2;
}

void Game::enemy_sprite() {
    // $A2DE.
    if (enemy_.sprite == 1) {
        enemy_.x = enemy_.y = 0;
    }
    set_sprite_position(1, enemy_.x, enemy_.y);
    sprite_regs_[1].pointer = u8(assets_.helicopter_tilt[std::min<u8>(enemy_.bank, 17)] + 1);
    if (enemy_.state == 4) {
        ++sprite_regs_[1].colour;
        if (--enemy_.timer == 0) {
            enemy_.state = 1;
            enemy_blip();
            enemy_.timer = 0xFF;
        }
    }
    if ((frame_ & 3) == 0) {
        enemy_.bank ^= 1;
    }
}

void Game::enemy_watchdog() {
    // $B268: while the player is underground and the enemy stays out of
    // sight, it is called back after a while to start again elsewhere.
    if (level_ == 0 && row_ < 0x10) {
        return;
    }
    if (enemy_.sprite != 1) {
        enemy_.watchdog = 0xAA;
    }
    if (frame_ & 1) {
        return;
    }
    if (--enemy_.watchdog == 0) {
        enemy_.timer = 0x0A;
        enemy_.state = 1;
        if (enemy_blip_on_) {
            enemy_blip();
        }
    }
}

} // namespace fort_apocalypse::game
