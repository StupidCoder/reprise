// Atomic Destroyer's boss (zone 5 act 3; type 55, bank 17, A842-ACFA).
// It chases the player along the act (kept a little behind him, the
// camera centred on him), races off at X 1000, and waits for him in the
// arena (1280); there, in three rounds of four hits, it swoops from the
// left and right corners (states 5-7) and charges along the floor (8-D);
// beaten (E) it blows up and drifts off, freeing the camera.
//
// Its own bytes: scratch 0 (34) rounds left, 1 (35) hits left in the
// round, 2 (36) frames it cannot be hit after a hit, 5 (39) a saved loop
// count.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr u8 chase_song = 0x9E;
} // namespace

bool Game::run_destroyer_boss_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::destroyer_start: // AB27: three rounds of four hits; the sprites' palette 1A faded
                                   // in
        o.next_state = 0x01;
        o.display |= 0x02;
        o.scratch[0] = 0x03;
        o.scratch[1] = 0x04;
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = 0x1A;
        return true;
    case Routine::destroyer_face_left: // AB40
        o.display |= display_flag::mirrored;
        return true;
    case Routine::destroyer_face_right: // AB45
        o.display &= u8(~display_flag::mirrored);
        return true;
    case Routine::destroyer_wait: // AB4A: the player above Y 100 and the camera past 580: the chase
                                  // (2) from X 540
        if (objects[0].y >= 0x0100 || camera.x < 0x0580) {
            return true;
        }
        o.x = 0x0540;
        o.next_state = 0x02;
        sound_.request(chase_song, 2);
        act.music_playing = chase_song;
        return true;
    case Routine::destroyer_centre_camera: // ABC2
        camera.mode |= camera_mode::centred;
        return true;
    case Routine::destroyer_chase: // AB75
        destroyer_chase(o);
        return true;
    case Routine::destroyer_race: // ABEE: at the player's height
        move_object(o);           // 5359 (0381)
        o.y = objects[0].y;
        return true;
    case Routine::destroyer_race_off: // ABC8: harmful, faster by 1/2; 256 past the player: off to
                                      // the arena (4)
        harmful_touch(o);             // 550E (0375)
        o.vx = std::uint16_t(o.vx + 0x80);
        if (std::uint16_t(objects[0].x + 0x0100) < o.x) {
            o.next_state = 0x04;
        }
        move_object(o);
        o.y = objects[0].y;
        return true;
    case Routine::destroyer_arena: // ABFB: the camera to the arena (1282, 10); there: round 1 (5),
                                   // no going back
        enable_map();              // 4CC3 (030F)
        script_camera(0x1282, 0x0010); // 4CDA (0321)
        if ((camera.x & 0xFFFC) != 0x1280) {
            return true;
        }
        o.next_state = 0x05;
        camera.min_x = camera.x;
        return true;
    case Routine::destroyer_swoop: // AC22: flickering while it cannot be hit; rising by 1/8 less
                                   // each frame
        o.display &= u8(~display_flag::flicker);
        if (o.scratch[2]) {
            o.display |= display_flag::flicker;
            if (--o.scratch[2]) {
                o.vy = std::uint16_t(o.vy - 0x20);
                destroyer_fly(o);
                return true;
            }
        }
        o.display &= u8(~display_flag::hidden);
        o.vy = std::uint16_t(o.vy - 0x20);
        destroyer_fly(o);
        return true;
    case Routine::destroyer_fly: // AC49
        destroyer_fly(o);
        return true;
    case Routine::destroyer_round_end: // AC82: the round's hits used up: the charge (8), a round
                                       // less
        if (o.scratch[1] && !(o.scratch[1] & 0x80)) {
            return true;
        }
        o.display &= u8(~(display_flag::hidden | display_flag::flicker));
        o.scratch[2] = 0;
        o.next_state = 0x08;
        --o.scratch[0];
        return true;
    case Routine::destroyer_to_middle: // AC9F: moving to X 1300: the charge (9)
        move_object(o);
        if ((o.x & 0xFFFE) == 0x1300) {
            o.next_state = 0x09;
        }
        return true;
    case Routine::destroyer_charge_side: // ACB9: from the left (A) or, facing left, the right (B)
        o.next_state = (o.display & display_flag::mirrored) ? 0x0B : 0x0A;
        return true;
    case Routine::destroyer_charge: // ACC8: harmful
        move_object(o);
        harmful_touch(o);
        return true;
    case Routine::destroyer_return: // ACCF: back at X 1300: four hits again, the next round (D)
        move_object(o);
        if ((o.x & 0xFFFE) == 0x1300) {
            o.scratch[1] = 0x04;
            o.next_state = 0x0D;
        }
        return true;
    case Routine::destroyer_save_loop: // ACED
        o.scratch[5] = o.loop_count;
        return true;
    case Routine::destroyer_restore_loop: // ACF4
        o.loop_count = o.scratch[5];
        return true;
    case Routine::destroyer_beaten: // AAE0: still, flickering, kept no longer, no placement
        o.display &= u8(~(display_flag::mirrored | 0x02));
        o.display |= display_flag::flicker;
        o.vx = 0;
        o.vy = 0;
        o.placement = 0;
        return true;
    case Routine::destroyer_drift: // AAFD: off screen: gone, the camera free to 1400 at this height
        if (!(o.display & display_flag::offscreen)) {
            move_object(o);
            return true;
        }
        release_camera(); // 4CF2 (0324)
        camera.max_x = 0x1400;
        camera.max_y = camera.y;
        camera.act_max_y = camera.y;
        camera.mode &= u8(~camera_mode::centred);
        o.param = 0;
        o.type = 0xFF;
        return true;
    default:
        return false;
    }
}

// AB75: the chase: at the camera's X 1000 it races off (3); otherwise at
// the player's height, and every other 8 frames its speed set: 5.5 when
// more than 64 behind the player, 6.5 when less, 10 when level or ahead.
void Game::destroyer_chase(Object& o) {
    if (camera.x >= 0x1000) {
        o.next_state = 0x03;
        return;
    }
    move_object(o); // 5359 (0381)
    o.y = objects[0].y;
    if (video.frame_counter & 0x08) {
        return;
    }
    const std::uint16_t lead = std::uint16_t(objects[0].x - 0x40);
    std::uint16_t v = 0x0580;
    if (lead >= o.x) {
        const std::uint16_t d = std::uint16_t(lead - o.x);
        v = (hi(d) || lo(d) >= 0x40) ? 0x0A00 : 0x0680;
    }
    o.vx = v;
}

// AC49: moving; unless just hit, a rolling or jumping player's touch
// bounces him and costs a hit (60 frames before the next); the last hit
// of the last round beats it (E).
void Game::destroyer_fly(Object& o) {
    move_object(o);
    if (o.scratch[2]) {
        return;
    }
    touch_test(o); // 58AC (0369)
    bounce_off(o); // 561B (0438)
    if (!(o.touch_sides & 0x0F) || !(objects[0].flags & 0x02)) {
        return;
    }
    o.scratch[2] = 0x3C;
    if (--o.scratch[1]) {
        return;
    }
    if (o.scratch[0] != 0x01) {
        return;
    }
    o.display &= u8(~(display_flag::hidden | display_flag::flicker));
    o.next_state = 0x0E;
}

} // namespace triple_trouble_gg
