// Common objects (bank 1F): sparkles, debris, scattered rings, the lava ball,
// explosions, monitors, the end-of-act sign, falling rocks and the intro's
// scenery.

#include "sonic2_gg/game/game.hpp"
#include <algorithm>

namespace sonic2_gg {

// --- 02: the sparkle of a collected map ring ------------------------------

// AAEF: at the collected ring's 16-pixel cell (X + 6).
void Game::sparkle_start(Object& o) {
    o.x = with_lo(probe.debris_x, (lo(probe.debris_x) & 0xF0) + 6);
    o.y = with_lo(probe.debris_y, lo(probe.debris_y) & 0xF0);
    o.next_state = 1;
    o.display |= display_flag::keep_on_screen;
}

// --- 04: a piece of a broken block ---------------------------------------

// AB26: piece `param` of four: offset and velocity from table AB8A (or ABAA
// when Sonic moves left), relative to the broken block (D35A/D35C).
void Game::debris_start(Object& o) {
    const unsigned table = (hi(sonic().vx) & 0x80) ? 0xABAA : 0xAB8A;
    const unsigned e = table + o.param * 8u;
    o.x = rom.word(0x1F, e) + probe.debris_x;
    o.y = rom.word(0x1F, e + 2) + probe.debris_y;
    o.vx = rom.word(0x1F, e + 4);
    o.vy = rom.word(0x1F, e + 6);
    o.next_state = 1;
    o.display |= 0x01;
    if (flow.zone == 6 && flow.act == 2) {
        o.tile_base = 0x9C;
    }
}

// ABCA: falls until off screen.
void Game::debris_fall(Object& o) {
    if (o.display & display_flag::offscreen) {
        delete_object(o); // 0206
        return;
    }
    fall(o, 0x00C0, 0x1000); // 020F
    move_object(o);          // 0257
}

// --- 08: a ring scattered when Sonic is hit -------------------------------

// AEE1: ring `param` leaves 16 pixels above Sonic with the velocity of
// tables AF32/AF40; it cannot be collected before its first bounce. 3C holds
// the speed of the next bounce.
void Game::lost_ring_start(Object& o) {
    const Object& s = sonic();
    o.x = unsigned(s.x);
    o.y = (s.y + 0xFFF0u) & 0xFFFF;
    const unsigned i = o.param * 2u;
    o.vx = rom.word(rom::lost_ring_vx, i);
    o.vy = rom.word(rom::lost_ring_vy, i);
    o.origin_y = 0xFC00;
    o.next_state = 1;
    o.flags |= 0xC0;
    o.display |= 0x01;
}

// AF4E: gravity 0x20; each landing bounces at 0.5 pixels per frame less,
// until the eighth; collected on touch once it has bounced.
void Game::lost_ring_move(Object& o) {
    o.vy += 0x20;
    touch_sonic(o); // 025A
    if (o.touch_sides & 0x0F) {
        // AFA3
        sound_.request(sound::ring);
        add_ring(); // 026C
        o.next_state = 2;
        return;
    }
    move_object(o);
    o.terrain &= 0xFD;
    if (!(hi(o.vy) & 0x80)) {
        object_floor(o); // 0260
    }
    if (o.terrain & 0x02) {
        o.flags &= 0xBF;
        const unsigned v = o.origin_y + 0x80u;
        if (v > 0xFFFF) {
            o.type = object_type::removed;
            return;
        }
        o.origin_y = v;
        o.vy = v;
    }
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
    }
}

// --- 0E: a lava ball ------------------------------------------------------

// B5D0: with a parameter it waits off screen (state 4), else jumps (B5DC).
void Game::lava_ball_start(Object& o) {
    if (o.param) {
        o.next_state = 4;
        return;
    }
    lava_ball_jump(o);
}

// B5DC: thrown up at 5 pixels per frame from its origin.
void Game::lava_ball_jump(Object& o) {
    o.vy = 0xFB00;
    o.vx = 0;
    o.y = unsigned(o.origin_y);
    o.next_state = 1;
}

// B5FF (state 1): rising.
void Game::lava_ball_rise(Object& o) {
    fall(o, 0x0010, 0x0400);
    if (!(hi(o.vy) & 0x80)) {
        o.next_state = 2;
    }
    lava_ball_move(o);
}

// B616 (state 2): falling back; below the origin it jumps again after a
// pause (state 3).
void Game::lava_ball_drop(Object& o) {
    fall(o, 0x0014, 0x0400);
    if (o.origin_y < o.y) {
        lava_ball_jump(o);
        o.next_state = 3;
    }
    lava_ball_move(o);
}

// B63B (state 4) / B645 (state 5): the horizontal kind starts when on
// screen and returns to its origin when it leaves.
void Game::lava_ball_wait(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 5;
}

void Game::lava_ball_fly(Object& o) {
    if (o.display & display_flag::offscreen) {
        o.next_state = 4;
        o.x = unsigned(o.origin_x);
    }
    lava_ball_move(o);
}

// B65F: moves; touching Sonic hurts him.
void Game::lava_ball_move(Object& o) {
    move_object(o);
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
}

// --- 0F: an explosion -----------------------------------------------------

// B6BC
void Game::explosion_start(Object& o) {
    o.display &= u8(~display_flag::mirrored);
    o.next_state = 1;
    o.tile_base = 0;
    o.tile_base_mirrored = 0;
    o.display |= 0x01;
}

// B6A2 (called from the script): the low 6 bits of the parameter count
// further explosions.
void Game::explosion_repeat(Object& o) {
    const u8 b = o.param;
    const u8 count = b & 0x3F;
    if (!count) {
        return;
    }
    o.param = u8((b & 0xC0) | (count - 1));
    o.next_state = u8((o.state & 1) + 1);
}

// B6D2: the end: parameter bit 6 removes the object for good (its
// placement stays used), bit 7 lets it be placed again later.
void Game::explosion_end(Object& o) {
    if (o.param & 0x40) {
        o.placement = 0;
        o.type = object_type::removed;
        return;
    }
    o.type = (o.param & 0x80) ? 0xFE : 0xFF;
}

// --- 10: a monitor --------------------------------------------------------

// B71F
void Game::monitor_start(Object& o) {
    o.flags |= 0x80;
    o.next_state = 1;
}

// B728
void Game::monitor_appear(Object& o) {
    request_tiles_on_screen(o, o.param); // B7A1
    o.next_state = 2;
}

// B733: solid; broken by a curled Sonic coming down onto it (from below it
// only knocks him back). The reward goes to D39D (B77A).
void Game::monitor_update(Object& o) {
    request_tiles_on_screen(o, o.param);
    if (o.display & display_flag::offscreen) {
        return;
    }
    solid_object(o); // 0218
    Object& s = sonic();
    if (!(s.flags & 0x02)) {
        return;
    }
    const u8 sides = o.touch_sides & 0x0F;
    if (!sides) {
        return;
    }
    if (sides == 0x02) {
        s.vy = 0x0200;
        return;
    }
    if (!s.vy || (hi(s.vy) & 0x80)) {
        return;
    }
    // B77A: the reward bit (table B797 by parameter).
    if (o.param < 0x0A) {
        player.item_flags |= rom.byte(rom::monitor_items, o.param);
        o.param = 0;
        o.param |= 0x40;
    }
    o.param = 0x40;
    if (s.state != 0x09) {
        s.vy = 0xFC00;
    }
    explode(o); // 025D
}

// B7A1: when the object comes on screen, its tiles are requested (7743).
void Game::request_tiles_on_screen(Object& o, u8 request) {
    const u8 before = o.hits;
    o.hits = o.display;
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (!(before & display_flag::offscreen)) {
        return;
    }
    tiles.object_request = request;
}

// --- 11-14: a Chaos Emerald ---------------------------------------------
// Its tiles are request 20 + zone. In Scrambled Egg (zone 5) it falls out
// of the boss and taking it completes the zone.

// B7D1
void Game::emerald_start(Object& o) {
    o.next_state = 1;
    if (flow.zone == 5) {
        tiles.object_request = u8(flow.zone + 0x20);
    } else {
        request_tiles_on_screen(o, u8(flow.zone + 0x20)); // B7A1
    }
    o.flags |= 0x80;
    o.display = u8((o.display & ~display_flag::mirrored) | display_flag::flicker);
    o.tile_base = 0;
    if (flow.zone == 5) {
        o.vy = 0xFA00;
    }
}

// B80C
void Game::emerald_wait(Object& o) {
    request_tiles_on_screen(o, u8(flow.zone + 0x20));
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (flow.zone == 5) {
        // B866: bouncing down.
        touch_sonic(o);          // 025A
        move_object(o);          // 0257
        fall(o, 0x0040, 0x0600); // 020F
        land_object(o, 0x0080);  // 0212
    }
    touch_sonic(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.next_state = 2;
    score.emerald_found = 0xFF;
    o.display |= display_flag::keep_on_screen;
    sonic().power_up = 0;
    if (flow.zone != 5) {
        sound_.request(sound::emerald);
        return;
    }
    sound_.request(sound::act_clear);
    flow.game_flags |= game_flag::zone_complete;
}

// B854 (after the jingle): gone; the act music resumes.
void Game::emerald_taken(Object& o) {
    o.placement = 0;
    o.type = object_type::removed;
    if (flow.zone != 5) {
        play_act_music(); // 0314
    }
}

// --- 18: the end-of-act sign ----------------------------------------------

// B8F9
void Game::sign_start(Object& o) {
    o.flags |= 0x80;
}

// B8DA (state 4): waits to come on screen.
void Game::sign_wait(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 5;
}

// B8E4: the camera may not go back left (or, in zone 4 act 1, further right).
void Game::sign_lock_camera() {
    if (flow.zone == 4 && flow.act == 0) {
        // 4A4F
        if (camera.max_x < camera.x) {
            return;
        }
        camera.max_x = camera.x;
        return;
    }
    // 4A3F
    if (camera.min_x >= camera.x) {
        return;
    }
    camera.min_x = camera.x;
}

// B8FE (state 1): touched, the clock stops, the sign jumps and the camera
// moves to the act's end position (table BA26).
void Game::sign_ready(Object& o) {
    sign_lock_camera();
    touch_sonic(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    clock.running = 0;
    o.next_state = 2;
    o.vy = with_hi(o.vy, 0xFC);
    const auto e = rom::sign_cameras + ((flow.zone * 8u + flow.act * 4u) & 0xFF);
    script_camera(rom.word(e), rom.word(e, 2)); // 02F6
}

// B91A (state 2): spinning while it falls back; landing shows the reward:
// a continue (all lives kept and 77 rings), an extra life (two lives gained
// in the act), ten rings (a round number), or nothing.
void Game::sign_spin(Object& o) {
    water.air_count = 0;
    fall(o, 0x0010, 0x0600);
    move_object(o);
    if (o.origin_y >= o.y) {
        return;
    }
    o.y = unsigned(o.origin_y);
    o.next_state = 3;
    sound_.request(sound::thud);
    u8 frame = 0x06;
    if (score.lives_at_start == score.lives && score.rings == 0x77) {
        // B964
        ++score.continues;
        score.bonus[1] = 0x02;
        frame = 0x07;
    } else if (score.lives_at_start > score.lives && score.lives_at_start - score.lives == 2) {
        // B983
        ++score.lives;
        sound_.request(sound::extra_life);
        show_lives();
        frame = 0x05;
    } else if (!(score.rings & 0x0F)) {
        // B9A5
        unsigned r = score.rings + 0x10u;
        if (r > 0x99) {
            r += 0x60;
        }
        score.rings = u8(r);
        sound_.request(sound::ring);
        show_rings();
        if (score.rings < 0x10) {
            ++score.lives;
            sound_.request(sound::extra_life);
            show_lives();
        }
        frame = 0x08;
    }
    o.counter = frame;
}

// B9DA / B9E5: the reward face, briefly then for long.
void Game::sign_face_short(Object& o) {
    o.frame_ticks = 0x08;
    o.frame = o.counter;
}

void Game::sign_face_long(Object& o) {
    o.frame_ticks = 0xE0;
    o.frame = o.counter;
}

// B9F0: once Sonic is on the ground he runs off (state 20).
void Game::sign_finish(Object& o) {
    (void)o;
    Object& s = sonic();
    if (s.state == 0x20) {
        return;
    }
    if (s.flags & 0x01) {
        return;
    }
    s.next_state = 0x20;
    sound_.request(sound::act_clear);
}

// --- 1B: a falling rock -----------------------------------------------------

// BA7A (states 6): thrown up-left.
void Game::rock_throw(Object& o) {
    o.next_state = 1;
    o.vx = 0xFF40;
    o.vy = 0xFB80;
}

// BADF: with a parameter it starts already falling apart (BB0D).
void Game::rock_start(Object& o) {
    if (o.param) {
        rock_break(o);
        return;
    }
    o.next_state = 1;
}

// BB45 (state 1): drops when Sonic is below it and close enough
// horizontally; the faster he runs, the earlier.
void Game::rock_wait(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    const Object& s = sonic();
    if (s.y < o.y) {
        return;
    }
    const unsigned dy = s.y - o.y;
    if (dy >> 8) {
        return;
    }
    u8 b = u8((dy & 0xFF) >> 1);
    unsigned speed = s.vx;
    if (speed & 0x8000) {
        speed = (0x10000 - speed) & 0xFFFF;
    }
    if (speed > 0x0180) {
        b = u8(b * 2);
        if (speed > 0x0380) {
            const unsigned twice = unsigned(b) * 2;
            b = twice > 0xFF ? 0xFF : u8(twice);
        }
    }
    unsigned dx = (s.x - o.x) & 0xFFFF;
    if (dx & 0x8000) {
        dx = (0x10000 - dx) & 0xFFFF;
    }
    if (dx >> 8) {
        return;
    }
    if ((dx & 0xFF) >= b) {
        return;
    }
    o.next_state = 2;
    o.timer = 0x10;
}

// BAEB (state 2): shaking.
void Game::rock_shake(Object& o) {
    touch_sonic(o);
}

// BAEF (state 3): falling; it breaks on Sonic or the floor.
void Game::rock_fall(Object& o) {
    touch_sonic(o);
    if (!(o.touch_sides & 0x0F)) {
        fall(o, 0x0050, 0x0600);
        move_object(o);
        object_floor(o);
        if (!(o.terrain & 0x02)) {
            return;
        }
    }
    rock_break(o);
}

// BB0D: bounces off (state 4).
void Game::rock_break(Object& o) {
    o.next_state = 4;
    o.vx = 0x00B0;
    o.vy = 0xFC00;
}

// BB24 (states 5, 7): falls off the screen and is removed.
void Game::rock_fly(Object& o) {
    fall(o, 0x0050, 0x0600);
    move_object(o);
    if (!(o.display & display_flag::offscreen)) {
        return;
    }
    o.type = o.placement ? 0xFE : 0xFF;
}

// --- 1C-1E: clouds and stars of the intro ---------------------------------

// BBBA: a cloud at a random height and position.
void Game::intro_cloud_start(Object& o) {
    o.x = 0x0080u + (refresh_register() & 0xF8);
    o.y = 0x0054u + (refresh_register() & 0x07);
    o.next_state = 1;
}

// BBDD: drifts left 1.5 pixels per frame.
void Game::intro_cloud_move(Object& o) {
    wrap_intro_scenery(o); // BC86
    unsigned x = o.x - 1u;
    if (!(video.frame_counter & 1)) {
        --x;
    }
    o.x = x & 0xFFFF;
}

// BC06: a star; its Y depends on its slot.
void Game::intro_star_start(Object& o) {
    o.x = refresh_register();
    const unsigned slot_low = (0xD500u + unsigned(&o - &objects[0]) * 0x40u) & 0xFF;
    const u8 rotated = u8((slot_low >> 3) | (slot_low << 5));
    o.y = (0x004Cu + rotated) & 0xFF;
    o.next_state = 1;
}

// BC29: twinkles (the flashing colours of 7E0F) and drifts left.
void Game::intro_star_move(Object& o) {
    flash_ring_colors(); // 02DE
    wrap_intro_scenery(o);
    unsigned x = o.x - 1u;
    if (!(video.frame_counter & 7)) {
        --x;
    }
    o.x = x & 0xFFFF;
}

// BC55: a hill line at the horizon.
void Game::intro_hill_start(Object& o) {
    o.x = 0x0080u + (refresh_register() & 0x30);
    o.y = 0x007A;
    o.next_state = 1;
}

// BC72: moves left 4 pixels per frame.
void Game::intro_hill_move(Object& o) {
    wrap_intro_scenery(o);
    o.x -= 4;
}

// BC86: leaving at the left (screen X below 40), the piece comes back on the
// right at a random distance.
void Game::wrap_intro_scenery(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (o.screen_x >= 0x40) {
        return;
    }
    const unsigned c = (0xC0u + (refresh_register() & 0x1F)) & 0xFF;
    o.x += int(c);
}

} // namespace sonic2_gg

namespace sonic2_gg {

// --- power-up images around Sonic ---------------------------------------------
// Sonic's IX+32 holds the power-up: 1 speed shoes, 2 invincibility.

// 03: the speed shoes' after-images (parameters 0 and 1 follow Sonic 3 and
// 7 frames behind, from the position history D373).

// AC7A (state 0): the first image starts the history full of Sonic's
// position.
void Game::shoe_image_start(Object& o) {
    o.next_state = u8(o.param + 1);
    o.display |= 0x01;
    move_to_sonic(o); // 0209
    if (o.param) {
        return;
    }
    player.speed_trail.fill({sonic().x, sonic().y});
}

// ACA5: gone with the shoes; the first image moves the history on (LDDR)
// and records Sonic; each takes its place from table ACF2.
void Game::shoe_image_follow(Object& o) {
    if (sonic().power_up != 0x01) {
        o.type = object_type::removed;
        return;
    }
    auto& trail = player.speed_trail;
    if (!o.param) {
        std::copy_backward(trail.begin(), trail.end() - 1, trail.end());
        trail[0] = {sonic().x, sonic().y};
    }
    // Table ACF2 points the images at entries 3 (D37F) and 7 (D38F).
    const auto& place = trail[o.param ? 7 : 3];
    o.x = place.x;
    o.y = place.y;
}

// 05: invincibility sparkles.

// AE06 (state 0)
void Game::star_start(Object& o) {
    o.next_state = u8(o.param + 1);
    o.display |= 0x01;
    sonic().flags |= 0x80;
    move_to_sonic(o);
}

// AE19: on Sonic (keeping him invulnerable) while it lasts.
void Game::star_follow(Object& o) {
    if (sonic().power_up != 0x02) {
        o.type = object_type::removed;
        return;
    }
    sonic().flags |= 0x80;
    move_to_sonic(o);
}

// 07: debris pieces (frame from the parameter) in Under Ground (tiles 88)
// and Gimmick Mt. (F6); none elsewhere.

// ABF2
void Game::debris2_start(Object& o) {
    u8 tiles;
    if (flow.zone == 0) {
        tiles = 0x88;
    } else if (flow.zone == 4) {
        tiles = 0xF6;
    } else {
        o.type = object_type::removed;
        return;
    }
    o.tile_base = tiles;
    o.tile_base_mirrored = tiles;
    o.frame = o.param;
    o.frame_ticks = 0x80;
}

} // namespace sonic2_gg
