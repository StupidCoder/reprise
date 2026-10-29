// Sonic's board (item 0C, player state 23, bank 0C A3CB-A4D8 and
// AF28-AF7F). Hypothesis: the snowboard of Robotnik Winter (zone 3), where
// the item was first met. Sonic rides at up to 6 pixels per frame, bounces
// on landing, jumps off with a button (the board flying off as type 34)
// and loses it against a wall. Landings throw up a spray (type 3A, bank 1E
// B92E-B99C): a puff of three (parameter 0 spawning two more, FF) that
// rises through frames 1-3 and back, then goes. The board left behind
// (type 34, B1A7-B1DE) jumps up at 3 and falls (gravity 1/8).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

bool Game::run_board_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::board_start: // A3CB
        board_start(o);
        return true;
    case Routine::board_frame_a: // AF28
        board_frame(o, 0);
        return true;
    case Routine::board_frame_b: // AF55
        board_frame(o, 1);
        return true;
    case Routine::board_ride: // A3E9
        board_ride(o);
        return true;
    case Routine::spray_start: // 1E:B982: unmirrored; parameter 0 state 1, others 2
        o.next_state = o.param ? 0x02 : 0x01;
        o.tile_base = 0xA6;
        o.display &= u8(~display_flag::mirrored);
        return true;
    case Routine::loose_board_start: // 1E:B1B7
        o.next_state = 0x01;
        o.vy = 0xFD00;
        o.tile_base = 0xA0;
        o.display = 0;
        return true;
    case Routine::loose_board_fall: // B1CC
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o); // 5359 (0381)
        return true;
    case Routine::spray_gone: // B998
        o.type = 0xFF;
        return true;
    default:
        return false;
    }
}

// A3CB: up at 3 and forward at 2.75. The landing note (34) starts as 3A:
// the byte read from A911 is an opcode (LD A,(nn)), so the first landing
// makes no spray.
void Game::board_start(Object& o) {
    o.vy = 0xFD00;
    o.vx = (o.display & display_flag::mirrored) ? 0xFD40 : 0x02C0;
    o.scratch[0] = 0x3A;
}

// AF28 / AF55: the frame (63 + 0-3) for 4 updates: tilted (2, or 3 in the
// second pose moving right) when rising or on landing attribute 0C moving
// left / 0A moving right; level (0 / 1) otherwise.
void Game::board_frame(Object& o, unsigned pose) {
    o.frame_ticks = 0x04;
    const bool left = o.vx & 0x8000;
    const u8 slope = left ? 0x0C : 0x0A;
    const u8 tilted = (pose && !left) ? 0x03 : 0x02;
    u8 b = tilted;
    if (probe.landed_attribute != slope && !(o.vy & 0x8000)) {
        b = u8(pose);
    }
    o.frame = u8(0x63 + b);
}

// A3E9
void Game::board_ride(Object& o) {
    // A landing (on the ground now, not the frame before): spray (type 3A)
    // and sound B6.
    if (!(o.scratch[0] & 0x02) && (o.terrain & 0x02)) {
        spawn_object_at(o, 0x3A, 0x00, o.x, o.y); // 510A (0357)
        sound_.request(0xB6, 1);
    }
    o.scratch[0] = o.terrain;
    // Gravity 1/4 up to 6 down.
    if (hi(o.vy) != 0x06) {
        o.vy = std::uint16_t(o.vy + 0x40);
    }
    // Ahead held: faster by 1/8 up to 6; let go: slower by 1/16 down to
    // 3 (A3, not below 0 the other way). A wall ahead ends the ride.
    const std::int16_t vx = std::int16_t(o.vx);
    if (!(o.display & display_flag::mirrored)) {
        if (o.terrain & 0x04) {
            lose_board(o);
            return;
        }
        if (input.held & 0x08) {
            if (vx < 0 || vx <= 0x0600) {
                o.vx = std::uint16_t(vx + 0x20);
            }
        } else if (vx >= 0 && vx <= 0x0300) {
            o.vx = std::uint16_t(vx - 0x10);
        }
    } else {
        if (o.terrain & 0x08) {
            lose_board(o);
            return;
        }
        if (input.held & 0x04) {
            if (vx >= 0 || std::uint16_t(vx) > 0xFA00) {
                o.vx = std::uint16_t(vx - 0x20);
            }
        } else if (vx < 0 && std::uint16_t(vx) > 0xFD00) {
            o.vx = std::uint16_t(vx + 0x10);
        }
    }
    face_input(o);   // B7B6
    keep_in_view(o); // B410
    // BF23: on the ground the landing attribute's slope pushes (bit 1:
    // left, else right) by 1/16.
    if ((o.terrain & 0x02) && probe.landed_attribute) {
        o.vx = std::uint16_t(o.vx + ((probe.landed_attribute & 0x02) ? 0xFFF0 : 0x0010));
    }
    move_object(o);    // 5359 (0381)
    player_terrain(o); // 5F14 (0393)
    // On the ground: bouncing (up at 5 with a button, else pressed down at
    // 3); in the air a button jumps off and the board flies on (type 34).
    const bool button = input.pressed & 0x30;
    if (o.terrain & 0x02) {
        o.vy = button ? 0xFB00 : 0x0300;
        return;
    }
    if (!button) {
        return;
    }
    spawn_object_at(o, 0x34, 0x00, o.x, o.y); // 510A (0357)
    jump(o);                                  // 3AD6 (0402)
}

// 5860 (0453): the power-up ends on the next update; in zone 3 the board
// flies off (type 34); falling (3B6F).
void Game::lose_board(Object& o) {
    player.power_up_ticks = 0x0001;
    if (flow.zone == 3) {
        spawn_object_at(o, 0x34, 0x00, o.x, o.y);
    }
    to_falling_state(o);
}

} // namespace triple_trouble_gg
