// Robotnik Winter's boss (zone 3 act 3; type 53, bank 17, 9B18-A0B0).
// Parameter 0 is the machine: it waits for the player (1), sends rolling
// balls while he approaches the arena (2), arrives with its three parts
// (parameters 9-B; 3), bobs at the arena's left until hit (4), swings on
// an arc throwing balls, scrolling the arena and pushing the player left
// (5), shakes when hit and loses a hit (6; 6 hits), runs off right when
// beaten (7), and blows up (8): the camera is freed, the act's song again
// and the player waits for the act's end. Its parts follow it (two fall
// away at 5 and 3 hits left). With it the rolling ball (type 32,
// bank 1E, AFD0-B11A; hypothesis: a snowball): rolling in from the right
// of the screen (parameter 0) or thrown by the boss (other parameters).
// And the watcher it leaves (type 17, 1E:9375-938D) that opens the act's
// right side once the player has dropped below the arena.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

bool Game::run_winter_boss_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::winter_boss_start: // 9E84: kept off screen; parameter 0 is the boss (D3E5, 6
                                     // hits), others its parts
        o.display |= 0x02;
        if (o.param) {
            o.next_state = o.param;
            return true;
        }
        o.next_state = 0x01;
        objects[0].scratch[1] = 0; // D535
        act.boss = u8(slot_number(o) + 1);
        o.scratch[1] = 0x06;
        return true;
    case Routine::winter_boss_wait: // 9EA8: the player past X 160 (state 2)
        if (objects[0].x >= 0x0160) {
            o.next_state = 0x02;
        }
        return true;
    case Routine::winter_boss_approach: // 9EB7: the camera no longer goes back; the player past
                                        // 7C0: the arena
        camera.min_x = camera.x;
        if (objects[0].x < 0x07C0) {
            return true;
        }
        o.next_state = 0x03;
        act.variant = u8(flow.zone + 1);
        script_camera(0x0859, 0x0090); // 4CDA (0321)
        o.x = 0x0900;
        o.y = 0x00E0;
        return true;
    case Routine::winter_boss_arrived: // 9B7C: the sprites' palette 18 faded in; the hit flash
                                       // colour; the boss song
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = 0x18;
        set_boss_flash_color(); // 5888 (050D)
        sound_.request(0x8A, 2);
        return true;
    case Routine::winter_boss_bob: // 9EEC: once the camera reaches the arena (858): hit by a
                                   // rolling player, swinging (5)
        move_object(o);            // 5359 (0381)
        if ((camera.x & 0xFFFC) != 0x0858) {
            return true;
        }
        if (!winter_boss_hit(o)) {
            return true;
        }
        o.counter = 0x10;
        disable_map(); // 4CCE (0312)
        o.next_state = 0x05;
        o.scratch[0] = camera.scroll_x;
        o.speed = 0x20;
        o.angle = 0x30;
        return true;
    case Routine::winter_boss_swing: // 9F2E: on an arc between angles 10 and F2; hit: shaking (6)
        scroll_arena(o);             // A00E
        if (!(o.scratch[5] & 0x01)) {
            o.angle = u8(o.angle - 2);
            if (o.angle < 0x10) {
                o.angle = 0x90;
                ++o.scratch[5];
            }
        } else {
            o.angle = u8(o.angle + 2);
            if (o.angle >= 0xF2) {
                o.angle = 0x70;
                ++o.scratch[5];
            }
        }
        velocity_from_angle(o); // 52E8 (032D)
        move_object(o);
        if (winter_boss_hit(o)) {
            o.next_state = 0x06;
        }
        return true;
    case Routine::winter_boss_shake: // 9F81: no hits left: off to the right (camera to 94A), then
                                     // state 7
        if (o.scratch[1] > 0x01) {
            scroll_arena(o);
        }
        move_object(o);
        touch_test(o); // 58AC (0369)
        bounce_off(o); // 561B (0438)
        if (o.scratch[1]) {
            return true;
        }
        o.vx = 0x0200;
        enable_map();                  // 4CC3 (030F)
        script_camera(0x094A, 0x0090); // 4CDA (0321)
        if ((camera.x & 0xFFFC) == 0x0948) {
            o.next_state = 0x07;
        }
        return true;
    case Routine::winter_boss_count: // 9FC7: a hit less; hits left: swinging again (5)
        if (!o.scratch[1]) {
            return true;
        }
        if (!--o.scratch[1]) {
            return true;
        }
        o.touch_sides = 0;
        o.next_state = 0x05;
        return true;
    case Routine::winter_boss_flee: // 9FD9: the last hit blows it up (8)
        move_object(o);
        if (winter_boss_hit(o)) {
            o.next_state = 0x08;
        }
        return true;
    case Routine::winter_boss_hold_player: // 9DDE: the controller off and released, the player
                                           // facing right
        input.disabled = 0xFF;
        input.held = 0;
        input.pressed = 0;
        objects[0].display &= u8(~display_flag::mirrored);
        return true;
    case Routine::winter_boss_sink: // 9FF6: falling, faster by 1/32 (not past the sign)
        move_object(o);
        if (!((o.vy + 0x08) & 0x8000)) {
            o.vy = std::uint16_t(o.vy + 0x08);
        }
        return true;
    case Routine::winter_boss_free_camera: // 9DF0: unless the player died: the camera follows him
                                           // within 96 right
        if (objects[0].state == 0x1F) {
            return true;
        }
        release_camera(); // 4CF2 (0324)
        camera.min_x = camera.x;
        camera.max_x = std::uint16_t(camera.x + 0x0060);
        act.variant = 0;
        o.placement = 0;
        act.boss = 0;
        stop_music(); // 2288 (033F)
        return true;
    case Routine::winter_boss_done: // 9E17: the act's song; palette 1B faded in; rings vanish; the
                                    // controller back
        if (objects[0].state == 0x1F) {
            return true;
        }
        sound_.request(0x84, 2);
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = 0x1B;
        act.rings_gone = 0xFF;
        act.extra_loader = 0xFF;
        input.disabled = 0;
        return true;
    case Routine::winter_boss_gone: // 9E37: the player down below Y 3A0: the act's end (3F53)
        if (objects[0].y >= 0x03A0) {
            boss_after(); // 3F53 (04D4)
        }
        return true;
    case Routine::winter_part_left: // A02E (part 9): 10 left; gone below 5 hits
        winter_part(o, 0xFFF6, true);
        if (objects[act.boss ? act.boss - 1u : 0].scratch[1] < 0x05) {
            o.type = 0xFF;
        }
        return true;
    case Routine::winter_part_right: // A048 (part A): 10 right; gone below 3 hits
        winter_part(o, 0x000A, true);
        if (objects[act.boss ? act.boss - 1u : 0].scratch[1] < 0x03) {
            o.type = 0xFF;
        }
        return true;
    case Routine::winter_part_core: // A062 (part B): with the machine running off (7): state C
        winter_part(o, 0, true);
        if (objects[act.boss ? act.boss - 1u : 0].state == 0x07) {
            o.next_state = 0x0C;
        }
        return true;
    case Routine::winter_part_core_off: // A07C: gone when the machine blows up (8)
        winter_part(o, 0, false);
        if (objects[act.boss ? act.boss - 1u : 0].state == 0x08) {
            o.type = 0xFF;
        }
        return true;
    case Routine::exit_watch_start: // 1E:9385 (type 17): kept off screen
        o.next_state = 0x01;
        o.display |= 0x02;
        return true;
    case Routine::exit_watch: // 938E: the player down on the ground below Y 350: the act's right
                              // limit back; gone
        if (objects[0].y < 0x0350 || !(objects[0].terrain & 0x02)) {
            return true;
        }
        camera.max_x = camera.act_max_x;
        o.type = 0xFF;
        return true;
    case Routine::ball_start: // 1E:B03A: tiles AC
        o.next_state = 0x01;
        o.tile_base = o.tile_base_mirrored = 0xAC;
        if (o.param) {
            o.param = 0;
            o.next_state = 0x03;
            return true;
        }
        // B055: dropped in 384 right of the camera, 32 down, at 6.5.
        o.display |= 0x02;
        o.x = std::uint16_t(camera.x + 0x0180 + wide_right); // Widescreen: beyond the view
        o.y = std::uint16_t(camera.y + 0x0020);
        o.vx = 0;
        o.vy = 0x0680;
        o.param = 0x0F;
        return true;
    case Routine::ball_fall: // B088: landing it rolls (state 2) left at 1.5
        move_object(o);      // 5359 (0381)
        object_terrain(o);   // 713D (0384)
        if (o.terrain & 0x02) {
            o.next_state = 0x02;
            o.vx = 0xFE80;
            o.vy = 0x0400;
            o.counter = 0;
        }
        return true;
    case Routine::ball_turn: // B0AC: the next of four frames (1-4, or 5-8 when status bit 6 is
                             // clear), 4 updates each
        o.frame = u8((o.counter & 0x03) + ((o.status & 0x40) ? 1 : 5));
        o.frame_ticks = 0x04;
        ++o.counter;
        return true;
    case Routine::ball_roll: // B0C5: once left of the player it may leave the screen
        move_object(o);
        object_terrain(o);
        touch_test(o); // 58AC (0369)
        if ((o.display & 0x02) && o.x < objects[0].x) {
            o.display &= u8(~0x02);
        }
        destroy_if_attacked(o); // 51B8 (0363)
        return true;
    case Routine::ball_throw: { // B004: thrown up at 3, left at 0.5, or faster the further right of
                                // the player
        o.vy = 0xFD00;
        o.vx = 0xFF80;
        if (o.x < objects[0].x) {
            return true;
        }
        const u8 d = lo(std::uint16_t(o.x - objects[0].x));
        if (d < 0x10) {
            return true;
        }
        o.vx = d < 0x30 ? 0xFEE0 : 0xFE80;
        return true;
    }
    case Routine::ball_fly: // B0EC: falling (1/8); a rolling player bounces off; landing it rolls
                            // on at 1.75
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        object_terrain(o);
        touch_test(o);
        bounce_off(o); // 561B (0438)
        if (o.terrain & 0x02) {
            o.vx = 0xFE40;
        }
        destroy_if_attacked(o);
        return true;
    default:
        return false;
    }
}

// A hit: touched by a rolling or jumping player (status bit 1), who
// bounces off; the flash (050A).
bool Game::winter_boss_hit(Object& o) {
    touch_test(o); // 58AC (0369)
    bounce_off(o); // 561B (0438)
    if (!(o.touch_sides & 0x0F) || !(objects[0].flags & 0x02)) {
        return false;
    }
    start_animator(0x0C); // 58A7 (050A)
    return true;
}

// A00E: every fourth update the arena scrolls a pixel and the player
// (right of X 894) is pushed a pixel left.
void Game::scroll_arena(Object& o) {
    if (++o.scratch[2] & 0x03) {
        return;
    }
    --camera.scroll_x;
    const std::uint16_t x = std::uint16_t(objects[0].x - 1);
    if (x >= 0x0894) {
        objects[0].x = x;
    }
}

// A093: a part at the machine's place (D3E5) plus `dx`, 4 lower; its
// touch hurts (550E) while `harmful`.
void Game::winter_part(Object& o, std::uint16_t dx, bool harmful) {
    if (harmful) {
        harmful_touch(o); // 550E (0375)
    }
    const auto& boss = objects[act.boss ? act.boss - 1u : 0];
    o.x = std::uint16_t(boss.x + dx);
    o.y = std::uint16_t(boss.y + 0x04);
}

} // namespace triple_trouble_gg
