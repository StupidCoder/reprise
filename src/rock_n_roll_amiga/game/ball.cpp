#include "rock_n_roll_amiga/game/ball.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"

namespace rocknroll::game {
namespace {

constexpr int reads_apart = 25;            // $70754: a read at least every 25 updates without grip
constexpr Fixed bounce_speed = 1000;        // $700E8: $3E8
constexpr Fixed ice_push = 0x1200;          // $7040C → $6C242/$6C246
constexpr Fixed view_centre_x = 0x90000, view_centre_y = 0x30000; // $707AA
constexpr Fixed view_min_x = 0x20000, view_max_x = 0x100000, view_max_y = 0x60000;

std::uint16_t high_word(Fixed v) { return std::uint16_t(std::uint32_t(v) >> 16); }
std::uint16_t low_word(Fixed v) { return std::uint16_t(std::uint32_t(v)); }

// NEG of a 16-bit multiply by -1 ($707F8 `muls.w #$ffff`): only the low
// word of the speed counts.
Fixed negated_word(Fixed v) { return -Fixed(std::int16_t(low_word(v))); }

// How hard one mouse axis pushes: 2 or more counts $200, one count 100
// ($64), compared as the original's 16-bit CMPI/BPL (wrapping).
Fixed push_for(std::uint16_t moved) {
    if (std::int16_t(std::uint16_t(moved - 0x1AE)) >= 0) return 0x200;
    if (std::int16_t(std::uint16_t(moved - 0x1E)) >= 0) return 100;
    return 0;
}

// Friction toward 0 ($705D2/$705EC). A negative speed is zeroed only when
// it reaches +friction or more, which never happens, so it may flip sign
// by less than one friction step; the next update clears it.
Fixed with_friction(Fixed v, Fixed friction) {
    if (v < 0) {
        v += friction;
        if (!(v < friction)) v = 0;
    } else if (v > 0) {
        v -= friction;
        if (!(v > friction)) v = 0;
    }
    return v;
}

struct Steered {
    Fixed x, y; // the speeds $7040C leaves in D3/D5, which $707AA uses
};

// $7040C: the mouse changes the speed; the camera moves by it.
Steered steer(Ball& b, MouseCounters mouse) {
    Fixed vx = b.speed_x, vy = b.speed_y;
    const auto finish = [&] {
        b.slow = false;
        b.speed_x = vx;
        b.speed_y = vy;
        b.camera_x += vx;
        b.camera_y += vy;
        return Steered{vx, vy};
    };
    if (!b.grip) {
        ++b.updates_without_read;
        b.grip = true;
        if (b.updates_without_read < reads_apart) return finish();
    }
    b.updates_without_read = 0;
    const std::uint16_t now = mouse.word(), before = b.last_mouse.word();
    b.last_mouse = mouse;

    const auto end_reading = [&] {
        b.steered = true;
        b.hold_x = b.hold_y = false;
        return finish();
    };
    // y: counter decreasing = mouse up = speed up the screen (negative).
    const std::uint16_t up = std::uint16_t((before & 0xFF00) - (now & 0xFF00));
    const std::uint16_t down = std::uint16_t((now & 0xFF00) - (before & 0xFF00));
    // The acceleration actually stored follows the original's path through
    // $7047E-$704E2 (and $70690 for ice, only within the limit).
    if (Fixed push = push_for(up)) {
        b.last_push_y = push;
        b.ice_push_y = -ice_push;
        if (vy > b.min_speed_y) {
            if (b.slow) b.last_push_y = 0x20;
            vy -= b.last_push_y;
        }
    } else if (Fixed push2 = push_for(down)) {
        b.last_push_y = push2;
        b.ice_push_y = ice_push;
        if (vy < b.max_speed_y) {
            if (b.slow) b.last_push_y = 0x20;
            vy += b.last_push_y;
        }
    } else {
        b.last_push_y = 0x200;
        // No y movement: with hold_y the x axis is not looked at at all
        // ($704EC → $70582).
        if (b.hold_y) return end_reading();
        if (b.steered && !b.slow) vy = with_friction(vy, b.friction);
    }
    // x, from the low byte moved up into the high one.
    const std::uint16_t left = std::uint16_t(((before & 0xFF) << 8) - ((now & 0xFF) << 8));
    const std::uint16_t right = std::uint16_t(((now & 0xFF) << 8) - ((before & 0xFF) << 8));
    if (Fixed push = push_for(left)) {
        b.last_push_x = push;
        b.ice_push_x = -ice_push;
        if (vx > b.min_speed_x) {
            if (b.slow) b.last_push_x = 0x20;
            vx -= b.last_push_x;
        }
    } else if (Fixed push2 = push_for(right)) {
        b.last_push_x = push2;
        b.ice_push_x = ice_push;
        if (vx < b.max_speed_x) {
            if (b.slow) b.last_push_x = 0x20;
            vx += b.last_push_x;
        }
    } else {
        b.last_push_x = 0x200;
        if (!b.hold_x && b.steered && !b.slow) vx = with_friction(vx, b.friction);
    }
    return end_reading();
}

// $703B4: after a contact, the speed along it is halved (arithmetic shift).
void halve_along_contact(Ball& b) {
    if (b.contact_rows != 0) b.speed_y >>= 1;
    if (b.contact_columns != 0) b.speed_x >>= 1;
}

// $707AA: the camera follows the ball only while the ball sits at the
// view's centre; at the map's edges the ball moves in the view instead.
// The moves use the speeds as $7040C left them (before any halving).
void keep_in_view(Ball& b, Steered s) {
    // y
    if (b.view_y >= view_centre_y && b.camera_y > 0) {
        if (std::uint32_t(b.view_y) <= std::uint32_t(view_centre_y) && b.camera_y < b.camera_limit_y) {
            b.view_y = view_centre_y;
        } else {
            b.camera_y = b.camera_limit_y;
            b.view_y += s.y;
            const Fixed moved = b.view_y;
            if (moved >= view_max_y) {
                b.view_y = view_max_y;
                b.speed_y = negated_word(s.y);
            }
            if (moved < view_centre_y) b.view_y = view_centre_y;
        }
    } else {
        b.camera_y = 0;
        b.view_y += s.y;
        const Fixed moved = b.view_y;
        if (moved <= 0) {
            b.view_y = 0;
            b.speed_y = negated_word(s.y);
        }
        if (moved > view_centre_y) b.view_y = view_centre_y;
    }
    // x
    if (b.view_x >= view_centre_x && b.camera_x > 0) {
        if (std::uint32_t(b.view_x) <= std::uint32_t(view_centre_x) && b.camera_x < b.camera_limit_x) {
            b.view_x = view_centre_x;
        } else {
            b.camera_x = b.camera_limit_x;
            b.view_x += s.x;
            const Fixed moved = b.view_x;
            if (moved >= view_max_x) {
                b.view_x = view_max_x;
                b.speed_x = negated_word(s.x);
            }
            if (moved < view_centre_x) b.view_x = view_centre_x;
        }
    } else {
        b.camera_x = 0;
        b.view_x += s.x;
        const Fixed moved = b.view_x;
        if (moved <= view_min_x) {
            b.view_x = view_min_x;
            b.speed_x = negated_word(s.x);
        }
        if (moved > view_centre_x) b.view_x = view_centre_x;
    }
}

// $6B4AC: which of the 2 x 2 cells under the ball holds its centre (1 top
// left … 4 bottom right). The fractions are added as 16-bit words, so a
// carry into the next tile is lost, as in the original.
int centre_of(const Ball& b) {
    int cell = 4;
    if (std::uint16_t(low_word(b.camera_x) + low_word(b.view_x)) <= 0xA000) cell -= 1;
    if (std::uint16_t(low_word(b.camera_y) + low_word(b.view_y)) <= 0x9000) cell -= 2;
    return cell;
}

// Linear index of the top-left cell under the ball ($6CFC6): whole tiles of
// camera and view added as 16-bit words, two columns left (the drawing
// buffer starts two columns before the camera's).
long first_cell_of(const Ball& b, const LevelMap& map) {
    const std::uint16_t row = std::uint16_t(high_word(b.camera_y) + high_word(b.view_y));
    const std::uint16_t column = std::uint16_t(high_word(b.camera_x) + high_word(b.view_x));
    return long(row) * map.columns + column - 2;
}

// $6CA26: the ball rolls off into the abyss; its speed is pushed out of
// the cell it fell from by at least $2000 (the bit is set on the size).
void start_fall(Ball& b, int cell) {
    b.fall_cell = cell;
    const auto away = [](Fixed v, bool negative) {
        return negative ? -((-v) | 0x2000) : (v | 0x2000);
    };
    b.speed_x = away(b.speed_x, cell == 1 || cell == 3);
    b.speed_y = away(b.speed_y, cell == 1 || cell == 2);
    b.mode = BallMode::falling;
}

bool is_floor(std::uint8_t v) { // handler $6E39A (plain floor: no rule while rolling, see game.md)
    return (v >= 0x15 && v <= 0x1C) || v == 0x25 || v == 0x27 || v == 0x28 || v == 0x3C || v == 0x41 || v == 0x42 ||
           (v >= 0x48 && v <= 0x64) || v == 0x6D || v == 0x6E || (v >= 0x77 && v <= 0x8C) || v == 0xA0 || v == 0xA1 ||
           v == 0xA3 || v == 0xA4 || (v >= 0xA6 && v <= 0xA8) || (v >= 0xAB && v <= 0xAD) || v == 0xAF || v == 0xB0 ||
           (v >= 0xB2 && v <= 0xB4) || v == 0xB7 || v == 0xB8 || v == 0xBA || v == 0xBB || (v >= 0xBD && v <= 0xBF) ||
           (v >= 0xC1 && v <= 0xD0) || (v >= 0xEC && v <= 0xF0);
}

// Pushes by arrows, the dome and slopes ($6E444-$6E58C): twice the last
// acceleration, within the speed limits.
void push_up(Ball& b) { if (b.speed_y > b.min_speed_y) b.speed_y -= 2 * b.last_push_y; }
void push_down(Ball& b) { if (b.speed_y < b.max_speed_y) b.speed_y += 2 * b.last_push_y; }
void push_left(Ball& b) { if (b.speed_x > b.min_speed_x) b.speed_x -= 2 * b.last_push_x; }
void push_right(Ball& b) { if (b.speed_x < b.max_speed_x) b.speed_x += 2 * b.last_push_x; }

// Slopes $65-$6C / $6F-$76 ($6E4DC/$6E546): once per update (hold_x), by the
// last acceleration. The rightward one tests the speed against the
// leftward limit, as the original does.
void slope_left(Ball& b) {
    if (b.hold_x) return;
    b.hold_x = true;
    if (b.speed_x > b.min_speed_x) b.speed_x -= b.last_push_x;
}
void slope_right(Ball& b) {
    if (b.hold_x) return;
    b.hold_x = true;
    if (b.speed_x > b.min_speed_x) b.speed_x += b.last_push_x;
}

// $6D0FA and the cell handlers (table $733C4) rebuilt so far.
void cell_rules(Ball& b, LevelMap& map) {
    const long first = first_cell_of(b, map);
    b.high_cell = false;
    b.hold_x = b.hold_y = false;
    const long cells[4] = {first, first + 1, first + map.columns, first + map.columns + 1};
    for (int n = 1; n <= 4; ++n) {
        const std::uint8_t v = map.cell(cells[n - 1]);
        if (v >= 0x50) b.high_cell = true;
        const bool centre = n == b.centre_cell;
        if (v == 0x21) { // abyss $6E378 → $6C9C0 (repair sets not rebuilt)
            if (centre && b.collisions && b.mode == BallMode::rolling) start_fall(b, n);
        } else if (v >= 0x09 && v <= 0x0C) { // arrows $6E444 up, $6E582 right, $6E4AE down, $6E518 left
            if (centre) (v == 0x09 ? push_up : v == 0x0A ? push_right : v == 0x0B ? push_down : push_left)(b);
        } else if (v >= 0x3D && v <= 0x40) { // dome quarters $6D58E-$6D612: away from its middle
            if (centre) {
                (v <= 0x3E ? push_up : push_down)(b);
                (v == 0x3D || v == 0x3F ? push_left : push_right)(b);
            }
        } else if (v >= 0x65 && v <= 0x6C) {
            if (centre) slope_left(b);
        } else if (v >= 0x6F && v <= 0x76) {
            if (centre) slope_right(b);
        } else if (v >= 0x1D && v <= 0x20) { // ice $6E38E ($719E8 item not rebuilt)
            if (centre) b.slow = true;
        } else if (v == 0x14 || (v >= 0x8D && v <= 0x9B) || is_floor(v)) {
            // $14: only the repair-set rule ($6D510); railings $6E3A0: none
        } else {
            ++b.unhandled_cells;
            b.unhandled_kinds[v] = true;
        }
    }
}

// $6C274: the rolling picture's phases follow the speed (on ice the
// direction last pushed, once).
void roll(Ball& b) {
    if (b.slow) {
        b.roll_x += b.ice_push_x;
        b.roll_y += b.ice_push_y;
        b.ice_push_x = b.ice_push_y = 0;
    } else {
        b.roll_x += b.speed_x;
        b.roll_y += b.speed_y;
    }
}

// Colour bit 4 of the picture in the view at a map pixel when the contact
// test runs: railings are drawn with colours 16-31, the abyss pattern has
// none. The four cells under the ball are left out by the tile drawer
// (markers $F1-$F4, $6D08A) and only those above $50 (except $EC) are drawn
// before the test ($6FCC0); the others are not there yet, so they count as
// empty (observed in rail1 update 113: a pickup under the ball's top).
bool solid_at(const LevelMap& map, const data::World& world, long first_under_ball, int x, int y) {
    if (x < 0 || y < 0) return false;
    const long index = long(y / 16) * map.columns + x / 16;
    const std::uint8_t v = map.cell(index);
    if (v == 0 || v > 0xF0 || data::is_abyss(v)) return false;
    const bool under_ball = index == first_under_ball || index == first_under_ball + 1 ||
                            index == first_under_ball + map.columns || index == first_under_ball + map.columns + 1;
    if (under_ball && (v <= 0x50 || v == 0xEC)) return false;
    const int tile = int(v) - 1;
    return world.bank.at(tile % 20 * 16 + x % 16, tile / 20 * 16 + y % 16) & 16;
}

// The ball's contact disc: colour bit 4 of the picture at bank row 456,
// x 128 ($2F450 + plane 4), rows 0-12.
constexpr int disc_x = 128, disc_y = 456, disc_rows = 13;

// $6F666/$6F60A/$6FDEE: the disc against the drawn railings; rows 0-3
// push from the top, 9-12 from the bottom, left/right halves sideways.
void touch(Ball& b, const LevelMap& map, const data::World& world) {
    b.contact_rows = b.contact_columns = 0;
    b.grip = true;
    if (!b.collisions || !b.high_cell) return;
    const int fine_x = (std::uint16_t(low_word(b.camera_x) + low_word(b.view_x)) & 0xF000) >> 12;
    const int left = (int(std::uint16_t(high_word(b.camera_x) + high_word(b.view_x))) - 2) * 16 + fine_x;
    const int top = (b.camera_y >> 12) + (b.view_y >> 12);
    const long first = first_cell_of(b, map);
    for (int r = 0; r < disc_rows; ++r) {
        bool hit_left = false, hit_right = false;
        for (int i = 0; i < 16; ++i) {
            if (!(world.bank.at(disc_x + i, disc_y + r) & 16)) continue;
            if (solid_at(map, world, first, left + i, top + r)) (i < 8 ? hit_left : hit_right) = true;
        }
        const int vertical = r <= 3 ? 1 : r >= 9 ? -1 : 0;
        const bool sideways = r >= 1 && r <= 11;
        if (hit_right) {
            b.contact_rows += vertical;
            if (sideways) b.contact_columns -= 1;
        }
        if (hit_left) {
            b.contact_rows += vertical;
            if (sideways) b.contact_columns += 1;
        }
    }
}

// $700E8: bounce away from the contact; the mouse is not read next update.
void bounce(Ball& b) {
    const int a = b.contact_rows, e = b.contact_columns;
    if (a == 0 && e == 0) return;
    const Fixed vx = b.speed_x, vy = b.speed_y;
    if (a == 0) {
        b.speed_x = (e > 0 ? bounce_speed : -bounce_speed) - vx;
    } else if (e == 0) {
        b.speed_y = (a > 0 ? bounce_speed : -bounce_speed) - vy;
    } else if (a > 0 && e > 0) {
        b.speed_x = bounce_speed - vy;
        b.speed_y = bounce_speed - vx;
    } else if (a > 0) {
        b.speed_x = -bounce_speed + vy;
        b.speed_y = bounce_speed + vx;
    } else if (e > 0) {
        b.speed_x = bounce_speed + vy;
        b.speed_y = -bounce_speed + vx;
    } else {
        b.speed_x = -bounce_speed - vy;
        b.speed_y = -bounce_speed - vx;
    }
    b.grip = false;
}

// $6ABF0, near the end of each update: speeds back within the limits
// (compared by the sign of the difference, as the original's CMP/BPL).
void clamp_speeds(Ball& b) {
    const auto clamp = [](Fixed& v, Fixed low, Fixed high) {
        const Fixed old = v;
        if (old < 0) {
            if (old - low >= 0) return;
            v = low;
        }
        if (old - high >= 0) v = high;
    };
    clamp(b.speed_x, b.min_speed_x, b.max_speed_x);
    clamp(b.speed_y, b.min_speed_y, b.max_speed_y);
}

} // namespace

Ball Ball::at_start(const LevelMap& map) {
    Ball b;
    b.camera_x = map.camera_start_x;
    b.camera_y = map.camera_start_y;
    b.view_x = map.view_start_x;
    b.view_y = map.view_start_y;
    b.camera_limit_x = map.camera_limit_x;
    b.camera_limit_y = map.camera_limit_y;
    return b;
}

void update_ball(Ball& b, LevelMap& map, const data::World& world, MouseCounters mouse) {
    if (b.mode != BallMode::rolling) return; // falling: next slice
    const Steered s = steer(b, mouse);
    halve_along_contact(b);
    keep_in_view(b, s);
    b.centre_cell = centre_of(b);
    cell_rules(b, map);
    if (b.mode == BallMode::rolling) roll(b);
    touch(b, map, world);
    bounce(b);
    clamp_speeds(b);
}

} // namespace rocknroll::game
