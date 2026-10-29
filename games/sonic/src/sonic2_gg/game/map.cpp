// The scrolling map: name table uploads of new rows and columns (573F, 57EB).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 49EF: once per frame while the map is enabled: move the camera targets,
// prepare any newly exposed column and row, and set the scroll registers.
// The frame interrupt then uploads them and makes the targets current.
void Game::update_map() {
    if (!(camera.map_flags & map_flag::enabled)) {
        return;
    }
    follow_camera();        // 5C18
    scroll_horizontally();  // 4A5F
    scroll_vertically();    // 4A9C
    set_scroll_registers(); // 4AE1
    camera.map_flags |= map_flag::update_pending;
}

// 4A0F / 4A15: enable or disable the map; disabling remembers the camera as
// the left limit.
void Game::enable_map() {
    camera.map_flags |= map_flag::enabled;
}

void Game::disable_map() {
    camera.map_flags &= u8(~map_flag::enabled);
    camera.min_x = camera.x;
}

// 4A21 / 4A34: a scripted camera target (BC, DE), or back to following Sonic.
void Game::script_camera(unsigned x, unsigned y) {
    camera.map_flags |= map_flag::enabled;
    camera.mode |= camera_mode::scripted;
    camera.scripted_x = x;
    camera.scripted_y = y;
}

void Game::follow_sonic() {
    camera.map_flags |= map_flag::enabled;
    camera.mode &= u8(~camera_mode::scripted);
}

// 4A3F (0302): the left camera limit follows the camera, so it cannot
// scroll back.
void Game::lock_camera_left() {
    if (camera.min_x < camera.x) {
        camera.min_x = camera.x;
    }
}

// 4A4F (0305): the same for the right limit.
void Game::lock_camera_right() {
    if (camera.max_x >= camera.x) {
        camera.max_x = camera.x;
    }
}

// 5C18: moves the camera targets toward Sonic. Only the low byte of his
// distance from the camera is compared, and a forward move is limited to 8
// pixels by replacing that low byte (LD L,A): the high byte stays, so
// Sonic 256 or more pixels ahead moves the target that far at once.
// Bits 0-3 of map_flags record the directions.
void Game::follow_camera() {
    camera.map_flags &= u8(~(map_flag::right | map_flag::left | map_flag::down | map_flag::up));
    if (camera.mode & camera_mode::scripted) {
        follow_script(); // 5D47
        return;
    }
    update_camera_window(); // 5CC7
    const Object& sonic = objects[0];
    unsigned diff = (sonic.x - camera.x) & 0xFFFF;
    if (diff) {
        const u8 l = u8(diff);
        if (l < camera.window[0]) {
            if (l < camera.window[2]) {
                u8 a = u8(l - camera.window[2]);
                if (a < 0xF7) {
                    a = 0xF8;
                }
                camera.target_x = (0xFF00u | a) + camera.x;
                camera.map_flags |= map_flag::left;
            }
        } else if (l >= camera.window[1]) {
            u8 a = u8(l - camera.window[1]);
            if (a >= 9) {
                a = 8;
            }
            camera.target_x = ((diff & 0xFF00) | a) + camera.x;
            camera.map_flags |= map_flag::right;
        }
    }
    // 5C7A: vertically, the distance is measured from the target, not the
    // current camera position.
    diff = (sonic.y - camera.target_y) & 0xFFFF;
    if (!diff) {
        return;
    }
    const u8 l = u8(diff);
    if (l < camera.window[3]) {
        if (l < camera.window[5]) {
            u8 a = u8(l - camera.window[5]);
            if (a < 0xF7) {
                a = 0xF8;
            }
            camera.target_y = (0xFF00u | a) + camera.y;
            camera.map_flags |= map_flag::up;
        }
    } else if (l >= camera.window[4]) {
        u8 a = u8(l - camera.window[4]);
        if (a >= 9) {
            a = 8;
        }
        camera.target_y = ((diff & 0xFF00) | a) + camera.y;
        camera.map_flags |= map_flag::down;
    }
}

// 5CC7: Sonic's horizontal screen position the camera aims for: 80 on the
// title/ending screens, 78 when the camera is held (D15F bits 0-1), 50
// normally and B0 when facing left. The dead zone (+-8 horizontally, +-4
// vertically) eases toward its aim by 2 (1) pixels per frame.
void Game::update_camera_window() {
    u8 aim = 0x80;
    if (!(flow.game_flags & game_flag::scene)) {
        aim = 0x78;
        if (!(camera.mode & (camera_mode::hold_vertical | camera_mode::scripted))) {
            aim = (objects[0].display & display_flag::mirrored) ? 0xB0 : 0x50;
        }
    }
    camera.offset_x = aim;
    u8 b = camera.window[0];
    if (aim != b) {
        b = aim > b ? u8(b + 2) : u8(b - 2);
        camera.window[0] = b;
        camera.window[2] = u8(b - 8);
        camera.window[1] = u8(b + 8);
    }
    b = camera.window[3];
    if (camera.offset_y != b) {
        b = camera.offset_y > b ? u8(b + 1) : u8(b - 1);
        camera.window[3] = b;
        camera.window[5] = u8(b - 4);
        camera.window[4] = u8(b + 4);
    }
}

// 5D26: the dead zone set to its aim at once.
void Game::reset_camera_window() {
    camera.window[0] = camera.offset_x;
    camera.window[2] = u8(camera.offset_x - 8);
    camera.window[1] = u8(camera.offset_x + 8);
    camera.window[3] = camera.offset_y;
    camera.window[5] = u8(camera.offset_y - 4);
    camera.window[4] = u8(camera.offset_y + 4);
}

// 5D47: a scripted camera moves one pixel per frame toward D2CE/D2D0, and
// (except in zone 6) narrows the horizontal limits behind it.
void Game::follow_script() {
    const unsigned x = camera.scripted_x;
    if (x != camera.x) {
        if (x > camera.x) {
            camera.target_x = camera.x + 1u;
            camera.map_flags |= map_flag::right;
            if (flow.zone != 6) {
                camera.max_x = x;
            }
        } else {
            camera.target_x = camera.x - 1u;
            camera.map_flags |= map_flag::left;
            if (flow.zone != 6) {
                camera.min_x = x;
            }
        }
    }
    const unsigned y = camera.scripted_y;
    if (y == camera.y) {
        return;
    }
    if (y > camera.y) {
        camera.target_y = camera.y + 1u;
        camera.map_flags |= map_flag::down;
    } else {
        camera.target_y = camera.y - 1u;
        camera.map_flags |= map_flag::up;
    }
}

// 4A5F / 4A9C: a target outside the camera limits is cancelled. (The
// original also prepares the column or row of the background entering the
// screen, 56E0 / 57A5; the background is drawn from the level map here, see
// level_background.)
void Game::scroll_horizontally() {
    if (camera.target_x < camera.min_x || camera.target_x >= camera.max_x) {
        camera.target_x = camera.x;
    }
}

void Game::scroll_vertically() {
    if (camera.target_y < camera.min_y || camera.target_y >= camera.max_y) {
        camera.target_y = camera.y;
    }
}

// 4AE1: register 8 scrolls left by camera_x + 1; register 9 is the camera Y
// plus 17, modulo the 224-line name table.
void Game::set_scroll_registers() {
    camera.scroll_x = u8(-(lo(camera.x) + 1));
    unsigned y = (camera.y + 0x11u) & 0xFFFF;
    while (y >= 0xE0) {
        y -= 0xE0;
    }
    camera.scroll_y = u8(y);
}

// The block from the level map through the row table, then the 8x8 cell's
// entry among the block's 4x4 entries (the 56E0/57A5 lookup).
std::uint16_t Game::level_entry(int x, int y) const {
    if (x < 0 || y < 0) {
        return 0;
    }
    const unsigned row = rom.word(1, level.row_table + ((unsigned(y) >> 5) * 2u & 0xFF));
    const unsigned cell = (row + ((unsigned(x) >> 5) & 0xFF) + 1u) & 0x0FFF;
    const u8 block = level.map[cell];
    const unsigned tiles = rom.word(level.map_bank, level.block_table + block * 2u);
    return std::uint16_t(rom.word(level.map_bank, tiles + ((unsigned(y) >> 3) & 3) * 8 +
                                                      ((unsigned(x) >> 3) & 3) * 2));
}

namespace {
// The value congruent to `value` modulo `period` that is nearest to `near`.
int nearest(int value, int period, int near) {
    int d = (value - near) % period;
    if (d < 0) {
        d += period;
    }
    if (d >= period / 2) {
        d -= period;
    }
    return near + d;
}
} // namespace

// The scroll is set for the camera (4AE1): horizontally -(camera X + 1),
// vertically camera Y + 17 in the 224-line background; with a screen shake
// (04E4) the picture moves by its offsets.
int Game::picture_x() const {
    return nearest(-int(display.scroll_x) - 1, 256, int(camera.x)) + 1;
}

int Game::picture_y() const {
    return nearest(int(display.scroll_y) - 17, 224, int(camera.y)) + 17;
}

Display::LevelBackground Game::level_background() const {
    const int x0 = picture_x(), y0 = picture_y();
    return [this, x0, y0](int column, int row) {
        const int dx = ((column * 8 - x0) % 256 + 256) % 256;
        const int dy = ((row * 8 - y0) % 224 + 224) % 224;
        return level_entry(x0 + dx, y0 + dy);
    };
}

void Game::freeze_level_background() {
    if (!display.level_shown) {
        return;
    }
    const auto level = level_background();
    for (int row = 0; row < Display::map_rows; ++row) {
        for (int column = 0; column < Display::map_columns; ++column) {
            display.map[std::size_t(row * Display::map_columns + column)] = level(column, row);
        }
    }
    display.level_shown = false;
}

gg::Screen Game::picture() const {
    return display.render(level_background());
}

} // namespace sonic2_gg
