// Aqua Lake Zone act 3: the boss (4A) jumps about, throws bombs (4F) and
// charges along the floor; a bomb coming down on it while it is not
// jumping hits it (six hits). Sonic standing on it can be carried and
// thrown.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// A1EF
void Game::alz_boss_start(Object& o) {
    player.power_up_hold = 0xFF;
    boss.slot = u8(slot_number(o));
    o.hits = 0x06;
}

// A202 (state 1): waits until within 192 pixels of the camera's left edge;
// then the camera is fixed.
void Game::alz_boss_wait(Object& o) {
    lock_camera_left(); // 0302
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    if (unsigned(o.x) >= camera.x && ((dx >> 8) || (dx & 0xFF) >= 0xC0)) {
        return;
    }
    o.next_state = 2;
    script_camera(0x0D60, 0x0090); // 02F6
}

// A1AD: counts its throws (IX+0A).
void Game::alz_boss_count(Object& o) {
    ++o.angle;
}

// A1B1: after the first throw a high jump (4), otherwise a low one (3).
void Game::alz_boss_choose_jump(Object& o) {
    o.next_state = u8(o.angle - 1) ? 0x03 : 0x04;
}

// A229: solid; Sonic landing on top hits it (state 5), from the sides he
// bounces off.
void Game::alz_boss_guard(Object& o) {
    solid_object(o); // 0218
    if (o.touch_sides & 0x01) {
        o.next_state = 5;
        return;
    }
    bounce_sonic_off(o); // 021E
}

// A23C (states 3-4): jumping (gravity 80, at most 2 pixels); stops on
// landing.
void Game::alz_boss_jump(Object& o) {
    bumper_object(o); // 021B
    move_object(o);
    fall(o, 0x0080, 0x0200);
    if (!probe_type(o, 0, 0)) { // 0263
        return;
    }
    if (hi(o.vy) & 0x80) {
        return;
    }
    o.vx = 0;
    o.vy = 0;
}

// A1BF: walks (state 6) for 128 frames.
void Game::alz_boss_to_walk(Object& o) {
    o.display &= u8(~(display_flag::flicker | display_flag::hidden));
    o.next_state = 6;
    o.counter = 0x80;
}

// A1D0 (state 5): a hit; blinking; the sixth destroys it (state 9).
void Game::alz_boss_hit(Object& o) {
    o.display |= display_flag::flicker;
    if (--o.hits) {
        return;
    }
    o.display &= u8(~display_flag::flicker);
    o.next_state = 0x09;
}

// A26A (state 6): walks toward Sonic at half a pixel per frame; Sonic
// jumping above it makes it rise to catch him (7); close to Sonic on the
// ground it leaps (A); after 128 frames it throws again (2).
void Game::alz_boss_walk(Object& o) {
    bumper_object(o);
    move_object(o);
    face_x(o, sonic().x); // 023F
    if (!--o.counter) {
        o.next_state = 2; // A2EB
        return;
    }
    unsigned hl = sonic().x, de = o.x;
    o.vx = 0;
    // A2C9: with Sonic jumping or launched, it rises to him when he is more
    // than 40 pixels above. The routine leaves HL and DE changed, and the
    // distance below is then measured from those.
    const u8 state = sonic().state;
    if (state == 0x0A || state == 0x1B) {
        hl = (unsigned(sonic().y) + 0x0028u) & 0xFFFF;
        de = o.y;
        const bool below = hl < de;
        hl = (hl - de) & 0xFFFF;
        if (below) {
            o.next_state = 0x07;
        }
    }
    const bool left = o.display & display_flag::mirrored;
    const unsigned d = left ? (de - hl) & 0xFFFF : (hl - de) & 0xFFFF;
    if (d >> 8) {
        return;
    }
    if ((d & 0xFF) < 0x38 && (sonic().terrain & 0x02)) {
        o.next_state = 0x0A; // A2BE
    }
    o.vx = left ? 0xFF80 : 0x0080;
}

// A2F0 (state 7): under Sonic while he is in the air; caught (touching)
// he is carried (8).
void Game::alz_boss_rise(Object& o) {
    if (sonic().state != 0x0A) {
        o.next_state = 2;
        return;
    }
    move_toward_x(o, sonic().x & 0xFFF8, 0xFFF8, 0x0400); // 0245
    move_object(o);
    solid_object(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.next_state = 0x08;
    o.counter = 0x08;
}

// A313 (state 8): carrying Sonic, facing the screen's middle; after eight
// frames of contact he is thrown away (launched).
void Game::alz_boss_carry(Object& o) {
    face_x(o, (camera.x + 0x0080u) & 0xFFFF); // 0242
    move_toward_x(o, sonic().x & 0xFFF8, 0xFFF8, 0x0400);
    move_object(o);
    solid_object(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (--o.counter) {
        return;
    }
    Object& s = sonic();
    s.next_state = 0x1B;
    s.vy = 0xF900;
    s.vx = (o.display & display_flag::mirrored) ? 0xFB00 : 0x0500;
    alz_boss_to_walk(o);
}

// A1E1 (state 9, after the explosions): the camera may scroll on.
void Game::alz_boss_gone(Object& o) {
    camera.max_x = 0x0F40;
    follow_sonic(); // 02F3
    o.type = object_type::waiting;
}

// A177 (state A): a leap over Sonic (velocity from the script).
void Game::alz_boss_leap(Object& o) {
    solid_object(o);
    o.touch_sides = 0;
    move_object(o);
    fall(o, 0x0040, 0x0600);
}

// A18B: turns around in the air, shifting 24 pixels.
void Game::alz_boss_turn(Object& o) {
    o.display ^= display_flag::mirrored;
    const unsigned dx = (o.display & display_flag::mirrored) ? 0x0018 : 0xFFE8;
    o.x = (unsigned(o.x) + dx) & 0xFFFF;
}

// A169: back on the ground it throws again (2).
void Game::alz_boss_land(Object& o) {
    object_floor(o); // 0260
    if (o.terrain & 0x02) {
        o.next_state = 2;
    }
}

// --- 4F: a bomb --------------------------------------------------------------

// 9F11: noted in D446; thrown up and forward.
void Game::alz_bomb_start(Object& o) {
    boss.target_slot = u8(slot_number(o));
    o.vx = (o.display & display_flag::mirrored) ? 0xFFC0 : 0x0040;
    o.vy = 0xFC80;
}

// 9F36 (state 1): explodes on Sonic. Coming down onto the boss: if it is
// jumping (state 3) the bomb is kicked away (state 2), far and low after a
// first throw or high otherwise; else it hits the boss (state 5).
void Game::alz_bomb_fly(Object& o) {
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        explode(o);
        return;
    }
    move_object(o);
    fall(o, 0x0020, 0x0600);
    if (hi(o.vy) & 0x80) {
        return;
    }
    Object& the_boss = slot_object(boss.slot);
    if (((unsigned(the_boss.y) + 0xFFE0u) & 0xFFFF) >= unsigned(o.y)) {
        return;
    }
    if (the_boss.state != 0x03) {
        the_boss.next_state = 0x05;
        o.type = object_type::removed;
        return;
    }
    const bool left = o.display & display_flag::mirrored;
    if (the_boss.angle & 0x01) {
        o.vy = 0xFA80;
        o.vx = left ? 0xFF00 : 0x0100;
    } else {
        o.vy = 0xFE80;
        o.vx = left ? 0xFC80 : 0x0380;
    }
    o.next_state = 2;
}

// 9FC6 (state 2): bounces along; hurts Sonic; gone off screen.
void Game::alz_bomb_bounce(Object& o) {
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
        return;
    }
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
        explode(o);
        return;
    }
    move_object(o);
    fall(o, 0x0050, 0x0600);
    if (land_object(o, 0x0080) < 0) { // 0212: in the air
        return;
    }
    if (!(o.display & display_flag::offscreen)) {
        sound_.request(sound::bomb_bounce);
    }
}

} // namespace sonic2_gg
