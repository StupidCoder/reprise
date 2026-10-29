// The player (Sonic, or Tails as the player): the state routines of his
// scripts (bank 0C), his movement (B05B) and animation (AC6E-ADB7).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Bank 0C tables: acceleration per state (4 bytes: left, right) when moving
// right (B448) or left (B4E4), friction (B580), the same underwater (B61C,
// B69C); slope forces by the floor's landing attribute (B79C); animation
// timings by speed (ACC1, AD5A); the jump's frames (AD32 Sonic, AD46
// Tails).
constexpr Table accel_right{0x0C, 0xB448}, accel_left{0x0C, 0xB4E4}, friction{0x0C, 0xB580};
constexpr Table water_accel_right{0x0C, 0xB61C}, water_accel_left{0x0C, 0xB69C};
constexpr Table slope_forces{0x0C, 0xB79C};
constexpr Table spin_timing{0x0C, 0xAD5A};
constexpr Table sonic_spin_frames{0x0C, 0xAD32}, tails_spin_frames{0x0C, 0xAD46};

std::uint16_t negate(std::uint16_t v) {
    return std::uint16_t(-v);
}

std::uint16_t magnitude(std::uint16_t v) {
    return (v & 0x8000) ? negate(v) : v;
}

u8 high_magnitude(std::uint16_t v) {
    const u8 h = hi(v);
    return (h & 0x80) ? u8(-h) : h;
}
} // namespace

bool Game::held(u8 bits) const {
    return input.held & bits;
}

// 0C:97BC in an act: top speed 4; the side probes (988E); the state
// after the parameter (+1); acts with special starts.
void Game::player_start_in_act(Object& o) {
    player.top_speed = 0x0400;
    set_player_box(); // 988E
    o.next_state = u8(o.param + 1);
    if (o.next_state == 0x3B || o.next_state == 0x47) {
        return;
    }
    if (flow.zone == 1 && flow.act == 2) {
        o.next_state = 0x2F;
        return;
    }
    if (flow.zone == 4 && flow.act == 2 && o.type != 1) {
        o.next_state = 0x48;
        return;
    }
    if (flow.zone == 1 && flow.act == 1) {
        camera.max_x = 0x0F00;
        if (!flow.restart_point && !flow.special_entry) {
            o.next_state = 0x17;
        }
    }
}

// 988E: the side probes at (-9, -C) and (9, -C) for both characters.
void Game::set_player_box() {
    player_box.left_x = 0xFFF7;
    player_box.left_y = 0xFFF4;
    player_box.right_x = 0x0009;
    player_box.right_y = 0xFFF4;
}

// 98CE: the camera aims at the player 78 lines down (38 in zone 1 act 3).
void Game::aim_camera_at_player() {
    camera.aim_y = (flow.zone == 1 && flow.act == 2) ? 0x38 : 0x78;
}

// 98E8 (state 1): standing. Moving or a direction starts walking; Up looks
// up, Down crouches (on the floor or on an object).
void Game::player_standing(Object& o) {
    aim_camera_at_player();
    player_move(o); // B05B
    if (o.next_state != 0x01) {
        return;
    }
    if (o.vx || held(0x0C)) {
        to_walking(o); // 03F6
        return;
    }
    if (o.status & 0x20) {
        return;
    }
    if (!o.touch && !(o.terrain & 0x02)) {
        return;
    }
    if (held(0x01)) {
        to_looking_up(o); // 03F9
    } else if (held(0x02)) {
        to_crouching(o); // 03FC
    }
}

// 9945 (state 3): looking up (the camera aims lower, A2); a jump button:
// Sonic's peel out (040B), Tails' flight (0408, not in water).
void Game::player_looking_up(Object& o) {
    if (!player.carried) {
        camera.aim_y = 0xA2;
    }
    if (o.type == 1) {
        if (input.pressed & 0x30) {
            to_peel_out(o); // 040B
            return;
        }
    } else if (!(o.flags & 0x04) && (input.pressed & 0x30)) {
        tails_take_off(o); // 3C91 (0408; nothing more this frame, taken off or not)
        return;
    }
    player_move(o);
    if (o.next_state != 0x03) {
        return;
    }
    if (held(0x0C)) {
        to_walking(o);
    } else if (held(0x02)) {
        to_crouching(o);
    } else if (!held(0x01)) {
        to_standing(o); // 040E
    }
}

// 998C (state 4): crouching (the camera aims higher, 30); a jump button
// charges a spin dash (0414).
void Game::player_crouching(Object& o) {
    if (!player.carried) {
        camera.aim_y = 0x30;
    }
    if (input.pressed & 0x30) {
        to_spin_dash(o); // 0414
        return;
    }
    player_move(o);
    if (o.next_state != 0x04) {
        return;
    }
    if (held(0x01)) {
        to_looking_up(o);
    } else if (!held(0x02)) {
        to_standing(o);
    }
}

// 99B6 (state 5): walking. Down rolls (when not slow), top speed runs;
// slowing down to a stop stands (9A04); turning against the motion skids
// (9A5B).
void Game::player_walking(Object& o) {
    if (o.frame == 0x1A) {
        o.vx = 0; // pushing a wall
    }
    aim_camera_at_player();
    player_move(o);
    if (o.next_state != 0x05) {
        return;
    }
    face_input(o); // B7B6
    if (u8(hi(o.vx) + 1) >= 2 && held(0x02)) {
        to_rolling(o); // 0423
        return;
    }
    if (!(o.flags & 0x04) && hi(player.top_speed) == hi(magnitude(o.vx))) {
        to_running(o); // 0426
        return;
    }
    stop_when_slow(o); // 9A04 (its state change does not end the routine)
    skid_check(o);     // 9A5B
}

// 9A04: no direction held and under 20/256 pixels per frame with little
// slope force: standing.
void Game::stop_when_slow(Object& o) {
    if (held(0x0C)) {
        return;
    }
    const std::uint16_t v = magnitude(o.vx);
    if ((lo(v) & 0xE0) | hi(v)) {
        return;
    }
    u8 a = lo(player.slope_force);
    if (a & 0x80) {
        a = u8(-a);
    }
    if (a < 0x10) {
        to_standing(o);
    }
}

// 9A2C: in the air or rolling: pressing against the motion skids; at a
// stop, standing (or crouching with Down).
void Game::skid_or_stop(Object& o) {
    if (hi(o.vx) & 0x80) {
        if (held(0x08)) {
            to_skid_8(o); // 041D
            return;
        }
    } else if (held(0x04)) {
        to_skid_7(o); // 0420
        return;
    }
    if (high_magnitude(o.vx)) {
        return;
    }
    if (held(0x02)) {
        to_crouching(o);
    } else {
        to_standing(o);
    }
}

// 9A5B: against the motion (faster than one pixel per frame): a skid.
void Game::skid_check(Object& o) {
    if (o.status & 0x02) {
        return;
    }
    const u8 h = hi(o.vx);
    if (!h || h == 0xFF) {
        return;
    }
    if (!(h & 0x80)) {
        if (held(0x04)) {
            to_skid_7(o);
        }
    } else if (held(0x08)) {
        to_skid_8(o);
    }
}

// 9A7C (state 6): running. Down rolls; under 4 pixels per frame walking.
void Game::player_running(Object& o) {
    if (o.contact & 0x0C) {
        o.vx = 0;
    }
    aim_camera_at_player();
    player_move(o);
    if (o.next_state != 0x06) {
        return;
    }
    if (held(0x02)) {
        to_rolling(o);
        return;
    }
    if (high_magnitude(o.vx) < 4) {
        to_walking(o);
        return;
    }
    skid_check(o);
}

// 9AAE / 9AD9 (states 7 and 8): skidding (facing right / left); a wall,
// a stop or the direction matching the motion end it.
void Game::player_skidding(Object& o, bool facing_left) {
    if (o.contact & (facing_left ? 0x04 : 0x08)) {
        to_walking(o);
        return;
    }
    player_move(o);
    if (o.next_state != (facing_left ? 0x08 : 0x07)) {
        return;
    }
    stop_when_slow(o);
    if (facing_left) {
        o.display |= display_flag::mirrored;
    } else {
        o.display &= u8(~display_flag::mirrored);
    }
    const u8 moving_left = hi(o.vx) & 0x80;
    const u8 right_held = held(0x08) ? 0x80 : 0x00;
    if (moving_left == right_held) {
        return;
    }
    to_walking(o);
}

// 9B04 (state 9): rolling.
void Game::player_rolling(Object& o) {
    player_move(o);
    if (o.next_state != 0x09) {
        return;
    }
    skid_or_stop(o);
}

// 9B40 (state 0A): jumping: held for up to 13 frames the jump keeps its
// starting speed (higher with status bit 6, lower under water); landing
// walks (unless on block kind 0D).
void Game::player_jumping(Object& o) {
    if (!(input.held & 0x30)) {
        player.jump_ticks = 0x20;
    } else if (++player.jump_ticks < 0x0E) {
        std::uint16_t v = (o.flags & 0x04) ? 0xFCC0 : 0xFBC0;
        if (o.status & 0x40) {
            v = (o.flags & 0x04) ? 0xFE00 : 0xFD00;
        }
        o.vy = v;
    }
    player_move(o);
    if (o.next_state != 0x0A) {
        return;
    }
    if (!(o.contact & 0x02)) {
        return;
    }
    if (terrain_state.flags != 0x0D) {
        to_walking(o);
    }
}

// A509 (state 24): Sonic's spin in the air (the jump button again, 3E7A):
// landing walks; otherwise moving, facing the direction held. The script
// falls on after two turns (3B62).
void Game::player_air_spin(Object& o) {
    if (o.contact & 0x02) {
        to_walking(o); // 03F6
        return;
    }
    player_move(o);
    if (o.next_state == 0x24) {
        face_input(o);
    }
}

// State 3B: the player on the title card at (80, 90) runs on after
// the card's three name table animations start (the zone's two, then 4).
void Game::card_run_start(Object& o) {
    o.x = 0x0080;
    o.y = 0x0090;
    start_map_animation(u8(flow.zone * 2 + 5)); // 23A3 (044D)
}

// 83CC (state 1E): thrown back by a hit: up at 4, 4 pixels up, off the
// floor; a shell (types 22, 2D) he rode drops away (up at 2, its timer
// FF) and a holder (type 1B) lets go (state 2).
void Game::hurt_start(Object& o) {
    o.vy = 0xFC00;
    o.terrain &= u8(~0x02);
    o.contact &= u8(~0x02);
    o.y = std::uint16_t(o.y - 4);
    if (player.shell) {
        auto& s = objects[player.shell - 1u];
        if (s.type == 0x22 || s.type == 0x2D) {
            s.next_state = 0x02;
            s.timer = 0xFF;
            s.vy = 0xFE00;
            player.shell = 0;
        }
    }
    if (!player.holder) {
        return;
    }
    auto& h = objects[player.holder - 1u];
    if (h.type != 0x1B) {
        return;
    }
    h.next_state = 0x02;
    player.holder = 0;
}

// 9CD6: thrown back: the jump buttons (A4D9), gravity 0.16 up to 5 pixels
// per frame, kept below the screen's top; landing walks.
void Game::player_hurt(Object& o) {
    jump_in_air(o); // A4D9
    if ((o.vy & 0x8000) || o.vy < 0x0500) {
        o.vy = std::uint16_t(o.vy + 0x28);
    }
    move_object(o);         // 5359 (0381)
    keep_in_view(o);        // B410
    aim_camera_at_player(); // 98CE
    player_terrain(o);      // 5F14 (0393)
    if (o.contact & 0x02) {
        to_walking(o); // 03F6
    }
}

// 843C / 8434 (states 1F, 28): dying: up at 3 (2.5 drowned), no sideways
// speed, shown, untouchable (flags bit 6); the camera stops (0312).
void Game::death_start(Object& o, std::uint16_t vy) {
    o.vy = vy;
    o.vx = 0;
    o.display &= u8(~(display_flag::hidden | display_flag::flicker));
    o.flags |= 0x40;
}

// A18D (state 1F): falling through the act (gravity 1/16) until 208 lines
// below the camera's top: the act ends with the life lost (D144 bit 2).
// Under water the drowning script takes over (Sonic 845D, Tails 9128).
void Game::player_dead(Object& o) {
    o.flags |= 0x08;
    if (o.flags & 0x04) {
        jump_script(o, objects[0].type == 0x01 ? 0x845D : 0x9128);
        o.frame_ticks = 0x01;
        return;
    }
    o.vy = std::uint16_t(o.vy + 0x10);
    move_object(o);
    dead_below_screen(o);
}

// A1D3 (state 28): drowned: sinking slower (gravity 1/32), shown,
// untouchable, a bubble (type 24, parameter 3) every 16 frames outside
// zone 2.
void Game::player_drowned(Object& o) {
    o.flags |= 0x08;
    o.display &= u8(~display_flag::hidden);
    o.flags |= 0x40;
    if ((video.frame_counter & 0x0F) == 0x0F && flow.zone != 2) {
        const auto& p = objects[0];
        spawn_object_at(o, 0x24, 0x03, p.x, std::uint16_t(p.y - 0x10)); // 510A (0357)
    }
    o.vy = std::uint16_t(o.vy + 0x08);
    move_object(o);
    dead_below_screen(o);
}

// A1B9 / A20E: falling, 208 lines below the camera's top the act ends.
void Game::dead_below_screen(Object& o) {
    if (o.vy & 0x8000) {
        return;
    }
    if (std::uint16_t(camera.y + 0xD0) >= objects[0].y) {
        return;
    }
    flow.frame_work |= 0x04;
}

// State 31 (floating up from a kind 8 block, 3EE3): its frames by zone,
// from each character's table (Sonic 866E: 8685, zone 1 8653, others 8639;
// Tails 938E: 93A5, zone 1 9373, others 9359).
void Game::float_start(Object& o) {
    const Table frames_by_zone{0x0C, std::uint16_t(o.type == 0x01 ? 0x8685 : 0x93A5)};
    jump_script(o, rom.word(frames_by_zone, flow.zone * 2u));
    o.frame_ticks = 0x01;
}

// A51C: drifting with gravity 1/8 (while not falling at 4 or more), the
// jump buttons; landing, touching something below or an object, or
// standing on a platform ends it (040E).
void Game::player_float(Object& o) {
    jump_in_air(o);        // A4D9
    tails_flight_start(o); // A4E6
    if ((o.vy & 0x8000) || hi(o.vy) < 0x04) {
        o.vy = std::uint16_t(o.vy + 0x20);
    }
    keep_in_view(o);   // B410
    move_object(o);    // 5359 (0381)
    player_terrain(o); // 5F14 (0393)
    if ((o.terrain & 0x02) || (o.touch_sides & 0x22) || player.carried || o.touch) {
        to_standing(o); // 040E
    }
}

// A562 / A570 (zone 1): held still, then 48 frames of slow rising
// (gravity 1/16, the jump buttons) before falling (03B7).
void Game::float_hold(Object& o) {
    o.vx = 0;
    o.vy = 0;
    o.counter = 0x30;
}

void Game::player_float_short(Object& o) {
    jump_in_air(o);
    tails_flight_start(o);
    o.vy = std::uint16_t(o.vy + 0x10);
    keep_in_view(o);
    move_object(o);
    if (!--o.counter) {
        to_falling(o); // 03B7
        return;
    }
    player_terrain(o);
}

// 84FF (state 20, 49): the act cleared, the player runs off to the right
// (speeding up by 1/16 to 6, shown, no fall): past X F8 on the screen the
// camera stops (0312); past 120 the act's loop ends (D144 bit 4).
void Game::run_off(Object& o) {
    o.display &= u8(~(display_flag::hidden | display_flag::mirrored));
    o.vy = 0;
    const std::uint16_t sx = std::uint16_t(o.x - camera.x);
    if (sx > 0x00F8) {
        disable_map(); // 4CCE (0312)
        // Widescreen: until he has left the presented view.
        if (sx > 0x0120 + wide_right) {
            o.vx = 0;
            flow.frame_work |= 0x10;
            return;
        }
    }
    if (o.state != 0x49) {
        if (o.vx & 0x8000) {
            o.vx = 0;
        }
        if (hi(o.vx) < 0x06) {
            o.vx = std::uint16_t(o.vx + 0x10);
        }
    }
    move_object(o); // 5359 (0381)
}

// State 44 (after a boss, 3F53): the player stops (A8D2), faces right
// (A915) and, by zone (table A8FD Sonic, A909 Tails), runs off right
// (A91A: speeding up by 1/16 to 4; at X 760 he stands, script 8B68 / 961C;
// zone 2 and zones 3-5 have their own runs, A95A and A993).
void Game::victory_start(Object& o) {
    o.display &= u8(~display_flag::mirrored);
    o.vx = 0;
    const Table scripts{0x0C, std::uint16_t(objects[0].type == 0x01 ? 0xA8FD : 0xA909)};
    jump_script(o, rom.word(scripts, flow.zone * 2u));
    o.frame_ticks = 0x01;
}

void Game::victory_run(Object& o) {
    aim_camera_at_player(); // 98CE
    if (o.vx <= 0x0400) {
        o.vx = std::uint16_t(o.vx + 0x10);
    }
    move_horizontally_only(o); // 539A (0462)
    if (o.x < 0x0760) {
        return;
    }
    jump_script(o, objects[0].type == 0x01 ? 0x8B68 : 0x961C);
    o.frame_ticks = 0x01;
}

// State 22: a pipe's chamber (exit block): BD69 notes which ways are
// pipes (byte 3E: right, left, down, up as bits 3-0; probes 32 across, 16
// down, 32 up); BD4D: a direction pressed that way sets off (state 21).
void Game::pipe_chamber(Object& o) {
    o.placement = 0;
    const auto open = [&](std::uint16_t dx, std::uint16_t dy) {
        probe_terrain(o, dx, dy); // 6FA6 (03FF)
        return (probe.flags & 0x1F) == 0x13;
    };
    const bool right = open(0x0020, 0xFFFC), left = open(0xFFE0, 0xFFFC);
    const bool down = open(0x0000, 0x0010), up = open(0x0000, 0xFFE0);
    o.placement = u8(right << 3 | left << 2 | down << 1 | u8(up));
}

void Game::pipe_choose(Object& o) {
    const u8 ways = input.pressed & o.placement;
    if (!ways) {
        return;
    }
    o.next_state = 0x21;
    if (ways & 0x01) {
        o.vx = 0, o.vy = 0xFA80, o.placement = 0xB6; // 740F
    } else if (ways & 0x02) {
        o.vx = 0, o.vy = 0x0580, o.placement = 0xB7; // 73E7
    } else if (ways & 0x04) {
        o.vx = 0xFA80, o.vy = 0, o.placement = 0xB9; // 7437
    } else {
        o.vx = 0x0580, o.vy = 0, o.placement = 0xB8; // 745F
    }
}

// BDC6 (state 40): out of a pipe, facing the way it moves; in zone 5,
// unless moving down, its own script (85A4 / 91FB).
void Game::pipe_out(Object& o) {
    if (o.vx & 0x8000) {
        o.display |= display_flag::mirrored;
    } else {
        o.display &= u8(~display_flag::mirrored);
    }
    if (flow.zone != 5) {
        return;
    }
    const u8 h = hi(o.vy);
    if (h && !(h & 0x80)) {
        return;
    }
    o.frame_ticks = 0x01;
    jump_script(o, objects[0].type == 0x01 ? 0x85A4 : 0x91FB);
}

// BDF9 (zone 5, out of a pipe): flying on (rolling), kept below the
// screen's top; moving sideways the walls stop it, otherwise the floor
// (falling) or the ceiling (rising); touching any ends in a roll (3D99).
void Game::pipe_fly(Object& o) {
    keep_in_view(o); // B410
    o.flags |= 0x02;
    move_object(o); // 5359 (0381)
    player_head(o); // 6E16 (038D)
    if (hi(o.vx)) {
        player_walls(o); // 6780 (0390)
        if (o.terrain & 0x0C) {
            to_bounced_roll(o); // 3D99 (03BD)
        }
        return;
    }
    if (o.vy & 0x8000) {
        player_ceiling(o); // 6A92 (04EC)
    } else {
        player_floor(o); // 5F53 (038A)
    }
    if (o.terrain & 0x03) {
        to_bounced_roll(o);
    }
}

// State 17: an act's entry (zone 1 act 2): the player drops in (A590),
// slides along a chute (A5C4), jumps off its end (A60B) and lands
// (A64F). Each part ends by moving the script to the next one's frames
// (Sonic 835D, 8368, 8373; Tails 90A7, 90B2, 90BD).
void Game::entry_drop(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x20);
    move_object(o);    // 5359 (0381)
    object_terrain(o); // 713D (0384)
    if (!(o.terrain & 0x02)) {
        return;
    }
    o.frame_ticks = 0x01;
    o.vy = 0x0600;
    jump_script(o, o.type == 0x01 ? 0x835D : 0x90A7);
}

// A5C4: speeding up to 6 pixels per frame; on block kind 17, past the
// middle of the block, up at 5.
void Game::entry_slide(Object& o) {
    player_head(o); // 6E16 (038D)
    if (hi(o.vx) < 0x06) {
        o.vx = std::uint16_t(o.vx + 0x40);
    }
    move_object(o);
    object_terrain(o);
    if ((probe.flags & 0x1F) != 0x17 || (o.x & 0x1F) < 0x10) {
        return;
    }
    o.frame_ticks = 0x01;
    o.vy = 0xFB00;
    jump_script(o, o.type == 0x01 ? 0x8368 : 0x90B2);
}

// A60B: through the air (gravity 24/256) until the block 16 pixels ahead
// is solid: then backward slowly.
void Game::entry_jump(Object& o) {
    player_head(o);
    o.vy = std::uint16_t(o.vy + 0x24);
    move_object(o);
    if (!(probe_flags(o, 0x0010, 0x0000) & 0x80)) { // 709A (0387)
        return;
    }
    o.frame_ticks = 0x01;
    o.vy = 0;
    o.vx = 0xFF40;
    jump_script(o, o.type == 0x01 ? 0x8373 : 0x90BD);
}

// A64F: falling (gravity 1/16) until landing, then the usual fall (03B7).
void Game::entry_land(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x10);
    move_object(o);
    object_terrain(o);
    if (o.terrain & 0x02) {
        to_falling(o); // 3B47 (03B7)
    }
}

// 9BAB (state 1C): sprung diagonally: the jump buttons (A4D9); landing
// walks; no longer rising, falling (03B7).
void Game::player_sprung_diagonally(Object& o) {
    jump_in_air(o); // A4D9
    player_move(o); // B05B
    if (o.next_state != 0x1C) {
        return;
    }
    if (o.terrain & 0x02) {
        to_walking(o); // 03F6
        return;
    }
    if (!(o.vy & 0x8000)) {
        to_falling(o); // 03B7
    }
}

// 9B10 (state 1B): bounced away rolling: on the floor a jump button jumps
// (03BA); slower than 1/4 pixel per frame he stands (040E).
void Game::player_bounced(Object& o) {
    player_move(o); // B05B
    if (o.next_state != 0x1B) {
        return;
    }
    if (!(o.contact & 0x02)) {
        return;
    }
    o.flags &= u8(~0x01);
    if (input.pressed & 0x30) {
        to_jumping(o); // 03BA
        return;
    }
    const std::uint16_t speed = (o.vx & 0x8000) ? std::uint16_t(-o.vx) : o.vx;
    if (!((lo(speed) & 0xC0) | hi(speed))) {
        to_standing(o); // 040E
    }
}

// A165 (state 19): balancing on a ledge: moving or left/right walks, Up
// looks up, Down crouches.
void Game::player_balancing(Object& o) {
    aim_camera_at_player(); // 98CE
    player_move(o);         // B05B
    if (o.next_state != 0x19) {
        return;
    }
    if (o.vx || (input.held & 0x0C)) {
        to_walking(o); // 03F6
        return;
    }
    if (input.held & 0x01) {
        to_looking_up(o); // 03F9
        return;
    }
    if (input.held & 0x02) {
        to_crouching(o); // 03FC
    }
}

// 9B8E (state 0B): sprung into the air: the jump buttons (A4D9); landing
// walks; falling (no longer rising) is state 0E.
void Game::player_sprung(Object& o) {
    jump_in_air(o); // A4D9
    player_move(o);
    if (o.next_state != 0x0B) {
        return;
    }
    if (o.terrain & 0x02) {
        to_walking(o);
        return;
    }
    if (!(o.vy & 0x8000)) {
        to_falling(o); // 03B7
        return;
    }
    face_input(o);
}

// 9C52 (state 0F): charging a spin dash (not moving, a wall pushes back 4
// pixels); letting go of Down releases it (0411).
void Game::player_charging(Object& o) {
    o.display &= u8(~display_flag::hidden);
    aim_camera_at_player();
    player_terrain(o); // 0393
    if (o.contact & 0x08) {
        o.x = std::uint16_t(o.x + 4);
    } else if (o.contact & 0x04) {
        o.x = std::uint16_t(o.x - 4);
    }
    keep_on_screen(o); // B42C
    if (!held(0x02)) {
        release_spin_dash(o); // 0411
    }
}

// 9BC6 (state 15): the peel out winding up (not moving, a wall pushes back
// 4); a jump button only makes the sound again; letting go of Up releases
// it (3C12).
void Game::player_peeling(Object& o) {
    aim_camera_at_player();
    player_terrain(o); // 0393
    if (o.contact & 0x08) {
        o.x = std::uint16_t(o.x + 4);
    } else if (o.contact & 0x04) {
        o.x = std::uint16_t(o.x - 4);
    }
    keep_on_screen(o); // B42C
    if (input.pressed & 0x30) {
        sound_.request(0xBA, 1);
    }
    if (!held(0x01)) {
        release_peel_out(o); // 3C12 (0429)
    }
}

// 9C06 (state 1A): the peel out's run: untouchable while D3C2 lasts (set
// to 30 by 8243); against a wall it stops; slower than 4 (or at a wall)
// walking, otherwise as when rolling (9A2C).
void Game::player_peel_run(Object& o) {
    if (!player.hurt_pending) {
        o.flags &= u8(~0x82);
    }
    if (o.contact & 0x0C) {
        o.vx = 0;
    }
    aim_camera_at_player();
    player_move(o);    // B05B
    keep_on_screen(o); // B42C
    if (o.next_state != 0x1A) {
        return;
    }
    if (o.terrain & 0x0C) {
        o.flags &= u8(~0x82);
        to_walking(o); // 03F6
        return;
    }
    if (high_magnitude(o.vx) < 0x04) {
        to_walking(o);
        return;
    }
    skid_or_stop(o); // 9A2C
}

// 9C8A (state 10): the spin dash's roll.
void Game::player_dashing(Object& o) {
    aim_camera_at_player();
    player_move(o);
    keep_on_screen(o);
    if (o.next_state != 0x10) {
        return;
    }
    skid_or_stop(o);
}

// 9C9C (state 0E): falling: the jump buttons (A4D9, Tails A4E6); landing
// walks.
void Game::player_falling(Object& o) {
    jump_in_air(o);
    tails_flight_start(o); // A4E6
    player_move(o);
    if (o.contact & 0x02) {
        to_walking(o);
    }
}

// A4D9: a jump button in the air: the special move (03D5).
void Game::jump_in_air(Object& o) {
    if (!(input.pressed & 0x30)) {
        return;
    }
    o.display &= u8(~display_flag::offscreen);
    special_move(o); // 03D5
}

// A0D7 (state 27): Tails flying (from 90E9: up at 2.5): landing or
// entering water ends it (walking); gravity 1/16 up to 2 pixels per frame
// down; left and right accelerate by 1/16 up to 2; walls stop him.
void Game::tails_fly(Object& o) {
    if ((o.contact & 0x02) || (o.flags & 0x04)) {
        to_walking(o); // 03F6
        return;
    }
    if ((o.vy & 0x8000) || o.vy < 0x0200) {
        o.vy = std::uint16_t(o.vy + 0x10);
    }
    const u8 held = input.held & 0x0C;
    if (held & 0x08) {
        if ((o.vx & 0x8000) || o.vx < 0x0200) {
            o.vx = std::uint16_t(o.vx + 0x10);
        }
    } else if (held & 0x04) {
        if (!(o.vx & 0x8000) || o.vx >= 0xFE00) {
            o.vx = std::uint16_t(o.vx - 0x10);
        }
    }
    // A147
    if ((o.display & display_flag::mirrored) ? (o.terrain & 0x08) : (o.terrain & 0x04)) {
        o.vx = 0;
    }
    move_object(o);    // 5359 (0381)
    face_input(o);     // B7B6
    player_terrain(o); // 5F14 (0393)
    keep_in_view(o);   // B410
}

// 3E9D: Tails flies (state 27) unless low on the screen (Y A0 on).
void Game::start_flight(Object& o) {
    if (o.type != 0x02) {
        return;
    }
    const std::uint16_t y = std::uint16_t(objects[0].y - camera.y);
    if (hi(y) || lo(y) >= 0xA0) {
        return;
    }
    o.next_state = 0x27;
}

// A4E6: Tails (as the player, not under water) starts flying with a jump
// button held.
void Game::tails_flight_start(Object& o) {
    if (player.tails_landing) {
        return;
    }
    if (o.type != 0x02) {
        return;
    }
    if (o.flags & 0x04) {
        return;
    }
    const u8 b = input.held & 0x31;
    if (b == 0x31 || b == 0x11 || b == 0x21) {
        start_flight(o); // 3E9D
    }
}

// A238 (state 18): Tails flying: landing stands, water walks; the flight
// time (D3C7) running out (or rising off blocks A4-A6... falling onto
// them) falls (3B47). Down speeds the descent by 1/16 to 1.125 (3 with the
// sneakers or in state 43), Up climbs by 1/16 to 1 (2), neither lets a
// climb ease by 1/32. Right and left speed up by 1/16 to 1.125 (5 with the
// sneakers or parameter bit 0; 7 in zone 5 act 3, where state 43 always
// flies right and only state 18 may turn left); a wall stops him.
void Game::tails_hover(Object& o) {
    if (o.contact & 0x02) {
        o.vy = 0;
        to_standing(o); // 3A3A (040E)
        return;
    }
    if (o.flags & 0x04) {
        to_walking(o); // 3A8D (03F6)
        return;
    }
    face_input(o); // B7B6
    if (!--player.flight_ticks) {
        to_falling(o); // 3B47 (03B7)
        return;
    }
    if (!(o.vy & 0x8000)) {
        const u8 b = terrain_state.block;
        if (b == 0xA4 || b == 0xA5 || b == 0xA6) {
            to_falling(o);
            return;
        }
    }
    const bool fast = o.power_up == 0x0D || o.state == 0x43;
    const bool final_act = flow.zone == 5 && flow.act == 2;
    const u8 held_now = input.held;
    if (held_now & 0x02) {
        const std::uint16_t limit = fast ? 0x0300 : 0x0120;
        if ((o.vy & 0x8000) || o.vy < limit) {
            o.vy = std::uint16_t(o.vy + 0x10);
        }
    } else if (held_now & 0x01) {
        const std::uint16_t limit = fast ? 0xFE00 : 0xFF00;
        if (!(o.vy & 0x8000) || o.vy >= limit) {
            o.vy = std::uint16_t(o.vy - 0x10);
        }
    }
    if (!(held_now & 0x03) && (o.vy & 0x8000)) {
        o.vy = std::uint16_t(o.vy + 0x08);
    }
    // A30B: across.
    const bool sprint = o.power_up == 0x0D || (o.param & 0x01);
    bool right = false, left = false;
    if (o.state == 0x43 && final_act) {
        right = true;
    } else if (held_now & 0x08) {
        if (o.terrain & 0x04) {
            o.vx = 0;
        } else {
            right = true;
        }
    } else if (held_now & 0x04) {
        if (o.terrain & 0x08) {
            o.vx = 0;
        } else if (o.state == 0x18 || !final_act) {
            left = true;
        }
    }
    if (right) {
        const std::uint16_t limit = sprint ? (final_act ? 0x0700 : 0x0500) : 0x0120;
        if ((o.vx & 0x8000) || o.vx < limit) {
            o.vx = std::uint16_t(o.vx + 0x10);
        }
    } else if (left) {
        const std::uint16_t limit = sprint ? 0xFB00 : 0xFEE0;
        if (!(o.vx & 0x8000) || o.vx >= limit) {
            o.vx = std::uint16_t(o.vx - 0x10);
        }
    }
    keep_in_view(o);        // B410
    move_object(o);         // 5359 (0381)
    player_terrain(o);      // 5F14 (0393)
    aim_camera_at_player(); // 98CE
}

// 3C91 (0408): Tails takes off (state 18: up at 1, 300 frames of flight)
// unless held by object 4D, low on the screen (line A0 on), in the boss
// act's variant 6, standing on a slope (status bit 5, not in zone 3) or on
// blocks A7-A9.
void Game::tails_take_off(Object& o) {
    if (o.touch == 0x4D) {
        return;
    }
    const std::uint16_t y = std::uint16_t(o.y - camera.y);
    if (hi(y) || lo(y) >= 0xA0) {
        return;
    }
    if (act.variant == 0x06) {
        return;
    }
    if (flow.zone != 3 && (o.status & 0x20)) {
        return;
    }
    o.param = 0;
    const u8 b = terrain_state.block;
    if (b == 0xA7 || b == 0xA8 || b == 0xA9) {
        return;
    }
    o.vx = 0;
    o.vy = 0xFF00;
    o.y = std::uint16_t(o.y - 1);
    player.flight_ticks = 0x012C;
    o.flags &= u8(~0x42);
    o.touch_sides &= u8(~0x02);
    o.contact &= u8(~0x02);
    player.carried = 0;
    o.next_state = 0x18;
}

// B7B6: facing the direction held (not while status bit 1).
void Game::face_input(Object& o) {
    if (o.status & 0x02) {
        return;
    }
    const u8 dirs = u8((input.held << 2) & 0x30);
    if (!dirs) {
        return;
    }
    o.display = u8((o.display & 0xEF) | (dirs & 0x10));
}

// B05B: one frame of movement: the controller's acceleration (B2A7), the
// horizontal and vertical speeds (B0F3, B1A3), the terrain (0393), special
// blocks (B093); then a jump button jumps.
void Game::player_move(Object& o) {
    if (o.terrain & 0x02) {
        o.status &= u8(~0x04);
    }
    accelerate(o); // B2A7
    if (o.state < 0x05) {
        player.slope_force = 0;
    }
    move_horizontally(o); // B0F3
    move_vertically(o);   // B1A3
    player_terrain(o);    // 0393
    special_blocks(o);    // B093
    if (o.state == 0x23 || o.state == 0x26) {
        return;
    }
    if (input.pressed & 0x30) {
        to_jumping(o); // 03BA
    }
}

// B2A7: at the screen's edges (38 and C7 pixels from the camera) the
// player is held (B3D3); otherwise the acceleration for the state and the
// direction held (doubled under 80/256 pixels per frame), or friction
// without one; and the slope force of the floor. Returns false when held
// at an edge.
bool Game::accelerate(Object& o) {
    if (widescreen()) {
        // Widescreen: see wide_screen_limits.
        const auto [left, right] = wide_screen_limits();
        const int sx = int(std::int16_t(std::uint16_t(o.x - camera.x)));
        if (sx < left) {
            hold_at_edge(o, unsigned(left)); // B3D3
            return false;
        }
        if (sx > right) {
            hold_at_edge(o, unsigned(right)); // B3E0
            return false;
        }
    } else {
        const u8 sx = u8(o.x - camera.x);
        if (sx < 0x38) {
            hold_at_edge(o, 0x38); // B3D3
            return false;
        }
        if (sx >= 0xC8) {
            hold_at_edge(o, 0xC7); // B3E0
            return false;
        }
    }
    const u8 s = o.state;
    const bool special = s == 0x23 || s == 0x26 || s == 0x24 || s == 0x29 || s == 0x48;
    if (!special) {
        if (s >= 0x1E) {
            return true;
        }
        if (o.flags & 0x04) {
            accelerate_in_water(o); // B346
            return true;
        }
    }
    // B2E3
    const unsigned row = u8(o.state * 4);
    if (o.status & 0x02) {
        return true;
    }
    Table table;
    unsigned side;
    if (!held(0x0C)) {
        // B3B2: friction
        player.input_force = 0;
        if (!o.vx) {
            return true;
        }
        table = friction;
        side = (o.vx & 0x8000) ? 0 : 2;
    } else {
        table = (o.vx & 0x8000) ? accel_left : accel_right;
        side = held(0x04) ? 0 : 2;
    }
    std::uint16_t a = rom.word(table, row + side);
    if (!hi(std::uint16_t(magnitude(o.vx) + 0x80))) {
        a = std::uint16_t(a * 2);
    }
    player.input_force = a;
    player.slope_force = rom.word(slope_forces, probe.landed_attribute);
    return true;
}

// B346: the same with the underwater tables; a slope (attribute 0E or
// more) gives no force while no direction is held.
void Game::accelerate_in_water(Object& o) {
    const unsigned row = u8(o.state * 4);
    if (!held(0x0C)) {
        // B3B2 (the friction table is the same)
        player.input_force = 0;
        if (!o.vx) {
            return;
        }
        std::uint16_t a = rom.word(friction, row + ((o.vx & 0x8000) ? 0 : 2));
        if (!hi(std::uint16_t(magnitude(o.vx) + 0x80))) {
            a = std::uint16_t(a * 2);
        }
        player.input_force = a;
        player.slope_force = rom.word(slope_forces, probe.landed_attribute);
        return;
    }
    const Table table = (o.vx & 0x8000) ? water_accel_left : water_accel_right;
    std::uint16_t a = rom.word(table, row + (held(0x04) ? 0 : 2));
    if (!hi(std::uint16_t(magnitude(o.vx) + 0x80))) {
        a = std::uint16_t(a * 2);
    }
    player.input_force = a;
    if (probe.landed_attribute >= 0x0E && !held(0x0C)) {
        player.slope_force = 0;
        return;
    }
    player.slope_force = rom.word(slope_forces, probe.landed_attribute);
}

// B3D3 / B3E0: held at the camera plus `offset`: the fraction cleared, the
// last move taken back, no speed.
void Game::hold_at_edge(Object& o, unsigned offset) {
    o.x = std::uint16_t(camera.x + offset);
    o.x_sub = 0;
    const std::uint32_t position = (std::uint32_t(o.x) << 8) | o.x_sub;
    const std::uint32_t v = (o.vx & 0x8000) ? (0xFF0000u | o.vx) : o.vx;
    const std::uint32_t back = (position - v) & 0xFFFFFF;
    o.x = std::uint16_t(back >> 8);
    o.x_sub = u8(back);
    player.input_force = 0;
    o.vx = 0;
}

// B410 / B42C: the screen edges as in B2A7 (B410 first stops a fall
// above the camera's Y + 38).
void Game::keep_on_screen(Object& o) {
    if (widescreen()) {
        // Widescreen: see wide_screen_limits.
        const auto [left, right] = wide_screen_limits();
        const int sx = int(std::int16_t(std::uint16_t(o.x - camera.x)));
        if (sx < left) {
            hold_at_edge(o, unsigned(left));
        } else if (sx > right) {
            hold_at_edge(o, unsigned(right));
        }
        return;
    }
    const u8 sx = u8(o.x - camera.x);
    if (sx < 0x38) {
        hold_at_edge(o, 0x38);
    } else if (sx >= 0xC8) {
        hold_at_edge(o, 0xC7);
    }
}

// B410: rising, the player stops 38 pixels below the camera's top; then
// the side edges (B42C).
void Game::keep_in_view(Object& o) {
    if ((o.vy & 0x8000) && std::uint16_t(camera.y + 0x38) >= objects[0].y) {
        o.vy = 0;
    }
    keep_on_screen(o);
}

// B0F3: the horizontal speed: plus the controller's and the slope's force
// (Tails' controller force is three quarters, also in zone 2's water),
// limited to the top speed; a wall in the direction of motion stops it.
// Then the position.
void Game::move_horizontally(Object& o) {
    std::uint16_t v;
    if (o.status & 0x02) {
        v = o.vx;
    } else {
        if (objects[0].type == 0x02) {
            three_quarters_force(); // B18B -> B191
        }
        if ((o.flags & 0x04) && flow.zone == 2) {
            three_quarters_force();
        }
        v = std::uint16_t(o.vx + player.input_force + player.slope_force);
        if (!(v & 0x8000)) {
            if ((o.terrain & 0x04) || (o.touch_sides & 0x04)) {
                o.vx = 0;
                player.input_force = 0;
                return;
            }
            o.angle = 0x40;
            if (hi(v) >= hi(player.top_speed)) {
                v = player.top_speed;
            }
        } else {
            if ((o.terrain & 0x08) || (o.touch_sides & 0x08)) {
                o.vx = 0;
                player.input_force = 0;
                return;
            }
            o.angle = 0xC0;
            if (hi(v) < u8(-hi(player.top_speed))) {
                v = negate(player.top_speed);
            }
        }
    }
    // B164
    o.vx = v;
    const std::uint32_t position = (std::uint32_t(o.x) << 8) | o.x_sub;
    const std::uint32_t d = (v & 0x8000) ? (0xFF0000u | v) : v;
    const std::uint32_t sum = (position + d) & 0xFFFFFF;
    o.x = std::uint16_t(sum >> 8);
    o.x_sub = u8(sum);
}

// B191: three quarters of the controller's force.
void Game::three_quarters_force() {
    const std::uint16_t half = std::uint16_t(std::int16_t(player.input_force) >> 1);
    const std::uint16_t quarter = std::uint16_t(std::int16_t(half) >> 1);
    player.input_force = std::uint16_t(half + quarter);
}

// B1A3: the vertical speed: gravity in the air (30/256 per frame, 18 in
// states 0B and 24, 24 in 1B; under water 18/0C/12) up to 5 (4 under
// water) pixels per frame; on the floor 5 (7 on attributes 0A and 0C)
// pressing down. Then the position (not with status bit 5).
void Game::move_vertically(Object& o) {
    std::uint16_t v = o.vy;
    const u8 s = o.state;
    if (s != 0x11 && s != 0x26 && s != 0x23) {
        bool airborne = o.flags & 0x01;
        if (!airborne) {
            airborne = o.status & 0x10;
            o.status &= u8(~0x10);
        }
        if (!airborne) {
            if (player.carried) {
                return;
            }
            v = magnitude(o.vy);
        } else if (!(o.flags & 0x04)) {
            const u8 n = o.next_state;
            const unsigned g = (n == 0x0B || n == 0x24) ? 0x18 : (n == 0x1B ? 0x24 : 0x30);
            v = std::uint16_t(o.vy + g);
            if (!(v & 0x8000) && hi(v) >= 0x05) {
                v = 0x0500;
            }
        } else {
            const unsigned g = (s == 0x0B || s == 0x24) ? 0x0C : (s == 0x1B ? 0x12 : 0x18);
            v = std::uint16_t(o.vy + g);
            if (!(v & 0x8000) && hi(v) >= 0x04) {
                v = 0x0400;
            }
        }
    }
    // B232
    if (o.state == 0x23) {
        // B274: on the board: rising slows by 1/4; falling speeds up by 1/2
        // to 5; on the ground held at 2 (no move).
        v = o.vy;
        if (v & 0x8000) {
            v = std::uint16_t(v + 0x40);
        } else if (o.terrain & 0x02) {
            o.vy = 0x0200;
            return;
        } else if (v < 0x0500) {
            v = std::uint16_t(v + 0x80);
        }
        o.vy = v;
        const std::uint32_t at = (std::uint32_t(o.y) << 8) | o.y_sub;
        const std::uint32_t sum = (at + ((v & 0x8000) ? (0xFF0000u | v) : v)) & 0xFFFFFF;
        o.y = std::uint16_t(sum >> 8);
        o.y_sub = u8(sum);
        return;
    }
    if (o.terrain & 0x02) {
        v = 0x0500;
        if (probe.landed_attribute == 0x0A || probe.landed_attribute == 0x0C) {
            v = 0x0700;
        }
    }
    if (o.status & 0x20) {
        return;
    }
    o.vy = v;
    const std::uint32_t position = (std::uint32_t(o.y) << 8) | o.y_sub;
    const std::uint32_t d = (v & 0x8000) ? (0xFF0000u | v) : v;
    const std::uint32_t sum = (position + d) & 0xFFFFFF;
    o.y = std::uint16_t(sum >> 8);
    o.y_sub = u8(sum);
}

// B093: special blocks under the player: 95 (an event when on it),
// landing attributes 6 and 8 (state 3F), and kind 8 (0435) outside a few
// blocks of zones 0 and 1; in zone 2 (acts 1-2) block 45 (046B).
void Game::special_blocks(Object& o) {
    if (terrain_state.block == 0x95) {
        if (o.terrain & 0x02) {
            act.event = 0x01;
        }
        return;
    }
    if (probe.landed_attribute == 0x06 || probe.landed_attribute == 0x08) {
        o.next_state = 0x3F;
        return;
    }
    if (flow.zone == 2) {
        if (flow.act != 2 && terrain_state.block == 0x45 && (o.terrain & 0x02)) {
            o.next_state = 0x3E; // 3F1E (046B): the tunnel's course
        }
        return;
    }
    if (flow.zone == 1 && terrain_state.block == 0x84) {
        return;
    }
    if (flow.zone == 0 && terrain_state.block == 0x87) {
        return;
    }
    if (terrain_state.flags == 0x08) {
        to_float(o); // 0435
    }
}

// Each character's walking and running animation (Sonic AC6E/ACDA, Tails
// 92AE/931A): the same code with its own frames and times.
struct Game::Gait {
    u8 walk_first, push_frame;
    Table walk_timing;
    u8 run_first, run_frames, run_ticks;
};

const Game::Gait Game::sonic_gait{0x4C, 0x1A, {0x0C, 0xACC1}, 0x52, 4, 4};
const Game::Gait Game::tails_gait{0x43, 0x42, {0x0C, 0x9301}, 0x49, 2, 2};

// AC6E / 92AE: walking frames (6, one step per frame of the script) with
// the time per frame by speed; pushing a wall in front shows the pushing
// frame.
void Game::animate_walking(Object& o, const Gait& g) {
    const u8 c = o.contact;
    if ((o.display & display_flag::mirrored) ? (c & 0x08) : (c & 0x04)) {
        o.frame = g.push_frame; // ACD1, 9311
        o.frame_ticks = 0x02;
        return;
    }
    if (flow.zone == 3 && (o.status & 0x20) && o.state == 0x05) {
        sound_.request(0xCD, 1);
    }
    if (++o.anim_step >= 0x06) {
        o.anim_step = 0;
    }
    o.frame = u8(g.walk_first + o.anim_step);
    o.frame_ticks = rom.byte(g.walk_timing, high_magnitude(o.vx));
}

// ACDA / 931A: the running frames.
void Game::animate_running(Object& o, const Gait& g) {
    if (++o.anim_step >= g.run_frames) {
        o.anim_step = 0;
    }
    o.frame = u8(g.run_first + o.anim_step);
    o.frame_ticks = g.run_ticks;
}

// ACF0: the spinning frames (AD32 Sonic, AD46 Tails): on the floor facing
// the motion, faster with speed (AD5A); in the air facing the direction
// held, 3 frames each.
void Game::animate_spinning(Object& o) {
    if (++o.anim_step >= 0x14) {
        o.anim_step = 0;
    }
    o.frame = rom.byte(o.type == 1 ? sonic_spin_frames : tails_spin_frames, o.anim_step);
    if (o.terrain & 0x02) {
        o.display &= u8(~display_flag::mirrored);
        u8 h = hi(o.vx);
        if (h & 0x80) {
            h = u8(-h);
            o.display |= display_flag::mirrored;
        }
        o.frame_ticks = rom.byte(spin_timing, h);
        return;
    }
    if (held(0x08)) {
        o.display &= u8(~display_flag::mirrored);
    } else if (held(0x04)) {
        o.display |= display_flag::mirrored;
    }
    o.frame_ticks = 0x03;
}

// 8DE1: a button (1, 2 or start) sends him running right at 2 (script
// 8DA8 / Tails 974B); when the countdown has run out (D539) he jumps up
// at 4 and falls away (8DB2 / 9755).
void Game::continue_wait(Object& o) {
    const bool sonic = o.type == 0x01;
    if (o.scratch[5] == 0xFF) {
        jump_script(o, sonic ? 0x8DB2 : 0x9755);
        o.frame_ticks = 0x01;
        o.vx = 0;
        o.vy = 0xFC00;
        return;
    }
    if (!(input.pressed & 0xB0)) {
        return;
    }
    jump_script(o, sonic ? 0x8DA8 : 0x974B);
    o.frame_ticks = 0x01;
    o.vx = 0x0200;
    o.vy = 0;
    o.scratch[2] = 0xFF; // D536
}

// A782: a fixed course in three legs (table A798): down-left until X
// 790, then down until Y 218, then left at 4 until X 718: down at 6,
// rolling (3D6E).
void Game::tunnel_run(Object& o) {
    move_object(o);    // 5359 (0381)
    object_terrain(o); // 713D (0384)
    switch (o.counter) {
    case 0: // A79E
        if (o.x < 0x0790) {
            o.vx = 0;
            ++o.counter;
        }
        return;
    case 1: // A7B2
        if (o.y >= 0x0218) {
            o.vx = 0xFC00;
            ++o.counter;
        }
        return;
    case 2: // A7C6
        if (o.x < 0x0718) {
            o.vy = 0x0600;
            to_rolling(o); // 3D6E (0423)
        }
        return;
    default:
        throw Unrecovered("tunnel leg past the table (A798)");
    }
}

// A6D7 / A713: swung round an overhang's edge (states 35, 36; from ceiling
// kind 1C): up at 2, sideways faster by `push` each update, held under
// solid blocks overhead (6D91); after 32 updates state 3D.
void Game::overhang_swing(Object& o, std::uint16_t push) {
    o.vx = std::uint16_t(o.vx + push);
    o.vy = 0xFE00;
    move_object(o);                                      // 5359 (0381)
    probe_terrain(o, 0, std::uint16_t(-o.frame_height)); // 6FA6 (03FF)
    ceiling_solid_from_below(o);                         // 6D91 (0468)
    if (!--o.counter) {
        o.next_state = 0x3D;
    }
}

// The player's state routines by Routine (dispatch.cpp hands them here).
bool Game::run_player_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::overhang_start_left: // A6CE (state 35): 32 updates, facing right
        o.counter = 0x20;
        o.display &= u8(~display_flag::mirrored);
        return true;
    case Routine::overhang_start_right: // A70A (state 36): facing left
        o.counter = 0x20;
        o.display |= display_flag::mirrored;
        return true;
    case Routine::continue_start: // 8DC8 (state 47, the continue screen): at (80, A0) facing right
        o.display &= u8(~display_flag::mirrored);
        o.flags |= 0x08;
        o.scratch[2] = 0; // D536: not running yet
        o.x = 0x0080;
        o.y = 0x00A0;
        return true;
    case Routine::continue_wait: // 8DE1
        continue_wait(o);
        return true;
    case Routine::continue_run: // 8E34: running off (D537 holds the countdown)
        move_object(o);         // 5359 (0381)
        o.scratch[3] = 0xFF;
        return true;
    case Routine::continue_fall: // 8DBA: falling (gravity 3/16)
        o.vy = std::uint16_t(o.vy + 0x30);
        move_object(o);
        return true;
    case Routine::chute_start: // A7D9 (state 3F, landing attributes 6 and 8): down at 7, along at 2
                               // (left on 6)
        o.counter = 0;
        o.vy = 0x0700;
        if (probe.landed_attribute == 0x06) {
            o.display |= display_flag::mirrored;
            o.vx = 0xFE00;
        } else {
            o.display &= u8(~display_flag::mirrored);
            o.vx = 0x0200;
        }
        return true;
    case Routine::chute_slide: // A800: 8 updates off attributes: walking (03F6)
        move_object(o);        // 5359 (0381)
        player_terrain(o);     // 5F14 (0393)
        if (probe.landed_attribute) {
            o.counter = 0;
            return true;
        }
        if (++o.counter >= 0x08) {
            to_walking(o); // 3A8D (03F6)
        }
        return true;
    case Routine::tunnel_start: // A76D (state 3E, from block 45 in zone 2): left at 4, down at 4
        o.vx = 0xFC00;
        o.vy = 0x0400;
        o.counter = 0;
        o.flags |= 0x02;
        return true;
    case Routine::tunnel_run: // A782
        tunnel_run(o);
        return true;
    case Routine::blast_wait:    // A81E (state 42, from block EA in zone 3)
        move_object(o);          // 5359 (0381)
        player_terrain_plain(o); // 5F27 (0486)
        return true;
    case Routine::blast_start: // A824: still
        o.status &= u8(~0x04);
        o.vx = 0;
        o.vy = 0;
        return true;
    case Routine::blast_rise: // A832: blown up faster by 1/8 to 6 (the script throws flakes, type
                              // 31)
        jump_in_air(o);       // A4D9
        if (hi(o.vy) != 0xFA) {
            o.vy = std::uint16_t(o.vy - 0x20);
        }
        move_object(o);
        player_terrain_plain(o);
        return true;
    case Routine::blast_end: // A84A
        to_falling_state(o); // 3B6F (0483)
        return true;
    case Routine::overhang_drop_start: // A746 (state 3D): 80 updates, no fall speed
        o.counter = 0x50;
        o.vy = 0;
        return true;
    case Routine::overhang_drop: // A752: moving freely until landing or time out (03B7)
        if (--o.counter) {
            move_horizontally(o); // B0F3
            move_vertically(o);   // B1A3
            player_terrain(o);    // 5F14 (0393)
            special_blocks(o);    // B093
            if (!(o.terrain & 0x02)) {
                return true;
            }
        }
        to_falling(o); // 3B47 (03B7)
        return true;
    case Routine::overhang_left: // A6D7
        overhang_swing(o, 0xFFE8);
        return true;
    case Routine::overhang_right: // A713
        overhang_swing(o, 0x0018);
        return true;
    case Routine::player_standing:
        player_standing(o);
        return true;
    case Routine::player_looking_up:
        player_looking_up(o);
        return true;
    case Routine::player_crouching:
        player_crouching(o);
        return true;
    case Routine::player_walking:
        player_walking(o);
        return true;
    case Routine::player_running:
        player_running(o);
        return true;
    case Routine::player_skidding_right:
        player_skidding(o, false);
        return true;
    case Routine::player_skidding_left:
        player_skidding(o, true);
        return true;
    case Routine::player_rolling:
        player_rolling(o);
        return true;
    case Routine::player_jumping:
        player_jumping(o);
        return true;
    case Routine::player_air_spin:
        player_air_spin(o);
        return true;
    case Routine::player_balancing:
        player_balancing(o);
        return true;
    case Routine::player_sprung_diagonally: // 9BAB
        player_sprung_diagonally(o);
        return true;
    case Routine::player_bounced: // 9B10
        player_bounced(o);
        return true;
    case Routine::run_off: // 84FF
        run_off(o);
        return true;
    case Routine::pipe_travel: // BD46: through a pipe; its junctions at the head (6E16)
        move_object(o);
        player_head(o);
        return true;
    case Routine::pipe_chamber: // BD69
        pipe_chamber(o);
        return true;
    case Routine::pipe_choose: // BD4D
        pipe_choose(o);
        return true;
    case Routine::pipe_out: // BDC6
        pipe_out(o);
        return true;
    case Routine::pipe_away: // BDBB: free of the pipe, moving on
        o.placement = 0;
        terrain_state.pipe_block = 0;
        move_object(o);
        return true;
    case Routine::pipe_fly: // BDF9
        pipe_fly(o);
        return true;
    case Routine::pipe_done: // 85BD: rolling (3D99)
        terrain_state.pipe_block = 0;
        o.terrain = 0;
        to_bounced_roll(o);
        return true;
    case Routine::victory_start: // A8D2
        victory_start(o);
        return true;
    case Routine::face_right: // A915
        o.display &= u8(~display_flag::mirrored);
        return true;
    case Routine::victory_run: // A91A
        victory_run(o);
        return true;
    case Routine::victory_still: // A953 (zone 2)
        o.vy = 0;
        return true;
    case Routine::victory_run_down: // A95A (zone 2): speeding up by 1/16 to 5 on the ground; at X
                                    // 774 standing (script 8B7E / 9632)
        if (o.vx <= 0x0500) {
            o.vx = std::uint16_t(o.vx + 0x10);
        }
        move_object(o);    // 5359 (0381)
        object_terrain(o); // 713D (0384)
        if (o.x >= 0x0774) {
            jump_script(o, o.type == 0x01 ? 0x8B7E : 0x9632);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::victory_run_far: // A993 (zones 3-5): facing right, speeding up by 1/32 to 5; at X
                                   // AE0 standing (script 8B96 / 964A)
        o.display &= u8(~display_flag::mirrored);
        if (hi(o.vx) != 0x05) {
            o.vx = std::uint16_t(o.vx + 0x08);
        }
        move_object(o);
        if (o.x >= 0x0AE0) {
            jump_script(o,
                        o.type == 0x01 ? 0x8B96 : 0x964A); // (the frame's updates left as they are)
        }
        return true;
    case Routine::float_start: // 866E
        float_start(o);
        return true;
    case Routine::player_float: // A51C
        player_float(o);
        return true;
    case Routine::float_hold: // A562
        float_hold(o);
        return true;
    case Routine::player_float_short: // A570
        player_float_short(o);
        return true;
    case Routine::hurt_start: // 83CC
        hurt_start(o);
        return true;
    case Routine::player_hurt: // 9CD6
        player_hurt(o);
        return true;
    case Routine::death_start: // 843C
        death_start(o, 0xFD00);
        return true;
    case Routine::drown_start: // 8434
        death_start(o, 0xFD80);
        return true;
    case Routine::player_dead: // A18D
        player_dead(o);
        return true;
    case Routine::player_drowned: // A1D3
        player_drowned(o);
        return true;
    case Routine::card_run_start: // 898D
        card_run_start(o);
        return true;
    case Routine::card_run_second: // 89A9
        start_map_animation(u8(flow.zone * 2 + 6));
        return true;
    case Routine::card_run_third: // 89B9
        start_map_animation(0x04);
        return true;
    case Routine::card_run: // 89C5
        move_object(o);
        return true;
    case Routine::flight_start: // 90E9
        o.vy = 0xFD80;
        return true;
    case Routine::tails_fly:
        tails_fly(o);
        return true;
    case Routine::entry_drop:
        entry_drop(o);
        return true;
    case Routine::entry_slide:
        entry_slide(o);
        return true;
    case Routine::entry_jump:
        entry_jump(o);
        return true;
    case Routine::entry_land:
        entry_land(o);
        return true;
    case Routine::path_right:
        path_right(o);
        return true;
    case Routine::path_left:
        path_left(o);
        return true;
    case Routine::path_rise:
        path_rise(o);
        return true;
    case Routine::animate_path:
        animate_path(o);
        return true;
    case Routine::animate_path_fall:
        animate_path_fall(o);
        return true;
    case Routine::path_fall:
        path_fall(o);
        return true;
    case Routine::to_falling_from_flight:
        to_falling_from_flight(o);
        return true;
    case Routine::player_sprung:
        player_sprung(o);
        return true;
    case Routine::player_charging:
        player_charging(o);
        return true;
    case Routine::swim_start: // A84D (state 41): still; not rolling or in the air; the air count
                              // over
        o.flags &= u8(~0x03);
        o.vx = 0;
        o.vy = 0;
        player.air_count = 0;
        return true;
    case Routine::air_breathed: // 8399 (state 25, a big bubble breathed in): the air count over
        player.air_count = 0;
        return true;
    case Routine::swim: // A862
        player_swim(o);
        return true;
    case Routine::fall_out_of_act: // 8EAB (state 4B, after the ground opened): the act ends (D144
                                   // bit 4)
        flow.frame_work |= 0x10;
        return true;
    case Routine::held_start: // 8E58 (state 4A, a scene holds him): facing right
        o.display &= u8(~display_flag::mirrored);
        return true;
    case Routine::held_fall: // 8E68: falling (1/16) to the ground, then standing still (script 8E50
                             // / Tails 979E)
        keep_on_screen(o);   // B42C
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o);    // 5359 (0381)
        object_terrain(o); // 713D (0384)
        if (o.terrain & 0x02) {
            jump_script(o, o.type == 0x01 ? 0x8E50 : 0x979E);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::held_wait: // 8E5D: the scene over (DBD7 clear): standing (3A3A)
        keep_on_screen(o);
        if (!player.in_scene) {
            to_standing(o); // 040E
        }
        return true;
    case Routine::tails_fly_start: // 922A: up at 1.25 (4 higher off the ground); not rolling, no
                                   // contact below
        o.vx = 0;
        o.vy = 0xFEC0;
        if (!(o.terrain & 0x02)) {
            o.y = std::uint16_t(o.y - 4);
        }
        o.flags &= u8(~0x02);
        o.terrain &= u8(~0x02);
        o.contact &= u8(~0x02);
        return true;
    case Routine::tails_fly_sound: // 9253: the rotor (C6 touching something, else BE)
        sound_.request((o.terrain & 0x0F) ? 0xC6 : 0xBE, 1);
        return true;
    case Routine::tails_hover: // A238
        tails_hover(o);
        return true;
    case Routine::player_peeling: // 9BC6
        player_peeling(o);
        return true;
    case Routine::peel_run_start: // 8243: rolling, untouchable, 30 frames (D3C2)
        o.flags |= 0x82;
        player.hurt_pending = 0x1E;
        return true;
    case Routine::player_peel_run: // 9C06
        player_peel_run(o);
        return true;
    case Routine::player_dashing:
        player_dashing(o);
        return true;
    case Routine::player_falling:
        player_falling(o);
        return true;
    case Routine::animate_walking:
        animate_walking(o, sonic_gait);
        return true;
    case Routine::animate_running:
        animate_running(o, sonic_gait);
        return true;
    case Routine::tails_walking:
        animate_walking(o, tails_gait);
        return true;
    case Routine::tails_running:
        animate_running(o, tails_gait);
        return true;
    case Routine::animate_spinning:
        animate_spinning(o);
        return true;
    default:
        return false;
    }
}

// A862 (state 41): swimming up: at 1/2 (Up: 1, Down: 3/16); held by a
// ceiling (contact bit 0); Left and Right move him a pixel; no higher
// than 32 below the water's surface; a button jumps (3AB8).
void Game::player_swim(Object& o) {
    aim_camera_at_player(); // 98CE
    const u8 held = input.held;
    std::uint16_t vy = 0xFF80;
    if (held & 0x01) {
        vy = 0xFF00;
    } else if (held & 0x02) {
        vy = 0xFFD0;
    }
    if (o.contact & 0x01) { // A87C, A88F: every way ends here
        vy = 0;
    }
    o.vy = vy;
    if (held & 0x04) {
        o.x = std::uint16_t(o.x - 1);
    } else if (held & 0x08) {
        o.x = std::uint16_t(o.x + 1);
    }
    const std::uint16_t top = std::uint16_t(act.water_level + 0x20);
    if (o.y < top) {
        o.y = top;
    }
    move_object(o);    // 5359 (0381)
    player_terrain(o); // 5F14 (0393)
    if (input.pressed & 0x30) {
        to_jumping(o); // 3AB8 (03BA)
    }
}

} // namespace triple_trouble_gg
