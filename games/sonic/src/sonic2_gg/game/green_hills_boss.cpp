// Green Hills Zone act 3: the boss (42) hops about and rolls at Sonic,
// bouncing off the screen edges with a jump that shakes the screen. Eight
// hits (only from above while it stands) destroy it. Also the bumper
// helpers AC29 / AC6C shared with later bosses.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// AC29: bumper-like; touching it reverses its X speed (and the rolling
// boss 42 hurts Sonic); while blinking (IX+1F) it cannot be touched.
void Game::boss_bumper(Object& o) {
    bumper_object(o); // 021B
    if (o.type == object_type::green_hills_boss && (o.touch_sides & 0x0F)) {
        player.hit_request = 0xFF;
    }
    if (o.touch_sides & 0x0F) {
        o.vx = (0x10000u - unsigned(o.vx)) & 0xFFFF;
    }
    boss_blink_or_clear(o);
}

// AC58 / ACAA: counting down a blink (touches ignored), or visible again.
void Game::boss_blink_or_clear(Object& o) {
    if (o.timer) {
        --o.timer;
        o.display |= display_flag::flicker;
        o.touch_sides = 0;
        return;
    }
    o.display &= u8(~(display_flag::flicker | display_flag::hidden));
    o.touch_sides = 0;
}

// AC6C: solid; a touch is a hit (64 frames of blinking).
void Game::boss_solid_hit(Object& o) {
    if (o.timer) {
        --o.timer;
        o.display |= display_flag::flicker;
        o.touch_sides = 0;
        return;
    }
    o.display &= u8(~(display_flag::flicker | display_flag::hidden));
    solid_object(o); // 0218
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.timer = 0x40;
    --o.hits;
    sound_.request(sound::boss_hit);
}

// 952F: eight hits.
void Game::ghz_boss_start(Object& o) {
    player.power_up_hold = 0xFF;
    o.hits = 0x08;
}

// 9539 (state 5): waits until within 208 pixels of the camera's left edge;
// then the camera is fixed.
void Game::ghz_boss_wait(Object& o) {
    lock_camera_left(); // 0302
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    if (unsigned(o.x) >= camera.x && ((dx >> 8) || (dx & 0xFF) >= 0xD0)) {
        return;
    }
    o.next_state = 1;
    script_camera(0x1230, 0x00A0); // 02F6
}

// 9560 (state 1): the next attack (2-4) from table 957A, by the clock's
// seconds and Sonic's X.
void Game::ghz_boss_choose(Object& o) {
    const unsigned index = (clock.seconds & 0x03) * 4u + (lo(sonic().x) & 0x03);
    o.next_state = rom.byte(rom::green_hills_boss_attacks, index);
}

// 9407: faces right before the attack.
void Game::ghz_boss_face_right(Object& o) {
    o.display &= u8(~display_flag::mirrored);
}

// 96B1: takes hits (0224: solid, 64 frames of blinking per hit); with
// none left it explodes (state B).
void Game::ghz_boss_guard(Object& o) {
    hit_object_solid(o); // 0224
    if (!o.hits) {
        o.next_state = 0x0B;
    }
}

// 9695 (wind-up frames): standing, pressed to the floor.
void Game::ghz_boss_stand(Object& o) {
    ghz_boss_guard(o);
    o.vx = 0;
    o.vy = 0x0100;
    move_object(o);
    object_floor(o); // 0260
}

// 964A: true (carry) when the boss should turn back: stopped in the
// middle of the screen, or within 56 pixels of an edge while moving toward
// it (X beyond the camera window 38-C7).
bool Game::ghz_boss_at_edge(const Object& o) const {
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    const bool left_side = unsigned(o.x) < camera.x || (!(dx >> 8) && (dx & 0xFF) < 0x38);
    const bool right_side = !left_side && ((dx >> 8) || (dx & 0xFF) >= 0xC8);
    if (left_side) {
        return !o.vx || (hi(o.vx) & 0x80); // 967C
    }
    if (right_side) {
        return !(hi(o.vx) & 0x80); // 968D
    }
    return !o.vx && !o.vy;
}

// 9603: at an edge: a jump back toward the middle (state 9).
void Game::ghz_boss_bounce_back(Object& o) {
    o.vy = 0xFC00;
    const unsigned middle = (camera.x + 0x0080u) & 0xFFFF;
    o.vx = middle < unsigned(o.x) ? 0xFFC0 : 0x0040;
    o.next_state = 0x09;
}

// 95CF: rolling speeds up by about 1/32 each frame until 4 pixels per
// frame: vx += ((vx >> 8) + 1 if positive) * 8.
void Game::ghz_boss_roll_on(Object& o) {
    u8 high = hi(o.vx);
    const u8 magnitude = (high & 0x80) ? u8(0x100 - high) : high;
    if (magnitude < 0x04) {
        const unsigned vx = o.vx;
        const bool negative = high & 0x80;
        const unsigned step =
            (((negative ? 0xFF00u : 0x0000u) | u8(high + (negative ? 0 : 1))) * 8u) & 0xFFFF;
        o.vx = (vx + step) & 0xFFFF;
    }
    move_object(o);
    if (ghz_boss_at_edge(o)) {
        ghz_boss_bounce_back(o);
    }
}

// 95C9 (states 6, 7): rolling along the floor.
void Game::ghz_boss_roll(Object& o) {
    object_floor(o);
    boss_bumper(o); // AC29
    ghz_boss_roll_on(o);
}

// 958A (state 8): a high hop (gravity 18) ending in a roll; in the air at
// an edge it stops moving sideways and jumps back once on the ground.
void Game::ghz_boss_hop(Object& o) {
    fall(o, 0x0018, 0x0400); // 020F
    object_floor(o);
    if (!(hi(o.vy) & 0x80) && (o.terrain & 0x02)) {
        o.vy = 0x0100;
        ghz_boss_roll_on(o);
        return;
    }
    boss_bumper(o);
    move_object(o);
    if (!ghz_boss_at_edge(o)) {
        return;
    }
    o.vx = 0;
    if (o.terrain & 0x02) {
        ghz_boss_bounce_back(o);
    }
}

// 973F: the jump back shakes the screen for 64 frames.
void Game::ghz_boss_shake() {
    camera.shake_ticks = 0x40;
}

// 962F (states 9, A): the jump back; landing it chooses again (1).
void Game::ghz_boss_jump_back(Object& o) {
    boss_bumper(o);
    move_object(o);
    fall(o, 0x0040, 0x0600);
    if (land_object(o, 0x02C0) == 0) { // 0212: landed and stopped (carry)
        o.next_state = 1;
    }
}

// 9521 (state B, after the explosions): the camera may scroll on.
void Game::ghz_boss_gone(Object& o) {
    camera.max_x = 0x1440;
    follow_sonic(); // 02F3
    o.type = object_type::waiting;
}

} // namespace sonic2_gg
