// Sonic in a pipe network (state 21, 43DC) and on a swing pole (state 22,
// 4290, grabbed via object 28). Pipe pieces are map blocks 30-3F under
// Sonic (D365, or D367): straights, corners and junctions where the
// controller picks the way; leaving the pipes launches him curled.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
constexpr u8 up = 0x01, down = 0x02, left = 0x04, right = 0x08;
} // namespace

// 46E4 / 46F4: centring on the pipe: X in its 32-pixel column (only the low
// byte), or Y on its row.
void Game::pipe_centre_x() {
    Object& o = sonic();
    o.x = with_lo(o.x, (lo(o.x) & 0xE0) + 0x0E);
    player.pipe_aligned = 0xFF;
}

void Game::pipe_centre_y() {
    Object& o = sonic();
    unsigned y = (unsigned(o.y) + 0xFFF0u) & 0xFFFF;
    y = (y & 0xFF00) | (y & 0xE0);
    o.y = (y + 0x002A) & 0xFFFF;
    player.pipe_aligned = 0xFF;
}

// 46A4 / 46B4 / 46C4 / 46D4: on along a new direction at 6 pixels per
// frame.
void Game::pipe_go(u8 direction) {
    Object& o = sonic();
    if (direction == right || direction == left) {
        pipe_centre_y();
        o.vy = 0;
        o.vx = direction == right ? 0x0600 : 0xFA00;
    } else {
        pipe_centre_x();
        o.vy = direction == down ? 0x0600 : 0xFA00;
        o.vx = 0;
    }
}

// 4686 / 468E / 4697: where in the block Sonic is: before the middle
// moving left, past it moving right, or in the vertical turning window.
bool Game::pipe_x_before_middle() const {
    return (lo(objects[0].x) & 0x1F) < 0x0E;
}

bool Game::pipe_x_past_middle() const {
    return (lo(objects[0].x) & 0x1F) >= 0x0E;
}

bool Game::pipe_y_in_window() const {
    return (u8(lo(objects[0].y) - 0x0E) & 0x1F) >= 0x14;
}

// 43DC (state 21)
void Game::sonic_pipe() {
    Object& o = sonic();
    o.display &= u8(~display_flag::hidden);
    camera.mode |= camera_mode::hold_vertical;
    if (player.floor_block != player.pipe_block) {
        player.pipe_aligned = 0;
    }
    player.pipe_block = player.floor_block;
    u8 piece = u8(player.floor_block - 0x30);
    bool check_aligned = true;
    if (player.floor_block < 0x30 || piece >= 0x10) {
        piece = u8(player.floor_block2 - 0x30);
        if (player.floor_block2 < 0x30 || piece >= 0x10) {
            // 4450: out of the pipes: launched.
            sound_.request(sound::pipe);
            player.hit_request = 0;
            o.touch_sides = 0;
            to_launched(); // 3091
            return;
        }
        check_aligned = false;
    }
    if (!check_aligned || !player.pipe_aligned) {
        pipe_piece(piece); // 441C
    }
    move_object(o);    // 66C3
    collide_terrain(); // 6AB6
}

// 442A: the pieces.
void Game::pipe_piece(u8 piece) {
    Object& o = sonic();
    const u8 held = input.held[0];
    const bool horizontal = hi(o.vx) != 0;
    const bool leftwards = hi(o.vx) & 0x80;
    const bool upwards = hi(o.vy) & 0x80;
    switch (piece) {
    case 0x0: // 446B: horizontal straight
    case 0xD:
    case 0xE:
        pipe_centre_y();
        return;
    case 0x1: // 4471: vertical straight
    case 0xB:
    case 0xC:
    case 0xF:
        pipe_centre_x();
        return;
    case 0x2: // 45FF: corner right/down
        if (!horizontal) {
            pipe_go(right);
        } else if (pipe_x_before_middle()) {
            pipe_go(down);
        }
        return;
    case 0x3: // 461D: corner left/down
        if (!horizontal) {
            pipe_go(left);
        } else if (pipe_x_past_middle()) {
            pipe_go(down);
        }
        return;
    case 0x4: // 463B: corner right/up
        if (!horizontal) {
            if (pipe_y_in_window()) {
                pipe_go(right);
            }
        } else if (pipe_x_before_middle()) {
            pipe_go(up);
        }
        return;
    case 0x5: // 4659: corner left/up
        if (!horizontal) {
            if (pipe_y_in_window()) {
                pipe_go(left);
            }
        } else if (pipe_x_past_middle()) {
            pipe_go(up);
        }
        return;
    case 0x6: // 4477: junction left/right/up
        if (!horizontal) {
            if (pipe_y_in_window()) {
                pipe_go((held & right) ? right : (held & left) ? left : up);
            }
        } else if (leftwards) {
            if (pipe_x_before_middle()) {
                pipe_go((held & up) ? up : left);
            }
        } else if (pipe_x_past_middle()) {
            pipe_go((held & up) ? up : right);
        }
        return;
    case 0x7: // 44D9: junction left/right/down
        if (!horizontal) {
            pipe_go((held & right) ? right : (held & left) ? left : down);
        } else if (leftwards) {
            if (pipe_x_before_middle()) {
                pipe_go((held & down) ? down : left);
            }
        } else if (pipe_x_past_middle()) {
            pipe_go((held & down) ? down : right);
        }
        return;
    case 0x8: // 459D: junction up/down/right
        if (horizontal) {
            if (pipe_x_before_middle()) {
                pipe_go((held & up) ? up : (held & down) ? down : right);
            }
        } else if (upwards) {
            pipe_go((held & right) ? right : up);
        } else if (pipe_y_in_window()) {
            pipe_go((held & right) ? right : down);
        }
        return;
    case 0x9: // 453B: junction up/down/left
        if (horizontal) {
            if (pipe_x_past_middle()) {
                pipe_go((held & up) ? up : (held & down) ? down : left);
            }
        } else if (upwards) {
            pipe_go((held & left) ? left : up);
        } else if (pipe_y_in_window()) {
            pipe_go((held & left) ? left : down);
        }
        return;
    default: // 4677: A does nothing
        return;
    }
}

// --- the swing pole --------------------------------------------------------

// 4309: Sonic on the circle around the pole: X = centre + r * sin(angle),
// Y = centre + 12 + r * cos(angle), r = 50 (38 with centre X bit 15), as
// 8 x 8 bit products of the radius and |sine|.
unsigned Game::swing_offset(u8 angle) const {
    const u8 radius = (hi(player.swing_center_x) & 0x80) ? 0x38 : 0x50;
    const u8 sine = rom.byte(rom::sine, angle);
    const bool negative = sine & 0x80;
    const unsigned magnitude = negative ? u8(0x100 - sine) : sine;
    const unsigned product = (magnitude * radius) & 0xFFFF;
    if (!negative) {
        return product >> 8;
    }
    return 0xFF00 | (((0x10000 - product) & 0xFFFF) >> 8);
}

void Game::swing_place() {
    Object& o = sonic();
    o.x = (swing_offset(player.swing_angle) + (unsigned(player.swing_center_x) & 0x7FFF)) & 0xFFFF;
    o.y = (swing_offset(u8(player.swing_angle + 0xC0)) + unsigned(player.swing_center_y) + 0x000C) &
          0xFFFF;
}

// 4290 (state 22): left/right change the swing speed (3-7.99 angle steps
// per frame); a whoosh each turn; a button lets go, flying off along the
// circle (state 0B) at 32 times the speed.
void Game::sonic_swing() {
    Object& o = sonic();
    touch_objects(); // 3773
    o.display &= u8(~display_flag::hidden);
    camera.mode |= camera_mode::hold_vertical;
    const u8 held = input.held[0] & 0x0C;
    if (held) {
        unsigned speed =
            (unsigned(player.swing_speed) + ((held & right) ? 0x0010u : 0xFFF0u)) & 0xFFFF;
        if (speed < 0x0300) {
            speed = 0x0300;
        }
        if (speed >= 0x07FF) {
            speed = 0x07FF;
        }
        player.swing_speed = speed;
    }
    const unsigned angle = player.swing_angle + hi(player.swing_speed);
    player.swing_angle = u8(angle);
    if (angle > 0xFF) {
        sound_.request(sound::swing);
    }
    swing_place(); // 4309
    if (!(input.pressed[0] & 0x30)) {
        return;
    }
    player.swing_pole_slot = 0;
    o.next_state = 0x0B;
    o.angle = u8(player.swing_angle + 0x40);
    o.speed = u8(hi(player.swing_speed) << 5);
    velocity_from_angle(o); // 6394
}

// 8823 (28, state 0)
void Game::swing_pole_start(Object& o) {
    o.flags |= 0x80;
}

// 8828 (state 1): touched, Sonic swings around it (state 22) starting at
// the angle of where he hangs (tables 88C0 from above, 88CA from below, by
// his distance to the pole's left).
void Game::swing_pole_grab(Object& o) {
    touch_sonic(o); // 025A
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    Object& s = sonic();
    if (s.state == 0x22) {
        return;
    }
    s.next_state = 0x22;
    player.swing_speed = 0x0300;
    const unsigned centre_y = (unsigned(o.y) + 0xFFF4u) & 0xFFFF;
    player.swing_center_y = centre_y;
    const bool below = unsigned(s.y) >= centre_y;
    player.swing_center_x = o.x;
    const unsigned d = (unsigned(o.x) + 0x0028u - unsigned(s.x)) & 0xFFFF;
    u8 l = (d >> 8) ? 0 : u8(d);
    if (l >= 0x4F) {
        l = 0x4F;
    }
    const unsigned index = (l & 0xF8) >> 3;
    player.swing_angle = rom.byte(below ? rom::swing_angles_below : rom::swing_angles_above, index);
}

} // namespace sonic2_gg
