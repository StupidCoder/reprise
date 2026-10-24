// The player's craft: camera, sprite, firing, bullets, terrain contact and
// flight (all run from the interrupt at raster $F9, once per frame).

#include "fort_apocalypse_c64/game/bcd.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <algorithm>

namespace fort_apocalypse::game {

// --- map ----------------------------------------------------------------------

u8 Game::map_at(int index) const {
    if (index >= 0 && index < int(map_.size())) {
        return map_[std::size_t(index)];
    }
    // Past the end: what the level's last compressed run wrote there.
    const int past = index - int(map_.size());
    if (past >= 0 && past < spill_terrain_.count) {
        return spill_terrain_.value;
    }
    return 0; // cleared memory
}

void Game::map_set(int index, u8 code) {
    if (index >= 0 && index < int(map_.size())) {
        map_[std::size_t(index)] = code;
    }
}

bool Game::solid_glyph(u8 code) const {
    // $98F2 / $A034: a character is solid when any of its 8 bytes is set.
    for (auto b : glyphs_[code & 0x7F]) {
        if (b) {
            return true;
        }
    }
    return false;
}

bool Game::solid_at_player_column(int column, int row) const {
    return solid_glyph(map_at(actor_index(column, row))); // $A031
}

// --- camera and the playfield window ($A64D) -------------------------------------

void Game::camera() {
    fine_x_ &= 0xFE;
    if (!split_layout_) {
        return;
    }
    u8 x = x_;
    if (x >= 0x83) {
        x = 0x82;
        x_ = x;
        if (camera_column_ >= 0xD9) {
            camera_column_ = 2;
        }
        fine_x_ = u8(fine_x_ - 2);
        if (((fine_x_ & 7) ^ 6) == 0) {
            ++copy_now_;
            ++camera_column_;
        }
    }
    if (x < 0x6E) {
        x = 0x6E;
        x_ = x;
        if (camera_column_ < 3) {
            camera_column_ = 0xD9;
        }
        fine_x_ = u8(fine_x_ + 2);
        if ((fine_x_ & 7) == 0) {
            --camera_column_;
            ++copy_now_;
        }
    }
    // Vertical: the camera follows between rows -1 and $19.
    u8 y = y_;
    auto scroll_down = [&] {
        fine_y_ = u8(fine_y_ - 1);
        if (((fine_y_ & 7) ^ 7) == 0) {
            ++camera_row_;
            ++copy_now_;
        }
    };
    auto scroll_up = [&] {
        fine_y_ = u8(fine_y_ + 1);
        if ((fine_y_ & 7) == 0) {
            --camera_row_;
            ++copy_now_;
        }
    };
    bool down_done = false;
    if (camera_row_ != 0x19 && y >= 0xA7) {
        y = 0xA6;
        y_ = y;
        scroll_down();
        down_done = true;
    }
    if (!down_done && y >= 0xD5) {
        y_ = 0xD4;
        if (((fine_y_ & 7) ^ 7) != 0 || camera_row_ != 0x18) {
            scroll_down();
        }
    }
    bool up_done = false;
    if (camera_row_ != 0xFF && y < 0x92) {
        y = 0x92;
        y_ = y;
        scroll_up();
        up_done = true;
    }
    if (!up_done && y < 0x64) {
        y_ = 0x64;
        if ((fine_y_ & 7) != 0 || camera_row_ != 0xFF) {
            scroll_up();
        }
    }
    xscroll_ = fine_x_ & 7;
    yscroll_ = fine_y_ & 7;
    if (--copy_timer_ == 0) {
        ++copy_now_;
    }
    if (!copy_now_) {
        return;
    }
    copy_window();
    copy_now_ = 0;
    bank_ ^= 1; // the rotor turns with each copy
    sprite_regs_[0].pointer = u8(assets_.helicopter_tilt[std::min<u8>(bank_, 17)] + 1);
    copy_timer_ = 7;
    interrupt_cut_short_ = true; // $A7BD: the rest of the interrupt is skipped
}

void Game::copy_window() {
    // $A72C: 16 map rows from the camera's row, 40 cells from its column, to
    // screen rows 9-24.
    window_ = {camera_column_ - 1, std::int8_t(camera_row_)};
    // The widescreen view's copy (presentation only). Past the map's ends
    // it repeats every 215 columns (columns 215-254 copy 0-39).
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < wide_window_columns; ++c) {
            int column = window_.column + c - wide_columns;
            if (column < 0 || column > 255) {
                column = (column % 215 + 215) % 215;
            }
            wide_window_[std::size_t(r * wide_window_columns + c)] =
                map_at((window_.row + r) * 256 + column);
        }
    }
    for (int r = 0; r < 16; ++r) {
        const int source = actor_index(camera_column_, std::int8_t(camera_row_) + r);
        for (int c = 0; c < 40; ++c) {
            screen_[std::size_t((9 + r) * 40 + c)] = map_at(source + c);
        }
    }
}

// --- the player's sprite ($A24B) ---------------------------------------------------

void Game::set_sprite_position(int sprite, u8 x, u8 y) {
    // $B0CB: VIC x = (x - $24) * 2, y = y + $16.
    auto& r = sprite_regs_[std::size_t(sprite)];
    r.x = int(u8(x - 0x24)) << 1;
    r.y = u8(y + 0x16);
}

void Game::player_sprite() {
    if (craft_ == Craft::appearing) {
        craft_ = Craft::flying;
        update_player_cell();
        return;
    }
    if (craft_ == Craft::inactive) {
        sprite_regs_[0].pointer = 0;
        return;
    }
    set_sprite_position(0, x_, y_);
    sprite_regs_[0].pointer = u8(assets_.helicopter_tilt[std::min<u8>(bank_, 17)] + 1);
    if (craft_ == Craft::crashing) {
        ++sprite_regs_[0].colour;
        --playfield_colour_;
        if (mode_ == Mode::playing) {
            if ((frame_ & 1) == 0) {
                ++y_;
            }
            if (--crash_timer_ == 0) {
                if (enemy_.state != 1) {
                    enemy_blip();
                    enemy_.state = 1;
                }
                radar_blip(column_, row_);
                mode_ = Mode::life_start;
            }
        }
    }
    if ((frame_ & 3) == 0) {
        bank_ ^= 1;
    }
    if (mode_ != Mode::playing) {
        return;
    }
    radar_blip(column_, row_);
    update_player_cell();
}

void Game::update_player_cell() {
    // $A2B9: the craft's map cell from its sprite position and the camera.
    column_ = u8(((u8(x_ - 0x24)) >> 2) + camera_column_);
    const u8 rows = u8(u8(y_ - 0x58) >> 3);
    const u8 top = std::int8_t(camera_row_) < 0 ? 0 : camera_row_;
    row_ = u8(top + rows);
    radar_blip(column_, row_);
}

// --- firing ($A5B4) -------------------------------------------------------------------

void Game::fire() {
    if (craft_ == Craft::crashing) {
        return;
    }
    if (play_kind_ == 0) {
        // The attract game fires every 16 frames.
        if ((frame_ & 0x0F) != 0) {
            return;
        }
    } else {
        if (!fire_) {
            fire_latch_ = 1;
            return;
        }
        if (fire_latch_ == 0) {
            return; // held: one shot per press
        }
        fire_latch_ = 0;
        if (mode_ == Mode::title || mode_ == Mode::options) {
            mode_ = Mode::new_game; // fire starts a game from the debrief or options
            play_kind_ = 3;
        }
    }
    sweep_direction_ ^= 0xFE; // every shot turns the sweeping walls round
    int slot = 1;
    while (slot >= 0 && bullets_[std::size_t(slot)].state != 0) {
        --slot;
    }
    if (slot < 0) {
        return;
    }
    auto& b = bullets_[std::size_t(slot)];
    b.state = bank_direction(bank_);
    b.x = u8((x_ & 3) + x_ + 6);
    b.y = u8(y_ + 8);
    shot_sound_ = 0x20;
    sound_shot(0x81);
}

u8 Game::bank_direction(u8 bank) {
    // $A5F2 / $9CFB: the bank (0-$10) gives the shot's direction 1-5.
    u8 d = u8((bank & 0x1E) >> 1);
    if (d >= 4) {
        d = d < 6 ? 3 : u8(d - 2);
    }
    if (d >= 6) {
        d = 5;
    }
    if (d == 0) {
        d = 1;
    }
    return d;
}

// --- bullets ($A332) -------------------------------------------------------------------

void Game::bullets() {
    for (int i = 2; i >= 0; --i) {
        auto& b = bullets_[std::size_t(i)];
        if (b.state != 0 && b.state != 7) {
            // The cell under the shot.
            u8 column = u8((u8(b.x - 0x30) >> 2) + camera_column_);
            if (options_.widescreen && b.x < 0x30) {
                column = u8(((b.x - 0x30) >> 2) + camera_column_); // see Options::widescreen
            }
            b.column = column;
            u8 a = std::int8_t(camera_row_) < 0 ? 0 : u8(~fine_y_ & 7);
            const u8 rows = u8(u8(a + b.y - 0x66) >> 3);
            const u8 top = std::int8_t(camera_row_) < 0 ? 0 : camera_row_;
            const u8 row = u8(top + rows);
            b.row = row;
            const int at = actor_index(column, row);
            if (solid_glyph(map_at(at))) {
                sound_explosion();
                shot_sound_ = 0;
                const u8 hit = map_at(at);
                b.saved = hit;
                happenings_.impacts.push_back({{column - 1, row}, b.state, hit, i < 2});
                bool restore = true;
                if (hit == 0x3F && level_ == 1) {
                    fort_ = 5; // the fort's core
                    restore = false;
                } else if (hit == 0x47) {
                    add_score(0x20, 0x00); // destructible rock
                    b.saved = 0;
                } else {
                    for (int k = 0x12; k >= 0; --k) {
                        if (hit == tables_.obstacles[std::size_t(k)]) {
                            restore = false; // the object handles its own death
                            break;
                        }
                    }
                }
                b.state = restore ? 7 : 0;
                map_set(at, 0x20);
                b.timer = 7;
            }
        }
        if (b.state == 0 || b.state == 7) {
            if (b.state == 0) {
                b.state = 0;
            }
            sprite_regs_[std::size_t(2 + i)].pointer = 0;
            continue;
        }
        if (b.x < 4 || b.x >= 0xFB || b.y >= 0xE6 || b.y < 0x64) {
            b.state = 0;
            sprite_regs_[std::size_t(2 + i)].pointer = 0;
            continue;
        }
        set_sprite_position(2 + i, b.x, b.y);
        sprite_regs_[std::size_t(2 + i)].pointer = b.state == 3 ? 0x21 : 0x20;
        b.x = u8(b.x + tables_.bullet_dx[b.state]);
        b.y = u8(b.y + tables_.bullet_dy[b.state]);
    }
    // $A427: splashes end after 7 frames; the hit character comes back unless
    // it was an explosion or one of the objects.
    for (int i = 2; i >= 0; --i) {
        auto& b = bullets_[std::size_t(i)];
        if (b.state != 7 || --b.timer != 0) {
            continue;
        }
        playfield_colour_ = 0;
        bool object = b.saved == 0x20;
        for (int k = 0x12; k >= 0 && !object; --k) {
            object = b.saved == tables_.obstacles[std::size_t(k)];
        }
        if (!object) {
            map_set(actor_index(b.column, b.row), b.saved);
        }
        b.state = 0;
    }
}

// --- terrain contact ($A066) -----------------------------------------------------------

void Game::terrain_contact() {
    if (craft_ == Craft::inactive || craft_ == Craft::crashing) {
        return;
    }
    if (craft_ == Craft::landed) {
        engine_sound_timer_ = 0x14; // $A05F
    } else if (craft_ != Craft::boarding && (frame_ & gravity_mask_) == 0) {
        ++y_; // gravity
    }
    auto crash = [&] {
        if (teleport_grace_ || spawn_grace_) {
            craft_ = Craft::flying;
            return;
        }
        crash_timer_ = 0x1E;
        sound_explosion();
        craft_ = Craft::crashing;
    };
    // Touching the enemy or its bullet (not the player's own bullets).
    const u8 s = sprite_hits_;
    if ((s & 1) && (s & 0x12) && !(s & 0x0C)) {
        crash();
        return;
    }
    auto contact = [&] {
        if (prisoner_in_reach() >= 0) {
            craft_ = Craft::boarding;
            return;
        }
        // $A0AA: the craft's cell and the two below it.
        int at = actor_index(column_, row_);
        int other = 0, floor = 0;
        for (int k = 0; k < 3; ++k) {
            const u8 code = map_at(at + 256 * k);
            bool landing = false;
            for (int j = 4; j >= 0; --j) {
                landing |= code == tables_.landing_characters[std::size_t(j)];
            }
            if (landing) {
                continue;
            }
            if (code == 0x48) {
                ++floor;
            } else {
                ++other;
            }
        }
        if (other == 3) {
            // Nowhere to land: a crash, unless it is a lit cavern barrier on
            // level 0 (the teleport).
            if ((barrier_a_ != 0 && barrier_b_ != 0) || level_ != 0) {
                crash();
                return;
            }
            sprite_hits_ = background_hits_ = 0;
            bool barrier_above = false;
            for (int k = 1; k >= -3 && !barrier_above; --k) {
                const u8 code = map_at(at + 256 * k);
                for (auto b : tables_.teleport_barriers) {
                    barrier_above |= code == b;
                }
            }
            if (!barrier_above || column_ < 0x28 || column_ >= 0xD8) {
                crash();
                return;
            }
            mode_ = Mode::teleport;
            teleport_grace_ = 1;
            sound_explosion();
            craft_ = Craft::flying;
            return;
        }
        // Landing ($A0FC), only where the VIC reported the touch: a craft
        // the probe finds next to solid cells but not touching stays up.
        if (!(background_hits_ & 1)) {
            craft_ = Craft::flying;
            return;
        }
        --y_;
        if (row_ >= 0x0E && engine_ == 9) {
            crash(); // out of fuel deep down
            return;
        }
        if (engine_ != 0x0A && floor == 0) {
            save_checkpoint();
        }
        craft_ = Craft::landed;
    };
    if (background_hits_ & 1) {
        contact();
        return;
    }
    // $A134: no contact reported; the cells round the craft are probed too.
    int at = actor_index(column_, row_);
    bool solid = solid_glyph(map_at(at)) || solid_glyph(map_at(at - 256));
    if (!solid) {
        const int left = (sprite_regs_[0].pointer == 7 || sprite_regs_[0].pointer == 8) ? 2 : 3;
        at = at - 256 - left;
        solid = solid_glyph(map_at(at)) || solid_glyph(map_at(at + 256));
    }
    if (solid) {
        contact();
    } else {
        craft_ = Craft::flying;
    }
}

int Game::prisoner_in_reach() {
    // $ABDA: a prisoner within reach boards.
    for (int i = 7; i >= 0; --i) {
        auto& p = prisoners_[std::size_t(i)];
        if (p.state == 1) {
            continue;
        }
        if (near_distance(u8(p.column - 4 - column_)) >= 4) {
            continue;
        }
        if (near_distance(u8(p.row - row_)) >= 4) {
            continue;
        }
        p.state = 0x0B;
        sound_rescue(0x13);
        return i;
    }
    return -1;
}

void Game::save_checkpoint() {
    checkpoint_ = {camera_column_, camera_row_, fine_x_, fine_y_, x_, y_, bank_};
}

// --- flight ($A4CE) ------------------------------------------------------------------

void Game::movement() {
    if (craft_ == Craft::inactive || craft_ == Craft::crashing) {
        return;
    }
    const u8 rotor = bank_ & 1;
    bank_ &= 0xFE;
    u8 stick = stick_;
    if (play_kind_ == 0) {
        stick = tables_.attract_joystick[attract_step_];
        stick_ = stick;
        if ((frame_ & 0x0F) == 0) {
            attract_step_ = u8(attract_step_ + 1 >= 0x6C ? 0 : attract_step_ + 1);
        }
    }
    if (stick == 0x0F) {
        sid_.write(0x00, 0x6E);
        level_bank();
        engine_pitch_ = 0x62;
    }
    if (engine_ == 9) {
        engine_pitch_ = 0x54;
    }
    if (!(stick & 8)) { // right
        sid_.write(0x00, 0x78);
        engine_pitch_ = 0x6A;
        if (bank_ >= 0x0E || (frame_ & 1) == 0) {
            ++x_;
        }
        if ((frame_ & 3) == 0) {
            bank_ = u8(bank_ + 2);
        }
    }
    if (!(stick & 4)) { // left
        sid_.write(0x00, 0x78);
        engine_pitch_ = 0x6A;
        if (bank_ < 4 || (frame_ & 1) == 0) {
            --x_;
        }
        if ((frame_ & 3) == 0) {
            bank_ = u8(bank_ - 2);
        }
    }
    if (engine_ != 9 && !(stick & 1)) { // up, with fuel
        sid_.write(0x00, 0x80);
        engine_pitch_ = 0x7E;
        --y_;
        level_bank();
    }
    if (!(stick & 2)) { // down
        sid_.write(0x00, 0x64);
        engine_pitch_ = 0x58;
        if (craft_ != Craft::landed && craft_ != Craft::boarding) {
            ++y_;
            level_bank();
        }
    }
    if (bank_ & 0x80) {
        bank_ = 0;
    }
    if (bank_ >= 0x12) {
        bank_ = 0x10;
    }
    bank_ |= rotor;
}

void Game::level_bank() {
    // $A62F: every 8 frames the bank eases back towards level (8).
    if ((frame_ & 7) != 0) {
        return;
    }
    if (bank_ >= 4 && bank_ < 0x0E) {
        return;
    }
    bank_ = bank_ < 8 ? u8(bank_ + 2) : u8(bank_ - 2);
}

} // namespace fort_apocalypse::game
