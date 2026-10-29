// Gimmick Mt. Zone objects (bank 1C): a jumping enemy (38), a shaking
// enemy that bursts into two fireballs (39, 3B), and a hopping enemy (3A).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
unsigned negated(unsigned v) {
    return (0x10000u - v) & 0xFFFF; // BC25
}
} // namespace

// BCB5: patrols 96 pixels (B) either side of its origin, turning at the
// ends; returns true there.
bool Game::patrol_turn(Object& o, unsigned range) {
    const unsigned x = o.x, origin = o.origin_x;
    const unsigned d = x >= origin ? x - origin : origin - x;
    if ((d >> 8) || (d & 0xFF) < range) {
        return false;
    }
    o.vx = negated(o.vx);
    return true;
}

// --- 38: a jumping enemy -----------------------------------------------------

// B768 (state 0)
void Game::jumper_start(Object& o) {
    if (!(o.display & display_flag::offscreen)) {
        o.next_state = 1;
    }
}

// B773 (state 1): Sonic within 96 pixels both ways: a jump (2). (The
// direction test reads BC2D's result rather than its side flag, so it
// always jumps left.)
void Game::jumper_watch(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    face_x(o, sonic().x); // 023F
    if (!sonic_near_y(o, 0x60) || !sonic_near_x(o, 0x60)) {
        return;
    }
    o.next_state = 2;
    o.vy = 0xFE80;
    o.vx = 0xFF80;
}

// B7B3 (state 2): in the air (gravity 14, moving twice per frame); on
// ground of profile 81 it lands (3), heading back toward its origin.
void Game::jumper_jump(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    face_x(o, sonic().x);
    touch_sonic(o);
    move_object(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o); // 020C
        return;
    }
    o.vy = (unsigned(o.vy) + 0x0014) & 0xFFFF;
    move_object(o);
    if (probe_type(o, 0, 0) != 0x81) { // 0263
        return;
    }
    o.next_state = 3;
    o.vx = unsigned(o.x) < unsigned(o.origin_x) ? 0x0080 : 0xFF80;
}

// B812 (state 3, a script entry objscan does not list: 0 ticks = 256):
// walks (4); the speed test compares with 0, so always left.
void Game::jumper_land(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 4;
    o.vy = 0;
    o.vx = 0xFF80;
}

// B846 (state 4): walks within 128 pixels of its origin, turning at edges;
// Sonic in the air within 64 pixels makes it jump straight up (2).
void Game::jumper_walk(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.vy = 0x00C0;
    face_x(o, sonic().x);
    touch_sonic(o);
    move_object(o);
    object_floor(o); // 0260
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
        return;
    }
    if (enemy_must_turn(o) & 0x01) { // BD38
        o.vx = negated(o.vx);        // BD28
    }
    const unsigned x = o.x, origin = o.origin_x;
    if (!(hi(o.vx) & 0x80)) {
        if (x >= origin && x - origin >= 0x0080) {
            o.vx = 0xFF80;
        }
    } else if (origin >= x && origin - x >= 0x0080) {
        o.vx = 0x0080;
    }
    if (!sonic_near_x(o, 0x40) || !(sonic().flags & 0x01)) {
        return;
    }
    o.next_state = 2;
    o.vy = 0xFE80;
    o.vx = 0;
}

// --- 39: a shaking enemy, 3B: its fireballs -----------------------------------

// B941 (state 0)
void Game::shaker_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.angle = 0x01;
    o.next_state = 1;
    o.vx = 0x0020;
}

// B958 (state 1): patrols slowly; Sonic within 64 pixels makes it shake
// (2). Touching it hurts.
void Game::shaker_patrol(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    face_motion(o); // 023C
    if (sonic_near_y(o, 0x40) && sonic_near_x(o, 0x40)) {
        o.next_state = 2;
    }
    touch_sonic(o);
    move_object(o);
    patrol_turn(o, 0x60); // BCB5
    if (enemy_must_turn(o) & 0x01) {
        o.vx = negated(o.vx);
    }
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF; // B9C8
    }
}

// B996 (state 2): shaking (vx reversed every frame); Sonic in the air
// right above it sets it off (3).
void Game::shaker_shake(Object& o) {
    face_motion(o);
    touch_sonic(o);
    move_object(o);
    o.vx = negated(o.vx);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
        return;
    }
    if (!(sonic().flags & 0x01)) {
        return;
    }
    if (sonic_near_x(o, 0x10)) {
        o.next_state = 3;
    }
}

// B9BD (state 3, after spawning two fireballs): explodes.
void Game::shaker_burst(Object& o) {
    o.param = 0x80;
    explode(o); // 025D
}

// BB3E (3B, state 0): thrown up and right (parameter 2: lower and slower)
// from its origin; gone if spawned off screen.
void Game::fireball_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
    }
    o.next_state = 1;
    o.counter = 0;
    o.origin_x = o.x;
    o.origin_y = o.y;
    if (o.param == 0x02) {
        o.vx = 0x00D0;
        o.vy = 0xFE20;
    } else {
        o.vx = 0x0140;
        o.vy = 0xFC80;
    }
}

// BB96 (state 1): every other frame it is drawn mirrored around its origin
// (two fireballs); it moves on the other frames (gravity 30, twice per
// frame). Hurts Sonic; gone below the screen.
void Game::fireball_fly(Object& o) {
    // Widescreen margins (zero on the LCD) keep it until it leaves the view.
    if (((camera.y + 0x00E0u + unsigned(wide_bottom)) & 0xFFFF) < unsigned(o.y)) {
        o.type = object_type::removed;
        return;
    }
    const bool odd = o.counter & 0x01;
    ++o.counter;
    const unsigned origin = o.origin_x;
    o.x = (origin * 2 - unsigned(o.x)) & 0xFFFF;
    if (odd) {
        touch_sonic(o);
        if (o.touch_sides & 0x0F) {
            player.hit_request = 0xFF;
        }
        return;
    }
    o.vy = (unsigned(o.vy) + 0x0030) & 0xFFFF;
    move_object(o);
    move_object(o);
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
}

// --- 3A: a hopping enemy -----------------------------------------------------

// B9F6 (state 0)
void Game::hopper2_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.vx = 0;
    o.vy = 0xFE80;
    o.counter = 0;
    o.angle = 0;
    o.next_state = 1;
}

// BA19 (state 1): three hops on the spot, then one aside (2).
void Game::hopper2_hop(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.vx = 0;
    if (o.counter == 0x03) {
        o.next_state = 2;
        return;
    }
    hopper2_air(o, rom.word(rom::hopping_enemy_jump));
}

// The shared part of BA32 / BAE9: moves (gravity 14, twice), destroyed if
// attacked; a landing on profile 81 hops again at `hop` and counts.
void Game::hopper2_air(Object& o, unsigned hop) {
    touch_sonic(o);
    move_object(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o); // 020C
        return;
    }
    o.vy = (unsigned(o.vy) + 0x0014) & 0xFFFF;
    move_object(o);
    if (probe_type(o, 0, 0) != 0x81) {
        return;
    }
    o.vy = hop;
    ++o.counter;
}

// BA72 (state 2): a sideways hop, alternately right and left.
void Game::hopper2_turn(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.counter = 0;
    o.vy = 0xFF00;
    ++o.angle;
    o.next_state = 3;
    o.vx = (o.angle & 0x01) ? 0x0060 : 0xFFA0;
}

// BAA5 (state 3): three low hops aside (stopping at walls), then back to
// hopping on the spot (1).
void Game::hopper2_move(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (o.counter == 0x03) {
        o.counter = 0;
        o.next_state = 1;
        o.vx = 0;
        o.vy = rom.word(rom::hopping_enemy_jump);
        return;
    }
    if (o.vx) {
        object_wall(o); // 0239
        if (o.terrain & 0x0C) {
            o.vx = 0;
        }
    }
    hopper2_air(o, 0xFF00);
}

} // namespace sonic2_gg
