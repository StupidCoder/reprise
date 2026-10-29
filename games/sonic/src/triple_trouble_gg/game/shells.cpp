// The shell walker (type 28, bank 1E, A7F6-A8D6) and its shell (type 2D,
// sharing its scripts; 9DFA-9F36): defeated, the walker leaves its shell,
// which the player can jump onto (state 16) and ride, or push.
//
// The ride (player state 16: Sonic's script 8317, Tails' 908E; bank 0C
// 8325, AEB2-AF27, BE2F-BFC3): the shell is carried under the player; Down
// with a button speeds him up by 1/16 the way he faces (up to 6); let go,
// he coasts (slopes push by 1/16; only a speed of exactly 1/32 stops);
// a single button jumps off, leaving the shell to slide on. Observed in
// the original: trace rp_z1a0 (the random player in Sunset Park act 1).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

bool Game::run_shell_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::walker_start: // A85A
        walker_start(o);
        return true;
    case Routine::walker_walk: // A892
        walker_walk(o);
        return true;
    case Routine::shell_ridden: // 9DFA: once the player gets off, it slides on at his speed
        if (player.shell != slot_number(o) + 1) {
            o.next_state = 0x02;
            o.vx = objects[0].vx;
        }
        return true;
    case Routine::shell_drop: // 9E13: a new shell drops at 6 for 24 frames
        if (!o.timer) {
            o.vy = 0x0600;
            o.counter = 0x18;
        }
        return true;
    case Routine::shell_slide: // 9E25
        shell_slide(o);
        return true;
    case Routine::shell_rest: // 9E8A
        shell_rest(o);
        return true;
    case Routine::shell_ride_start: // 0C:8325
        shell_ride_start(o);
        return true;
    case Routine::shell_ride_frame: // 0C:AEB2
        shell_ride_frame(o);
        return true;
    case Routine::shell_ride: // 0C:BE2F
        shell_ride(o);
        return true;
    default:
        return false;
    }
}

// A85A: tile base 7A, at rest: parameter FE drops (state 5), 0 walks
// (state 4), others are shells from the start (type 2D, state 2).
void Game::walker_start(Object& o) {
    o.tile_base = o.tile_base_mirrored = 0x7A;
    o.scratch[0] = 0;
    o.vx = o.vy = 0;
    if (o.param == 0xFE) {
        o.next_state = 0x05;
    } else if (!o.param) {
        o.next_state = 0x04;
    } else {
        o.type = 0x2D;
        o.next_state = 0x02;
    }
}

// A892: walking; touched by a rolling or jumping player (flags bit 7 or
// 1) it is defeated: an explosion 16 pixels up, sound CE, and it becomes
// its shell (type 2D, state 2). Otherwise the player is hit.
void Game::walker_walk(Object& o) {
    move_object(o);    // 5359 (0381)
    object_terrain(o); // 713D (0384)
    touch_test(o);     // 58AC (0369)
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    const u8 f = objects[0].flags;
    if (!(f & 0x80) && !(f & 0x02)) {
        player.hit = 0xFF;
        return;
    }
    spawn_object_at(o, 0x07, 0x00, o.x, std::uint16_t(o.y - 0x10)); // 510A (0357)
    o.type = 0x2D;
    o.next_state = 0x02;
    sound_.request(0xCE, 1);
}

// 9E25: a dropping shell falls (gravity 1/8); a sliding one (scratch 2)
// slows by 1/4 pixel per frame (9EE4), stops at walls (16 pixels ahead,
// 8 up; checked on odd frames) and rests (state 3) once stopped on the
// floor. Free of the player and of its short drop time it can be jumped
// onto (9E81).
void Game::shell_slide(Object& o) {
    if (o.timer) {
        o.vy = std::uint16_t(o.vy + 0x20); // 9F24
        move_object(o);
        return;
    }
    if (o.scratch[2]) {
        shell_friction(o); // 9EE4
        move_object(o);
        object_terrain(o);
        if ((o.terrain & 0x02) && !o.vx) {
            o.next_state = 0x03;
            return;
        }
        if (video.frame_counter & 1) {
            const std::uint16_t ahead = (o.vx & 0x8000) ? 0xFFF0 : 0x0010;
            if (probe_flags(o, ahead, 0xFFF8) & 0x80) { // 709A (0387)
                o.vx = 0;
            }
        }
        if (player.shell) {
            return;
        }
        if (o.counter) {
            --o.counter;
            return;
        }
    }
    // 9E81: only with power-up 5 or none.
    const u8 power = objects[0].power_up;
    if (power != 0x05 && power) {
        return;
    }
    shell_rest(o);
}

// 9EE4: toward 0 by 1/4 pixel per frame; reaching it on the floor ends the
// slide.
void Game::shell_friction(Object& o) {
    const std::uint16_t v = o.vx;
    if (!v) {
        return;
    }
    std::uint16_t next;
    bool stopped;
    if (!(v & 0x8000)) {
        stopped = v < 0x0040;
        next = std::uint16_t(v - 0x0040);
    } else {
        stopped = v >= 0xFFC0;
        next = std::uint16_t(v + 0x0040);
    }
    if (stopped) {
        next = 0;
        if (o.terrain & 0x02) {
            o.scratch[2] = 0;
        }
    }
    o.vx = next;
}

// 9E8A: solid (51FF); the player landing on it (not rising, not in state
// 16, no spring delay) jumps onto it: he takes its place, it is his shell
// (D3CB, state 1) and he rides it (state 16, 3EBA). Pushing against its
// side stops him.
void Game::shell_rest(Object& o) {
    auto& p = objects[0];
    if (p.state == 0x16 || terrain_state.spring_ticks) {
        return;
    }
    solid_object(o); // 51FF (036C)
    const u8 sides = o.touch_sides & 0x0F;
    if (!sides) {
        return;
    }
    if (sides & 0x01) {
        p.touch_sides = 0;
        if (p.vy & 0x8000) {
            return;
        }
        p.x = o.x;
        p.y = o.y;
        player.shell = u8(slot_number(o) + 1);
        o.next_state = 0x01;
        p.next_state = 0x16; // 3EBA (03CF)
        o.flags |= 0x02;
        return;
    }
    const bool blocked =
        (p.display & display_flag::mirrored) ? (o.touch_sides & 0x04) : (o.touch_sides & 0x08);
    if (blocked) {
        p.vx = 0;
    }
}

// The ride's bytes of the player (slot 0): 34-37 (scratch 0-3) are
// cleared at its start; 36 is FF while speeding up, 37 nonzero while
// standing still, 38 latches the speed-up sound.
namespace {
constexpr unsigned ride_speeding = 2, ride_still = 3, ride_sound = 4;
// AF1A-AF27: the frames, Sonic's then Tails': speeding (five, 3 updates
// each) and coasting (two, 6 updates each).
constexpr Table sonic_speeding{0x0C, 0xAF1A}, sonic_coasting{0x0C, 0xAF1F};
constexpr Table tails_speeding{0x0C, 0xAF21}, tails_coasting{0x0C, 0xAF26};
} // namespace

// 8325: falling at 3; the speed the bytes 38-39 hold if byte 37 is FF,
// else 0; the step counter and 34-37 cleared; the start's X kept (3A).
void Game::shell_ride_start(Object& o) {
    o.vx = o.scratch[ride_still] == 0xFF
               ? std::uint16_t(o.scratch[ride_sound] | o.scratch[ride_sound + 1] << 8)
               : 0;
    o.counter = 0;
    std::fill_n(o.scratch.begin(), 4, u8(0));
    o.vy = 0x0300;
    o.origin_x = o.x;
}

// AEB2: still: frame 06 (Tails 5B) for 2 updates. Otherwise the next of
// the speeding or coasting frames; each round of them clicks (sound C5).
void Game::shell_ride_frame(Object& o) {
    const bool sonic = o.type == 0x01;
    if (o.scratch[ride_still]) {
        o.frame = sonic ? 0x06 : 0x5B;
        o.frame_ticks = 0x02;
        return;
    }
    const bool speeding = o.scratch[ride_speeding] == 0xFF;
    const Table frames =
        speeding ? (sonic ? sonic_speeding : tails_speeding) : (sonic ? sonic_coasting : tails_coasting);
    const u8 count = speeding ? 0x05 : 0x02;
    o.frame_ticks = speeding ? 0x03 : 0x06;
    o.frame = rom.byte(frames, o.counter);
    if (++o.counter >= count) {
        o.counter = 0;
        sound_.request(0xC5, 1);
    }
}

// BE2F
void Game::shell_ride(Object& o) {
    // BF90: the shell under the player.
    if (player.shell) {
        auto& shell = objects[player.shell - 1u];
        shell.x = o.x;
        shell.y = o.y;
    }
    shell_ride_input(o); // BE71
    // BF14
    o.scratch[ride_still] = o.vx ? 0x00 : 0x01;
    move_object(o); // 5359 (0381)
    // BF44: on the ground pressed down at 5; gravity 1/8 up to 6.
    player_terrain(o); // 5F14 (0393)
    if (o.terrain & 0x02) {
        o.vy = 0x0500;
    }
    if (hi(o.vy) != 0x06) {
        o.vy = std::uint16_t(o.vy + 0x20);
    }
    keep_in_view(o); // B410
    face_input(o);   // B7B6
    o.flags |= 0x02;
    if (probe.landed_attribute || !o.vx) {
        return;
    }
    if (o.status & 0x01) {
        o.status &= u8(~0x01);
        o.contact &= 0xF3;
    }
}

// BE71: the buttons held (not while status bit 3).
void Game::shell_ride_input(Object& o) {
    if (!(o.status & 0x08)) {
        const u8 held = input.held & 0x32;
        if (held == 0x12 || held == 0x22) {
            shell_ride_speed_up(o); // BE94
            return;
        }
        o.scratch[ride_sound] = 0;
        // BFA9: a single button jumps off (not right after speeding up):
        // the shell slides on (its byte 36 FF).
        if ((held == 0x10 || held == 0x20) && !o.scratch[ride_speeding]) {
            if (player.shell) {
                objects[player.shell - 1u].scratch[2] = 0xFF;
            }
            player.shell = 0;
            jump(o); // 3AD6 (0402)
            return;
        }
    }
    // BED9: coasting.
    if ((o.terrain & 0x02) && probe.landed_attribute) { // BF23
        o.vx = std::uint16_t(o.vx + ((probe.landed_attribute & 0x02) ? 0xFFF0 : 0x0010));
    }
    if (o.scratch[ride_speeding]) {
        o.scratch[ride_speeding] = 0;
        o.counter = 0;
        o.frame_ticks = 0x01;
    }
    if (o.vx == 0x0008 || o.vx == 0xFFF8) {
        o.vx = 0;
    }
}

// BE94: by 1/16 the way he faces, up to 6 (only while slower); the sound
// (BA) once per press.
void Game::shell_ride_speed_up(Object& o) {
    const std::uint16_t vx = o.vx;
    if (!(o.display & display_flag::mirrored)) {
        if ((vx & 0x8000) || vx <= 0x0600) {
            o.vx = std::uint16_t(vx + 0x0010);
        }
    } else if (!(vx & 0x8000) || vx > 0xFA00) {
        o.vx = std::uint16_t(vx - 0x0010);
    }
    o.scratch[ride_speeding] = 0xFF;
    if (o.scratch[ride_sound]) {
        return;
    }
    o.scratch[ride_sound] = 0xFF;
    sound_.request(0xBA, 1);
}

} // namespace triple_trouble_gg
