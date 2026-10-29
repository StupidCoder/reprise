// Gimmick Mt. Zone act 3 (44; 43 is the same object): the boss rolls along
// the floor, faster and faster, bouncing off the screen edges with a jump
// that shakes the screen; landing, it stands and drops boulders (45). Eight
// hits destroy it.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 97E4
void Game::gmz_boss_start(Object& o) {
    player.power_up_hold = 0xFF;
    o.hits = 0x08;
}

namespace {
// Sonic no more than 16 pixels below the boss's Y.
bool sonic_level_with(const Object& o, const Object& s) {
    return ((unsigned(o.y) + 0x0010u) & 0xFFFF) >= unsigned(s.y);
}
} // namespace

// 97EE (state 8): Sonic level with it and it within 288 pixels of the
// camera: it appears (9).
void Game::gmz_boss_wait(Object& o) {
    if (!sonic_level_with(o, sonic())) {
        return;
    }
    if (((camera.x + 0x0120u) & 0xFFFF) < unsigned(o.x)) {
        return;
    }
    o.next_state = 0x09;
}

// 9816 (state 1): within 208 pixels of the camera's left edge, the camera
// is fixed and it starts.
void Game::gmz_boss_approach(Object& o) {
    lock_camera_left(); // 0302
    if (!sonic_level_with(o, sonic())) {
        return;
    }
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    if (unsigned(o.x) >= camera.x && ((dx >> 8) || (dx & 0xFF) >= 0xD0)) {
        return;
    }
    gmz_boss_aim(o);               // 9917
    script_camera(0x0221, 0x0050); // 02F6
}

// 9917: a slow start toward the screen's middle (vx +-1/256), 64 frames of
// turning (state 6).
void Game::gmz_boss_aim(Object& o) {
    const bool left = unsigned(o.x) > ((camera.x + 0x0080u) & 0xFFFF);
    o.vx = left ? 0xFFFF : 0x0001;
    o.angle = 0x40;
    o.next_state = 6;
}

// 999A: Sonic touching it from above or below is hurt.
void Game::gmz_boss_hurts(Object& o) {
    if (o.touch_sides & 0x03) {
        player.hit_request = 0xFF;
    }
}

// 996D (state 6): facing its way for 64 frames, then rolls (2).
void Game::gmz_boss_turn(Object& o) {
    face_motion(o);   // 023C
    bumper_object(o); // 021B
    gmz_boss_hurts(o);
    o.touch_sides = 0;
    if (!--o.angle) {
        o.next_state = 2;
    }
}

// 98C3: hitting something or an edge: it jumps back (state 4) at a 16th of
// the speed - in the high byte only; the low byte keeps the reversed
// speed's (it stores L instead of the shifted A).
void Game::gmz_boss_rebound(Object& o) {
    const unsigned reversed = (0x10000u - unsigned(o.vx)) & 0xFFFF;
    const u8 high = u8(std::int8_t(u8(reversed >> 8)) >> 4);
    o.vx = (high << 8) | (reversed & 0xFF);
    o.vy = 0xFC00;
    ghz_boss_shake(); // 973F
    sound_.request(sound::boss_hit);
    o.next_state = 4;
}

// 984E (state 2): rolling, speeding up without limit; Sonic touching its
// left side (or, rolling right, either side) or the screen edge (window
// 48-BB) sends it back.
void Game::gmz_boss_roll(Object& o) {
    o.vy = 0;
    bumper_object(o);
    gmz_boss_hurts(o);
    const bool leftwards = hi(o.vx) & 0x80;
    if ((!leftwards && (o.touch_sides & 0x04)) || (o.touch_sides & 0x08)) {
        gmz_boss_rebound(o);
        return;
    }
    o.touch_sides = 0;
    face_motion(o);
    move_object(o);
    const u8 high = hi(o.vx);
    const bool negative = high & 0x80;
    const unsigned step =
        (((negative ? 0xFF00u : 0x0000u) | u8(high + (negative ? 0 : 1))) * 8u) & 0xFFFF;
    o.vx = (unsigned(o.vx) + step) & 0xFFFF;
    const unsigned dx = (unsigned(o.x) - camera.x) & 0xFFFF;
    const bool left_edge = unsigned(o.x) < camera.x || (!(dx >> 8) && (dx & 0xFF) < 0x48);
    const bool right_edge = !left_edge && ((dx >> 8) || (dx & 0xFF) >= 0xBC);
    if (left_edge && (hi(o.vx) & 0x80)) {
        gmz_boss_rebound(o);
    } else if (right_edge && !(hi(o.vx) & 0x80)) {
        gmz_boss_rebound(o);
    }
}

// 9940 (state 4): the jump; landing (3) it stands and drops boulders.
void Game::gmz_boss_jump(Object& o) {
    solid_object(o); // 0218
    o.touch_sides = 0;
    move_object(o);
    fall(o, 0x0040, 0x0600);
    if (land_object(o, 0x0300) == 0) { // 0212: stopped (carry)
        o.next_state = 3;
    }
}

// 98F8 (state 3): standing it can be hit (0224); a hit makes it blink
// (state 7); the last destroys it (5).
void Game::gmz_boss_stand(Object& o) {
    hit_object_solid(o); // 0224
    push_sonic_out(o);   // 0227
    if (!o.hits) {
        o.next_state = 5;
        return;
    }
    if (!o.timer) {
        return;
    }
    o.display |= display_flag::flicker;
    o.next_state = 7;
}

// 9983 (state 7): blinking; then it rolls again.
void Game::gmz_boss_blink(Object& o) {
    solid_object(o);
    o.touch_sides = 0;
    if (--o.timer) {
        return;
    }
    o.display &= u8(~(display_flag::flicker | display_flag::hidden));
    gmz_boss_aim(o);
}

// 995F (state 5, after the explosions): the camera may scroll on.
void Game::gmz_boss_gone(Object& o) {
    camera.max_x = 0x0440;
    follow_sonic(); // 02F3
    o.type = object_type::waiting;
}

// --- 45: a falling boulder -------------------------------------------------------

// 99BD: at the top of the screen near Sonic (table 99ED: X offset and
// speed by parameter). Widescreen: above the presented view.
void Game::boulder_start(Object& o) {
    o.y = camera.y - unsigned(wide_top);
    const auto entry = rom::boulder_offsets + u8(o.param * 4);
    o.x = (rom.word(entry) + unsigned(sonic().x)) & 0xFFFF;
    o.vy = rom.word(entry, 2);
}

// 99F9: falls (gravity 4); hurts Sonic; gone below the screen.
void Game::boulder_fall(Object& o) {
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
        o.type = object_type::removed;
        return;
    }
    fall(o, 0x0004, 0x0600);
    move_object(o);
    // Widescreen margins (zero on the LCD) keep it until it leaves the view.
    if (((camera.y + 0x00C0u + unsigned(wide_bottom)) & 0xFFFF) >= unsigned(o.y)) {
        return;
    }
    o.type = object_type::removed;
}

} // namespace sonic2_gg
