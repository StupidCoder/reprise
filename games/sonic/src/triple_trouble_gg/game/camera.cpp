// The camera and the scrolling map (3F81, 4B1D) and the act's background
// drawn from the level map.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

// 3F81: once per frame while the map is enabled (and no line effect runs):
// the camera's next position, the map's newly exposed column or row, the
// scroll registers; the frame interrupt makes the position current.
void Game::update_map() {
    if (act.line_effect) {
        return;
    }
    if (!(camera.map_flags & map_flag::enabled)) {
        return;
    }
    follow_camera();        // 4B1D
    scroll_horizontally();  // 3FA6
    scroll_vertically();    // 3FED
    set_scroll_registers(); // 4034
    camera.map_flags |= map_flag::update_pending;
}

// 4CC3 (030F): the camera follows the player again.
void Game::enable_map() {
    camera.map_flags |= map_flag::enabled;
    camera.mode &= u8(~camera_mode::scripted);
}

// 4D13: the step limit ramps up again from 2 (4CA3).
void Game::ramp_camera() {
    camera.mode |= camera_mode::ramp;
    camera.step = 0x0200;
}

// 4D00 (031B): the ramp; the camera cannot go back left of where it is.
void Game::hold_camera_left() {
    ramp_camera();
    if (camera.min_x < camera.x) {
        camera.min_x = camera.x;
    }
}

// 4CDA (0321): the camera follows a script to (x, y) (4C54), ramping.
void Game::script_camera(std::uint16_t x, std::uint16_t y) {
    camera.map_flags |= map_flag::enabled;
    camera.mode |= camera_mode::scripted | camera_mode::ramp;
    camera_targets.scripted_x = x;
    camera_targets.scripted_y = y;
    ramp_camera();
}

// 4CF2 (0324): the camera follows the player again, ramping.
void Game::release_camera() {
    camera.map_flags |= map_flag::enabled;
    camera.mode &= u8(~camera_mode::scripted);
    ramp_camera();
}

// 4CCE (0312): the camera stops; it cannot go back left of here.
void Game::disable_map() {
    camera.map_flags &= u8(~map_flag::enabled);
    camera.min_x = camera.x;
}

// 4B1D: the next position. The player's distance from it (low byte only)
// is compared with the window around the aim; the camera moves by at most
// the step limit (8, or less while it ramps) horizontally and 7
// vertically. Only the low byte of the distance is replaced (LD L,A): a
// player 256 or more pixels away moves the camera that far at once.
void Game::follow_camera() {
    camera.map_flags &= 0xF0;
    if (camera.mode & camera_mode::scripted) {
        follow_script(); // 4C54
        return;
    }
    ramp_camera_step();     // 4CA3
    update_camera_window(); // 4BD9
    const auto& p = objects[0];
    u8 limit = hi(camera.step);
    unsigned diff = (p.x - camera.next_x) & 0xFFFF;
    if (diff) {
        const u8 l = u8(diff);
        if (l < camera.window_x) {
            limit = u8(-limit);
            if (l < camera.window_left) {
                u8 a = u8(l - camera.window_left);
                if (a < limit) {
                    a = u8(limit + 1);
                }
                camera.next_x = std::uint16_t((0xFF00u | a) + camera.x);
                camera.map_flags |= map_flag::left;
            }
        } else if (l >= camera.window_right) {
            u8 a = u8(l - camera.window_right);
            if (a >= limit) {
                a = u8(limit - 1);
            }
            camera.next_x = std::uint16_t(((diff & 0xFF00) | a) + camera.x);
            camera.map_flags |= map_flag::right;
        }
    }
    // 4B88
    diff = (p.y - camera.next_y) & 0xFFFF;
    if (!diff) {
        return;
    }
    const u8 l = u8(diff);
    if (l < camera.window_y) {
        if (l < camera.window_top) {
            u8 a = u8(l - camera.window_top);
            if (a < 0xF8) {
                a = 0xF9;
            }
            camera.next_y = std::uint16_t((0xFF00u | a) + camera.y);
            camera.map_flags |= map_flag::up;
        }
    } else if (l >= camera.window_bottom) {
        u8 a = u8(l - camera.window_bottom);
        if (a >= 8) {
            a = 7;
        }
        camera.next_y = std::uint16_t(((diff & 0xFF00) | a) + camera.y);
        camera.map_flags |= map_flag::down;
    }
}

// 4C54: toward the scripted target one pixel per frame; the limit on the
// side it moves to becomes the target.
void Game::follow_script() {
    const auto tx = camera_targets.scripted_x, ty = camera_targets.scripted_y;
    if (tx != camera.x) {
        if (tx > camera.x) {
            camera.next_x = std::uint16_t(camera.x + 1);
            camera.map_flags |= map_flag::right;
            camera.max_x = tx;
        } else {
            camera.next_x = std::uint16_t(camera.x - 1);
            camera.map_flags |= map_flag::left;
            camera.min_x = tx;
        }
    }
    if (ty == camera.y) {
        return;
    }
    if (ty > camera.y) {
        camera.next_y = std::uint16_t(camera.y + 1);
        camera.map_flags |= map_flag::down;
    } else {
        camera.next_y = std::uint16_t(camera.y - 1);
        camera.map_flags |= map_flag::up;
    }
}

// 4CA3: after 4D13 the step limit grows from 2 by 8/256 per frame back to
// 8; otherwise it is 8.
void Game::ramp_camera_step() {
    if (camera.mode & camera_mode::ramp) {
        camera.step = std::uint16_t(camera.step + 8);
        if (hi(camera.step) < 8) {
            return;
        }
        camera.mode &= u8(~camera_mode::ramp);
    }
    camera.step = 0x0800;
}

// 4BD9: the player's aimed screen X: 78 while the camera is scripted or
// centred, else 60 (90 when facing left); the window moves one pixel per
// frame toward the aim.
void Game::update_camera_window() {
    u8 aim = 0x78;
    if (!(camera.mode & (camera_mode::scripted | camera_mode::centred))) {
        aim = (objects[0].display & display_flag::mirrored) ? 0x90 : 0x60;
    }
    camera.aim_x = aim;
    if (camera.aim_x != camera.window_x) {
        const u8 x =
            camera.aim_x < camera.window_x ? u8(camera.window_x - 1) : u8(camera.window_x + 1);
        camera.window_x = x;
        camera.window_left = u8(x - 8);
        camera.window_right = u8(x + 8);
    }
    if (camera.aim_y == camera.window_y) {
        return;
    }
    const u8 y = camera.aim_y < camera.window_y ? u8(camera.window_y - 1) : u8(camera.window_y + 1);
    camera.window_y = y;
    camera.window_top = u8(y - 4);
    camera.window_bottom = u8(y + 4);
}

// 4C2D: the window at the aim; the step limit's whole part 8.
void Game::reset_camera_window() {
    camera.window_x = camera.aim_x;
    camera.window_left = u8(camera.aim_x - 8);
    camera.window_right = u8(camera.aim_x + 8);
    camera.window_y = camera.aim_y;
    camera.window_top = u8(camera.aim_y - 4);
    camera.window_bottom = u8(camera.aim_y + 4);
    camera.step = std::uint16_t((camera.step & 0x00FF) | 0x0800);
}

// 3FA6: a horizontal move within the limits (otherwise none); when the low
// byte changes, the new column is made ready (45E5).
void Game::scroll_horizontally() {
    if (camera.map_flags & map_flag::left) {
        if (hi(camera.next_x) == 0xFF || camera.next_x < camera.min_x) {
            camera.next_x = camera.x;
            return;
        }
    } else if (camera.map_flags & map_flag::right) {
        if (camera.next_x >= camera.max_x) {
            camera.next_x = camera.x;
            return;
        }
    } else {
        return;
    }
    if (lo(camera.x) != lo(camera.next_x)) {
        fetch_map_column(); // 45E5
    }
}

// 3FED: the same vertically (a new row: 46AA).
void Game::scroll_vertically() {
    if (camera.map_flags & map_flag::up) {
        if (hi(camera.next_y) == 0xFF || camera.next_y < camera.min_y) {
            camera.next_y = camera.y;
            return;
        }
    } else if (camera.map_flags & map_flag::down) {
        if (camera.next_y >= camera.max_y) {
            camera.next_y = camera.y;
            return;
        }
    } else {
        return;
    }
    if (lo(camera.y) != lo(camera.next_y)) {
        fetch_map_row(); // 46AA
    }
}

// 4034: the scroll registers for the current camera (the frame interrupt
// shows them while it makes the next position current).
void Game::set_scroll_registers() {
    camera.scroll_x = u8(-(lo(camera.x) + 1));
    camera.scroll_y = u8((camera.y + 0x11u) % 0xE0);
}

// 4053: the map cell at the camera's X (8 further when moving left) and
// its Y plus `dy`, with its block column and row.
unsigned Game::camera_cell(unsigned dy) {
    unsigned x = camera.x;
    if (camera.map_flags & map_flag::left) {
        x = (x + 8) & 0xFFFF;
    }
    camera.block_column = std::uint16_t(x >> 5);
    const unsigned y = (camera.y + dy) & 0xFFFF;
    camera.block_row = u8(y >> 5);
    const unsigned row = rom.word(0x01, level.row_table + u8(camera.block_row * 2));
    const unsigned address = (camera.block_column + row + 0xC001) & 0xFFFF;
    camera.map_cell = std::uint16_t(address - 0xC000);
    return address;
}

// 45E5: the column entering the screen: 8 blocks down from the camera's
// row (+8 pixels), 256 pixels on when moving right; of each block the 4
// entries in the camera's tile column.
void Game::fetch_map_column() {
    unsigned cell = camera_cell(8) - 0xC000u;
    if (camera.map_flags & map_flag::right) {
        cell += 8;
    }
    const unsigned tile_column = (camera_left_x() >> 3) & 3;
    for (unsigned b = 0; b < 8; ++b) {
        const unsigned tiles =
            rom.word(level.block_bank, level.block_table + level_block(cell) * 2u);
        for (unsigned r = 0; r < 4; ++r) {
            camera.column_entries[b * 4 + r] =
                rom.word(level.block_bank, tiles + r * 8 + tile_column * 2);
        }
        cell += level.row_stride;
    }
    camera.map_flags |= map_flag::column_pending;
}

// 46AA: the row entering the screen (the bottom one, D194 further, when
// moving down): 9 blocks across from the camera's column; of each block
// the 4 entries in the camera's tile row.
void Game::fetch_map_row() {
    unsigned cell = camera_cell(0) - 0xC000u;
    if (camera.map_flags & map_flag::down) {
        cell += level.bottom_offset;
    }
    const unsigned tile_row = (camera.y & 0x18) >> 3;
    for (unsigned b = 0; b < 9; ++b) {
        const unsigned tiles =
            rom.word(level.block_bank, level.block_table + level_block(cell + b) * 2u);
        for (unsigned c = 0; c < 4; ++c) {
            camera.row_entries[b * 4 + c] =
                rom.word(level.block_bank, tiles + tile_row * 8 + c * 2);
        }
    }
    camera.map_flags |= map_flag::row_pending;
}

// A block of the level map as the streaming reads it (16-bit cell index).
u8 Game::level_block(unsigned cell) const {
    cell &= 0xFFFF;
    if (cell >= level_map.cells.size()) {
        throw Unrecovered("map streaming past the level map: cell " + std::to_string(cell));
    }
    return level_map.cells[cell];
}

// The camera's X as 4053 and the streaming use it: 8 pixels on when
// moving left.
unsigned Game::camera_left_x() const {
    return (camera.map_flags & map_flag::left) ? (camera.x + 8u) & 0xFFFF : camera.x;
}

// 4644 (in the frame interrupt): the column's 27 entries into the name
// table at the camera's column, from the row 8 pixels below its top
// (wrapping at the table's 28 rows).
void Game::stream_column() {
    const unsigned column = (camera_left_x() >> 3) & 31;
    const unsigned y = (camera.y + 8u) & 0xFFFF;
    const unsigned first = (y >> 3) & 3;
    for (unsigned i = 0; i < 27; ++i) {
        const unsigned row = ((y >> 3) + i) % Display::map_rows;
        display.map[row * Display::map_columns + column] = camera.column_entries[first + i];
    }
}

// 46F0 (in the frame interrupt): the row's 33 entries (32 when moving
// left) into the name table at the camera's row, from its column
// (wrapping at the table's 32 columns).
void Game::stream_row() {
    const unsigned x = camera_left_x();
    const unsigned first = (x >> 3) & 3;
    const unsigned row = (camera.y >> 3) % Display::map_rows;
    const unsigned count = (camera.map_flags & map_flag::left) ? 32 : 33;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned column = ((x >> 3) + i) & 31;
        display.map[row * Display::map_columns + column] = camera.row_entries[first + i];
    }
}

// 068C (in the frame interrupt): the prepared row (46F0) and column (4644)
// go to the name table, then the prepared position becomes current.
void Game::apply_camera() {
    if (video.water_line_mode == 0xFE || !(camera.map_flags & map_flag::update_pending)) {
        return;
    }
    camera.map_flags &= u8(~map_flag::update_pending);
    if (camera.map_flags & map_flag::row_pending) {
        stream_row();
    }
    if (camera.map_flags & map_flag::column_pending) {
        stream_column();
    }
    camera.map_flags &= u8(~(map_flag::row_pending | map_flag::column_pending));
    camera.x = camera.next_x;
    camera.y = camera.next_y;
    display.level_shown = true;
}

// The background entry of the act's map at a world position (the
// original's map rows and columns: 46AA/45E5, blocks of 4x4 entries).
std::uint16_t Game::level_entry(int x, int y) const {
    if (x < 0 || y < 0) {
        return 0;
    }
    const unsigned row = rom.word(0x01, level.row_table + u8((unsigned(y) >> 5) * 2));
    const unsigned cell = (row + (unsigned(x) >> 5) + 1) & 0xFFFF;
    if (cell >= level_map.cells.size()) {
        return 0;
    }
    const u8 block = level_map.cells[cell];
    const unsigned tiles = rom.word(level.block_bank, level.block_table + block * 2u);
    return rom.word(level.block_bank,
                    tiles + ((unsigned(y) >> 3) & 3) * 8 + ((unsigned(x) >> 3) & 3) * 2);
}

namespace {
// The value v + k * period nearest to `near`.
int nearest(int v, int period, int near) {
    const int k = (near - v + period / 2) / period;
    int best = v + k * period;
    for (int d = -1; d <= 1; ++d) {
        const int c = v + (k + d) * period;
        if (std::abs(c - near) < std::abs(best - near)) {
            best = c;
        }
    }
    return best;
}
} // namespace

// The world position of the 256x192 picture's top left corner, from the
// scroll the frame is shown with (shake included) and the camera it was
// computed from (the frame interrupt records it in the display).
int Game::picture_x() const {
    return nearest(-int(display.scroll_x) - 1, 256, int(camera.x)) + 1;
}

int Game::picture_y() const {
    return nearest(int(display.scroll_y) - 17, 224, int(camera.y)) + 17;
}

// The act's background for the display: each background cell holds the
// map's entry for the world position within the 256x224 area from the
// picture's corner that falls on it.
Display::LevelBackground Game::level_background() const {
    const int x0 = picture_origin.x, y0 = picture_origin.y;
    return [this, x0, y0](int column, int row) {
        const int dx = ((column * 8 - x0) % 256 + 256) % 256;
        const int dy = ((row * 8 - y0) % 224 + 224) % 224;
        return level_entry(x0 + dx, y0 + dy);
    };
}

// The act's map stops being drawn from the level (the original's name
// table keeps what was streamed into it, which the display's map holds).
void Game::freeze_level_background() {
    display.level_shown = false;
}

} // namespace triple_trouble_gg
