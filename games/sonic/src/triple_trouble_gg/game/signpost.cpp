// The act's end sign (type 1F, bank 1E, 98AD-9BA0): when it comes into
// view the camera is scripted to frame it; touched, it spins, lands and
// shows an item by the rings and lives (table 9AE7), which it awards; then
// the player walks off (state 20) to the act's tally.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr Table sign_faces{0x1E, 0x9AE7};
constexpr u8 spin_again = 0x1D;
constexpr u8 sign_sprite_palette = 0x40;
constexpr u8 clear_song = 0x89;
} // namespace

bool Game::run_sign_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::sign_start: // 9968
        sign_start(o);
        return true;
    case Routine::sign_wait: // 99DD: on screen, the camera stops going back
        if (o.display & display_flag::offscreen) {
            return true;
        }
        o.next_state = 0x02;
        hold_camera_left(); // 4D00 (031B)
        return true;
    case Routine::sign_frame: // 99E9
        sign_frame(o);
        return true;
    case Routine::sign_touch: // 9A4B
        sign_touch(o);
        return true;
    case Routine::sign_spin: // 9A81: up, then falling (gravity 1/8) until it lands (state 4)
        move_object(o);      // 5359 (0381)
        object_terrain(o);   // 713D (0384)
        if (o.terrain & 0x02) {
            o.next_state = 0x04;
        } else {
            o.vy = std::uint16_t(o.vy + 0x20);
        }
        return true;
    case Routine::sign_done: // 9B37
        sign_done(o);
        return true;
    case Routine::sign_drop: // 9B63: dropping in from above (gravity 1/16) until it lands (state 5)
        move_object(o);
        o.vy = std::uint16_t(o.vy + 0x10);
        object_terrain(o);
        if (o.terrain & 0x02) {
            o.next_state = 0x05;
        }
        return true;
    case Routine::sign_shake_left: // 9B83
        o.x = std::uint16_t(o.x - 2);
        return true;
    case Routine::sign_shake_right: // 9B92
        o.x = std::uint16_t(o.x + 2);
        return true;
    default:
        return false;
    }
}

// 9968: (zone 5: a player in state 40 falls); its sprite colours (not in
// zone 4's act 2); the camera no longer goes back; objects run plainly
// (no act music, D3EB); its art requested (list 10). Parameter nonzero:
// it drops from above the screen at the player's X (state 7); otherwise it
// waits to be seen (state 1).
void Game::sign_start(Object& o) {
    if (flow.zone == 5 && objects[0].state == 0x40) {
        objects[0].next_state = 0x0E;
    }
    if (!(flow.zone == 4 && flow.act == 1)) {
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = sign_sprite_palette;
    }
    hold_camera_left(); // 4D00 (031B)
    o.tile_base = o.tile_base_mirrored = 0x94;
    o.flags |= 0x80;
    o.display |= 0x02;
    act.object_mode = 0xFF;
    object_art.request = 0x10;
    if (!o.param) {
        o.next_state = 0x01;
        return;
    }
    o.param = 0;
    o.y = std::uint16_t(camera.y - 0x20 - wide_top); // Widescreen: above the presented view
    o.x = objects[0].x;
    o.next_state = 0x07;
}

// 99E9: within 176 lines of the camera's top the camera is scripted to
// frame the sign (its X - 80, Y - 99); in zone 1's act 2 to (F00, 152),
// with the rings cut to 5.
void Game::sign_frame(Object& o) {
    if (std::uint16_t(camera.y + 0xB0) < o.y) {
        return;
    }
    std::uint16_t tx = std::uint16_t(o.x - 0x80), ty = std::uint16_t(o.y - 0x99); // Y via D11C
    o.next_state = 0x03;
    if (flow.zone == 1 && flow.act == 1) {
        if ((score.rings_low | score.rings_high << 8) >= 0x0005) {
            score.rings_low = 0x05;
            score.rings_high = 0;
        }
        tx = 0x0F00;
        ty = 0x0152;
    }
    script_camera(tx, ty); // 4CDA (0321)
}

// 9A4B: touched: up at 4 and spinning (script 98E5, sound D8), the clock
// stops; in mode 11 slot 12 becomes a type 19 (parameter 5).
void Game::sign_touch(Object& o) {
    touch_test(o); // 58AC (0369)
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.vy = 0xFC00;
    jump_script(o, 0x98E5);
    o.frame_ticks = 0x01;
    clock.stopped = 0xFF;
    if (flow.mode != 0x11) {
        return;
    }
    objects[12].type = 0x19;
    objects[12].param = 0x05;
}

// 9AA3: the face it stops on, by the rings and lives (table 9AE7); face
// 1D spins once more (then 17); the face's art is requested and awarded.
Flow Game::sign_result(Object& o) {
    o.next_state = 0x05;
    const u8 index = u8(0x17 - u8(score.rings_low + score.lives)) & 0x0F;
    u8 face = rom.byte(sign_faces, index);
    if (face == spin_again) {
        if (o.param != spin_again) {
            object_art.request = spin_again;
            o.param = spin_again;
            co_return;
        }
        face = 0x17;
    }
    object_art.request = face;
    o.param = face;
    co_await sign_award(o); // 9AF8
}

// 9AF8: 11 (Sonic's face) and 12 (Tails') give the character shown a
// life (3342), the other 10000 points (242D); 18 keeps 50 rings for the next act; 16
// counts a reward; 17 gives ten rings.
Flow Game::sign_award(Object& o) {
    const u8 face = o.param;
    const bool sonic = objects[0].type == 0x01;
    if (face == 0x11 || face == 0x12) {
        if (sonic == (face == 0x11)) {
            co_await extra_life(); // 3342 (04E9)
            co_return;
        }
        // 242D: the other character's face: 10000 points (table 249E).
        add_points(std::array<u8, 3>{rom.byte(Table{0x00, 0x249E}, 0),
                                     rom.byte(Table{0x00, 0x249E}, 1),
                                     rom.byte(Table{0x00, 0x249E}, 2)});
        co_return;
    }
    if (face == 0x18) {
        flow.rings_kept = 0x50;
    } else if (face == 0x16) {
        ++score.point_rewards;
    } else if (face == 0x17) {
        add_ten_rings(); // 3396 (0432)
    }
}

// 9B37: once the player stands: face 1D waits to be touched again (state
// 3); otherwise the act is cleared: (zone 1 act 2: the sign shakes, state
// 8) state 6 and 3E5C: the clock stops, the clearing song, the player
// walks off (state 20; 49 from 48).
void Game::sign_done(Object& o) {
    if (!(objects[0].terrain & 0x02)) {
        return;
    }
    if (o.param == spin_again) {
        o.next_state = 0x03;
        return;
    }
    if (flow.zone == 1 && flow.act == 1) {
        o.next_state = 0x08;
        return;
    }
    o.next_state = 0x06;
    run_off_to_clear(); // 3E5C (03C0)
}

// 3E5C (03C0): the act is cleared: the clock stops, the clearing song, the
// player runs off (state 20; from 48, 49).
void Game::run_off_to_clear() {
    clock.stopped = 0xFF;
    sound_.request(clear_song, 2);
    auto& p = objects[0];
    p.next_state = p.state == 0x48 ? 0x49 : 0x20;
}

// 1E:9BA1 (state 8, Sunset Park act 2: the sign shakes and falls away):
// falling (1/8); below the screen it is gone and the train act begins
// without a tally: the train's art (7A41: list 1A:B7B1, with frame
// waits), its director (type 13) in the partner's slot, the camera no
// longer following, the HUD away (type 0B), no tile animations, no ring
// art.
Flow Game::sign_fall_away(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x20);
    move_object(o);                                              // 5359 (0381)
    if (std::uint16_t(camera.y + 0x00E0 + wide_bottom) >= o.y) { // Widescreen: below the view
        co_return;
    }
    o.type = 0xFF;
    di();
    co_await load_tile_list_slowly(Table{0x1A, 0xB7B1}); // 7A58 (04AD: 7A41)
    objects[1].type = 0x13;
    camera.map_flags &= u8(~map_flag::enabled);
    spawn_object(o, 0x0B, 0x00); // 50E1 (0354)
    for (auto& a : animation.animators) {
        a.type = 0;
    }
    animation.ring_vram = 0;
}

} // namespace triple_trouble_gg
