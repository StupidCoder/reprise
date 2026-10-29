// The Great Turquoise boss (type 50, bank 17, 8BD0-8FEC): it drives in
// from the right when seen, patrols left and right dropping bouncers
// (type 23), takes four hits from a rolling player (each flashing a sprite
// colour), rises off screen, drops back in a second time and, beaten again,
// explodes; then the act's song returns and the player can leave.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr u8 act_song = 0x81;
} // namespace

// The boss's own bytes: counter (1E) and timer (1F) pace its drops;
// status_ticks (26) its hits left; scratch[0] the state it was hit in,
// scratch[1] set once it has been beaten the first time, scratch[2] the
// state after it turns; origin_x where it stops driving in.

bool Game::run_boss_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::boss_start: // 8D92
        boss_start(o);
        return true;
    case Routine::boss_wait: // 8DB8
        boss_wait(o);
        return true;
    case Routine::boss_drive_in: // 8DD9: until back at its start X, then patrolling (state 3)
        move_object(o);          // 5359 (0381)
        if (o.origin_x >= o.x) {
            o.next_state = 0x03;
            return true;
        }
        touch_test(o); // 58AC (0369)
        if (o.touch_sides & 0x0F) {
            bounce_off(o); // 561B (0438)
        }
        return true;
    case Routine::boss_left: // 8DFF
        boss_patrol(o, false);
        return true;
    case Routine::boss_right: // 8E40
        boss_patrol(o, true);
        return true;
    case Routine::boss_turn: // 8EA1
        boss_touch(o);       // 8F5E
        move_object(o);
        return true;
    case Routine::boss_turned: // 8EA8
        o.next_state = o.scratch[2];
        return true;
    case Routine::boss_flash: // 050A (58A7): a hit's flash
        start_animator(0x0C); // 259E
        return true;
    case Routine::boss_hit: // 8EAF: only bounces the player meanwhile
        touch_test(o);
        if (o.touch_sides & 0x0F) {
            bounce_off(o);
        }
        return true;
    case Routine::boss_hit_end: // 8EBC
        boss_hit_end(o);
        return true;
    case Routine::boss_rise_start: // 8EE0
        boss_rise_start(o);
        return true;
    case Routine::boss_rise: // 8F0E: up and away; off screen its return (script 8C66)
        touch_test(o);       // 8F51
        if (o.touch_sides & 0x0F) {
            bounce_off(o);
        }
        o.vy = std::uint16_t(o.vy - 0x10);
        move_object(o);
        if (o.display & display_flag::offscreen) {
            jump_script(o, 0x8C66);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::boss_return_height: // 8F05
        o.y = std::uint16_t((0x01 << 8) | 0x2D);
        return true;
    case Routine::boss_return: // 8F36: the player back on the floor (Y 1CE on): patrolling with 4
                               // hits
        if (objects[0].y >= 0x01CE) {
            return true;
        }
        o.next_state = 0x03;
        o.status_ticks = 0x04;
        o.vy = 0;
        return true;
    case Routine::boss_fall: // 8F8C: beaten: falling to Y 170, then exploding (script 8C9C)
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o);
        if (o.y < 0x0170) {
            return true;
        }
        jump_script(o, 0x8C9C);
        o.frame_ticks = 0x01;
        return true;
    case Routine::boss_explode: // 8F82: the rings stay away; it blinks
        act.rings_gone_too = 0xFF;
        o.display |= display_flag::flicker;
        return true;
    case Routine::boss_gone: // 8FB9: no longer the act's boss; the song stops (2288)
        act.variant = 0;
        o.placement = 0;
        act.boss = 0;
        stop_music();
        return true;
    case Routine::boss_song_end: // 8FCA: the act's song, its colours back
        sound_.request(act_song, 2);
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = 0x1B;
        return true;
    case Routine::boss_release: // 8FDB: the act's right end opens; the camera follows
        act.rings_gone = 0xFF;
        act.extra_loader = 0xFF;
        camera.max_x = camera.act_max_x;
        release_camera(); // 4CF2 (0324)
        return true;
    case Routine::boss_after: // 8FD8 (3F53)
        boss_after();
        return true;
    default:
        return false;
    }
}

// 8D92: seen from afar (display bit 1); the act's variant marks the boss
// (zone + 1), D3E5 names it; four hits.
void Game::boss_start(Object& o) {
    o.next_state = 0x01;
    o.display |= 0x02;
    act.variant = u8(flow.zone + 1);
    act.boss = u8(slot_number(o) + 1);
    o.origin_x = o.x;
    o.status_ticks = 0x04;
}

// 8DB8: on screen it moves 256 pixels right to drive in (state 2); the
// camera is scripted to the arena (52B, 1DD); its sprite colours; the
// zone's flash colour.
void Game::boss_wait(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 0x02;
    o.x = std::uint16_t(o.x + 0x0100);
    script_camera(0x052B, 0x01DD); // 4CDA (0321)
    palette.fades[1].mode |= 0x20;
    palette.fades[1].palette = 0x15;
    set_boss_flash_color(); // 5888 (050D)
}

// 8DFF / 8E40: patrolling left (speeding up by 1/32 to 1.5) or right; every
// 224 frames a bouncer drops (type 23: FD and FB alternately, entering at
// the screen's right / left); past the screen's middle it turns (state 5).
void Game::boss_patrol(Object& o, bool right) {
    // 8E81
    if (++o.counter >= 0xE0) {
        o.counter = 0;
        ++o.timer;
        spawn_object(o, 0x23, (o.timer & 1) ? 0xFB : 0xFD); // 50E1 (0354)
    }
    move_object(o);
    const std::uint16_t v = o.vx;
    if (!right) {
        if (!(v & 0x8000) || v > 0xFE80) {
            o.vx = std::uint16_t(v - 0x08);
        }
        if (std::uint16_t(camera.x + 0x80) >= o.x) {
            o.scratch[2] = 0x04;
            o.next_state = 0x05;
            return;
        }
    } else {
        if ((v & 0x8000) || v <= 0x0180) {
            o.vx = std::uint16_t(v + 0x08);
        }
        if (std::uint16_t(camera.x + 0x80) < o.x) {
            o.scratch[2] = 0x03;
            o.next_state = 0x05;
            return;
        }
    }
    boss_touch(o); // 8F5E
}

// 8F5E: touched by a rolling player: bounced off, the boss hit (state 6,
// remembering its state); otherwise the player is hit.
void Game::boss_touch(Object& o) {
    touch_test(o); // 58AC (0369)
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (!(objects[0].flags & 0x02)) {
        player.hit = 0xFF;
        return;
    }
    bounce_off(o); // 561B (0438)
    o.scratch[0] = o.state;
    o.next_state = 0x06;
}

// 8EBC: a hit counted; with hits left back to its state; the first time
// beaten it rises away (state 7), the second time it falls (state 8).
void Game::boss_hit_end(Object& o) {
    if (--o.status_ticks) {
        o.next_state = o.scratch[0];
        return;
    }
    if (!o.scratch[1]) {
        o.next_state = 0x07;
        o.scratch[1] = 0xFF;
        return;
    }
    o.next_state = 0x08;
}

// 8EE0: still, sound CC; the camera's limits close on where it is, and it
// follows the player again (4CF2).
void Game::boss_rise_start(Object& o) {
    o.vx = 0;
    o.vy = 0;
    sound_.request(0xCC, 1);
    camera.min_x = camera.x;
    camera.max_x = camera.x;
    camera.max_y = camera.y;
    release_camera(); // 4CF2 (0324)
}

// 3F53 (04D4): after the boss: a flying Tails (state 18) falls; on the
// floor the player waits (state 44), not moving left.
void Game::boss_after() {
    auto& p = objects[0];
    if (p.state == 0x18) {
        to_falling(p); // 3B47
        return;
    }
    if (p.state == 0x4B) {
        return;
    }
    if (!(p.terrain & 0x02)) {
        return;
    }
    p.next_state = 0x44;
    if (p.vx & 0x8000) {
        p.vx = 0;
    }
}

// 561B (0438): a rolling player touching the object bounces off it at 5
// pixels per frame away from the side touched (sound C1).
void Game::bounce_off(Object& o) {
    auto& p = objects[0];
    if (!(p.flags & 0x02)) {
        return;
    }
    const u8 c = o.touch_sides;
    const u8 sides = c & 0x0F;
    if (!sides) {
        return;
    }
    if (sides == 0x02 && (p.terrain & 0x02)) {
        return;
    }
    std::uint16_t vy = 0;
    if (c & 0x01) {
        vy = 0xFB00;
    } else if (c & 0x02) {
        vy = 0x0500;
    }
    std::uint16_t vx = 0;
    if (c & 0x08) {
        vx = 0xFB00;
    } else if (c & 0x04) {
        vx = 0x0500;
    }
    p.vx = vx;
    p.vy = vy;
    sound_.request(0xC1, 1);
    p.touch_sides = 0;
}

} // namespace triple_trouble_gg
