// The rival (type 8, bank 16, B9CD-BFFF) in his scenes: in Atomic
// Destroyer act 3 and in zone 6 act 1. Each act has a list of his states
// (table BD0A), stepped through by BCDB; the states are short acts: walk
// or run (with the camera following), brake, face a way, hop, shake the
// screen, wait for the player to come near (who is then held, player state
// 4A), point the camera, leave. In other acts his list starts with
// "removed" (state 9). For his scene the HUD slides off the screen
// (type 0B, bank 1E, 886C-88D8). In zone 6 he stands by a prop (type 38,
// 1E:B71F-B772) that drops 6 when he lands a hop (his byte 1F).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// BC4B: per zone (12 bytes) and act (4) his start (X, Y); BD0A: per zone
// (6 bytes) and act (2) the address of his list of states.
constexpr Table rival_starts{0x16, 0xBC4B};
constexpr Table rival_lists{0x16, 0xBD0A};
// BFF2: the speeds of the pieces thrown in BFBE.
constexpr Table piece_speeds{0x16, 0xBFF2};
constexpr u8 meet_song = 0x97;
} // namespace

bool Game::run_rival_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::rival_start: { // BC08: at his start; the act's boss (D3E5) is he; tiles at 10
        const Table start = rival_starts + (flow.zone * 12u + flow.act * 4u);
        o.x = rom.word(start, 0);
        o.y = rom.word(start, 2);
        act.boss = u8(slot_number(o) + 1);
        o.tile_base = o.tile_base_mirrored = 0x10;
        o.counter = 0;
        rival_next(o);
        return true;
    }
    case Routine::rival_next: // BCDB (BB67)
        rival_next(o);
        return true;
    case Routine::rival_hold_camera: // BCCF: the lowest camera Y becomes the camera's now
        camera.max_y = camera.y;
        o.scratch[0] = lo(camera.y);
        o.scratch[1] = hi(camera.y);
        rival_next(o);
        return true;
    case Routine::rival_stride: { // BD7D: the next walking frame (1-6), shown for 6 minus his speed
        const u8 h = hi(o.vx);
        o.frame_ticks = u8(6 - ((h & 0x80) ? u8(-h) : h));
        if (++o.frame >= 0x07) {
            o.frame = 0x01;
        }
        return true;
    }
    case Routine::rival_run_to: // BD9C: running until X 11D0
        if (o.x >= 0x11D0) {
            player.in_scene = 0;
            rival_next(o);
            return true;
        }
        rival_run(o);
        return true;
    case Routine::rival_run: // BDAE
        rival_run(o);
        return true;
    case Routine::rival_brake: // BDEF
        rival_brake(o);
        return true;
    case Routine::rival_face_left: // BE1A (his art faces left; mirrored he faces right)
        o.display &= u8(~display_flag::mirrored);
        return true;
    case Routine::rival_face_right: // BE1F
        o.display |= display_flag::mirrored;
        return true;
    case Routine::rival_restart: // BE24: his list from its start
        o.counter = 0;
        return true;
    case Routine::rival_pause: // BFAC
        o.frame_ticks = 0x02;
        return true;
    case Routine::rival_leave: // BE29
        rival_leave(o);
        return true;
    case Routine::rival_meet: // BE5F
        rival_meet(o);
        return true;
    case Routine::rival_remove: // BEA2: gone; the partner's art held back, the common art again
        o.type = 0xFF;
        player.partner_hidden = 0xFF;
        rival_restore_art(); // 762C (04A1)
        return true;
    case Routine::rival_hop: // BF04: falling (1/8); landing he hops on (script BAA7)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o); // 5359 (0381)
        if (o.vy & 0x8000) {
            return true;
        }
        object_terrain(o); // 713D (0384)
        if (o.terrain & 0x02) {
            o.timer = 0xFF;
            jump_script(o, 0xBAA7);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::rival_hop_on: // BF2D: the same; landing: his next state
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        if (o.vy & 0x8000) {
            return true;
        }
        object_terrain(o);
        if (o.terrain & 0x02) {
            rival_next(o);
        }
        return true;
    case Routine::rival_stamp: // BF4A: the player freed, the screen shakes (4), sound B2
        player.in_scene = 0;
        camera.shake_ticks = 0x04;
        sound_.request(0xB2, 1);
        return true;
    case Routine::rival_art_back: // BF59
        player.partner_hidden = 0xFF;
        rival_restore_art();
        rival_next(o);
        return true;
    case Routine::rival_wait_boss: // BF65: once the boss signals (D3FF): rings vanish, he goes
                                   // where the boss is
        if (!act.boss_signal) {
            return true;
        }
        act.rings_gone_too = 0xFF;
        if (act.boss) {
            const auto& boss = objects[act.boss - 1u];
            o.x = boss.x;
            o.y = boss.y;
        }
        player.partner_hidden = 0;
        rival_next(o);
        return true;
    case Routine::rival_drop: // BF94: falling (1/16) to the ground
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o);
        object_terrain(o);
        if (o.terrain & 0x02) {
            rival_next(o);
        }
        return true;
    case Routine::rival_to_corner: // BFB1
        o.x = 0x0080;
        o.y = 0x00A0;
        return true;
    case Routine::rival_scatter: // BFBE
        rival_scatter(o);
        return true;
    case Routine::rival_shuffle: // BB8F: a pixel every other frame the way he faces
        if (!(video.frame_counter & 1)) {
            return true;
        }
        o.x = std::uint16_t((o.display & display_flag::mirrored) ? o.x + 1 : o.x - 1);
        return true;
    case Routine::knuckles_act_wait: // 1E:B4D7: seen: the zone's scene (state zone + 2)
        if (!(o.display & display_flag::offscreen)) {
            o.next_state = u8(flow.zone + 2);
        }
        return true;
    case Routine::knuckles_act_enter: { // B4E5: no pause; the camera to the scene; he runs in from
                                        // 256 right of it at 1.125
        act.no_pause = 0xFF;
        std::uint16_t cx = 0x06F0, cy = 0x0130;
        if (flow.zone == 2) {
            cx = 0x0716;
            cy = 0x02BD;
        } else if (flow.zone == 3) {
            cx = 0x0A80;
            cy = 0x031A;
        }
        script_camera(cx, cy); // 4CDA (0321)
        o.x = std::uint16_t(cx + 0x0100);
        o.vx = 0xFEE0;
        return true;
    }
    case Routine::knuckles_act_run: { // B51C: running left to his mark (7A8; zone 2: 7C8; zone 3:
                                      // B30), then the scene (script B225)
        move_object(o);               // 5359 (0381)
        const std::uint16_t mark = flow.zone == 2 ? 0x07C8 : flow.zone == 3 ? 0x0B30 : 0x07A8;
        if (o.x < mark) {
            jump_script(o, 0xB225);
            o.frame_ticks = 0x01;
        }
        return true;
    }
    case Routine::knuckles_act_hop:      // B54A: falling (1/4); landing: script B24A
    case Routine::knuckles_act_hop_back: // B577: the same; landing: script B25C
        o.vy = std::uint16_t(o.vy + 0x40);
        move_object(o);
        object_terrain(o); // 713D (0384)
        if (!(o.vy & 0x8000) && (o.terrain & 0x02)) {
            jump_script(o, r == Routine::knuckles_act_hop ? 0xB24A : 0xB25C);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::knuckles_act_signal: // B5A4: the zone's tile animation (D490: 9; zone 3: 11)
        animation.animators[0].type = flow.zone == 3 ? 0x11 : 0x09;
        return true;
    case Routine::prop_start: // 1E:B737: tiles 9C, 8 further right
        o.next_state = 0x01;
        o.tile_base = o.tile_base_mirrored = 0x9C;
        o.x = std::uint16_t(o.x + 8);
        return true;
    case Routine::prop_wait: // B754
        if (act.boss && objects[act.boss - 1u].timer) {
            o.next_state = 0x02;
            o.y = std::uint16_t(o.y + 6);
        }
        return true;
    case Routine::hud_slide_start: // 1E:8882: kept off screen
        o.next_state = 0x01;
        o.display |= 0x02;
        return true;
    case Routine::hud_slide: // 888B: the HUD's sprites (entries 51-63) 2 higher a frame; past the
                             // top: gone, the HUD's entries free (DBC3)
        for (unsigned e = 51; e <= 63; ++e) {
            sprites.table[e].y = u8(sprites.table[e].y - 2);
        }
        if (sprites.table[54].y < 0x08) {
            o.type = 0xFF;
            sprites.hud_small = 0xFF;
        }
        return true;
    case Routine::hold_player: // 3F76 (04FE): the player held by the scene (state 4A)
        player.in_scene = 0xFF;
        objects[0].next_state = 0x4A;
        return true;
    default:
        return false;
    }
}

// BCDB: his next state from the act's list.
void Game::rival_next(Object& o) {
    const Table list{0x16, rom.word(rival_lists, flow.zone * 6u + flow.act * 2u)};
    o.next_state = rom.byte(list, o.counter);
    ++o.counter;
}

// BDAE: moving on the ground (in act 1 the player is no longer held),
// speeding up by 1/8 toward 4 the way he faces (mirrored: right).
void Game::rival_run(Object& o) {
    if (flow.act == 0) {
        player.in_scene = 0;
    }
    move_object(o);    // 5359 (0381)
    object_terrain(o); // 713D (0384)
    const std::int16_t v = std::int16_t(o.vx);
    if (o.display & display_flag::mirrored) {
        if (v < 0 || v <= 0x0400) {
            o.vx = std::uint16_t(v + 0x20);
        }
    } else if (v >= 0 || std::uint16_t(v) > 0xFC00) {
        o.vx = std::uint16_t(v - 0x20);
    }
}

// BDEF: braking by 1/4 to a stop, then his next state.
void Game::rival_brake(Object& o) {
    move_object(o);
    object_terrain(o);
    const std::uint16_t v = o.vx;
    if (v & 0x8000) {
        const unsigned sum = unsigned(v) + 0x40;
        if (sum < 0x10000) {
            o.vx = std::uint16_t(sum);
            return;
        }
    } else if (v >= 0x40) {
        o.vx = std::uint16_t(v - 0x40);
        return;
    }
    o.vx = 0;
    rival_next(o);
}

// BE29: once off screen: gone (state 9); the camera follows the player
// again from where it is, with the act's limits back (in zone 5 the right
// limit 7A5).
void Game::rival_leave(Object& o) {
    if (!(o.display & display_flag::offscreen)) {
        return;
    }
    o.next_state = 0x09;
    camera.min_x = camera.x;
    release_camera(); // 4CF2 (0324)
    if (flow.zone == 5) {
        camera.max_x = 0x07A5;
        return;
    }
    camera.max_x = camera.act_max_x;
    camera.min_y = camera.act_min_y;
    camera.max_y = std::uint16_t(o.scratch[0] | o.scratch[1] << 8);
}

// BE5F: waiting until the player is within C0 across and A0 up or down
// (in zone 6 act 1 a Tails within 180 across stops flying, 53BB): the
// meeting's song, the player held (3F76); outside act 3 an object 0B.
void Game::rival_meet(Object& o) {
    const auto near = [](std::uint16_t a, std::uint16_t b, unsigned range) { // 543A
        const std::uint16_t d = std::uint16_t(a - b);
        return ((d & 0x8000) ? std::uint16_t(-d) : d) < range;
    };
    auto& p = objects[0];
    if (flow.zone == 6 && flow.act == 0 && near(o.x, p.x, 0x0180)) {
        // 53BB (03B4): a flying Tails stops flying.
        if (p.type != 0x01 && p.next_state == 0x18) {
            p.next_state = 0x0E;
        }
    }
    if (!near(o.x, p.x, 0x00C0) || !near(o.y, p.y, 0x00A0)) { // 5424, 5430
        return;
    }
    sound_.request(meet_song, 2);
    player.in_scene = 0xFF; // 3F76 (04FE)
    p.next_state = 0x4A;
    if (flow.act != 2) {
        spawn_object(o, 0x0B, 0x00); // 50E1 (0354)
    }
    rival_next(o);
}

// BEAF: the clock stops; in act 3 the camera goes to (700, 1D0); otherwise
// object 38 is placed at (1368, EE), more art is loaded (79A2, 79E7) and
// the camera goes to him (his place minus B0, 50), a flying Tails stops.
Flow Game::rival_scene_camera(Object& o) {
    clock.stopped = 0xFF;
    if (flow.act == 2) {
        script_camera(0x0700, 0x01D0); // 4CDA (0321)
        rival_next(o);
        co_return;
    }
    spawn_object_at(o, 0x38, 0x00, 0x1368, 0x00EE);                          // 510A (0357)
    load_tiles(0x1380, 0x0A, 0xBD9A);                                        // 79A2 (0465)
    co_await reload_sprite_art();                                            // 79E7 (048F)
    script_camera(std::uint16_t(o.x - 0x00B0), std::uint16_t(o.y - 0x0050)); // 4CDA (0321)
    auto& p = objects[0];
    if (p.type != 0x01 && p.next_state == 0x18) { // 53BB (03B4): a flying Tails stops flying
        p.next_state = 0x0E;
    }
    rival_next(o);
}

// 79E7 (048F): the rings' art (0A:A370 at tile A2) and 06:BA8C at tile AC
// again, with frame waits (D12C).
Flow Game::reload_sprite_art() {
    video.loading = 0xFF;
    co_await load_tiles_slowly(0x1440, 0x0A, 0xA370);
    co_await load_tiles_slowly(0x1580, 0x06, 0xBA8C);
    video.loading = 0;
}

// 762C: the common art (1D:8A94) at tile 10 again.
void Game::rival_restore_art() {
    load_tiles(0x0200, 0x1D, 0x8A94);
}

// BFBE: as many pieces (type 30, parameters E on) as six minus D17E fly
// out at speeds -1 to 1.5 (table BFF2); the music stops.
void Game::rival_scatter(Object& o) {
    if (flow.emeralds >= 0x06) {
        return;
    }
    const unsigned count = 0x06 - flow.emeralds;
    for (unsigned i = 0; i < count; ++i) {
        Object* piece = spawn_object(o, 0x30, u8(0x0E + i)); // 50E1 (0354)
        if (piece) {
            piece->vx = rom.word(piece_speeds, i * 2u); // BFDD
        }
    }
    stop_music(); // 2288 (033F)
}

} // namespace triple_trouble_gg
