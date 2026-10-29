// Item box power-ups (bank 0C, B811-BA5D): an item noted by a broken box
// (D3E3) starts on the player's next update; timed ones count down in
// D3E4 and end with the act's music again. Several start with a song and a
// frame wait (0300 = 084C), so this runs in the player's update coroutine.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr u8 sneakers = 0x0D;
constexpr u8 sneakers_song = 0x90;
constexpr u8 invincible_song = 0x92, tails_flight_song = 0x98;
} // namespace

// B811: items 1, 3, 0B and 4 act at once; a running power-up counts down
// (0C, 1, 3, 8, 0E end at once; 9 keeps; the sneakers hold the top speed
// at 6.75); otherwise a waiting item starts.
Flow Game::player_power_ups() {
    const u8 pending = player.power_up_pending;
    if (pending == 0x0B || pending == 0x04) {
        player.power_up_pending = 0; // B993: an extra life (3342, 04E9)
        co_await extra_life();
        co_return;
    }
    if (pending == 0x03) {
        co_await restart_point_item(); // B92B
        co_return;
    }
    if (pending == 0x01) {
        // B8E4: with 50 rings or more (and not after a special stage) the
        // way into the special stage (type 0E) appears 72 above the player.
        player.power_up_pending = 0;
        const unsigned rings = score.rings_low | score.rings_high << 8;
        if (rings < 0x50 || flow.special_entry) {
            co_return;
        }
        auto& p = objects[0];
        spawn_object_at(p, 0x0E, 0x00, p.x, std::uint16_t(p.y - 0x48)); // 510A (0357)
        co_return;
    }
    auto& p = objects[0];
    const u8 power = p.power_up;
    if (!power) {
        // B8A4
        if (!pending) {
            co_return;
        }
        player.power_up_pending = 0;
        p.power_up = pending;
        co_await start_power_up(pending);
        co_return;
    }
    if (power == 0x0C || power == 0x01 || power == 0x03 || power == 0x08 || power == 0x0E) {
        p.power_up = 0; // B84A
        co_return;
    }
    if (power == 0x09) {
        co_return;
    }
    if (power == sneakers) {
        player.top_speed = 0x06C0;
    }
    if (--player.power_up_ticks) {
        co_return;
    }
    co_await end_power_up();
}

// B92B: item 3: the act restarts here after a lost life: sound D1, a
// frame's wait, then the player's place (32 higher), the camera and the
// clock are kept (D169-D17D).
Flow Game::restart_point_item() {
    player.power_up_pending = 0;
    sound_.request(0xD1, 1);
    co_await next_frame(); // 0300
    const auto& p = objects[0];
    flow.checkpoint = {p.x, std::uint16_t(p.y - 0x20)};
    flow.restart_point = 0xFF;
    flow.checkpoint_camera = {camera.x, camera.y};
    flow.checkpoint_time = {score.seconds, score.minutes, score.clock_extra};
}

// 3342: a life more (BCD); unless a power-up's song plays (0D, 05, 0F,
// 0A, 09) the extra life song with a frame's wait, the act's song back
// after 120 frames (D3EE); the HUD's lives and rings.
Flow Game::extra_life() {
    score.lives = bcd_add(score.lives, 1);
    const u8 power = objects[0].power_up;
    if (power != 0x0D && power != 0x05 && power != 0x0F && power != 0x0A && power != 0x09) {
        sound_.request(0x8C, 2);
        co_await next_frame(); // 084C
        act.music_ticks = 0x0078;
    }
    show_lives(); // 3370
}

// 3342 from code that cannot suspend (rings counted in the physics).
void Game::extra_life_now() {
    score.lives = bcd_add(score.lives, 1);
    const u8 power = objects[0].power_up;
    if (power != 0x0D && power != 0x05 && power != 0x0F && power != 0x0A && power != 0x09) {
        sound_.request(0x8C, 2);
        wait_frame_now(); // 084C
        act.music_ticks = 0x0078;
    }
    show_lives(); // 3370
}

// B866: the time is up: (but for the sneakers and 5) falling (3B62); no
// longer untouchable or blinking, top speed 4; power-up 2 lets go of its
// holder, others bring the act's music back (2216).
Flow Game::end_power_up() {
    auto& p = objects[0];
    const u8 power = p.power_up;
    if (power != sneakers && power != 0x05) {
        to_falling_from_flight(p); // 03D2
    }
    p.flags &= u8(~0xA0);
    player.top_speed = 0x0400;
    if (power == 0x02) {
        objects[player.holder - 1u].next_state = 0x02; // B891
        player.holder = 0;
        p.power_up = 0;
        co_return;
    }
    co_await restore_act_music(); // 2216 (04E6)
    p.power_up = 0;
}

// 3CFC (0405): flying (state 11), still, top speed 7; its song and a
// frame's wait (in zone 5 act 3 none: right at 4, facing right).
Flow Game::start_power_flight(Object& p) {
    p.vx = 0;
    p.vy = 0;
    player.top_speed = 0x0700;
    if (flow.zone == 5 && flow.act == 2) {
        p.vx = 0x0400;
        p.display &= u8(~display_flag::mirrored);
    } else {
        sound_.request(0x8F, 2);
        co_await next_frame(); // 084C
    }
    p.flags &= u8(~0x42);
    p.next_state = 0x11;
}

// B8AB: the item's start (table B8BF).
Flow Game::start_power_up(u8 item) {
    switch (item) {
    case sneakers: // B9E5: their song, 720 frames
        sound_.request(sneakers_song, 2);
        co_await next_frame(); // 0300
        player.power_up_ticks = 0x02D0;
        player.power_flags |= 0x04;
        act.music_ticks = 0;
        co_return;
    case 0x02: { // B90A: held (state 12) by a new holder (type 1B)
        auto& p = objects[0];
        p.flags = u8((p.flags & ~0x02) | 0x01);
        Object* h = spawn_object(p, 0x1B, 0x00); // 50E1 (0354)
        player.holder = h ? u8(slot_number(*h) + 1) : 0;
        p.next_state = 0x12;
        p.power_up = 0;
        player.power_up_pending = 0;
        co_return;
    }
    case 0x06:
    case 0x07:
    case 0x10: // B8E3
        co_return;
    case 0x05: { // B9FF: its song, a frame's wait; untouchable (flags 1, 5, 7) for 660 frames with
                 // the sparkle (type 5)
        act.music_ticks = 0;
        sound_.request(invincible_song, 2);
        co_await next_frame(); // 0300
        auto& p = objects[0];
        p.flags |= 0xA2;
        player.power_up_ticks = 0x0294;
        spawn_object(p, 0x05, 0x00); // 50E1 (0354)
        co_return;
    }
    case 0x08: // B993: an extra life (3342, 04E9)
        player.power_up_pending = 0;
        co_await extra_life();
        co_return;
    case 0x0F: { // BA23: Tails only: he flies (state 43, byte 3F FF) for 300 frames with its song
                 // (6000 in zone 5 act 3, keeping the act's)
        auto& p = objects[0];
        if (p.type != 0x02) {
            co_return;
        }
        if (flow.zone == 5 && flow.act == 2) {
            player.power_up_ticks = 0x1770;
            player.flight_ticks = 0x1770;
        } else {
            sound_.request(tails_flight_song, 2);
            co_await next_frame(); // 0300
            player.power_up_ticks = 0x012C;
            player.flight_ticks = 0x012C;
            act.music_ticks = 0;
        }
        p.param = 0xFF; // 3F48 (04C8)
        p.next_state = 0x43;
        co_return;
    }
    case 0x0A: { // B96B: Sonic only: its song, a frame's wait, 300 frames of state 26 (3EC4, 041A)
        auto& p = objects[0];
        if (p.type != 0x01) {
            co_return;
        }
        p.flags &= u8(~0x02);
        sound_.request(0x95, 2);
        co_await next_frame(); // 0300
        player.power_up_ticks = 0x012C;
        p.next_state = 0x26;
        co_return;
    }
    case 0x09: { // B99A: Sonic flies (state 11) for 300 frames (6000 in zone 5 act 3); Tails gets
                 // nothing
        auto& p = objects[0];
        if (p.type != 0x01) {
            p.power_up = 0;
            player.power_up_ticks = 0;
            co_return;
        }
        const bool final_act = flow.zone == 5 && flow.act == 2;
        if (!final_act) {
            act.music_ticks = 0;
        }
        player.power_up_ticks = final_act ? 0x1770 : 0x012C;
        co_await start_power_flight(p); // 3CFC (0405)
        co_return;
    }
    case 0x0E: // B9DA: the clock at 1:30
        score.minutes = 0x01;
        score.seconds = 0x30;
        co_return;
    case 0x0C: { // B986: Sonic only: state 23 (3ECA, 0417)
        auto& p = objects[0];
        if (p.type == 0x01) {
            p.flags &= u8(~0x02);
            p.next_state = 0x23;
        }
        co_return;
    }
    default:
        throw Unrecovered("power-up " + std::to_string(item) + " start (B8BF)");
    }
}

} // namespace triple_trouble_gg
