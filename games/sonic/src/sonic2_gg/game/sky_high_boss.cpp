// Sky High Zone act 3: the boss (48) sends waves of helpers (36 hopping
// minions, 46 bouncing balls, 47 ball launchers) and waits until all are
// gone (D3BC lists them); then the floor gives way (Sonic states 26/27)
// and the boss itself attacks from the air, dropping fliers (49).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
constexpr u8 sonic_curled = 0x02;
} // namespace

// 9D6A: notes the object command 6 has just placed (IY) in a free entry
// of D3BC.
void Game::boss_add_part() {
    for (u8& part : boss.parts) {
        if (!part) {
            part = spawned_slot_;
            return;
        }
    }
}

// 9D81 / B69A: removes the object from D3BC.
void Game::boss_remove_part(Object& o) {
    const u8 slot = u8(slot_number(o));
    for (u8& part : boss.parts) {
        if (part == slot) {
            part = 0;
            return;
        }
    }
}

// A helper destroyed by a curled Sonic: removed from the list, 100
// points, an explosion.
bool Game::boss_part_hit(Object& o) {
    if (!(o.touch_sides & 0x0F) || !(sonic().flags & sonic_curled)) {
        return false;
    }
    boss_remove_part(o);
    return true;
}

// --- 48: the boss ----------------------------------------------------------

// 9D98: six hits (eight for the unused variant with a parameter, state D).
void Game::shz_boss_start(Object& o) {
    player.power_up_hold = 0xFF;
    boss.slot = 0;
    if (!o.param) {
        o.next_state = 1;
        o.hits = 0x06;
        return;
    }
    o.next_state = 0x0D;
    o.hits = 0x08;
}

// 9DB9 (state 1): when Sonic is within 96 pixels the camera is fixed.
void Game::shz_boss_wait(Object& o) {
    if (((sonic().x + 0x0060u) & 0xFFFF) < unsigned(o.x)) {
        return;
    }
    script_camera(0x0814, 0x003E); // 02F6
    o.next_state = 2;
}

// 9DD8 (states 3, 5, 8): the next state once every helper is gone.
void Game::shz_boss_wait_parts(Object& o) {
    u8 any = 0;
    for (u8 part : boss.parts) {
        any |= part;
    }
    if (any) {
        return;
    }
    o.next_state = u8(o.state + 1);
}

// 9DF0 (state 6): the camera follows Sonic again (no further right than
// it is now) and Sonic drops through the floor (state 26).
void Game::shz_boss_open_floor(Object& o) {
    follow_sonic(); // 02F3
    camera.max_x = camera.min_x;
    o.next_state = 7;
    sonic().next_state = 0x26;
}

// 9E03 (state 9): placed at (840, 1F0) below.
void Game::shz_boss_place(Object& o) {
    o.x = 0x0840;
    o.y = 0x01F0;
}

// 9E32: keeps 64 pixels left of Sonic, bobbing up and down every 64
// frames.
void Game::shz_boss_hover(Object& o) {
    move_toward_x(o, (sonic().x + 0xFFC0u) & 0xFFFF, 0, 0x00C0); // 0248
    ++o.counter;
    o.vy = (o.counter & 0x40) ? 0x0070 : 0xFF90;
}

// 9E16 (state A): hit by Sonic it blinks for 64 frames (state B).
void Game::shz_boss_fly(Object& o) {
    move_object(o);
    shz_boss_hover(o);
    bumper_object(o); // 021B
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.timer = 0x40;
    o.display |= display_flag::flicker;
    o.next_state = 0x0B;
}

// 9E59 (state B): blinking (untouchable); the last hit destroys it (C).
void Game::shz_boss_blink(Object& o) {
    move_object(o);
    shz_boss_hover(o);
    bumper_object(o);
    o.touch_sides = 0;
    if (--o.timer) {
        return;
    }
    o.display &= u8(~(display_flag::flicker | display_flag::hidden));
    o.next_state = --o.hits ? 0x0A : 0x0C;
}

// 9D62 / 9E81 (state C, after the explosions): the helpers stop, and the
// camera may scroll to the capsule.
void Game::shz_boss_gone(Object& o) {
    boss.slot = 0xFF;
    camera.max_x = 0x0B00;
    enable_map(); // 0278
    o.type = object_type::waiting;
}

// --- 46: a bouncing ball ---------------------------------------------------

// 9A5E / 9A65 / 9A6C: frames 1-3, or 4-6 moving left.
void Game::ball_frame(Object& o, u8 right_frame) {
    o.frame = (hi(o.vx) & 0x80) ? u8(right_frame + 3) : right_frame;
    o.frame_ticks = 0x04;
}

// 9A7F (state 1): thrown up (vy F400 from the script) until it falls.
void Game::ball_rise(Object& o) {
    move_object(o);
    fall(o, 0x0080, 0x0600);
    if (hi(o.vy) & 0x80) {
        return;
    }
    o.next_state = 2;
}

// 9A95 (state 2): bounces toward Sonic (each landing a hop of at least
// 3 pixels per frame); gone when the boss is.
void Game::ball_bounce(Object& o) {
    if (!boss.slot) {
        touch_sonic(o);
        move_object(o);
        fall(o, 0x0020, 0x0600);
        if (land_object(o, 0x0200) >= 0) { // 0212: on the ground
            o.vx = sonic().x >= o.x ? 0x0080 : 0xFF80;
            if (unsigned(o.vy) < 0xFD00) {
                o.vy = 0xFD00;
            }
        }
        if (!boss_part_hit(o)) {
            return;
        }
    }
    add_score(rom::points_100); // 0317
    explode(o);                 // 025D
}

// --- 47: a ball launcher ---------------------------------------------------

// 9B1B: with parameter 0 it launches balls (state 1), otherwise it drops
// (state 2).
void Game::launcher_start(Object& o) {
    if (o.param) {
        o.next_state = 2;
        return;
    }
    o.next_state = 1;
    o.flags |= 0x80;
}

// 9B2F (state 1)
void Game::launcher_wait(Object& o) {
    touch_sonic(o);
    if (!boss_part_hit(o)) {
        return;
    }
    add_score(rom::points_100);
    explode(o);
}

// 9B47 (state 2)
void Game::launcher_drop(Object& o) {
    touch_sonic(o);
    move_object(o);
    fall(o, 0x0010, 0x0600);
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
    }
}

// --- 49: a flier dropped by the boss ---------------------------------------

// 9EA6: flies right at 1.5 pixels per frame at Sonic's height - 24; hurts
// him; gone 256 pixels right of the camera.
void Game::shz_flier(Object& o) {
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
    move_object(o);
    move_toward_y(o, sonic().y, 0xFFE8, 0x00C0); // 024B
    o.vx = 0x0180;
    // Widescreen margins (zero on the LCD) keep it until it leaves the view.
    if (((camera.x + 0x0100u + unsigned(wide_right)) & 0xFFFF) >= unsigned(o.x)) {
        return;
    }
    o.type = object_type::removed;
}

// --- 36: a hopping minion --------------------------------------------------
// Thrown up (vy F400) it lands and hops at Sonic: short hops (states 3-7)
// or, with parameter bit 0, a walk then a high jump (8-A).

// B684: destroyed by a curled Sonic.
void Game::minion_check_hit(Object& o) {
    if (!boss_part_hit(o)) {
        return;
    }
    o.param = 0;
    explode(o);
}

// B4EB (state 0)
void Game::minion_start(Object& o) {
    o.next_state = 1;
}

// B4F0 (state 1): rising.
void Game::minion_rise(Object& o) {
    move_object(o);
    fall(o, 0x0080, 0x0600);
    if (hi(o.vy) & 0x80) {
        return;
    }
    o.next_state = 2;
    o.vy = 0;
    minion_check_hit(o);
}

// B510 (state 2): falling onto ground of profile 81.
void Game::minion_land(Object& o) {
    o.vy = (unsigned(o.vy) + 0x0020) & 0xFFFF;
    touch_sonic(o);
    move_object(o);
    object_floor(o); // 0260
    if (probe_type(o, 0, 0) == 0x81) {
        o.vy = 0;
        o.counter = 0x20;
        o.next_state = (o.param & 0x01) ? 8 : 3;
    }
    minion_check_hit(o);
}

// B558 / B5EF (states 3 and 7): waiting 32 frames.
void Game::minion_wait(Object& o) {
    touch_sonic(o);
    if (!--o.counter) {
        o.next_state = 4;
    }
    minion_check_hit(o);
}

// B568 (state 4): a hop toward Sonic.
void Game::minion_hop(Object& o) {
    o.vy = 0xFB00;
    if (sonic().x >= o.x) {
        o.vx = 0x0100;
        o.next_state = 5;
    } else {
        o.vx = 0xFF00;
        o.next_state = 6;
    }
    minion_check_hit(o);
}

// B59F (states 5 and 6): in the air until it lands (8 pixels lower).
void Game::minion_hop_air(Object& o) {
    o.vy = (unsigned(o.vy) + 0x0040) & 0xFFFF;
    touch_sonic(o);
    move_object(o);
    object_floor(o);
    if (probe_type(o, 0, 0x0008) == 0x81) {
        o.vx = 0;
        o.vy = 0;
        o.counter = 0x20;
        o.next_state = o.state == 5 ? 3 : 7;
    }
    minion_check_hit(o);
}

// B5FF (states 8 and 9): walking for 64 frames, then a high jump (A).
void Game::minion_walk(Object& o) {
    move_object(o);
    touch_sonic(o);
    if (!--o.counter) {
        o.next_state = 0x0A;
        o.vy = 0xFB00;
    }
    minion_check_hit(o);
}

// B61B (state A): straight up and down, then walks toward Sonic.
void Game::minion_jump(Object& o) {
    o.vx = 0;
    o.vy = (unsigned(o.vy) + 0x0040) & 0xFFFF;
    touch_sonic(o);
    move_object(o);
    object_floor(o);
    if (probe_type(o, 0, 0x0008) == 0x81) {
        o.vy = 0;
        o.counter = 0x40;
        if (sonic().x >= o.x) {
            o.vx = 0x0080;
            o.next_state = 8;
        } else {
            o.vx = 0xFF80;
            o.next_state = 9;
        }
    }
    minion_check_hit(o);
}

// --- Sonic falling through the floor ---------------------------------------

// AA56: falling straight down (gravity 20, at most 5 pixels).
void Game::sonic_drop_straight() {
    Object& o = sonic();
    move_object(o); // 0257
    fall(o, 0x0020, 0x0500);
    o.vx = 0;
}

// AA26 / AA3E (states 26 and 27): down to Y 190 (240), then a normal fall
// with the camera stopped.
void Game::sonic_drop_through(unsigned bottom) {
    sonic_drop_straight();
    if (sonic().y < bottom) {
        return;
    }
    sonic().next_state = 0x0E;
    disable_map(); // 0275
}

} // namespace sonic2_gg
