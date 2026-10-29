// The guardians of the third and fifth special stages (zone 6 acts 2
// and 3, bank 17), met at the stage's top before its emerald.
//
// Type 5A (B074-B4BB): the HUD slides off (type 0B), the camera goes to
// (399, 50) and the player is held; then the rival's song, the camera
// held, and it flies across the room and back (4 passes: in from the
// left at Y A0, then (471, 90) down-left, (3C1, 90) down-right, (471, D0)
// left), trailing sparkles (type 39, FC/FD). A rolling player's touch
// costs it a hit, once a pass (3 in all; the frame shows the hit); any
// other touch hurts him. Beaten it flashes white, blows up and flies off
// upwards; then the camera is free and the sprite art comes back (79E7).
// Type 5B (B4BC-B7BB): likewise met (camera (DE5, 16E)); after its art
// (7AAD: list 1A:B918) and the song it runs in from the right to X E65,
// sways after the player (turning four times), then rises and drops
// onto the floor (Y 20E: the screen shakes, sound B2) — four drops; then
// it is stunned: each touch from the side presses it into the ground
// (by 9) down to Y 249, where it is beaten. Its touches only bounce the
// player off (48 frames before the next), from above they hurt him.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr u8 rival_song = 0x97;
} // namespace

bool Game::run_special_guardian_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::glider_start: // B2B4: D3E4 F8, the clock stopped, the HUD away; 3 hits; the
                                // camera to (399, 50)
        act.variant = 0xF8;
        clock.stopped = 0x01;
        o.next_state = 0x01;
        o.display |= 0x02;
        spawn_object(o, 0x0B, 0x00); // 50E1 (0354)
        o.status_ticks = 0x03;
        o.placement = 0;
        script_camera(0x0399, 0x0050); // 4CDA (0321)
        return true;
    case Routine::guardian_wait_hud: // B2DF, B5CD: the camera no further left; the HUD gone (DBC3):
                                     // on (2)
        camera.min_x = camera.x;
        if (sprites.hud_small) {
            o.next_state = 0x02;
        }
        return true;
    case Routine::glider_meet_song: // B0FA: the camera held at its height; the rival's song
        camera.max_y = camera.y;
        camera.act_max_y = camera.y;
        sound_.request(rival_song, 2);
        return true;
    case Routine::glider_right: // B109: facing right at 3; from the screen's left (camera + 20, A0)
        o.display |= display_flag::mirrored;
        o.vx = 0x0300;
        o.vy = 0;
        o.x = std::uint16_t(camera.x + 0x20);
        o.y = 0x00A0;
        return true;
    case Routine::glider_pass_start: // B2FA (state 3): a new pass
        o.display |= display_flag::mirrored;
        o.vx = 0x0300;
        o.vy = 0;
        o.scratch[0] = 0;
        o.x = std::uint16_t(camera.x + 0x20);
        o.y = 0x00A0;
        return true;
    case Routine::glider_left: // B120: facing left at 3
        o.display &= u8(~display_flag::mirrored);
        o.vx = 0xFD00;
        o.vy = 0;
        return true;
    case Routine::glider_place: { // B328, B351, B37A: the other passes' starts

        struct Pass {
            std::uint16_t x;
            u8 y;
            bool right;
            std::uint16_t vy;
        };

        static constexpr Pass passes[] = {{0x0471, 0x90, false, 0x0200},
                                          {0x03C1, 0x90, true, 0x0200},
                                          {0x0471, 0xD0, false, 0x0000}};
        const Pass& p = passes[o.state - 4];
        o.x = p.x;
        o.y = p.y;
        if (p.right) {
            o.display |= display_flag::mirrored;
            o.vx = 0x0300;
        } else {
            o.display &= u8(~display_flag::mirrored);
            o.vx = 0xFD00;
        }
        o.vy = p.vy;
        o.scratch[0] = 0;
        return true;
    }
    case Routine::glider_frame: // B19D: 16 updates of frame 1 (4 just after a hit)
        o.frame_ticks = 0x10;
        o.frame = 0x01;
        if (o.scratch[4]) {
            o.frame = 0x04;
            o.scratch[4] = 0;
        }
        return true;
    case Routine::glider_pass1: // B3A3: past X 479: pass 2 (4)
        if (o.x >= 0x0479) {
            o.next_state = 0x04;
        }
        glider_fly(o);
        return true;
    case Routine::glider_pass2: // B3B8: left of 399: pass 3 (5); falling slower
        if (o.x < 0x0399) {
            o.next_state = 0x05;
        }
        glider_level_off(o);
        return true;
    case Routine::glider_pass3: // B3CF: past 479: pass 4 (6)
        if (o.x >= 0x0479) {
            o.next_state = 0x06;
        }
        glider_level_off(o);
        return true;
    case Routine::glider_pass4: // B3F8: left of 399: pass 1 again (3)
        if (o.x < 0x0399) {
            o.next_state = 0x03;
        }
        glider_fly(o);
        return true;
    case Routine::glider_beaten: // B44F: the colours back (35, 1D); still, flashing
        act.variant = 0;
        o.display |= display_flag::flicker;
        o.vx = 0;
        o.vy = 0;
        palette.fade_period = 0x02;
        palette.fades[0] = {0x90, 0x35};
        palette.fades[1] = {0x90, 0x1D};
        return true;
    case Routine::guardian_blast_frame: // B4B3
        o.frame = 0x04;
        o.frame_ticks = 0x08;
        return true;
    case Routine::knuckles_guard_start: // B59A: D3E4 F8; the HUD away; the clock stopped; the
                                        // camera to (DE5, 16E), held there; 4 drops
        act.variant = 0xF8;
        o.next_state = 0x01;
        o.display |= 0x02;
        o.placement = 0;
        spawn_object(o, 0x0B, 0x00);
        clock.stopped = 0x01;
        camera.max_y = 0x016E;
        camera.act_max_y = 0x016E;
        script_camera(0x0DE5, 0x016E);
        o.timer = 0x04;
        return true;
    case Routine::knuckles_run_in: // B609: in to X E65 (4), four turns
        knuckles_touch(o);
        move_object(o);
        if (o.x < 0x0E65) {
            o.next_state = 0x04;
            o.counter = 0x04;
        }
        return true;
    case Routine::knuckles_chase: // B625
        knuckles_chase(o);
        return true;
    case Routine::knuckles_drop: // B68F: falling (1/8) onto Y 20E: shake, sound B2; a drop less:
                                 // rise (8) or stunned (6)
        knuckles_touch(o);
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        if (o.y <= 0x020E) {
            return true;
        }
        camera.shake_ticks = 0x10;
        sound_.request(0xB2, 1);
        o.y = 0x020E;
        o.next_state = --o.timer ? 0x08 : 0x06;
        return true;
    case Routine::knuckles_touch_only: // B6D3
        knuckles_touch(o);
        return true;
    case Routine::knuckles_stunned: // B6D7: a touch from the left presses it 9 down; at Y 249
                                    // beaten (7)
        knuckles_touch(o);
        if ((o.touch_sides & 0x0F) && (o.touch_sides & 0x01)) {
            o.y = std::uint16_t(o.y + 9);
        }
        if (o.y >= 0x0249) {
            o.next_state = 0x07;
        }
        return true;
    case Routine::knuckles_beaten: // B736: D3E4 0; flashing
        act.variant = 0;
        o.display |= display_flag::flicker;
        return true;
    case Routine::knuckles_flash: // B73F: white (3), sound D2
        palette.fades[0].mode |= 0x20;
        palette.fades[0].palette = 0x03;
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = 0x03;
        sound_.request(0xD2, 1);
        return true;
    case Routine::knuckles_colours: // B755: the colours back (36, 1D)
        palette.fade_period = 0x02;
        palette.fades[0] = {0x90, 0x36};
        palette.fades[1] = {0x90, 0x1D};
        return true;
    case Routine::knuckles_rise: // B7A3: up to Y 1C0: the chase again (4), four turns
        move_vertically_only(o); // 5379 (045F)
        if (o.y < 0x01C0) {
            o.next_state = 0x04;
            o.counter = 0x04;
        }
        return true;
    default:
        return false;
    }
}

// B2EF: its art (048C: 7AA6, list 1A:B908, with frame waits); silence;
// the player held (3F76).
Flow Game::glider_meet(Object&) {
    co_await load_tile_list_slowly(Table{0x1A, 0xB908}); // 7A58
    stop_music();                                        // 2288 (033F)
    player.in_scene = 0xFF;                              // 3F76 (04FE)
    objects[0].next_state = 0x4A;
}

// B5DD: its art (049E: 7AAD, list 1A:B918), the rival's song; in from
// the right at (EE5, 1C0); the partner's art away, the common art back.
Flow Game::knuckles_appear(Object& o) {
    co_await load_tile_list_slowly(Table{0x1A, 0xB918}); // 7A58
    sound_.request(rival_song, 2);
    o.next_state = 0x03;
    o.x = 0x0EE5;
    o.y = 0x01C0;
    o.scratch[0] = 0;
    di();
    player.partner_hidden = 0xFF;
    rival_restore_art(); // 762C (04A1)
}

// B482: gone upwards: off the screen's top it is gone, the camera free
// to the act's right end, the sprite art back (79E7).
Flow Game::guardian_fly_off(Object& o) {
    o.vy = std::uint16_t(o.vy - 0x08);
    move_object(o);
    if (o.y >= camera.y) {
        co_return;
    }
    o.type = 0xFF;
    enable_map(); // 4CC3 (030F)
    camera.max_x = camera.act_max_x;
    co_await reload_sprite_art(); // 79E7 (048F)
}

// B773: once the player stands, it is gone; as B482.
Flow Game::knuckles_gone(Object& o) {
    if (!(objects[0].terrain & 0x02)) {
        co_return;
    }
    o.type = 0xFF;
    enable_map();
    camera.max_x = camera.act_max_x;
    co_await reload_sprite_art();
}

// B3E1: its fall slows (1/32 a frame) to level.
void Game::glider_level_off(Object& o) {
    if (o.vy) {
        o.vy = std::uint16_t(o.vy - 0x08);
    }
    glider_fly(o);
}

// B40D: moving; once a pass a rolling player's touch costs a hit (the
// last: beaten, 7, the colours white); any other touch hurts him.
void Game::glider_fly(Object& o) {
    move_object(o);
    if (o.scratch[0]) {
        return;
    }
    touch_test(o); // 58AC (0369)
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (!(objects[0].flags & 0x02)) {
        player.hit = 0xFF;
        return;
    }
    o.scratch[4] = 0xFF;
    bounce_off(o); // 561B (0438)
    o.scratch[0] = 0x01;
    if (--o.status_ticks) {
        return;
    }
    o.next_state = 0x07;
    palette.fades[0].mode |= 0x20;
    palette.fades[0].palette = 0x03;
    palette.fades[1].mode |= 0x20;
    palette.fades[1].palette = 0x03;
}

// B709: a touch from above (bit 1) hurts the player; otherwise he bounces
// off (48 frames before the next).
void Game::knuckles_touch(Object& o) {
    o.touch_sides = 0;
    if (o.scratch[0]) {
        --o.scratch[0];
        return;
    }
    touch_test(o);
    const u8 sides = o.touch_sides & 0x0F;
    if (!sides) {
        return;
    }
    if (sides & 0x02) {
        player.hit = 0xFF;
        return;
    }
    o.scratch[0] = 0x30;
    bounce_off(o); // 561B (0438)
}

// B625: towards the player (1/32 a frame up to 1.5); each turn counts
// (byte 1E), after four it rises and drops (5).
void Game::knuckles_chase(Object& o) {
    knuckles_touch(o);
    if (objects[0].x >= o.x) {
        if ((o.vx & 0x8000) || o.vx <= 0x0180) {
            o.vx = std::uint16_t(o.vx + 0x08);
            const bool was = o.display & display_flag::mirrored;
            o.display |= display_flag::mirrored;
            if (!was && !--o.counter) {
                o.next_state = 0x05;
            }
        }
    } else if (!(o.vx & 0x8000) || o.vx > 0xFE80) {
        o.vx = std::uint16_t(o.vx - 0x08);
        const bool was = o.display & display_flag::mirrored;
        o.display &= u8(~display_flag::mirrored);
        if (was && !--o.counter) {
            o.next_state = 0x05;
        }
    }
    move_object(o);
}

} // namespace triple_trouble_gg
