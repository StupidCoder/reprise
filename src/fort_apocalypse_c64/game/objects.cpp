// The objects the main loop moves: mines, tanks, homing missiles and
// prisoners, plus refuelling and the radar window. They live in the level
// map itself: each draws its characters into the map, keeps what was under
// them and restores it as it moves, and reacts to the characters it finds.

#include "fort_apocalypse_c64/game/bcd.hpp"
#include "fort_apocalypse_c64/game/game.hpp"

namespace fort_apocalypse::game {

void Game::object_blip(u8 column, u8 row) {
    radar_blip(column, row); // $AD8B: same picture, object columns
}

void Game::add_score(u8 low, u8 middle) {
    // $AA37: nothing counts in the attract game.
    if (play_kind_ == 0) {
        return;
    }
    bool carry = false;
    score_[0] = bcd_add(score_[0], low, carry);
    score_[1] = bcd_add(score_[1], middle, carry);
    score_[2] = bcd_add(score_[2], 0, carry);
}

// --- mines ($94D2) ------------------------------------------------------------------

void Game::mines() {
    // Fifteen of the 39 slots per pass, round robin.
    for (int n = 0; n < 15; ++n) {
        mine_step(mine_cursor_);
        mine_cursor_ = u8(mine_cursor_ + 1 >= 0x27 ? 0 : mine_cursor_ + 1);
    }
}

void Game::mine_step(int slot) {
    auto& m = mines_[std::size_t(slot)];
    u8 phase = m.state; // $E9
    const int type = m.state & 0x0F;
    if (type == 1) {
        return;
    }
    auto cells = [&] { return object_index(m.column, m.row); };
    auto erase = [&] { // $95AD
        object_blip(m.column, m.row);
        map_set(cells(), m.saved_left);
        map_set(cells() + 1, m.saved_right);
    };
    auto draw = [&] { // $95BD
        object_blip(m.column, m.row);
        m.saved_left = map_at(cells());
        m.saved_right = map_at(cells() + 1);
        const int pair = (phase >> 3) & 6;
        map_set(cells(), tables_.mine_characters[std::size_t(pair)]);
        map_set(cells() + 1, tables_.mine_characters[std::size_t(pair + 1)]);
    };
    if (type == 7) {
        // $9545: a random empty spot, column $32-$CD, any row.
        do {
            do {
                m.column = noise(0x9545);
            } while (m.column < 0x32 || m.column >= 0xCE);
            do {
                m.row = noise(0x9553);
            } while (m.row >= 0x28);
        } while (map_at(cells()) | map_at(cells() + 1));
        m.state = 2;
        phase = 2;
        draw();
        m.direction = 1;
        return;
    }
    // $957B: an explosion or a missile in its cells destroys it.
    for (int k = 0; k < 2; ++k) {
        const u8 code = map_at(cells() + k);
        if (code == 0x71 || code == 0x72 || code == 0x20) {
            const int pair = (phase >> 3) & 6;
            happenings_.mines.push_back({{m.column - 5, m.row},
                                         {tables_.mine_characters[std::size_t(pair)],
                                          tables_.mine_characters[std::size_t(pair + 1)]},
                                         (m.direction & 0x80) ? -1 : 1});
            erase();
            m.state = 1;
            add_score(0x50, 0x00);
            return;
        }
    }
    erase();
    // $95E0: four phases per cell; it turns at anything but empty cells and
    // at the edges of its column band.
    for (;;) {
        if (m.direction & 0x80) {
            phase = u8((phase - 0x10) & 0x3F);
            if ((phase & 0xF0) == 0x30) {
                --m.column;
            }
        } else {
            phase = u8((phase + 0x10) & 0x3F);
            if ((phase & 0xF0) == 0) {
                ++m.column;
            }
        }
        if ((map_at(cells()) | map_at(cells() + 1)) == 0 && m.column >= 0x32 && m.column < 0xCE) {
            break;
        }
        m.direction ^= 0xFE;
    }
    m.state = phase;
    draw();
}

// --- tanks ($992A) ------------------------------------------------------------------

void Game::tank_blip(int i) {
    // $9919: the turret row's blip.
    const auto& t = tanks_[std::size_t(i)];
    object_blip(t.column, u8(t.row - 1));
}

void Game::tanks() {
    if (--tank_countdown_ == 0) {
        tank_countdown_ = tank_period_;
        for (int i = 5; i >= 0; --i) {
            tank_step(i);
        }
    }
    // $9A0D: explosions end after ten passes; gone tanks come back home
    // when the player is below and their home is clear.
    if (--tank_explosion_ != 0) {
        return;
    }
    tank_explosion_ = 0x0A;
    for (int i = 5; i >= 0; --i) {
        auto& t = tanks_[std::size_t(i)];
        if (t.state == 4) {
            t.state = 1;
            const int at = object_index(t.column, t.row);
            for (int k = 0; k < 3; ++k) {
                map_set(at + k, t.saved[std::size_t(k)]);
            }
            map_set(at - 256 + 1, 0);
            add_score(0x50, 0x02);
            tank_blip(i);
        }
        if (t.state != 1 || row_ < 3 || u8(row_ - 3) < t.home_row) {
            continue;
        }
        const int home = object_index(u8(t.home_column - 5), t.home_row);
        bool clear = true;
        for (int k = 12; k >= 0 && clear; --k) {
            const u8 code = map_at(home + k);
            clear = code != 0x6C && code != 0x6D && code != 0x6E;
        }
        if (clear) {
            t.state = 7;
        }
    }
}

void Game::tank_step(int i) {
    auto& t = tanks_[std::size_t(i)];
    if (t.state == 1 || t.state == 4) {
        return;
    }
    if (t.state == 7) {
        t.state = 2;
        t.column = t.home_column;
        t.row = t.home_row;
        t.direction = static_cast<std::int8_t>(noise(0x9956)) < 0 ? 1 : 0xFF;
        tank_blip(i);
    } else {
        // Its cells: an explosion or a missile destroys it; otherwise the
        // background comes back.
        const int at = object_index(t.column, t.row);
        for (int k = 0; k < 3; ++k) {
            const u8 code = map_at(at + k);
            if (code == 0x20 || code == 0x71 || code == 0x72) {
                t.state = 4;
                happenings_.tanks.push_back({{t.column - 5, t.row}, map_at(at - 256 + 1)});
                for (int j = 2; j >= 0; --j) {
                    map_set(at + j, 0x20);
                }
                tank_explosion_ = 0x0A;
                return;
            }
            map_set(at + k, t.saved[std::size_t(k)]);
        }
        map_set(at - 256 + 1, 0); // the turret
    }
    // $99B4: one cell on, turning at obstacles.
    for (;;) {
        tank_blip(i);
        t.column = u8(t.column + t.direction);
        tank_blip(i);
        const int at = object_index(t.column, t.row);
        for (int k = 0; k < 3; ++k) {
            t.saved[std::size_t(k)] = map_at(at + k);
        }
        auto blocked = [&](int k) {
            const u8 code = map_at(at + k);
            for (int j = 0x15; j >= 0; --j) {
                if (code == tables_.obstacles[std::size_t(j)]) {
                    return true;
                }
            }
            return false;
        };
        if (blocked(0) || blocked(2)) {
            t.direction ^= 0xFE;
            continue;
        }
        for (int k = 2; k >= 0; --k) {
            map_set(at + k, tables_.tank_characters[std::size_t(k)]);
        }
        // The turret points at the player.
        const u8 turret = (u8(column_ - t.column) & 0x80) ? turret_left : turret_right;
        map_set(at - 256 + 1, turret);
        return;
    }
}

// --- homing missiles ($9644) ----------------------------------------------------------

void Game::restore_missile_cell(int i) {
    // $9706: the background comes back unless it was a mine.
    const auto& m = missiles_[std::size_t(i)];
    const u8 saved = m.saved;
    const bool write = saved == 0x47 ||
                       (saved < 0xE0 && saved != 0x40 && (saved < 0x5B || saved >= 0x60));
    if (write) {
        map_set(object_index(m.column, m.row), saved);
    }
}

void Game::missile_detonate(int i) {
    // $96EB.
    auto& m = missiles_[std::size_t(i)];
    restore_missile_cell(i);
    sound_explosion();
    m.state = 1;
    m.fuel = 0xFF;
    add_score(0x10, 0x00);
}

void Game::missiles() {
    if (--missile_countdown_ == 0) {
        missile_countdown_ = missile_period_;
        for (int i = 5; i >= 0; --i) {
            auto& m = missiles_[std::size_t(i)];
            const auto& tank = tanks_[std::size_t(i)];
            if (m.state == 7) {
                // $96B1: from above its tank, towards the player.
                m.column = u8(tank.column + 1);
                m.row = u8(tank.row - 2);
                m.state = (u8(column_ - tank.column) & 0x80) ? 4 : 8;
                m.saved = 0;
                m.fuel = 0x14;
                if (fuel_[1] != 0) {
                    sound_missile();
                }
            } else if (m.state != 1) {
                if (map_at(object_index(m.column, m.row)) == 0x20) {
                    missile_detonate(i);
                } else {
                    restore_missile_cell(i);
                    missile_move(i);
                    // $978E: draw, and detonate on anything solid.
                    const int at = object_index(m.column, m.row);
                    m.saved = map_at(at);
                    map_set(at, m.state == 4 ? 0x71 : 0x72);
                    if (solid_glyph(m.saved)) {
                        missile_detonate(i);
                    }
                }
            }
            // $9669: launch when the player is near above the tank.
            if (tank.state != 2) {
                continue;
            }
            const u8 rows_above = u8(tank.row - row_);
            if ((rows_above & 0x80) || rows_above >= 0x0E || m.state != 1) {
                continue;
            }
            if (near_distance(u8(column_ - 2 - tank.column)) < 9) {
                m.state = 7;
            }
        }
    }
    bool all_idle = true;
    for (const auto& m : missiles_) {
        all_idle &= m.state == 1;
    }
    if (all_idle) {
        missiles_active_ = 0;
    }
}

void Game::missile_move(int i) {
    // $9724: one column on, steering towards the player's row; it falls when
    // the player is behind it, at the band's edges or out of fuel.
    auto& m = missiles_[std::size_t(i)];
    m.column = m.state == 4 ? u8(m.column - 1) : u8(m.column + 1);
    bool fall = m.fuel & 0x80;
    if (!fall) {
        const u8 d = u8(column_ - m.column);
        fall = m.state == 4 ? !(d & 0x80) : (d & 0x80) != 0;
    }
    if (!fall) {
        fall = m.column >= 0xD8 || m.column < 0x2D;
    }
    if (fall) {
        ++m.row;
    } else {
        u8 target = row_;
        if (level_ != 0 || row_ >= 0x14) {
            target = u8(target - 2);
        }
        const u8 d = u8(target - m.row);
        if (d != 0) {
            if (d & 0x80) {
                --m.row;
            } else {
                ++m.row;
            }
        }
    }
    // Never onto another missile.
    for (;;) {
        const u8 code = map_at(object_index(m.column, m.row));
        if (code != 0x71 && code != 0x72) {
            break;
        }
        ++m.row;
    }
    if (!(m.fuel & 0x80)) {
        --m.fuel;
    }
}

// --- prisoners ($AABA) ----------------------------------------------------------------

void Game::prisoners() {
    const int i = prisoner_cursor_;
    auto& p = prisoners_[std::size_t(i)];
    if (p.state == 0x0B) {
        prisoner_removed(i);
        add_score(0x00, 0x08);
        ++rescued_;
    } else if (p.state != 1) {
        const int at = object_index(p.column, p.row);
        bool dead = false;
        for (int k = 0; k < 2; ++k) {
            const u8 code = map_at(at - 256 * k);
            dead |= code == 0 || code == 0x20 || code == 0x71 || code == 0x72;
        }
        if (dead) {
            prisoner_removed(i);
        } else {
            // $AB62, $AB72, $ABB1: restore the walkway, run along it, draw.
            map_set(at, 0x48);
            map_set(at - 256, 0x1F);
            for (;;) {
                if (!(p.motion & 0x80)) {
                    p.motion = u8(((p.motion + 1) & 1) | 0x10);
                    ++p.column;
                } else {
                    p.motion = u8(((p.motion - 1) & 1) | 0xF0);
                    --p.column;
                }
                if (map_at(object_index(p.column, p.row)) == 0x48) {
                    break;
                }
                p.motion ^= 0xE0;
            }
            const int leg = p.motion & 3;
            const int now = object_index(p.column, p.row);
            const auto& c = tables_.prisoner_characters;
            if (p.motion & 0x80) {
                map_set(now, c[std::size_t(4 + leg)]);
                map_set(now - 256, c[std::size_t(leg)]);
            } else {
                map_set(now, c[std::size_t(6 + leg)]);
                map_set(now - 256, c[std::size_t(2 + leg)]);
            }
        }
    }
    prisoner_cursor_ = u8((prisoner_cursor_ + 1) & 7);
    if (message_timer_ && --message_timer_ == 0) {
        clear_message_row();
    }
}

void Game::prisoner_removed(int i) {
    // $AB31: rescued or dead, he leaves the count.
    auto& p = prisoners_[std::size_t(i)];
    const int at = object_index(p.column, p.row);
    map_set(at, 0x48);
    map_set(at - 256, 0x1F);
    p.state = 1;
    --men_left_;
    show_men_to_rescue();
}

void Game::show_men_to_rescue() {
    // $AB3B.
    draw_text(tables_.men_to_rescue, 9, 0);
    const u8 left = u8(men_left_ | 0x10);
    screen_[245] = left;
    screen_[246] = u8(left == 0x10 ? 0x0A : (left & 0x8F));
    message_timer_ = 0x5A;
}

// --- refuelling ($AC2B) ---------------------------------------------------------------

void Game::draw_depot(int column, int stage) {
    // $ACD5: five rows of six cells; stage s starts 6 * s bytes in.
    int at = object_index(u8(column), 0x0B);
    int x = stage * 6;
    for (int row = 0; row < 5; ++row) {
        for (int k = 0; k < 6; ++k) {
            map_set(at + k, tables_.depot[std::size_t(x++)]);
        }
        at += 256;
    }
}

void Game::refuel() {
    if (engine_ == 0x0A) {
        if (--depot_timer_ != 0) {
            return;
        }
        depot_timer_ = 1;
        if (!(depot_stage_ & 0x80)) {
            if (level_ == 1) {
                draw_depot(0x82, depot_stage_);
            } else {
                draw_depot(0x17, depot_stage_);
                draw_depot(0xEE, depot_stage_);
            }
            --depot_stage_;
            return;
        }
        refuel_sound_ = row_ >= 0x0D ? 1 : 0;
        if (row_ >= 0x0A) {
            return;
        }
        // Lifted off: the depot is full again.
        engine_ = 8;
        depot_stage_ = 4;
        if (level_ == 1) {
            draw_depot(0x82, 4);
        } else {
            draw_depot(0x17, 4);
            draw_depot(0xEE, 4);
        }
        --depot_stage_;
        save_checkpoint();
        return;
    }
    if (craft_ == Craft::landed && row_ >= 9 && row_ < 0x0D) {
        engine_ = 0x0A;
        depot_timer_ = 1;
        depot_stage_ = 4;
    }
    if (engine_ != 0x0A) {
        if (fuel_[1] != 0) {
            return;
        }
        if ((frame_ & 8) == 0) {
            draw_text(tables_.low_on_fuel, 9, 0);
            return;
        }
    }
    if (message_timer_ == 0) {
        clear_message_row();
    }
}

// --- the radar window ($AD2E) -----------------------------------------------------

void Game::radar_window() {
    // Twelve bytes by 24 lines of the radar picture around the camera, into
    // the HUD characters $5C-$7F.
    int first_line = 0;
    if (camera_row_ != 0 && !(camera_row_ & 0x80)) {
        first_line = camera_row_ >= 0x11 ? 0x10 : camera_row_;
    }
    const int start = first_line * 40 + (camera_column_ >> 3);
    for (int r = 0; r < 3; ++r) {
        for (int line = 0; line < 8; ++line) {
            for (int c = 0; c < 12; ++c) {
                const int offset = start + (r * 8 + line) * 40 + c;
                const u8 value = offset < 1600 ? radar_[std::size_t(offset / 40)][std::size_t(offset % 40)] : 0;
                radar_glyphs_[std::size_t(r * 12 + c)][std::size_t(line)] = value;
            }
        }
    }
}

} // namespace fort_apocalypse::game
