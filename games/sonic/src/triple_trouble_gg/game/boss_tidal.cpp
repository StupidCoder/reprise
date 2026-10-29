// Tidal Plant's boss (zone 4 act 3; type 54, bank 17, A292-A841). The
// machine (parameter 0) waits for the player (1), comes in from the right
// (2) and then works through a long dance (3, script A2E0-A442): bobbing,
// rising, sinking, throwing debris (parameters 3-6: states 4-7) and a
// bubble (type 3B, 10), while the arena scrolls left and pushes the player
// (A7DE: every 32 steps a drop, parameter 9: state A). A hit (a rolling
// player's touch, or byte 30 set by something it touched) costs one of 16
// hits (24 against Tails), pauses it (8) and resumes the dance where it
// was (its script position kept in 38). Beaten (9): the background fades
// to white, it blows up and sinks, and the player runs off to the act's
// end once on the ground.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Script positions the boss moves to.
constexpr std::uint16_t dance_start = 0xA2CC, dance_resume = 0xA2E4, dance_rise = 0xA3D6;
constexpr std::uint16_t shard_roll = 0xA451, sink_end = 0xA511;
} // namespace

// The address of the object's next script step (what the original keeps
// in bytes 0E-0F).
std::uint16_t Game::step_address(const Object& o) const {
    const auto& steps = scripts_.script(o.script.type, o.script.state, o.script.bank);
    return o.script.step < steps.size() ? steps[o.script.step].source : 0;
}

bool Game::run_tidal_boss_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::tidal_start: { // A547: tiles 62; the state is the parameter + 1
        o.tile_base = 0x62;
        const u8 state = u8(o.param + 1);
        o.param = 0;
        o.next_state = state;
        if (state != 0x01) {
            return true;
        }
        o.display |= 0x02;
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = 0x19;
        o.timer = 0x20;
        o.scratch[2] = 0x02;
        o.status_ticks = objects[0].type == 0x01 ? 0x10 : 0x18;
        o.scratch[0] = 0;
        o.scratch[5] = 0;
        o.scratch[4] = 0;
        act.variant = 0xFF;
        return true;
    }
    case Routine::tidal_wait: // A599: the player past X 250: in from 303 (2), the camera to (233,
                              // FC), the boss song
        if (objects[0].x < 0x0250) {
            return true;
        }
        o.x = 0x0303;
        o.next_state = 0x02;
        script_camera(0x0233, 0x00FC); // 4CDA (0321)
        sound_.request(0x8A, 2);
        act.boss = u8(slot_number(o) + 1);
        return true;
    case Routine::tidal_variant: // A5C5
        act.variant = 0x05;
        return true;
    case Routine::tidal_enter: // A5CB: moving in; at X 2E0 the dance starts (script A2CC)
        move_object(o);        // 5359 (0381)
        if (o.x < 0x02E0) {
            jump_script(o, dance_start);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::tidal_dance_start: { // A644: still; resuming where a hit stopped it (38)
        o.vx = 0;
        o.floor_off = 0x08;
        const std::uint16_t resume = std::uint16_t(o.scratch[4] | o.scratch[5] << 8);
        if (resume) {
            jump_script(o, resume);
            o.frame_ticks = 0x01;
        }
        return true;
    }
    case Routine::tidal_frame: // A821
        tidal_frame(o);
        return true;
    case Routine::tidal_move: // A79B
        move_object(o);
        tidal_hit_check(o);
        return true;
    case Routine::tidal_hit_check: // A79E
        tidal_hit_check(o);
        return true;
    case Routine::tidal_rise: // A5E8: rising to Y 100, then the next part (script A3D6)
        if (o.y < 0x0100) {
            jump_script(o, dance_rise);
            o.frame_ticks = 0x01;
            tidal_hit_check(o);
            return true;
        }
        move_object(o);
        tidal_scroll(o);
        return true;
    case Routine::tidal_sink: // A60B: sinking to Y 160, then the dance again (script A2E4)
        if (o.y >= 0x0160) {
            jump_script(o, dance_resume);
            o.frame_ticks = 0x01;
            o.vy = 0;
            o.floor_off = 0x08;
            o.angle = 0;
            tidal_hit_check(o);
            return true;
        }
        move_object(o);
        tidal_scroll(o);
        return true;
    case Routine::tidal_pause: // A72C: no hits left: beaten (9), 12 higher
        if (o.status_ticks) {
            return true;
        }
        o.next_state = 0x09;
        o.y = std::uint16_t(o.y - 0x0C);
        return true;
    case Routine::tidal_scroll: // A7DE
        tidal_scroll(o);
        return true;
    case Routine::tidal_flash_bg: // A746: the background's fade to palette 3
        palette.fades[0].mode |= 0x20;
        palette.fades[0].palette = 0x03;
        return true;
    case Routine::tidal_white: // A74F: the background fades to white (29)
        palette.fade_period = 0x02;
        palette.fades[0] = {0x90, 0x29};
        return true;
    case Routine::tidal_sink_away: // A761: below the screen (camera + A0): waiting for the player
                                   // (script A511)
        move_object(o);
        if (std::uint16_t(camera.y + 0x00A0) < o.y) {
            jump_script(o, sink_end);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::tidal_done: { // A782: the player on the ground (or in 48): the act cleared (3E5C)
        const u8 s = objects[0].state;
        if (s != 0x48 && !(objects[0].terrain & 0x02)) {
            return true;
        }
        if (s == 0x20 || s == 0x49) {
            return true;
        }
        run_off_to_clear(); // 3E5C (03C0)
        return true;
    }
    case Routine::tidal_debris_fall: // A663: falling (1/64), harmful; touched it bursts, on the
                                     // floor it rolls (script A451)
        o.vy = std::uint16_t(o.vy + 0x04);
        move_object(o);
        harmful_touch(o); // 550E (0375)
        if (o.touch_sides & 0x0F) {
            o.param = 0;
            sound_.request(0xC1, 1);
            become_explosion(o); // 51D0 (04F2)
            return true;
        }
        if (probe_flags(o, 0, 0x0010) & 0x80) { // 709A (0387)
            jump_script(o, shard_roll);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::tidal_debris_roll: // A6A5: rolling off (faster left by 1/16, up by 1/64); off
                                     // screen gone
        if (o.display & display_flag::offscreen) {
            o.type = 0xFF;
            return true;
        }
        o.vx = std::uint16_t(o.vx - 0x10);
        o.vy = std::uint16_t(o.vy - 0x04);
        move_object(o);
        tidal_shard(o);
        return true;
    case Routine::tidal_shard_fly: // A6D2: falling (3/32)
        o.vy = std::uint16_t(o.vy + 0x18);
        tidal_shard(o);
        return true;
    case Routine::tidal_drop_tiles: // A707
        o.tile_base = 0;
        return true;
    case Routine::tidal_drop_fall: // A70C: falling (1/16) to the ground, then gone
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o);
        object_terrain(o); // 713D (0384)
        if (o.terrain & 0x02) {
            o.type = 0xFF;
        }
        return true;
    case Routine::tidal_gone: // A727
        o.type = 0xFF;
        return true;
    default:
        return false;
    }
}

// A821: the next frame: 8, or (the player hurt or dying) 4 and 5 by turns,
// for 8 updates.
void Game::tidal_frame(Object& o) {
    ++o.speed;
    o.frame_ticks = 0x08;
    u8 f = 0x08;
    const u8 s = objects[0].state;
    if (s == 0x1F || s == 0x1E) {
        f = u8(0x04 + (o.speed & 0x01));
    }
    o.frame = f;
}

// A79E: unless just hit (35 counts 16 frames): a hit when byte 30 is set
// (sound C1) or a rolling player touches it (he bounces off): a hit less,
// the pause (8), its place in the dance kept.
void Game::tidal_hit_check(Object& o) {
    if (o.scratch[1]) {
        --o.scratch[1];
        tidal_scroll(o);
        return;
    }
    if (o.surface) {
        sound_.request(0xC1, 1);
    } else {
        touch_test(o); // 58AC (0369)
        if (!(o.touch_sides & 0x0F)) {
            tidal_scroll(o);
            return;
        }
        bounce_off(o); // 561B (0438)
    }
    o.surface = 0;
    o.scratch[1] = 0x10;
    --o.status_ticks;
    o.next_state = 0x08;
    const std::uint16_t at = step_address(o);
    o.scratch[4] = lo(at);
    o.scratch[5] = hi(at);
    tidal_scroll(o);
}

// A7DE: every other frame the arena scrolls a pixel left and pushes the
// player (from screen X 44); every 32 such steps a drop falls (parameter
// 9) from 24 right of and above it.
void Game::tidal_scroll(Object& o) {
    if (--o.scratch[2]) {
        return;
    }
    o.scratch[2] = 0x02;
    --camera.scroll_x;
    if (lo(objects[0].screen_x) < 0x44) {
        return;
    }
    --objects[0].x;
    if (++o.counter < 0x20) {
        return;
    }
    o.counter = 0;
    spawn_object_at(o, 0x54, 0x09, std::uint16_t(o.x + 0x18),
                    std::uint16_t(o.y - 0x18)); // 510A (0357)
}

// A6E2: moving, harmful; touched or landing it bursts (sound C1).
void Game::tidal_shard(Object& o) {
    move_object(o);
    harmful_touch(o);
    if (!(o.touch_sides & 0x0F)) {
        object_terrain(o); // 713D (0384)
        if (!(o.terrain & 0x02)) {
            return;
        }
    }
    o.param = 0;
    sound_.request(0xC1, 1);
    become_explosion(o); // 51D0 (04F2)
}

} // namespace triple_trouble_gg
