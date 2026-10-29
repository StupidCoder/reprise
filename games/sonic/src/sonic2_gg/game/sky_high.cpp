// Sky High Zone objects (bank 1C): a block that drops when pushed (25)
// and a hopping enemy (30-32, also in later zones). The glider (27) is in
// glider.cpp.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// --- 25: a block that drops when pushed ------------------------------------

// 8622 (state 1, velocity 100 left from the script): Sonic pushing it
// (touching floor and right wall, holding only right) sets it off.
void Game::drop_block_wait(Object& o) {
    if (sonic().contact != 0x06) {
        return;
    }
    if (input.held[0] != 0x08) {
        return;
    }
    o.next_state = 2;
}

// 8633 (state 2): slides and falls; gone off screen.
void Game::drop_block_fall(Object& o) {
    move_object(o);
    fall(o, 0x0020, 0x0600); // 020F
    land_object(o, 0x0200);  // 0212
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
    }
}

// --- 30-32: a hopping enemy -------------------------------------------------
// Even parameters wait for Sonic, jump (state 2) and run at him; odd ones
// run toward him at once (state 5), stop near him, drop and walk.

namespace {
constexpr u8 param_runs_first = 0x01;
constexpr u8 param_runs_left = 0x02;
} // namespace

// AD2F (state 0)
void Game::hopper_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = (o.param & param_runs_first) ? 5 : 1;
}

// AEB0: a hopper touched by Sonic is destroyed if he attacks.
void Game::hopper_touch(Object& o) {
    touch_sonic(o); // 025A
    move_object(o);
}

// AD45 (state 1): faces Sonic; jumps (state 2) when he is within 64 pixels
// both ways.
void Game::hopper_watch(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    face_x(o, sonic().x); // 023F
    hopper_touch(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o); // 020C
        return;
    }
    if (sonic_near_x(o, 0x40) && sonic_near_y(o, 0x40)) {
        o.next_state = 2;
    }
}

// AD72 / ADC2 (states 2 and 4): moving.
void Game::hopper_move(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    hopper_touch(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
    }
}

// AD86 (state 3): after the jump, runs at 2 pixels per frame toward Sonic
// (always left with parameter bit 1).
void Game::hopper_charge(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    face_x(o, sonic().x);
    o.vy = 0;
    o.next_state = 4;
    if (sonic().x >= o.x && !(o.param & param_runs_left)) {
        o.vx = 0x0200;
    } else {
        o.vx = 0xFE00;
    }
}

// ADD6 (state 5): runs toward Sonic.
void Game::hopper_run(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 6;
    o.vy = 0;
    o.vx = sonic().x < o.x ? 0xFE00 : 0x0200;
}

// AE09 (state 6): within 64 pixels of Sonic it stops and drops (state 7).
void Game::hopper_approach(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    hopper_touch(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
        return;
    }
    if (!sonic_near_x(o, 0x40)) {
        return;
    }
    o.vx = 0;
    o.next_state = 7;
    o.vy = 0x0100;
}

// AE3A (state 7): dropping; on ground of profile 81 it walks at half a
// pixel per frame toward Sonic (state 8).
void Game::hopper_drop(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    face_x(o, sonic().x);
    hopper_touch(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
        return;
    }
    if (probe_type(o, 0, 0) != 0x81) { // 0263
        return;
    }
    o.next_state = 8;
    o.vx = sonic().x < o.x ? 0xFF80 : 0x0080;
}

// AE84 (state 8): walking; at an edge or wall it jumps again (state 2).
void Game::hopper_walk(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.vy = 0x0100;
    if (enemy_must_turn(o) & 0x01) { // BD38
        o.next_state = 2;
        return;
    }
    hopper_touch(o);
    object_floor(o); // 0260
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
    }
}

} // namespace sonic2_gg
