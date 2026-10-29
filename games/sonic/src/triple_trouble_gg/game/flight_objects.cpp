// The flight stage's objects (mode 0D, frontend/flight.cpp).
//
// Type 41 (bank 17, 8000-8298), the opening message: parameter 0 places
// it on the screen, spawns its eleven letters and digits (parameters 1-B;
// 5-7 the ring goal's digits, 8252-826E), and after a while (DBE6 FF,
// sound C7) the letters fly apart and fall away; then its art for the
// flight (1A:B943 / B953, with frame waits), the horizon split on, the
// flight's speed 2, and it is gone.
// Type 0C (bank 1E, 88D9-8989), a ring of the course: it comes nearer
// with the distance (552C), is drawn by its size and the shared phase,
// and touched by the plane (55DB) counts (sound DD); enough rings win
// the stage (D180). With byte 37 FF (slot 4) it is the HUD's ring.
// Type 46 (bank 17, 84C6-873F), act 2's stars: parameter FF sends a star
// (parameters: their directions) or a bright one (type 6B, 1-8) every 8
// frames until the course ends; each starts at (80, 70) and drifts out,
// blinking, faster after a while. Type 6B (bank 1A, BDD1-BEB7): the bright
// ones, by parameter one of eight directions, three speeds.
// Type 37 (bank 1E, B6AF-B71E), an obstacle of the course: it comes
// nearer as the rings do; touched by the plane it is gone and the plane
// tumbles (DBD2, sound D6).
// Type 20 (bank 1E, 9BF2-9DA3), the clouds: parameter 0 sends one of nine
// clouds (parameters 1-9: place and direction) every 16 frames until the
// course ends (DBD5); each drifts off the screen.
// Type 5C (bank 17, B7BC-BA1F), zone 9's guardian, met after a won
// flight: after ten seconds it wakes (the clock stopped, D182; the
// partner's art away), and hops at the player between X 1138 and 1187
// (sound B7 at each landing). A rolling player's touch costs it a hit
// (16 frames before the next; 16 in all); any other touch hurts him (D3C1
// 8). Beaten it flashes white (palette 3), blows up in eight pieces four
// times over, the colours come back (26, 1D), and the act's end is
// signalled (D3FF).
// Type 5D (bank 17, BA20-BB3A), zone 9's guardian the second time: it
// holds the player (3F76, DBD7) and after ten seconds wakes as the first
// does; its rider (type 5E) comes along. Coming in it stops at X 1170
// (D3E4 5D: the rider's turn) and sways up and down, bouncing the player
// off, until D3E4 is 0; then it blows up as the first (B82C), the camera
// held at its height, the colours back.
// Type 5E (bank 1C, B48E-B667), its rider: it rides 4 right of and 16
// below it until D3E4 5D; then it drops to Y 120 (harmful) and runs
// across the screen to the left and back (sound C8 at each turn, 3.5
// fast); the fourth time it runs into the guardian: gone, D3E4 0, the
// colours white (3).
//
// The projection (552C): an object's mark (bytes 34-35) less the flight's
// distance is its depth; nearer than 80 it is seen (byte 38 its depth),
// placed around (80, 60) by its course place (bytes 36, 37) scaled by
// the row of table 18:B4D8 for the depth (32 bytes a row, a byte per 4).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr Table depth_scales{0x18, 0xB4D8};
constexpr Table ring_frames{0x1E, 0x898E};
constexpr Table rider_frames{0x1C, 0xB649};
constexpr std::uint16_t letters_away = 0x8170;
constexpr u8 sky_tiles = 0x62, ring_tiles = 0x44, cloud_tiles = 0x90;
} // namespace

bool Game::run_flight_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::flight_title_start: // 8184: state by parameter; the controller off while it shows
        o.next_state = u8(o.param + 1);
        o.tile_base = sky_tiles;
        if (o.next_state == 0x01) {
            act.work[0] = 0;
            input.disabled = 0xFF;
        }
        return true;
    case Routine::flight_title_place: // 819C: at the camera's (+40, +10); the camera still
        o.x = std::uint16_t((camera.x + 0x40) & ~0x01u);
        o.y = std::uint16_t(camera.y + 0x10);
        disable_map(); // 4CCE (0312)
        return true;
    case Routine::flight_title_go: // 81BB: the letters away (DBE6), sound C7
        act.work[0] = 0xFF;
        sound_.request(0xC7, 1);
        return true;
    case Routine::flight_letter_fall: // 81E8: falling (1/4)
        o.vy = std::uint16_t(o.vy + 0x40);
        move_object(o); // 5359 (0381)
        return true;
    case Routine::flight_letter_hold: // 81FB
        o.frame_ticks = 0x40;
        return true;
    case Routine::flight_letter_wait: // 8200: away once DBE6 is set (8170)
        if (act.work[0]) {
            jump_script(o, letters_away);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::flight_letter_gather: // 8212: by 2 towards the screen's (80, 60)
        flight_letter_gather(o);
        return true;
    case Routine::flight_goal_hundreds: // 8252
        o.tile_base = u8((score.ring_goal[1] & 0x0F) * 2 + 0x2E);
        return true;
    case Routine::flight_goal_tens: // 825E
        o.tile_base = u8((score.ring_goal[0] >> 4) * 2 + 0x2E);
        return true;
    case Routine::flight_goal_ones: // 826E
        o.tile_base = u8((score.ring_goal[0] & 0x0F) * 2 + 0x2E);
        return true;
    case Routine::flight_letter_sound: // 827A: the first letter: sound DC
        if (o.param == 0x01) {
            sound_.request(0xDC, 1);
        }
        return true;
    case Routine::flight_letter_away: // 8286: gone off the screen
        move_object(o);
        if (hi(o.screen_y) || hi(o.screen_x)) {
            o.type = 0xFF;
        }
        return true;
    case Routine::flight_ring_start: // 8902: tiles 44; the HUD's ring (byte 37 FF) at (38, 1F),
                                     // size 30
        o.tile_base = ring_tiles;
        o.tile_base_mirrored = ring_tiles;
        o.next_state = 0x01;
        if (o.scratch[3] != 0xFF) {
            return true;
        }
        o.x = std::uint16_t((o.x & 0xFF00) | 0x38);
        o.y = std::uint16_t((o.y & 0xFF00) | 0x1F);
        o.scratch[4] = 0x30;
        o.next_state = 0x02;
        o.display |= 0x02;
        return true;
    case Routine::flight_ring_fly: // 892A
        flight_project(o);         // 552C (039C)
        flight_touch(o);           // 55DB (039F)
        if (!o.touch_sides) {
            return true;
        }
        o.type = 0xFF;
        sound_.request(0xDD, 1);
        add_ring(); // 33AB (0345)
        if ((score.rings_low | score.rings_high << 8) >=
            (score.ring_goal[0] | score.ring_goal[1] << 8)) {
            flow.flight_won = 0xFF;
        }
        return true;
    case Routine::flight_ring_frame: { // 8952: by its size (0, <20, <40, <60, <70, more) and the
                                       // phase
        o.frame_ticks = 0x01;
        const u8 size = o.scratch[4];
        if (!size) {
            o.frame = 0;
            return true;
        }
        const u8 row = size < 0x20   ? 0x00
                       : size < 0x40 ? 0x04
                       : size < 0x60 ? 0x08
                       : size < 0x70 ? 0x0C
                                     : 0x10;
        o.frame = rom.byte(ring_frames, u8(row + act.scene[scene_byte::phase]));
        return true;
    }
    case Routine::flight_at_plane: // 88F5
        o.x = std::uint16_t((o.x & 0xFF00) | act.scene[scene_byte::plane_x]);
        o.y = std::uint16_t((o.y & 0xFF00) | act.scene[scene_byte::plane_y]);
        return true;
    case Routine::obstacle_start: // B6C1: tiles C2
        o.tile_base = 0xC2;
        o.tile_base_mirrored = 0xC2;
        o.next_state = 0x01;
        if (o.scratch[3] == 0xFF) {
            o.display |= 0x02;
        }
        return true;
    case Routine::obstacle_frame: { // B6D8: by its size (0, <20, <40, <60, <70, more)
        o.frame_ticks = 0x01;
        const u8 size = o.scratch[4];
        o.frame = !size         ? 0
                  : size < 0x20 ? 4
                  : size < 0x40 ? 3
                  : size < 0x60 ? 2
                  : size < 0x70 ? 1
                                : 0;
        return true;
    }
    case Routine::obstacle_fly: // B706
        flight_project(o);      // 552C (039C)
        flight_touch(o);        // 55DB (039F)
        if (!o.touch_sides) {
            return true;
        }
        o.type = 0xFF;
        act.flight_hit = 0xFF;
        sound_.request(0xD6, 1);
        return true;
    case Routine::guardian_start: // B8BF: D3E4 F8; 16 hits; the act's boss
        act.variant = 0xF8;
        o.next_state = 0x01;
        o.display |= 0x02;
        o.status_ticks = 0x10;
        act.boss = u8(slot_number(o) + 1);
        return true;
    case Routine::guardian_wait: // B8D7
        o.next_state = 0x02;
        return true;
    case Routine::guardian_wake: // B8DC: the clock stopped, left at 1/4; the partner's art away,
                                 // the common art back (762C)
        clock.stopped = 0x01;
        o.next_state = 0x03;
        o.vx = 0xFF40;
        di();
        player.partner_hidden = 0xFF;
        rival_restore_art(); // 762C (04A1)
        return true;
    case Routine::guardian_hop_far: // B8F8: landing again at B80C
        guardian_hop_start(o, 0xB80C);
        return true;
    case Routine::guardian_hop_near: // B902: landing again at B7EC
        guardian_hop_start(o, 0xB7EC);
        return true;
    case Routine::guardian_hop: // B966
        guardian_hop(o);
        return true;
    case Routine::guardian_hold: // B9EF
        o.frame_ticks = 0x04;
        return true;
    case Routine::guardian_colours: // B9F4: the colours back (26, 1D); flashing
        palette.fade_period = 0x02;
        palette.fades[0] = {0x90, 0x26};
        palette.fades[1] = {0x90, 0x1D};
        act.variant = 0;
        o.display |= display_flag::flicker;
        return true;
    case Routine::guardian_done: // BA1A
        act.boss_signal = 0xFF;
        return true;
    case Routine::star_start: // 861B: tiles 90; FF the source (1), others a star (2)
        o.tile_base = cloud_tiles;
        o.next_state = o.param == 0xFF ? 0x01 : 0x02;
        return true;
    case Routine::star_launch: // 8637: blinking; speed 4 the way of its parameter, from (80, 70)
        o.display |= display_flag::flicker;
        o.speed = 0x40;
        o.angle = o.param;
        o.x = std::uint16_t((o.x & 0xFF00) | 0x80);
        o.y = std::uint16_t((o.y & 0xFF00) | 0x70);
        velocity_from_angle(o); // 52E8 (032D)
        return true;
    case Routine::star_faster: // 8630
        o.speed = 0x80;
        velocity_from_angle(o);
        return true;
    case Routine::bright_star_start: // 1A:BE41: at (80, 70), tiles C0; state by parameter
        o.x = std::uint16_t((o.x & 0xFF00) | 0x80);
        o.y = std::uint16_t((o.y & 0xFF00) | 0x70);
        o.display &= u8(~0x02);
        o.next_state = o.param;
        o.tile_base = 0xC0;
        return true;
    case Routine::bright_star_launch: // BE58-BE86: the way 10, 30, ... F0 by state; speed 2
        o.angle = u8(0x10 + (o.state - 1) * 0x20);
        o.speed = 0x20;
        velocity_from_angle(o);
        return true;
    case Routine::bright_star_faster: // BE8D
        o.speed = 0x40;
        velocity_from_angle(o);
        return true;
    case Routine::bright_star_fastest: // BE94
        o.speed = 0x60;
        velocity_from_angle(o);
        return true;
    case Routine::guardian2_start: // BAD5: D3E4 F8; the act's boss
        act.variant = 0xF8;
        o.next_state = 0x01;
        o.display |= 0x02;
        o.status_ticks = 0x04;
        act.boss = u8(slot_number(o) + 1);
        return true;
    case Routine::guardian2_in: // BB0D: in to X 1170
        move_object(o);
        if (o.x < 0x1170) {
            o.next_state = 0x04;
        }
        return true;
    case Routine::guardian2_turn: // BB22
        act.variant = 0x5D;
        return true;
    case Routine::guardian2_sway: // BB28: the player bounces off; D3E4 0: beaten (5)
        move_object(o);
        touch_test(o); // 58AC (0369)
        bounce_off(o); // 561B (0438)
        if (!act.variant) {
            o.next_state = 0x05;
        }
        return true;
    case Routine::guardian2_beaten: // BAA9: the camera held at its height
        act.variant = 0;
        camera.max_y = camera.y;
        camera.act_max_y = camera.y;
        return true;
    case Routine::guardian2_colours: // BAB7: the colours back (26, 1D)
        palette.fade_period = 0x02;
        palette.fades[0] = {0x90, 0x26};
        palette.fades[1] = {0x90, 0x1D};
        return true;
    case Routine::rider_start: // 1C:B4DA: tiles 84 (94 mirrored)
        o.display |= 0x02;
        o.tile_base = 0x84;
        o.tile_base_mirrored = 0x94;
        o.next_state = 0x01;
        return true;
    case Routine::rider_ride: { // B4EB
        if (const Object* g = final_machine()) {
            o.x = std::uint16_t(g->x + 0x04);
            o.y = std::uint16_t(g->y + 0x10);
        }
        if (act.variant == 0x5D) {
            o.next_state = 0x02;
            o.counter = 0;
            o.timer = 0;
        }
        return true;
    }
    case Routine::rider_drop_start: // B524: falling at 1/2
        o.vx = 0;
        o.vy = 0x0080;
        return true;
    case Routine::rider_drop: // B535: harmful; at Y 120 the runs (3)
        move_object(o);
        harmful_touch(o); // 550E (0375)
        if (o.y >= 0x0120) {
            o.next_state = 0x03;
            o.scratch[0] = 0;
        }
        return true;
    case Routine::rider_run_left_start: // B551
        rider_run_left_start(o);
        return true;
    case Routine::rider_run_left: { // B5A4: the fourth run into the guardian; off the screen's
                                    // left: back (4)
        if (o.timer == 0x04) {
            const Object* g = final_machine();
            if (g && o.x < g->x) {
                o.type = 0xFF;
                act.variant = 0;
                palette.fades[0].mode |= 0x20;
                palette.fades[0].palette = 0x03;
                palette.fades[1].mode |= 0x20;
                palette.fades[1].palette = 0x03;
            }
        }
        harmful_touch(o);
        move_horizontally_only(o); // 539A (0462)
        if (o.x < camera.x) {
            o.next_state = 0x04;
        }
        return true;
    }
    case Routine::rider_run_right_start: // B5F5: sound C8, facing left... right at 3.5
        sound_.request(0xC8, 1);
        o.display |= display_flag::mirrored;
        o.vy = 0;
        o.vx = 0x0380;
        return true;
    case Routine::rider_run_right: // B60F: past the screen's right: left again (3)
        harmful_touch(o);
        move_horizontally_only(o);
        if (o.x >= std::uint16_t(camera.x + 0x0100)) {
            o.next_state = 0x03;
        }
        return true;
    case Routine::rider_frame: // B629: frames 1, 2, 3, 1, 2, 3, 2 by turns, 3 updates each
        o.frame = rom.byte(rider_frames, o.counter);
        o.frame_ticks = 0x03;
        if (++o.counter >= 0x07) {
            o.counter = 0;
        }
        return true;
    case Routine::bloom_start: // 17:87F1 (type 49, the special stages' blooming scenery)
        o.next_state = 0x01;
        return true;
    case Routine::cloud_start: // 9CE9: tiles 90, speed 3; state by parameter
        o.tile_base = cloud_tiles;
        o.speed = 0x30;
        o.next_state = u8(o.param + 1);
        if (o.next_state == 0x01) {
            o.display |= 0x02;
        }
        return true;
    case Routine::cloud_stop: // 9C7C: no more clouds once the course ends
        if (act.flight_rings_off) {
            o.type = 0xFF;
        }
        return true;
    case Routine::cloud_drift: // 9D00: gone outside (30-CF, 18-A7)
        move_object(o);
        if (lo(o.x) < 0x30 || lo(o.x) >= 0xD0 || lo(o.y) < 0x18 || lo(o.y) >= 0xA8) {
            o.type = 0xFF;
        }
        return true;
    case Routine::cloud_faster: // 9CE2
        o.speed = 0x50;
        velocity_from_angle(o); // 52E8 (032D)
        return true;
    case Routine::cloud_place: { // 9D1D-9DA1: per state a place and a direction

        struct Place {
            u8 x, y, angle;
        };

        static constexpr Place places[] = {
            {0x80, 0x60, 0x00}, {0x8E, 0x63, 0x10}, {0x96, 0x6A, 0x20},
            {0x9D, 0x72, 0x30}, {0x98, 0x70, 0x40}, {0x68, 0x70, 0xC0},
            {0x63, 0x72, 0xD0}, {0x6A, 0x6A, 0xE0}, {0x72, 0x63, 0xF0}};
        const Place& p = places[o.state - 2];
        o.x = std::uint16_t((o.x & 0xFF00) | p.x);
        o.y = std::uint16_t((o.y & 0xFF00) | p.y);
        o.angle = p.angle;
        velocity_from_angle(o); // 52E8 (032D)
        return true;
    }
    default:
        return false;
    }
}

// 81C6: its art for the flight (0489: 7A48, list 1A:B943 in act 1, B953
// in act 2, with frame waits); the horizon split on, the map on, the
// controller on; it is gone (FE); the flight's speed 2.
Flow Game::flight_title_end(Object& o) {
    co_await load_tile_list_slowly(
        Table{0x1A, flow.act ? std::uint16_t(0xB953) : std::uint16_t(0xB943)}); // 7A58
    act.scene[scene_byte::split] = 0xFF;
    enable_map(); // 4CC3 (030F)
    o.placement = 0;
    input.disabled = 0;
    o.type = 0xFE;
    if (flow.mode == 0x0D) {
        act.scene[scene_byte::speed] = 0x02;
    }
}

// 8212
void Game::flight_letter_gather(Object& o) {
    const u8 sx = lo(o.screen_x);
    if (sx != 0x80) {
        o.x = std::uint16_t(o.x + (sx < 0x80 ? 2 : -2));
    } else {
        return;
    }
    const u8 sy = lo(o.screen_y);
    if (sy != 0x60) {
        o.y = std::uint16_t(o.y + (sy < 0x60 ? 2 : -2));
    }
}

// B90A: the hop (up 2, or 3.3 when the player's frame ticks are odd),
// facing by the player's X (its lowest bit); towards X 1138 or 1187 at
// 3/4 by its last way (byte 36 bit 4); bytes 34-35 where its script goes
// on when it lands.
void Game::guardian_hop_start(Object& o, std::uint16_t landing) {
    o.scratch[0] = lo(landing);
    o.scratch[1] = hi(landing);
    if (objects[0].x & 0x01) {
        o.display &= u8(~display_flag::mirrored);
    } else {
        o.display |= display_flag::mirrored;
    }
    o.vy = (objects[0].frame_ticks & 0x01) ? 0xFCC0 : 0xFE00;
    const std::uint16_t turn = (o.scratch[2] & 0x10) ? 0x1187 : 0x1138;
    if (o.x >= turn) {
        o.scratch[2] &= u8(~0x10);
        o.vx = 0xFF40;
    } else {
        o.scratch[2] |= 0x10;
        o.vx = 0x00C0;
    }
}

// B966: falling (1/8 up, 3/16 down); landed: still, its script on at bytes
// 34-35, sound B7. Then moved; the touch (see the file's comment).
void Game::guardian_hop(Object& o) {
    o.vy = std::uint16_t(o.vy + ((o.vy & 0x8000) ? 0x20 : 0x30));
    if (!(o.vy & 0x8000)) {
        object_terrain(o); // 713D (0384)
        if (o.terrain & 0x02) {
            jump_script(o, std::uint16_t(o.scratch[0] | o.scratch[1] << 8));
            o.frame_ticks = 0x01;
            o.vx = 0;
            o.vy = 0;
            sound_.request(0xB7, 1);
        }
    }
    move_object(o); // 5359 (0381)
    if (o.scratch[3]) {
        --o.scratch[3];
        return;
    }
    touch_test(o); // 58AC (0369)
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (!(objects[0].flags & 0x02)) {
        player.hit = 0x08;
        return;
    }
    o.scratch[3] = 0x10;
    bounce_off(o); // 561B (0438)
    if (--o.status_ticks) {
        return;
    }
    o.next_state = 0x04;
    palette.fades[0].mode |= 0x20;
    palette.fades[0].palette = 0x03;
    palette.fades[1].mode |= 0x20;
    palette.fades[1].palette = 0x03;
}

// B551: the first time straight to the run (B4BE); sound C8; left at 3.5;
// a run more (byte 1F), the fourth at the guardian's height less 8.
void Game::rider_run_left_start(Object& o) {
    if (!o.scratch[0]) {
        ++o.scratch[0];
        jump_script(o, 0xB4BE);
        o.frame_ticks = 0x01;
    }
    sound_.request(0xC8, 1);
    o.display &= u8(~display_flag::mirrored);
    o.vy = 0;
    o.vx = 0xFC80;
    if (++o.timer != 0x04) {
        return;
    }
    if (const Object* g = final_machine()) {
        o.y = std::uint16_t(g->y - 0x08);
    }
}

// 552C: see the file's comment. Behind the flight it is gone.
void Game::flight_project(Object& o) {
    o.x = 0;
    o.y = 0;
    o.scratch[4] = 0;
    const std::uint16_t mark = std::uint16_t(o.scratch[0] | o.scratch[1] << 8);
    const std::uint16_t distance =
        std::uint16_t(act.scene[scene_byte::distance] | act.scene[scene_byte::distance + 1] << 8);
    if (mark < distance) {
        o.type = 0xFF;
        return;
    }
    const std::uint16_t depth = std::uint16_t(mark - distance);
    if (depth >= 0x0080) {
        return;
    }
    o.scratch[4] = lo(depth);
    if (lo(depth) <= 0x10) {
        return;
    }
    const Table row = depth_scales + ((lo(depth) - 0x10) >> 2) * 0x20u;
    auto scaled = [&](u8 place) {
        const bool negative = place & 0x80;
        const u8 v = rom.byte(row, u8(negative ? -place : place) >> 2);
        return negative ? u8(-v) : v;
    };
    o.x = u8(scaled(o.scratch[2]) + 0x80);
    o.y = u8(scaled(o.scratch[3]) + 0x60);
}

// 55DB: seen and nearer than 22, within 16 of the plane both ways.
void Game::flight_touch(Object& o) {
    o.touch_sides = 0;
    const u8 size = o.scratch[4];
    if (!size || size >= 0x22) {
        return;
    }
    const auto near = [](u8 a, u8 b) {
        return u8(a >= b ? a - b : b - a) < 0x10;
    };
    if (near(act.scene[scene_byte::plane_y], lo(o.y)) &&
        near(act.scene[scene_byte::plane_x], lo(o.x))) {
        o.touch_sides = 0xFF;
    }
}

} // namespace triple_trouble_gg
