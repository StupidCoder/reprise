// Scrambled Egg Zone act 3: the boss (4E) jumps, charges and bounces
// around the arena; eight hits (it is solid: AC6C) destroy it. With the
// five earlier emeralds it leaves the sixth (14), whose pickup completes
// the zone; otherwise its explosion does.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// A838
void Game::sez_boss_start(Object& o) {
    player.power_up_hold = 0xFF;
    o.hits = 0x08;
    o.display = u8((o.display | display_flag::keep_on_screen) &
                   ~(display_flag::offscreen | display_flag::hidden));
}

// A9E7 (state 1): Sonic within 128 pixels and not below it: the camera is
// fixed and the fight starts (2).
void Game::sez_boss_wait(Object& o) {
    const unsigned sx = sonic().x, x = o.x;
    const unsigned d = sx >= x ? sx - x : x - sx;
    if ((d >> 8) || (d & 0xFF) >= 0x80) {
        return;
    }
    if (unsigned(o.y) < unsigned(sonic().y)) {
        return;
    }
    o.next_state = 2;
    script_camera(0x00D8, 0x0018); // 02F6
}

// AC99: no hits left: it explodes (10), or with five emeralds leaves the
// sixth (11).
void Game::sez_boss_defeated(Object& o) {
    o.next_state = score.emeralds == 0x1F ? 0x11 : 0x10;
}

// AC91 (states 2, 3, 6, 7, C): standing, it can be hit.
void Game::sez_boss_guard(Object& o) {
    boss_solid_hit(o); // AC6C
    if (!o.hits) {
        sez_boss_defeated(o);
    }
}

// AA1F (state 2): faces the middle and picks an attack from Sonic's X
// fraction and the clock's seconds: a hop (5), a charge left (6) or right
// (7) for 64 frames, or a jump (4) / a pause (3) on odd seconds.
void Game::sez_boss_choose(Object& o) {
    face_x(o, (camera.x + 0x0080u) & 0xFFFF); // 0242
    const u8 seconds = clock.seconds;
    const u8 rotated = u8((seconds >> 3) | (seconds << 5));
    switch (u8(sonic().x_sub + seconds + rotated) & 0x03) {
    case 0: // AA63
        o.vy = 0xFE00;
        o.vx = 0;
        o.next_state = 5;
        return;
    case 1: // AA5A
        o.counter = 0x40;
        o.next_state = 6;
        return;
    case 2: // AA51
        o.counter = 0x40;
        o.next_state = 7;
        return;
    default: // AA7A
        if (seconds & 0x01) {
            o.next_state = 3;
            return;
        }
        o.vy = 0xFD00;
        o.vx = 0;
        o.next_state = 4;
        return;
    }
}

// AA9D / AAC9 / AB21: in the air (gravity 20, at most 1 pixel), landing
// on ground (profile below it, AAC9 two pixels lower).
bool Game::sez_boss_fall(Object& o, unsigned probe_dy) {
    boss_solid_hit(o);
    if (!o.hits) {
        sez_boss_defeated(o);
        return false;
    }
    move_object(o);
    fall(o, 0x0020, 0x0100);
    if (hi(o.vy) & 0x80) {
        return false;
    }
    return probe_type(o, 0, probe_dy) != 0; // 0263
}

// AA9D (state 4) and AB21 (state 9)
void Game::sez_boss_jump(Object& o) {
    if (sez_boss_fall(o, 0)) {
        o.next_state = 2;
    }
}

// AAC9 (state 5): the hop lands into a dash (8).
void Game::sez_boss_hop(Object& o) {
    if (sez_boss_fall(o, 0x0002)) {
        o.next_state = 8;
    }
}

// ABDE: true at an edge (window 40-BF of the screen) when moving toward
// it, or when stopped in the middle (as 964A).
bool Game::sez_boss_at_edge(const Object& o) const {
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    const bool left_side = unsigned(o.x) < camera.x || (!(dx >> 8) && (dx & 0xFF) < 0x40);
    const bool right_side = !left_side && ((dx >> 8) || (dx & 0xFF) >= 0xC0);
    if (left_side) {
        return !o.vx || (hi(o.vx) & 0x80);
    }
    if (right_side) {
        return !(hi(o.vx) & 0x80);
    }
    return !o.vx && !o.vy;
}

// AAF5 (state 8): a dash at 6 pixels per frame to the edge, then a jump (9).
void Game::sez_boss_dash(Object& o) {
    boss_solid_hit(o);
    if (!o.hits) {
        sez_boss_defeated(o);
        return;
    }
    move_object(o);
    if (!sez_boss_at_edge(o)) {
        return;
    }
    o.vy = 0xFF00;
    o.vx = 0;
    o.next_state = 0x09; // (5 first, replaced at once)
}

// AB4D (state A): a charge (6 pixels per frame) for 64 frames, bouncing
// off Sonic; then a jump (B).
void Game::sez_boss_charge(Object& o) {
    boss_bumper(o); // AC29
    if (--o.counter) {
        return;
    }
    o.vy = 0xFC00;
    o.next_state = 0x0B;
}

// AB64 (state B): the jump (gravity 1C); at an edge in the air it stops
// moving sideways; on the ground at an edge it stands (C).
void Game::sez_boss_leap(Object& o) {
    boss_bumper(o);
    fall(o, 0x001C, 0x0400);
    if (!(hi(o.vy) & 0x80) && probe_type(o, 0, 0)) {
        o.vy = 0;
        move_object(o);
        if (sez_boss_at_edge(o)) {
            o.next_state = 0x0C;
        }
        return;
    }
    move_object(o);
    if (sez_boss_at_edge(o)) {
        o.vx = 0;
    }
}

// ABA7 (state D): the other charge (4 pixels per frame), 64 frames, then
// bouncing between the edges (E).
void Game::sez_boss_charge2(Object& o) {
    boss_bumper(o);
    if (!--o.counter) {
        o.next_state = 0x0E;
    }
}

// ABB3 (states E, F): at an edge it turns and jumps (B).
void Game::sez_boss_bounce(Object& o) {
    boss_bumper(o);
    move_object(o);
    if (!sez_boss_at_edge(o)) {
        return;
    }
    o.vx = (0x10000u - unsigned(o.vx)) & 0xFFFF;
    o.vy = 0xFC00;
    o.next_state = 0x0B;
}

// A9DD (end of state 10): the zone is complete. A9E2 (state 11): gone.
void Game::sez_boss_gone(Object& o, bool zone_complete) {
    if (zone_complete) {
        flow.game_flags |= game_flag::zone_complete;
    }
    o.type = object_type::waiting;
}

} // namespace sonic2_gg
