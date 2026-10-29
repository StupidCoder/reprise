// The intro's objects (mode 1): the emeralds fall, Knuckles hops from one
// to the next taking them, Sonic runs after him and turns into Tails flying
// off, and Eggman's ship follows, leaving a trail of sparkles.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Scratch byte 34: set by another object to start this one.
constexpr unsigned trigger = 0;
} // namespace

// 0C:985D (sonic_start in the intro): Sonic waits at the origin (state 38);
// any other player type flies in from (50, 50) (state 39).
void Game::sonic_intro_place(Object& o) {
    if (o.type == 1) {
        o.next_state = 0x38;
        o.x = 0;
        o.y = 0;
    } else {
        o.next_state = 0x39;
        o.x = 0x0050;
        o.y = 0x0050;
    }
}

// 0C:88B0: once Knuckles gives the signal, the script goes on past its loop
// (889B).
void Game::sonic_intro_wait(Object& o) {
    if (!o.scratch[trigger]) {
        return;
    }
    jump_script(o, 0x889B);
    o.frame_ticks = 1;
}

// 0C:88C2: the camera follows; Sonic runs at 3 pixels per frame from Y AE.
void Game::sonic_intro_start(Object& o) {
    enable_map(); // 030F
    o.vx = 0x0300;
    o.y = 0x00AE;
}

// 0C:88D6: faster each frame; past X 500 the camera stops, past X 600 he
// becomes Tails (type 2, state 39).
void Game::sonic_intro_run(Object& o) {
    o.vx = std::uint16_t(o.vx + 1);
    move_object(o);
    if (o.x > 0x0600) {
        o.type = 0x02;
        o.next_state = 0x39;
        return;
    }
    if (o.x > 0x0500) {
        disable_map(); // 0312
    }
}

// 0C:950F: Tails starts at the camera plus (20, 20), rising.
void Game::tails_intro_start(Object& o) {
    auto& p = objects[0];
    p.x = std::uint16_t(camera.x + 0x20);
    p.y = std::uint16_t(camera.y + 0x20);
    p.vx = 0x0100;
    p.vy = 0x0300;
    (void)o;
}

// 0C:9530: the climb steepens and speeds up; past X 600 the script goes on
// past its loop (94FF: Eggman's ship appears).
void Game::tails_intro_fly(Object& o) {
    auto& p = objects[0];
    p.vy = std::uint16_t(p.vy - 0x10);
    p.vx = std::uint16_t(p.vx + 4);
    move_object(o);
    if (p.x < 0x0600) {
        return;
    }
    jump_script(o, 0x94FF);
    o.frame_ticks = 1;
}

// 1E:ADE6: type 30 (an emerald). Parameters 0E-12 start in that state at
// (80, A0) rising; FE and FF in state 7. Otherwise (outside zone 6 with
// tile base D0) at the camera's Y: parameter 0 (the first) at X 50 in
// state 1, others in state parameter + 1.
void Game::emerald_start(Object& o) {
    o.display &= u8(~display_flag::mirrored);
    o.display |= display_flag::flicker | 0x02;
    o.flags |= 0x80;
    const u8 param = o.param;
    if (param >= 0x0E && param < 0x13) {
        o.next_state = param;
        o.vy = 0xFD00;
        o.tile_base = 0x24;
        o.x = 0x0080;
        o.y = 0x00A0;
        return;
    }
    if (param == 0xFE) {
        throw Unrecovered("emerald parameter FE (AEE4)");
    }
    o.origin_x = o.x;
    o.origin_y = o.y;
    if (flow.zone != 6) {
        o.tile_base = 0xD0;
        o.tile_base_mirrored = 0xD0;
    }
    o.counter = 0;
    if (param == 0xFF) {
        o.next_state = 0x07;
        return;
    }
    o.y = camera.y;
    if (!param) {
        o.param = 0x01;
        o.x = 0x0050;
        o.next_state = 0x01;
        return;
    }
    o.next_state = u8(param + 1);
}

// 1E:AE86: falling; on the floor the others appear (state 2).
void Game::emerald_drop(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x20);
    move_object(o);
    object_terrain(o); // 0384
    if (o.terrain & 0x02) {
        o.next_state = 0x02;
    }
}

// 1E:AED8: the emerald Knuckles has just taken (counted in D171)
// disappears.
void Game::emerald_taken(Object& o) {
    if (flow.restart_point == o.param) {
        o.type = 0xFF;
    }
}

// 1E:AEAA: falling until on the floor (counter FF).
void Game::emerald_fall(Object& o) {
    emerald_taken(o);
    if (o.counter) {
        return;
    }
    o.vy = std::uint16_t(o.vy + 0x28);
    move_object(o);
    object_terrain(o);
    if (o.terrain & 0x02) {
        o.counter = 0xFF;
    }
}

// 1E:ADB7 (states E-12): an emerald the rival tosses (thrown up at 3 from
// (80, A0)): falling (1/8) until it comes down to Y A0, then still.
void Game::emerald_toss(Object& o) {
    if (o.scratch[0]) {
        return;
    }
    o.vy = std::uint16_t(o.vy + 0x20);
    move_object(o);
    if (o.vy & 0x8000) {
        return;
    }
    if (o.y >= 0x00A0) {
        o.scratch[0] = 0xFF;
    }
}

// B478: at a zone's end Knuckles waits (state 1) by his prop (type 38) at
// a place by zone (zone 2: 7A8, 30E; zone 3: B08, 36E; else 78C, 16E);
// he is the act's "boss" (D3E5).
void Game::knuckles_in_act(Object& o) {
    act.boss = u8(slot_number(o) + 1);
    std::uint16_t x = 0x078C, y = 0x016E;
    if (flow.zone == 2) {
        x = 0x07A8;
        y = 0x030E;
    }
    if (flow.zone == 3) {
        x = 0x0B08;
        y = 0x036E;
    }
    spawn_object_at(o, 0x38, 0x00, x, y); // 510A (0357)
    o.next_state = 0x01;
    o.y = y;
}

// 1E:B462: type 35 (Knuckles): kept on screen and not drawn at first; on
// the title (mode 2) state 0A, in the intro (zone 7) state 9 at (80, 60).
void Game::knuckles_start(Object& o) {
    o.display |= 0x02 | display_flag::offscreen;
    if (flow.mode == mode::title) {
        o.next_state = 0x0A; // B4CA
        o.tile_base = 0x14;
        o.tile_base_mirrored = 0x14;
        return;
    }
    if (flow.zone != 7) {
        knuckles_in_act(o); // B478
        return;
    }
    o.next_state = 0x09; // B4AD
    o.tile_base = 0x10;
    o.tile_base_mirrored = 0x10;
    o.x = 0x0080;
    o.y = 0x0060;
}

// 1E:B311: at the camera's X + 20, Y 80; the button code not entered.
void Game::knuckles_intro_place(Object& o) {
    o.x = std::uint16_t(camera.x + 0x20);
    o.y = 0x0080;
    flow.intro_secret = 0;
}

// 1E:B32B: on the emerald's signal the script goes on past its loop (B288,
// hopping) with speed (100, 0).
void Game::knuckles_intro_wait(Object& o) {
    if (!o.scratch[trigger]) {
        return;
    }
    jump_script(o, 0xB288);
    o.frame_ticks = 1;
    o.vx = 0x0100;
    o.vy = 0;
    o.counter = 0;
    o.scratch[2] = 0;    // 36: the bobbing direction (B42A)
    o.scratch[3] = 0x20; // 37: and its frames left
}

// 1E:B356: hops from emerald to emerald; Up and Start held while he is at
// X 6E-71 enter the button code (DC98, sound C9). Each landing takes one
// (D171); after the fifth the script goes on (B290).
void Game::knuckles_intro_hop(Object& o) {
    if (input.held == 0x81 && lo(o.x) >= 0x6E && lo(o.x) < 0x72) {
        flow.intro_secret = 0x01;
        sound_.request(0xC9, 1);
    }
    o.vy = std::uint16_t(o.vy + 0x30);
    move_object(o);
    object_terrain(o); // 0384
    if (!(o.terrain & 0x02)) {
        return;
    }
    sound_.request(0xD5, 1);
    if (++flow.restart_point == 0x05) {
        jump_script(o, 0xB290);
        o.frame_ticks = 1;
        return;
    }
    o.vy = 0xFCD0;
    o.vx = 0x00E0;
}

// 1E:B3E5: speeding up to 3 pixels per frame while bobbing (B42A); then
// swaying, the speed changing by +5 or -3 every frame, the direction
// turning every 32 frames.
void Game::knuckles_intro_fly(Object& o) {
    std::uint16_t v = o.vx;
    if (hi(v) < 0x03) {
        o.vx = std::uint16_t(v + 0x0C);
        knuckles_bob(o); // B42A
        move_object(o);
        return;
    }
    o.vx = std::uint16_t(v + ((o.timer & 1) ? 0xFFFD : 0x0005));
    move_object(o);
    if (++o.counter < 0x20) {
        return;
    }
    ++o.timer;
    o.counter = 0;
}

// 1E:B42A: the vertical speed changes by +8 for 22 frames, then by -10 for
// 32 frames, and so on.
void Game::knuckles_bob(Object& o) {
    u8& direction = o.scratch[2];
    u8& left = o.scratch[3];
    if (!(direction & 1)) {
        o.vy = std::uint16_t(o.vy + 0x0008);
        if (--left) {
            return;
        }
        left = 0x16;
    } else {
        o.vy = std::uint16_t(o.vy - 0x0010);
        if (--left) {
            return;
        }
        left = 0x20;
    }
    ++direction;
}

// 1E:B7EF: type 39 (a sparkle): parameters FD and FC start in states 6
// and 7; it remembers its 32-pixel cell (bytes 34-37); parameter 0 floats
// down (state 1) with tile base F8 and sound BD; others in state 2, or
// parameter + 1 for 2-4, with tile base 90 (BC in act 3).
void Game::sparkle_start(Object& o) {
    o.display &= u8(~0x02);
    if (o.param == 0xFD) {
        o.next_state = 0x06;
        return;
    }
    if (o.param == 0xFC) {
        o.next_state = 0x07;
        return;
    }
    o.scratch[1] = hi(o.x);
    o.scratch[0] = lo(o.x) & 0xE0;
    o.scratch[3] = hi(o.y);
    o.scratch[2] = u8((lo(o.y) & 0xE0) | 0x10);
    if (!o.param) {
        o.next_state = 0x01;
        o.tile_base = 0xF8;
        o.tile_base_mirrored = 0xF8;
        o.display &= u8(~display_flag::mirrored);
        o.vy = 0x0100;
        sound_.request(0xBD, 1);
        return;
    }
    o.display &= u8(~display_flag::mirrored);
    o.tile_base = 0x90;
    o.tile_base_mirrored = 0x90;
    if (flow.act == 2) {
        o.tile_base = 0xBC;
        o.tile_base_mirrored = 0xBC;
    }
    const u8 p = o.param;
    o.next_state = (p == 2 || p == 3 || p == 4) ? u8(p + 1) : 0x02;
}

// 1E:B8C7 / B8ED / B8A4 (type 39, parameter 1): a bumper's light: its
// block (the cell in bytes 38-39, drawn at bytes 34-37) shows 86, then 87,
// then 0 (gone off screen: 0 at once); then the object is gone.
void Game::bumper_light(Object& o, u8 block) {
    if (o.display & display_flag::offscreen) {
        block = 0x00;
    }
    probe.block_x = std::uint16_t(o.scratch[0] | o.scratch[1] << 8);
    probe.block_y = std::uint16_t(o.scratch[2] | o.scratch[3] << 8);
    probe.cell = std::uint16_t((o.scratch[4] | o.scratch[5] << 8) - 0xC000);
    set_map_block_at(probe.cell, block, probe.block_x, probe.block_y); // 6580 (0348)
    if (!block) {
        o.type = 0xFF; // B898
    }
}

// 1E:BF55 / BF68 (type 3F): a pressed switch stays down while the player
// stands on it (block 94); then it is block 93 again and the object goes.
void Game::switch_wait(Object& o) {
    if ((objects[0].terrain & 0x02) && terrain_state.block == 0x94) {
        return;
    }
    o.next_state = 0x02;
}

void Game::switch_release(Object& o) {
    probe.block_x = o.x;
    probe.block_y = o.y;
    probe.cell = std::uint16_t((o.scratch[4] | o.scratch[5] << 8) - 0xC000);
    set_map_block_at(probe.cell, 0x93, probe.block_x, probe.block_y); // 6580 (0348)
    o.type = 0xFF;
}

// 17:8B6D: type 4F (Eggman's ship in the intro): at the camera's X and Y +
// 70, facing right, tile base 10; its art is streamed in (request 19).
void Game::ship_intro_start(Object& o) {
    o.next_state = 0x01;
    o.display |= 0x02 | display_flag::mirrored;
    o.x = camera.x;
    o.y = std::uint16_t(camera.y + 0x70);
    object_art.request = 0x19;
    o.tile_base = 0x10;
    o.tile_base_mirrored = 0x10;
}

} // namespace triple_trouble_gg
