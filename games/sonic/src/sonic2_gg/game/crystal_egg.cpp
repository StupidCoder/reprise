// Crystal Egg Zone objects (bank 1C): a swing pole riding a moving
// platform (2A), a swinging spiked ball (2B), a block that drops on Sonic
// passing under it (2C) and a crawling enemy (37).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// --- 2A: a moving swing pole --------------------------------------------------

// 8B7A
void Game::moving_pole_start(Object& o) {
    o.flags |= 0x80;
    o.speed = 0x40;
}

// 8B83: circles (angle + 4 per frame) while drifting 1 pixel per frame
// between its origin and parameter pixels right of it (turning only at a
// circle's half). Sonic grabbing it swings as on a pole (state 22) around
// it with the short radius; D3CD notes which pole.
void Game::moving_pole_update(Object& o) {
    o.angle = u8(o.angle + 0x04);
    velocity_from_angle(o); // 0236
    o.vx = (o.counter & 0x01) ? 0xFF00 : 0x0100;
    move_object(o);
    if (!(o.angle & 0x7F)) {
        if (unsigned(o.x) < unsigned(o.origin_x)) {
            o.counter &= u8(~0x01);
        } else {
            const unsigned d = (unsigned(o.x) - unsigned(o.origin_x)) & 0xFFFF;
            if ((d >> 8) || (d & 0xFF) >= o.param) {
                o.counter |= 0x01;
            }
        }
    }
    const u8 slot = u8(slot_number(o));
    if (player.swing_pole_slot) {
        if (player.swing_pole_slot != slot) {
            return;
        }
        player.swing_center_x = unsigned(o.x) | 0x8000;
        player.swing_center_y = (unsigned(o.y) + 0xFFFAu) & 0xFFFF;
    }
    touch_sonic(o); // 025A
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    Object& s = sonic();
    if (s.state == 0x22) {
        return;
    }
    player.swing_pole_slot = slot;
    s.next_state = 0x22;
    player.swing_speed = 0x0300;
    const unsigned centre_y = (unsigned(o.y) + 0xFFFAu) & 0xFFFF;
    player.swing_center_y = centre_y;
    const bool below = unsigned(s.y) >= centre_y;
    player.swing_center_x = unsigned(o.x) | 0x8000;
    const unsigned d = ((unsigned(o.x) | 0x8000) + 0x0028u - unsigned(s.x)) & 0xFFFF;
    u8 l = (d >> 8) ? 0 : u8(d);
    if (l >= 0x4F) {
        l = 0x4F;
    }
    player.swing_angle = rom.byte(
        below ? rom::moving_pole_angles_below : rom::moving_pole_angles_above, (l & 0xF8) >> 3);
}

// --- 2B: a swinging spiked ball -------------------------------------------------
// The angle (IX+0A, fraction in IX+3F) swings from 80 one way and back;
// the speed (IX+1E, from 8D95) is greatest at the bottom.

// 8CC9
void Game::pendulum_start(Object& o) {
    o.next_state = 1;
    o.angle = 0x80;
    o.param = 0x00;
    o.timer = 0;
}

// 8D95: the swing speed for the angle: 40 at the ends rising toward the
// bottom (C0 in the first direction, 40 in the second).
u8 Game::pendulum_speed(const Object& o, bool back) const {
    u8 a = o.angle;
    if (!back) {
        a = a < 0xC0 ? u8(a - 0x80) : u8(0x100 - a);
    } else if (a >= 0x40) {
        a = u8(u8(0x100 - a) - 0x80);
    }
    return u8(a + 0x40);
}

// 8CDB: three steps of the speed per frame; at the end of a swing it turns
// (the way back also puts it back at its origin). It hurts Sonic.
void Game::pendulum_swing(Object& o) {
    const bool back = o.timer & 0x01;
    o.counter = pendulum_speed(o, back);
    const unsigned step = o.counter;
    unsigned angle = (o.angle << 8) | o.param;
    bool turned = false;
    for (int i = 0; i < 3 && !turned; ++i) {
        if (!back) {
            angle += step;
            turned = angle > 0xFFFF;
        } else {
            turned = angle < step;
            angle -= step;
        }
        angle &= 0xFFFF;
        o.angle = u8(angle >> 8);
        o.param = u8(angle);
    }
    if (turned) {
        ++o.timer;
        o.angle = 0x80;
        o.param = 0x00;
        if (back) {
            o.x = o.origin_x;
            o.x_sub = 0;
            o.y = o.origin_y;
            o.y_sub = 0;
        }
    }
    // 8D78
    o.speed = u8(std::int8_t(o.counter) >> 1);
    velocity_from_angle(o); // 0236
    touch_sonic(o);
    move_object(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
}

// --- 2C: a block that drops on Sonic ------------------------------------------

// 8DF8 (state 1): Sonic below it within a horizontal range of half his
// distance below (doubled above 1.5, again above 3.5 pixels per frame of
// speed): it shakes (2) for 16 frames.
void Game::drop_block2_watch(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    const Object& s = sonic();
    if (unsigned(s.y) < unsigned(o.y)) {
        return;
    }
    const unsigned dy = unsigned(s.y) - unsigned(o.y);
    if (dy >> 8) {
        return;
    }
    u8 range = u8((dy & 0xFF) >> 1);
    const unsigned speed =
        (hi(s.vx) & 0x80) ? (0x10000u - unsigned(s.vx)) & 0xFFFF : unsigned(s.vx);
    if (speed > 0x0180) {
        range = u8(range * 2);
        if (speed > 0x0380) {
            range = range >= 0x80 ? 0xFF : u8(range * 2);
        }
    }
    const unsigned sx = s.x, x = o.x;
    const unsigned dx = sx >= x ? sx - x : x - sx;
    if ((dx >> 8) || (dx & 0xFF) >= range) {
        return;
    }
    o.next_state = 2;
    o.timer = 0x10;
}

// 8E5D (state 2)
void Game::drop_block2_shake(Object& o) {
    if (!--o.timer) {
        o.next_state = 3;
    }
}

// 8E66 (state 3): falls (gravity 50) until it hits Sonic or the ground.
void Game::drop_block2_fall(Object& o) {
    touch_sonic(o);
    if (!(o.touch_sides & 0x0F)) {
        fall(o, 0x0050, 0x0600);
        move_object(o);
        object_floor(o); // 0260
        if (!(o.terrain & 0x02)) {
            return;
        }
    }
    o.next_state = 4;
}

// 8E89 (state 4): breaks into pieces at its cell.
void Game::drop_block2_break(Object& o) {
    probe.debris_x = unsigned(o.x) & 0xFFE0;
    probe.debris_y = unsigned(o.y) & 0xFFE0;
    spawn_debris(); // 02E4
    o.param = 0x82;
    o.placement = 0;
    explode(o); // 025D
}

// --- 37: a crawler ---------------------------------------------------------------

// B6C5
void Game::crawler_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 1;
    o.vx = 0x0080;
    o.counter = 0;
    o.vy = 0x0020;
}

// B6E5: crawls, bobbing up and down every 128 frames, within 192 pixels
// of its origin, turning at walls.
void Game::crawler_move(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (++o.counter & 0x80) {
        o.vy = (0x10000u - unsigned(o.vy)) & 0xFFFF;
        o.counter = 0;
    }
    face_motion(o); // 023C
    touch_sonic(o);
    move_object(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o); // 020C
        return;
    }
    patrol_turn(o, 0xC0); // BCB5
    object_wall(o);       // 0239
    if (o.terrain & 0x0C) {
        o.vx = (0x10000u - unsigned(o.vx)) & 0xFFFF;
    }
}

} // namespace sonic2_gg
