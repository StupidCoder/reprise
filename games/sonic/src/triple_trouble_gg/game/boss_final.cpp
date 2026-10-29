// Atomic Destroyer act 3's last machine (type 66, bank 0F, B027-BFB2),
// met after the rival's scene. One type for all its parts, chosen by the
// spawn parameter (B7CF):
//
//   0     the machine (state 1): its art, palette 2A; D3E4 6 and D3E5 name
//         it the act's boss.
//         First form (1A-1C): it waits for the player below (680, 200),
//         then hops at him (status 26: ten hits) until beaten; it runs off
//         to the right and waits for him at the arena (893, 1DB).
//         Second form (3, 4, C, D): it comes down with its core (1) and
//         follows the player, turning three times between bomb drops (4);
//         when the core is broken it rises, comes down in the middle with a
//         hatch (B) and sways; when the hatch has blinked open (FE) one
//         touch beats it (10): it falls, pieces fly (10, 1C), and it drifts
//         off to the right.
//         Last form (14): in a room (9B0-A10) it flies up and down at the
//         player (eight hits, each touch from the wrong side hurts him),
//         with lightning (15, 16) over the floor; beaten (19, DBD1) it
//         sinks and the act's end scene (type 57) starts.
//   1     the core under it (5): sixteen hits, firing shots (2, 3); broken
//         it falls and blows up (8, 9, A).
//   4     a bomb (7): five drops (5) that burst in two (6, 7) on the floor.
//   B     the hatch (E); C-F its pieces (F); 10 the smoke (11) with its
//         pieces 11-13 (12); 15, 16 the lightning (15, 16) and its bolts
//         17-1A (17) and sparks 1B (18); 1C the falling piece (1D).
//
// The machine's own bytes: scratch 0 (34) frames it cannot be hit (or,
// with 1, the script position saved when the hatch opened: the parts read
// byte 35 as "busy"), 2 (36) the core is broken, 3 (37) the hatch: FF open
// it, FE open, FD seen, 5 (39) frames before the last form can be hit
// again; timer (1F) turns before a bomb; angle (0A) bit 4 its last
// direction, bit 0 the sway's; speed (0B) nonzero once beaten; status
// (26) hits left.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Script positions the parts move to.
constexpr std::uint16_t first_form_wait = 0xB390, first_form_landed = 0xB3B3,
                        first_form_hop = 0xB3A3;
constexpr std::uint16_t first_form_arena = 0xB3D5, first_form_arrived = 0xB3DD;
constexpr std::uint16_t bomb_drop = 0xB0AB, sway = 0xB0F2, hatch_open = 0xB110,
                        beaten_drift = 0xB144;
constexpr std::uint16_t last_room = 0xB160, last_fight = 0xB193;
constexpr std::uint16_t core_broken = 0xB57A, core_blast = 0xB58A, pieces_blast = 0xB5A8;
constexpr std::uint16_t bomb_burst = 0xB644, hatch_pieces = 0xB710;

constexpr u8 machine_song = 0x8B;
constexpr u8 tiles_base = 0x2E;           // the bolts', sparks' and pieces' tiles (B9A8)
constexpr std::uint16_t floor_y = 0x026E; // the arena's and the room's floor
} // namespace

Object* Game::final_machine() {
    return act.boss ? &objects[act.boss - 1u] : nullptr;
}

bool Game::run_final_machine_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::machine_start: // B7CF
        machine_start(o);
        return true;
    case Routine::machine_wake: // B9ED
        o.next_state = 0x02;
        return true;
    case Routine::machine_to_first_form: // BA01
        o.next_state = 0x1A;
        return true;
    case Routine::machine_wait_player: // B3E9: the player past (680, 200): placed at (8A5, camera +
                                       // A0), ten hits; 5 seconds' wait (B390)
        player.in_scene = 0;
        if (objects[0].y < 0x0200 || objects[0].x < 0x0680) {
            return true;
        }
        jump_script(o, first_form_wait);
        o.frame_ticks = 0x01;
        o.x = 0x08A5;
        o.y = std::uint16_t(camera.y + 0x00A0);
        o.status_ticks = 0x0A;
        return true;
    case Routine::machine_first_form: // B429: hopping (1B) up and left; its song
        o.next_state = 0x1B;
        o.vx = 0xFF00;
        o.vy = 0xFC00;
        sound_.request(machine_song, 2);
        act.music_playing = machine_song;
        return true;
    case Routine::machine_clear_cooldown: // B448
        o.scratch[0] = 0;
        return true;
    case Routine::machine_hop: // B44D: falling (1/8); landed: the screen shakes (4), sound B2, a
                               // second's rest (B3B3)
        first_form_hit(o);
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o); // 5359 (0381)
        if (o.vy & 0x8000) {
            return true;
        }
        object_terrain(o); // 713D (0384)
        if (!(o.terrain & 0x02)) {
            return true;
        }
        jump_script(o, first_form_landed);
        o.frame_ticks = 0x01;
        camera.shake_ticks = 0x04;
        sound_.request(0xB2, 1);
        return true;
    case Routine::machine_rest: // B487
        first_form_hit(o);
        return true;
    case Routine::machine_hop_again: // B48B: up (4) and towards the player (1)
        jump_script(o, first_form_hop);
        o.frame_ticks = 0x01;
        o.vy = 0xFC00;
        o.vx = objects[0].x < o.x ? 0xFF00 : 0x0100;
        return true;
    case Routine::machine_run_off: // B4EB: past X 8A5: the camera free to the act's right end;
                                   // waiting (B3D5)
        move_object(o);
        if (o.x < 0x08A5) {
            return true;
        }
        jump_script(o, first_form_arena);
        o.frame_ticks = 0x01;
        camera.max_x = camera.act_max_x;
        return true;
    case Routine::machine_wait_arena: // B50E: the player past X 89A: the camera to (893, 1DB), it
                                      // at (976, 208); after this step, B3DD
        if (objects[0].x < 0x089A) {
            return true;
        }
        script_camera(0x0893, 0x01DB); // 4CDA (0321)
        o.x = 0x0976;
        o.y = 0x0208;
        jump_script(o, first_form_arrived); // the step's own ticks still run out
        return true;
    case Routine::machine_arena: // B53A: the second form (3); the camera held at (892, 198-1D4)
        o.next_state = 0x03;
        release_camera(); // 4CF2 (0324)
        camera.min_x = 0x0892;
        camera.max_x = 0x0892;
        camera.max_y = 0x01D4;
        camera.min_y = 0x0198;
        return true;
    case Routine::machine_descend: // BA06: coming in from the right; at X 913 following the player
                                   // (4)
        machine_hit(o);
        move_object(o);
        if (o.x >= 0x0913) {
            return true;
        }
        o.next_state = 0x04;
        return true;
    case Routine::machine_follow: // BA1E
        machine_follow(o);
        return true;
    case Routine::machine_rise_start: // B0CD
        o.y = std::uint16_t(o.y - 0x10);
        return true;
    case Routine::machine_rise: // BA98: up to Y 1B0, then down in the middle (D)
        move_object(o);
        if (o.y < 0x01B0) {
            o.next_state = 0x0D;
        }
        return true;
    case Routine::machine_drop_start: // BAAD: at X 910, falling at 1
        o.vx = 0;
        o.vy = 0x0100;
        o.x = 0x0910;
        o.speed = 0;
        return true;
    case Routine::machine_drop: // BAC9: down to Y 202: swaying there (B0F2), its height kept
        move_object(o);
        if (o.y < 0x0202) {
            return true;
        }
        jump_script(o, sway);
        o.frame_ticks = 0x01;
        o.origin_y = o.y;
        return true;
    case Routine::machine_sway_up: // BAF2
        o.vy = 0xFF00;
        return true;
    case Routine::machine_sway_down: // BAFB
        o.vy = 0x0100;
        return true;
    case Routine::machine_sway: // BB04
        machine_sway(o);
        return true;
    case Routine::machine_open: // BBA6: open, any touch beats it (10)
        touch_test(o);          // 58AC (0369)
        if (!(o.touch_sides & 0x0F)) {
            return true;
        }
        bounce_off(o); // 561B (0438)
        o.next_state = 0x10;
        o.speed = 0xFF;
        start_animator(0x0C); // 58A7 (050A)
        return true;
    case Routine::machine_close: // BB8C: swaying again at its height
        jump_script(o, sway);
        o.frame_ticks = 0x01;
        o.y = o.origin_y;
        return true;
    case Routine::machine_fall: // BBBE: beaten: falling (1/8) to Y 230, then off to the right
                                // (B144)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        if (o.y < 0x0230) {
            return true;
        }
        jump_script(o, beaten_drift);
        o.frame_ticks = 0x01;
        return true;
    case Routine::machine_drift: // BBEB: a screen right of the camera: gone to the last room (B160)
        move_object(o);
        if (std::uint16_t(camera.x + 0x0100 + wide_right) >= o.x) { // Widescreen: past the view
            return true;
        }
        jump_script(o, last_room);
        o.frame_ticks = 0x01;
        o.status_ticks = 0;
        return true;
    case Routine::machine_last_room: // BC0E: the camera held in the room (9B0-A10, 1D0); it at
                                     // (AC0, 24E)
        camera.min_y = 0x01D0;
        camera.max_y = 0x01D0;
        camera.min_x = 0x09B0;
        camera.max_x = 0x0A10;
        o.x = 0x0AC0;
        o.y = 0x024E;
        return true;
    case Routine::machine_to_last_form: // BC34
        o.next_state = 0x14;
        return true;
    case Routine::machine_last_hits: // BC39: eight hits
        o.status_ticks = 0x08;
        return true;
    case Routine::machine_wait_last: // B170: the player past X A30: the fight (B193)
        if (objects[0].x < 0x0A30) {
            return true;
        }
        jump_script(o, last_fight);
        o.frame_ticks = 0x01;
        return true;
    case Routine::machine_last_fly: // BC3E
        move_object(o);
        last_form_touch(o, false);
        return true;
    case Routine::machine_last_charge: // BC7A
        last_form_touch(o, true);
        return true;
    case Routine::machine_place_low: // BCBD
        o.x = 0x0AC0;
        o.y = 0x02CE;
        return true;
    case Routine::machine_place_high: // BCCE
        o.x = 0x0A00;
        o.y = 0x01CE;
        return true;
    case Routine::machine_flash: // BCDF: the screen flashes white (animator 10); the bolt may hurt
                                 // again
        start_animator(0x10);    // 259E (033C)
        o.scratch[4] = 0;
        return true;
    case Routine::machine_bolt: // BCE9: the bolt between X A28 and A98 hurts the player once
        if (o.scratch[4]) {
            return true;
        }
        if (objects[0].x <= 0x0A28 || objects[0].x > 0x0A98) {
            return true;
        }
        player.hit = 0xFF;
        o.scratch[4] = 0xFF;
        return true;
    case Routine::machine_trail: // B2F7: gone with the last form
        if (act.machine_down) {
            o.type = 0xFF;
        }
        return true;
    case Routine::machine_strike: // BD09: down to the floor (then gone), harmful every other frame
        if (o.y >= floor_y) {
            o.type = 0xFF;
            return true;
        }
        move_object(o);
        if (video.frame_counter & 0x01) {
            harmful_touch(o); // 550E (0375)
        }
        return true;
    case Routine::machine_hold: // B383
        o.frame_ticks = 0x20;
        return true;
    case Routine::core_start: // BD27: sixteen hits
        o.status_ticks = 0x10;
        return true;
    case Routine::core_follow: // BD2C
        core_hit(o);
        if (Object* m = final_machine()) {
            o.x = m->x;
        }
        return true;
    case Routine::core_broken: // BD42: the machine rises
        if (Object* m = final_machine()) {
            m->scratch[2] = 0xFF;
        }
        return true;
    case Routine::core_fall: // BD4D: falling (1/8) to the floor: the screen shakes (16), sound B2,
                             // it blows up (B58A)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        if (o.y < floor_y) {
            return true;
        }
        camera.shake_ticks = 0x10;
        sound_.request(0xB2, 1);
        jump_script(o, core_blast);
        o.frame_ticks = 0x01;
        return true;
    case Routine::machine_shot_start: { // BD84: none while the machine drops a bomb; down (1) and
                                        // away (1)
        const Object* m = final_machine();
        if (m && m->scratch[1]) {
            o.type = 0xFF;
            return true;
        }
        o.vx = (o.angle & 0x10) ? 0xFF00 : 0x0100;
        o.vy = 0x0100;
        return true;
    }
    case Routine::machine_bomb_start: // BDAF: the machine busy (35)
        if (Object* m = final_machine()) {
            m->scratch[1] = 0xFF;
        }
        return true;
    case Routine::machine_bomb_end: // BDBA
        if (Object* m = final_machine()) {
            m->scratch[1] = 0;
        }
        return true;
    case Routine::machine_bomb_fall: // BDC5: harmful on even frames; on odd ones, at the floor it
                                     // bursts (B644)
        move_object(o);
        if (!(video.frame_counter & 0x01)) {
            harmful_touch(o);
            return true;
        }
        if (o.y >= floor_y) {
            jump_script(o, bomb_burst);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::machine_spark: // BDE9
        o.display = u8((o.display & ~0x02) | 0x01);
        move_object(o);
        harmful_touch(o);
        return true;
    case Routine::hatch_blink: // BDF7
        hatch_hit(o);
        hatch_follow(o);
        return true;
    case Routine::hatch_follow: // BDFA
        hatch_follow(o);
        return true;
    case Routine::hatch_open: // BE41: the machine open (37 FF)
        if (Object* m = final_machine()) {
            m->scratch[3] = 0xFF;
        }
        return true;
    case Routine::machine_piece_start: // BE4C, BE90
        o.display = u8((o.display & ~0x02) | 0x01);
        return true;
    case Routine::machine_piece: { // BE55: harmful, gone with the machine or at the floor
        const Object* m = final_machine();
        if (!m || m->status_ticks == 0xFF) {
            o.type = 0xFF;
            return true;
        }
        harmful_touch(o);
        move_object(o);
        if (o.y >= floor_y) {
            o.type = 0xFF;
        }
        return true;
    }
    case Routine::machine_smoke: { // BE7E: gone once the machine's hits are 0
        const Object* m = final_machine();
        if (m && m->status_ticks) {
            return true;
        }
        o.type = 0xFF;
        return true;
    }
    case Routine::machine_debris: // BE99: falling (1/8)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        return true;
    case Routine::machine_part_bounce: // B753: falling (1/16) to the ground, then blowing up (B5A8)
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o);
        object_terrain(o);
        if (!(o.terrain & 0x02)) {
            return true;
        }
        jump_script(o, pieces_blast);
        o.frame_ticks = 0x01;
        return true;
    case Routine::machine_part_gone: // BF4C
        o.type = 0xFF;
        return true;
    default:
        return false;
    }
}

// B9F2 (state 2): the machine's art (7A33: list 1A:B8DE, with frame
// waits), palette 2A for the sprites, its flash colour.
Flow Game::machine_art(Object&) {
    co_await load_tile_list_slowly(Table{0x1A, 0xB8DE}); // 7A58
    palette.fades[1].mode |= 0x20;
    palette.fades[1].palette = 0x2A;
    set_boss_flash_color(); // 5888 (050D)
}

// 0510 (7A3A; state 3): the second form's art (list 1A:B8D8, with frame
// waits).
Flow Game::machine_core_art(Object&) {
    co_await load_tile_list_slowly(Table{0x1A, 0xB8D8}); // 7A58
}

// B7CF: the part named by the parameter.
void Game::machine_start(Object& o) {
    o.display |= 0x02;
    auto speeds = [&](std::uint16_t vx, std::uint16_t vy) {
        o.vx = vx;
        o.vy = vy;
    };
    switch (o.param) {
    case 0x00: // B862: the machine: 0A:BE3C at tile E1 (7B97)
        display.put_art(0x1C20, rom.bytes(0x0A, 0xBE3C, 0x40));
        o.next_state = 0x01;
        act.variant = 0x06;
        o.status_ticks = 0x06;
        act.boss = u8(slot_number(o) + 1);
        o.scratch = {};
        return;
    case 0x01:
        o.next_state = 0x05;
        return;
    case 0x02:
    case 0x03:
        o.next_state = 0x06;
        return;
    case 0x04:
        o.next_state = 0x07;
        return;
    case 0x05:
        o.next_state = 0x08;
        return;
    case 0x06:
        speeds(0xFE00, 0xFE00);
        o.next_state = 0x09;
        return;
    case 0x07:
        speeds(0x0200, 0xFE00);
        o.next_state = 0x09;
        return;
    case 0x08:
        o.next_state = 0x0A;
        return;
    case 0x09:
        o.vx = 0xFCC0;
        o.next_state = 0x0B;
        return;
    case 0x0A:
        o.vx = 0x0340;
        o.next_state = 0x0B;
        return;
    case 0x0B:
        o.next_state = 0x0E;
        return;
    case 0x0C:
        speeds(0xFE40, 0x0400);
        o.next_state = 0x0F;
        return;
    case 0x0D:
        speeds(0xFEE0, 0x0400);
        o.next_state = 0x0F;
        return;
    case 0x0E:
        speeds(0x0120, 0x0400);
        o.next_state = 0x0F;
        return;
    case 0x0F:
        speeds(0x01C0, 0x0400);
        o.next_state = 0x0F;
        return;
    case 0x10: // B91C (its read of address 0 has no effect)
        o.next_state = 0x11;
        return;
    case 0x11:
    case 0x12:
    case 0x13: { // B931: thrown up (3) from the machine, sound CE
        speeds(o.param == 0x11 ? 0xFF00 : o.param == 0x12 ? 0x0000 : 0x0100, 0xFD00);
        if (const Object* m = final_machine()) {
            o.x = m->x;
            o.y = m->y;
        }
        o.next_state = 0x12;
        sound_.request(0xCE, 1);
        return;
    }
    case 0x14:
        o.next_state = 0x13;
        return;
    case 0x15:
        o.next_state = 0x15;
        return;
    case 0x16:
        o.next_state = 0x16;
        return;
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A: // B976: the bolts at X A30-A90, Y 200
        o.x = std::uint16_t(0x0A30 + (o.param - 0x17) * 0x20);
        o.y = 0x0200;
        o.next_state = 0x17;
        o.tile_base = tiles_base;
        return;
    case 0x1B: { // B9AD: a spark somewhere in (A40-A7F, 220-25F), by chance (LD A,R)
        o.tile_base = tiles_base;
        o.next_state = 0x18;
        const u8 frames = video.frame_counter;
        o.x = std::uint16_t(0x0A40 + (u8(frames + refresh_register()) & 0x3F));
        o.y = std::uint16_t(0x0220 + (u8(u8(-frames) + refresh_register()) & 0x3F));
        return;
    }
    case 0x1C: // B9E4
        o.tile_base = tiles_base;
        o.next_state = 0x1D;
        return;
    default:
        throw Unrecovered("final machine part " + std::to_string(o.param) + " (B7CF)");
    }
}

// B4BF: the first form: after a hit 12 frames cannot hit; a rolling
// player's touch bounces him and costs a hit (the last: off, 1C).
void Game::first_form_hit(Object& o) {
    if (o.scratch[0]) {
        --o.scratch[0];
        return;
    }
    touch_test(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    bounce_off(o);
    if (!(objects[0].flags & 0x02)) {
        return;
    }
    start_animator(0x0C); // 58A7 (050A)
    o.scratch[0] = 0x0C;
    if (--o.status_ticks) {
        return;
    }
    o.next_state = 0x1C;
}

// BED3: a touch bounces the player and flashes (8 frames before the next);
// it costs nothing.
void Game::machine_hit(Object& o) {
    if (o.scratch[0]) {
        --o.scratch[0];
        return;
    }
    touch_test(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    bounce_off(o);
    o.scratch[0] = 0x08;
    start_animator(0x0C);
}

// BA1E: with the core broken it rises (C). Otherwise it speeds towards
// the player (1/32 a frame up to 1.5); each turn counts, and after three a
// bomb (B0AB).
void Game::machine_follow(Object& o) {
    if (o.scratch[2]) {
        o.next_state = 0x0C;
        return;
    }
    const bool right = objects[0].x >= o.x;
    if (right) {
        if ((o.vx & 0x8000) || o.vx <= 0x0180) {
            o.vx = std::uint16_t(o.vx + 0x08);
            const bool was = o.angle & 0x10;
            o.angle |= 0x10;
            if (!was) {
                machine_turned(o);
            }
        }
    } else if (!(o.vx & 0x8000) || o.vx > 0xFE80) {
        o.vx = std::uint16_t(o.vx - 0x08);
        const bool was = o.angle & 0x10;
        o.angle &= u8(~0x10);
        if (was) {
            machine_turned(o);
        }
    }
    move_object(o);
}

// BA87
void Game::machine_turned(Object& o) {
    if (--o.timer) {
        return;
    }
    jump_script(o, bomb_drop);
    o.frame_ticks = 0x01;
}

// BB04: once the hatch blinked open (37 FF): the script position kept in
// bytes 34-35, open (B110, 37 FE). Otherwise swaying between X 8F0 and
// 930 (1/32 a frame up to 1.25), turning at the ends (angle bit 0).
void Game::machine_sway(Object& o) {
    if (o.scratch[3] == 0xFF) {
        const std::uint16_t at = step_address(o);
        o.scratch[0] = lo(at);
        o.scratch[1] = hi(at);
        jump_script(o, hatch_open);
        o.frame_ticks = 0x01;
        o.scratch[3] = 0xFE;
        return;
    }
    if (!(o.angle & 0x01)) {
        if (!(o.vx & 0x8000) || o.vx > 0xFEC0) {
            o.vx = std::uint16_t(o.vx - 0x08);
        }
        if (o.x < 0x08F0) {
            ++o.angle;
        }
    } else {
        if ((o.vx & 0x8000) || o.vx <= 0x0140) {
            o.vx = std::uint16_t(o.vx + 0x08);
        }
        if (o.x >= 0x0930) {
            ++o.angle;
        }
    }
    move_object(o);
}

// BC3E / BC7A: the last form, 16 frames after a touch before the next. A
// touch from the side it moves towards hurts the player (D3C1); otherwise
// he bounces off. In the charge (BC7A) every touch counts: a lightning
// (15), the player thrown 5 the way it moves, sound C1. Each counted
// touch costs a hit; the last: DBD1, sinking (19).
void Game::last_form_touch(Object& o, bool charge) {
    if (o.scratch[5]) {
        --o.scratch[5];
        return;
    }
    touch_test(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (charge) {
        spawn_object(o, 0x66, 0x15); // 50E1 (0354)
        o.scratch[5] = 0x10;
        objects[0].vx = (o.vy & 0x8000) ? 0xFB00 : 0x0500;
        sound_.request(0xC1, 1);
    } else {
        o.scratch[5] = 0x10;
        const u8 sides = o.touch_sides & 0x0F;
        const u8 ahead = (o.vy & 0x8000) ? 0x01 : 0x02;
        if (sides & ahead) {
            player.hit = 0xFF;
            return;
        }
        bounce_off(o);
    }
    start_animator(0x0C);
    if (--o.status_ticks) {
        return;
    }
    act.machine_down = 0xFF;
    o.next_state = 0x19;
}

// BEF1: the core, while the machine is not busy: 8 frames after a hit
// before the next; the last of its hits breaks it (B57A).
void Game::core_hit(Object& o) {
    const Object* m = final_machine();
    if (m && m->scratch[1]) {
        return;
    }
    if (o.scratch[0]) {
        --o.scratch[0];
        return;
    }
    touch_test(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    bounce_off(o);
    o.scratch[0] = 0x08;
    start_animator(0x0C);
    if (--o.status_ticks) {
        return;
    }
    jump_script(o, core_broken);
    o.frame_ticks = 0x01;
}

// BF2A: the hatch: a touch flashes and bounces the player (8 frames
// before the next); its count has no use.
void Game::hatch_hit(Object& o) {
    if (o.scratch[0]) {
        --o.scratch[0];
        return;
    }
    touch_test(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    start_animator(0x0C);
    bounce_off(o);
    o.scratch[0] = 0x08;
    --o.status_ticks;
}

// BDFA: the hatch rides 16 below the machine; gone when the machine is or
// is beaten; when the machine opens (37 FE) it is seen (FD) and throws its
// pieces (B710).
void Game::hatch_follow(Object& o) {
    Object* m = final_machine();
    if (!m || m->speed) {
        o.type = 0xFF;
        return;
    }
    if (m->scratch[3] == 0xFE) {
        m->scratch[3] = 0xFD;
        jump_script(o, hatch_pieces);
        o.frame_ticks = 0x01;
        return;
    }
    o.x = m->x;
    o.y = std::uint16_t(m->y + 0x10);
}

} // namespace triple_trouble_gg
