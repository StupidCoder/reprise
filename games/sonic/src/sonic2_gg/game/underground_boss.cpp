// Under Ground Zone act 3: the boss (4B) picks Sonic up and carries him to
// a ledge, then throws bouncing bombs (4C) at a target (4D). Six hits on
// the target bring the boss down; its fall opens the way to the capsule.

#include "sonic2_gg/game/game.hpp"
#include <algorithm>

namespace sonic2_gg {

namespace {
// Sonic's states set by the boss.
constexpr u8 carried_state = 0x23; // A81B: velocity 0, frames 11-15
constexpr u8 fall_state = 0x0E;
} // namespace

// 0233 (6385): the object in slot `n` (1-based, as 0230 returns it).
Object& Game::slot_object(u8 n) {
    return objects[u8(n - 1) & 0x1F];
}

// --- 4B: the boss ----------------------------------------------------------

// A47B (state 0): waits until Sonic is down at Y 210.
void Game::ugz_boss_wait(Object& o) {
    o.frame_ticks = 0xE0;
    if (sonic().y < 0x0210) {
        return;
    }
    o.frame_ticks = 0x01;
}

// A513: noted in D445; comes in from the right edge (80 pixels beyond the
// LCD). Widescreen: from beyond the presented view.
void Game::ugz_boss_enter(Object& o) {
    boss.slot = u8(slot_number(o)); // 0230
    o.flags |= 0x80;
    const unsigned beyond = unsigned(0xD0 + wide_right + 32); // the LCD ends at D0
    o.x = (camera.x + std::max(0x0120u, beyond)) & 0xFFFF;
}

// A52E (state 1): flies over Sonic at the camera's height + 50.
void Game::ugz_boss_approach(Object& o) {
    lock_camera_left();                     // 0302
    move_toward_x(o, sonic().x, 0, 0x06C0); // 0248
    move_object(o);
    o.y = (camera.y + 0x0050u) & 0xFFFF;
    if (o.vx) {
        return;
    }
    o.next_state = 2;
    sound_.request(sound::boss);
}

// A55F (state 2): drops onto Sonic.
void Game::ugz_boss_swoop(Object& o) {
    lock_camera_left();
    o.x = sonic().x;
    o.vy = 0x0E00;
    move_object(o);
    touch_sonic(o); // 025A
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.next_state = 0x0B;
}

// A585 (state 0B): follows Sonic 32 pixels above him until low enough to
// grab him (Y 360).
void Game::ugz_boss_grab(Object& o) {
    o.x = sonic().x;
    o.y = (sonic().y + 0xFFE0u) & 0xFFFF;
    if (o.y < 0x0360) {
        return;
    }
    o.next_state = 0x0A;
    sonic().next_state = carried_state;
    o.vy = 0;
}

// A72B: Sonic hangs at the boss's position + (dx, dy).
void Game::ugz_boss_carry(Object& o, unsigned dx, unsigned dy) {
    sonic().x = (unsigned(o.x) + dx) & 0xFFFF;
    sonic().y = (unsigned(o.y) + dy) & 0xFFFF;
}

// A5B5 (state 0A): rises with Sonic to Y 60. Holding exactly button 2 and
// down at 9 minutes lets Sonic drop off.
void Game::ugz_boss_lift(Object& o) {
    lock_camera_left();
    o.vy = 0xFE00;
    o.vx = 0;
    move_object(o);
    ugz_boss_carry(o, 0, 0x0020);
    if (o.y > 0x0060) {
        return;
    }
    o.next_state = 3;
    if (input.held[0] != 0x22 || clock.minutes != 0x09) {
        return;
    }
    sonic().next_state = fall_state;
    o.next_state = 5;
}

// A5FD (state 3): carries him right to X D50.
void Game::ugz_boss_carry_right(Object& o) {
    lock_camera_left();
    o.vx = 0x0200;
    o.vy = 0;
    move_object(o);
    ugz_boss_carry(o, 0, 0x0020);
    if (o.x <= 0x0D50) {
        return;
    }
    o.next_state = 4;
    o.vx = 0;
}

// A639 (state 4): down to Y A8, lets go, and the camera is fixed.
void Game::ugz_boss_set_down(Object& o) {
    lock_camera_left();
    o.vy = 0x0200;
    move_object(o);
    ugz_boss_carry(o, 0, 0x0020);
    if (o.y <= 0x00A8) {
        return;
    }
    o.next_state = 5;
    o.vy = 0;
    sonic().next_state = fall_state;
    script_camera(0x0CEA, 0x0090); // 02F6
}

// A67A (state 5): to its throwing place (CA0, 20); then state 6 throws a
// bomb every 240 frames.
void Game::ugz_boss_to_post(Object& o) {
    move_toward_x(o, 0x0CA0, 0, 0x0200); // 0248
    move_toward_y(o, 0x0020, 0, 0x0200); // 024B
    move_object(o);
    if (o.vx || o.vy) {
        return;
    }
    o.next_state = 6;
}

// A6AA (state 7): beaten; falls and rolls right into the target's place,
// which then breaks (4D state 3).
void Game::ugz_boss_fall(Object& o) {
    o.flags &= u8(~0x80);
    touch_sonic(o);
    object_floor(o); // 0260
    o.vy = 0x0200;
    o.vx = (o.terrain & 0x02) ? 0x0280 : 0;
    move_object(o);
    if (o.x < 0x0DB0) {
        return;
    }
    o.next_state = 8;
    slot_object(boss.target_slot).next_state = 3;
}

// A6F1 (state 9): wobbling flight away: IX+1E steps by +-8 (IX+1F bit 0
// turns at zero); vy = 1E - 180, vx = (1E + 80) - 100.
void Game::ugz_boss_fly_off(Object& o) {
    const u8 step = u8(((o.timer & 0x01) << 4) - 0x08);
    const u8 phase = u8(o.counter + step);
    if (!phase) {
        --o.timer;
    }
    o.counter = phase;
    o.vy = (phase + 0xFE80u) & 0xFFFF;
    o.vx = (u8(phase + 0x80) + 0xFF00u) & 0xFFFF;
    move_object(o);
}

// --- 4C: a bomb --------------------------------------------------------------

// A37C: thrown right, the farther the higher the clock's seconds; on some
// seconds also upwards. D447 keeps the seconds (its bit 1 also picks the
// gravity below).
void Game::ugz_bomb_throw(Object& o) {
    player.power_up_hold = 0xFF;
    o.vx = ((clock.seconds & 0x1F) * 8u + 0x00C0u) & 0xFFFF;
    boss.throw_seconds = clock.seconds;
    if (clock.seconds & 0x02) {
        return;
    }
    o.vy = 0xFA10;
}

// A3A9: bounces (losing 80 of its speed per bounce); hurts Sonic. On
// reaching the target (X >= DA0, Y >= 120) it explodes; a target that is
// not already hit or breaking blinks for 64 frames and counts the hit;
// the sixth hit sends the boss down (state 7).
void Game::ugz_bomb_fly(Object& o) {
    object_floor(o);  // 0260
    bumper_object(o); // 021B
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
        explode(o); // 025D
        return;
    }
    move_object(o);
    if (boss.throw_seconds & 0x02) {
        fall(o, 0x0060, 0x0600); // 020F
    } else {
        fall(o, 0x005E, 0x0800);
    }
    if (o.terrain & 0x02) {
        if (!(o.display & display_flag::offscreen)) {
            sound_.request(sound::bomb_bounce);
        }
        const unsigned vy = o.vy;
        if (!(vy & 0x8000)) {
            o.vy = vy <= 0x0080 ? 0 : (0x0080u - vy) & 0xFFFF;
        }
    }
    if (o.x < 0x0DA0 || o.y < 0x0120) {
        return;
    }
    Object& target = slot_object(boss.target_slot); // 0233
    if (target.state == 2) {
        explode(o);
        return;
    }
    if (target.next_state != 3 && o.state != 3) {
        target.next_state = 2;
        target.counter = 0x40;
        if (!--target.hits) {
            slot_object(boss.slot).next_state = 7;
        }
    }
    explode(o);
}

// --- 4D: the target ---------------------------------------------------------

// A7B7: noted in D446, with six hits to take (IX+24).
void Game::ugz_target_start(Object& o) {
    boss.target_slot = u8(slot_number(o));
    o.hits = 0x06;
}

// A7C5 / A7CC (state 1): frames 1 and 2 alternate, faster with fewer hits
// left.
void Game::ugz_target_frame(Object& o, u8 frame) {
    o.frame = frame;
    o.frame_ticks = u8((o.hits + 1) * 2);
}

// A7D9 (state 2): hit: blinks for IX+1E frames.
void Game::ugz_target_hit(Object& o) {
    if (o.next_state == 3) {
        return;
    }
    bumper_object(o); // 021B
    if (--o.counter) {
        return;
    }
    o.next_state = 1;
}

// A7EB (state 3, after the debris): the camera may scroll on to the capsule.
void Game::ugz_target_gone(Object& o) {
    camera.max_x = 0x0F40;
    follow_sonic(); // 02F3
    o.type = object_type::waiting;
}

// --- 06: the clock sliding away during the boss -----------------------------

// AE45
void Game::hud_slide_start(Object& o) {
    o.display |= display_flag::keep_on_screen;
    o.next_state = 1;
}

// AE4E (state 1): on every other frame the ring counter's and the clock's
// sprites (DB38-DB3F) move up and the lives' (DB34-DB37) down, until the
// clock is near the top; from 9:50 on they come back (state 2).
void Game::hud_slide_out(Object& o) {
    const unsigned time = clock.seconds | (clock.minutes << 8);
    if (time < 0x0950) {
        if (!(o.frame_ticks & 0x01)) {
            return;
        }
        for (unsigned i = hud_rings; i < hud_end; ++i) {
            --sprites.table[i].y;
        }
        for (unsigned i = hud_first; i < hud_rings; ++i) {
            ++sprites.table[i].y;
        }
        if (sprites.table[hud_end - 1].y >= 0x08) {
            return;
        }
    }
    o.next_state = 2;
}

// AE7C (state 2): from 9:50 the sprites move back; then the object goes.
void Game::hud_slide_back(Object& o) {
    const unsigned time = clock.seconds | (clock.minutes << 8);
    if (time < 0x0950) {
        return;
    }
    if (!(o.frame_ticks & 0x01)) {
        return;
    }
    for (unsigned i = hud_rings; i < hud_end; ++i) {
        ++sprites.table[i].y;
    }
    for (unsigned i = hud_first; i < hud_rings; ++i) {
        --sprites.table[i].y;
    }
    if (sprites.table[hud_end - 1].y < 0x28) {
        return;
    }
    o.type = object_type::removed;
}

} // namespace sonic2_gg
