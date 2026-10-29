// Sky High Zone's hang glider: the glider object (27) and Sonic's glider
// states 11-15 (bank 1, 403C-428F). Left pulls the glider up (14) by
// trading speed for height, right dives (15); landing, touching a wall,
// leaving the screen or pressing a button drops it.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
// Sonic's flags and states used here (see player.cpp).
constexpr u8 airborne = 0x01, curled = 0x02, riding = 0x04;
constexpr u8 glider_standing = 0x11, glider_running = 0x12, glider_flying = 0x13;
constexpr u8 glider_climbing = 0x14, glider_diving = 0x15;
constexpr u8 floor_contact = 0x02;
// Held buttons (D137).
constexpr u8 left = 0x04, right = 0x08;

// Rain mode 2 (DBC0, Sky High act 2): the wind.
bool windy(const RainState& rain) {
    return rain.mode == 0x02;
}

unsigned magnitude(unsigned v) {
    return (v & 0x8000) ? (~(v - 1)) & 0xFFFF : v;
}
} // namespace

// 403C: standing with the glider.
void Game::glider_stand(Object& o) {
    o.flags = u8((o.flags & ~airborne) | riding);
    o.next_state = glider_standing;
    o.vx = 0;
}

// 404F (027E): running with it. (The glider object calls it on itself,
// see glider_wait.)
void Game::glider_run(Object& o) {
    o.flags = u8((o.flags & ~airborne) | riding);
    o.next_state = glider_running;
}

// 405C: off the ground: flying; taking off from the ground with a hop.
void Game::glider_take_off(Object& o) {
    if (!(o.terrain & floor_contact)) {
        o.next_state = glider_flying;
        return;
    }
    o.flags |= airborne;
    o.next_state = glider_flying;
    o.vy = 0xFE00;
    o.terrain &= u8(~floor_contact);
}

// 40A6 (state 11): normal ground movement; moving 1/16 pixel or more per
// frame starts the run, losing the ground the flight.
void Game::sonic_glider_stand() {
    Object& o = sonic();
    camera.mode |= camera_mode::hold_vertical;
    o.display &= u8(~display_flag::hidden);
    sonic_physics(); // 3A73
    if (magnitude(o.vx) & 0xFFF0) {
        glider_run(o);
        return;
    }
    if (!(o.terrain & floor_contact)) {
        glider_take_off(o);
    }
}

// 40CF (state 12)
void Game::sonic_glider_run() {
    Object& o = sonic();
    camera.mode |= camera_mode::hold_vertical;
    o.display &= u8(~display_flag::hidden);
    sonic_physics();
    if (!(o.terrain & floor_contact)) {
        glider_take_off(o);
        return;
    }
    if (!(magnitude(o.vx) & 0xFFF0)) {
        glider_stand(o);
    }
}

// 4107: after the flight's move: landing walks on; a wall, leaving the
// screen or a button drops the glider (falling).
void Game::glider_check_end() {
    Object& o = sonic();
    o.flags &= u8(~curled);
    touch_objects(); // 3773
    if (o.terrain & floor_contact) {
        o.flags &= u8(~riding);
        to_walking(); // 2FE3
        return;
    }
    if ((o.terrain & 0x0F) || glider_off_screen() || (input.pressed[0] & 0x30)) {
        o.flags &= u8(~riding);
        to_falling(); // 312B
    }
}

// 4134: outside the area 0-BF right of the camera and 28-FF below it.
bool Game::glider_off_screen() const {
    const Object& o = objects[0];
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    if (unsigned(o.x) < camera.x || dx >= 0x00C0) {
        return true;
    }
    const unsigned dy = (unsigned(o.y) - camera.y) & 0xFFFF;
    return unsigned(o.y) < camera.y || (dy >> 8) || dy < 0x0028;
}

// 40F8 (state 13): flying.
void Game::sonic_glider_fly() {
    Object& o = sonic();
    camera.mode |= camera_mode::hold_vertical;
    o.display &= u8(~display_flag::hidden);
    glider_fly_control(); // 4168
    collide_terrain();    // 6AB6
    glider_check_end();
}

// 4168: speeds up to 2.5 pixels per frame and sinks slowly (gravity 4, at
// most 2 pixels). Right dives; left (or the wind) climbs: the vertical
// speed becomes 80 - (vy + min(|vx|, 1C0)) / 2 when that rises.
void Game::glider_fly_control() {
    Object& o = sonic();
    // 424A
    unsigned vx = (unsigned(o.vx) + 0x0006) & 0xFFFF;
    if (!(vx & 0x8000) && vx >= 0x0280) {
        vx = 0x0280;
    }
    o.vx = vx;
    fall(o, 0x0004, 0x0200); // 61BE
    move_object(o);          // 66C3
    const u8 held = input.held[0];
    if (!(held & left)) {
        if (held & right) {
            o.next_state = glider_diving;
            return;
        }
        if (!windy(rain)) {
            return;
        }
    }
    // 4190
    unsigned speed = magnitude(o.vx);
    if (speed >= 0x01C0) {
        speed = 0x01C0;
    }
    unsigned lift = (unsigned(o.vy) + speed) & 0xFFFF;
    lift = ((lift >> 1) | (lift & 0x8000)) & 0xFFFF; // SRA H; RR L
    if (!(lift & 0x8000)) {
        const unsigned climb = ((0x10000u - lift) & 0xFFFF) + 0x0080u;
        if (climb < 0x10000) { // no carry from the ADD
            o.vy = climb;
        }
    }
    o.next_state = glider_climbing;
}

// 41D5 (state 14): climbing.
void Game::sonic_glider_climb() {
    Object& o = sonic();
    camera.mode |= camera_mode::hold_vertical;
    o.display &= u8(~display_flag::hidden);
    // 41E7
    move_object(o);
    glider_slow_down(); // 426C
    const u8 held = input.held[0];
    if ((held & right) || !windy(rain)) {
        fall(o, 0x0008, 0x0400);
        if (!(input.held[0] & left)) {
            if (hi(o.vy) & 0x80) {
                o.vy = 0;
            }
            o.next_state = glider_flying;
        }
    }
    collide_terrain();
    glider_check_end();
}

// 421E (state 15): diving.
void Game::sonic_glider_dive() {
    Object& o = sonic();
    camera.mode |= camera_mode::hold_vertical;
    o.display &= u8(~display_flag::hidden);
    // 4230
    glider_slow_down();
    fall(o, 0x0010, 0x0400);
    move_object(o);
    if (!(input.held[0] & right)) {
        o.next_state = glider_flying;
    }
    collide_terrain();
    glider_check_end();
}

// 426C: vx drops by 10/256 per frame, to no less than 60.
void Game::glider_slow_down() {
    Object& o = sonic();
    const unsigned vx = (unsigned(o.vx) - 0x000A) & 0xFFFF;
    o.vx = ((vx & 0x8000) || vx < 0x0060) ? 0x0060 : vx;
}

// --- 27: the glider ----------------------------------------------------------

// 8798
void Game::glider_start(Object& o) {
    o.next_state = 1;
}

// 879D (state 1): touched by Sonic, he takes it (state 11) and the object
// hides while it is carried (state 2). The 027E call runs with IX on the
// glider itself: it changes the glider's flags, and the state it sets is
// replaced right away.
void Game::glider_wait(Object& o) {
    touch_sonic(o); // 025A
    if (!o.touch_sides) {
        return;
    }
    o.touch_sides = 0;
    sonic().touch_sides = 0;
    sonic().next_state = glider_standing;
    glider_run(o); // 027E
    o.next_state = 2;
    o.display |= display_flag::hidden;
}

// 87BD (state 2): follows Sonic (also as its origin); when he lets go (a
// state below 11) it flies off up and back.
void Game::glider_carried(Object& o) {
    move_to_sonic(o); // 0209
    o.origin_x = sonic().x;
    o.origin_y = sonic().y;
    if (sonic().state >= glider_standing) {
        return;
    }
    o.vx = (unsigned(sonic().vx) + 0xFF80u) & 0xFFFF;
    o.vy = 0xFF00;
    o.next_state = 3;
    o.display &= u8(~display_flag::hidden);
}

// 87F9 (state 3): drifting down, gravity 8.
void Game::glider_drift(Object& o) {
    o.vy = (unsigned(o.vy) + 0x0008) & 0xFFFF;
    move_object(o);
}

} // namespace sonic2_gg
