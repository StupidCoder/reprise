// The player and harm (37B5-3A3A): being crushed, hit or touching an
// object; losing rings (scattered as type 1E) or a life; the blinking time
// after a hit. Restarting music waits for a frame here (2216, 21DC, 3942),
// so these run as part of the player's update coroutine.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr u8 scattered_ring = 0x1E;
constexpr u8 hurt_sound = 0xB3;
constexpr u8 death_song = 0x8D;
} // namespace

// 37B5: standing on a platform while pushed against a ceiling crushes
// (D3C1, D3C4); state 21 only counts the blinking down; then the contact
// bits (5A3B); a bounced player (flags bit 6) and the blinking time skip
// the rest; power-up 5 is immune; a hit or a harmful touch hurts.
Flow Game::player_contact(Object& p) {
    if (player.carried && p.touch != 0x4D && (p.terrain & 0x01)) {
        player.hit = 0xFF;
        player.touch_kind = 0x50;
    }
    if (p.state == 0x21) {
        player.hit = 0;
        p.touch_sides = 0;
        blink_after_hit(p); // 39CF
        co_return;
    }
    update_contact(p); // 5A3B
    if (p.flags & 0x40) {
        p.touch = 0; // 3A33
        if (p.state == 0x1E) {
            p.display &= u8(~display_flag::hidden); // 39ED
        } else {
            blink_after_hit(p); // 39CF
        }
        co_return;
    }
    if (player.hurt_pending) {
        blink_tick(p); // 39D4
        co_return;
    }
    if (p.power_up == 0x05) {
        player.hit = 0;
        p.touch = 0;
        co_return;
    }
    if (player.hit) {
        player.hit = 0;
        p.contact &= u8(~0x01);
        co_await hurt_player(p); // 384A
        co_return;
    }
    if (p.touch) {
        if (player_touch(p)) { // 381B
            co_await hurt_player(p);
        }
    }
}

// 384A from the terrain (spikes), in code that cannot suspend: as
// hurt_player, its frame waits taken at once (084C).
void Game::hurt_player_now(Object& p) {
    if (p.state == 0x48 && --p.param) {
        player.hit = 0; // 3A2B
        p.touch = 0;
        return;
    }
    if (p.power_up && request_restored_music()) {
        wait_frame_now(); // 2216
    }
    const u8 power = p.power_up;
    p.power_up = 0;
    player.power_up_pending = 0;
    player.power_up_ticks = 0;
    const u8 s = p.state;
    if (power == 0x0D || s == 0x11 || s == 0x43) {
        if (request_restored_music()) {
            wait_frame_now(); // 388F
        }
    } else if (s == 0x29) {
        if (request_act_music()) {
            wait_frame_now(); // 3894
        }
    } else if (s != 0x16 && s != 0x23) {
        if (!score.rings_low && !score.rings_high) {
            if (start_death(p)) {
                wait_frame_now(); // 3942
            }
            return;
        }
        scatter_rings(p); // 389A
    }
    if (p.state == 0x29 && request_restored_music()) {
        wait_frame_now(); // 38F2 ... 390A
    }
    throw_back(p); // 38F2
}

// 384A: hurt. In state 48 the parameter counts hits first. A power-up
// ends (its music too); in states 11, 16, 23, 29, 43 or with power-up 0D
// no rings are lost. Without rings the player dies (3942); otherwise rings
// scatter (389A). Then he is thrown back (38F2).
Flow Game::hurt_player(Object& p) {
    if (p.state == 0x48 && --p.param) {
        player.hit = 0; // 3A2B
        p.touch = 0;
        co_return;
    }
    if (p.power_up) {
        co_await restore_act_music(); // 2216
    }
    const u8 power = p.power_up;
    p.power_up = 0;
    player.power_up_pending = 0;
    player.power_up_ticks = 0;
    const u8 s = p.state;
    if (power == 0x0D || s == 0x11 || s == 0x43) {
        co_await restore_act_music(); // 388F
    } else if (s == 0x29) {
        co_await play_act_music(); // 3894
    } else if (s != 0x16 && s != 0x23) {
        if (!score.rings_low && !score.rings_high) {
            co_await kill_player(p); // 3942
            co_return;
        }
        scatter_rings(p); // 389A
    }
    if (p.state == 0x29) {
        co_await restore_act_music(); // 38F2 ... 390A
    }
    throw_back(p);
}

// 389A: up to 7 rings scatter (one per 16 held, type 1E); 30 (50 when
// crushed) are lost (all with act variant), sound B3.
void Game::scatter_rings(Object& p) {
    u8 count = u8(((score.rings_low >> 4) & 0x0F) + 1);
    if (count >= 0x08) {
        count = 0x07;
    }
    for (u8 i = 0; i < count; ++i) {
        spawn_object(p, scattered_ring, i); // 50E1
    }
    player.carried = 0;
    lose_rings(); // 38C4
    show_rings(); // 33BA
    sound_.request(hurt_sound, 1);
}

// 38F2: thrown back (state 1E) away from the side touched, up at 4 (1
// when against a ceiling), untouchable and blinking for 120 frames (D3C2).
void Game::throw_back(Object& p) {
    p.display &= u8(~display_flag::hidden);
    p.flags |= 0xC0;
    player.hurt_pending = 0x78;
    p.touch = 0;
    p.next_state = 0x1E;
    p.flags |= 0x01;
    p.terrain &= u8(~0x02);
    p.vy = (p.terrain & 0x01) ? 0x0100 : 0xFC00;
    p.vx = (p.contact & 0x08) ? 0x0100 : 0xFF00;
    player.input_force = 0;
}

// 38C4: the rings lost (BCD): D3E4 nonzero takes all.
void Game::lose_rings() {
    if (!act.variant) {
        const u8 lost = player.touch_kind;
        const u8 low = score.rings_low;
        score.rings_low = bcd_sub(low, lost);
        if (low >= lost) {
            return;
        }
        const u8 high = score.rings_high;
        score.rings_high = bcd_sub(high, 1);
        if (high >= 1) {
            return;
        }
    }
    score.rings_low = 0;
    score.rings_high = 0;
}

// 3942: dying (state 1F, under water 28), unless already: up at 5, the
// death song; the clock's time-over flag keeps only its top bit. The
// program waits a frame here.
Flow Game::kill_player(Object& p) {
    if (start_death(p)) {
        co_await next_frame(); // 084C
    }
}

// 3942's start; true when the frame's wait follows.
bool Game::start_death(Object& p) {
    if (p.state == 0x1F || p.state == 0x28) {
        return false;
    }
    p.next_state = (p.flags & 0x04) ? 0x28 : 0x1F;
    p.flags |= 0x01;
    p.display = 0;
    p.vy = 0xFB00;
    player.input_force = 0;
    p.terrain &= u8(~0x02);
    act.time_over &= 0x80;
    sound_.request(death_song, 2);
    return true;
}

// 39CF / 39D4: after a hit the player blinks (hidden on 2 of every 4
// frames; not in states 1E and 1A) until the count ends (39F7): then
// touchable again; Sonic below frame 6 keeps flags bit 1, Tails in his
// rolling frames (2F-31) gets it.
void Game::blink_after_hit(Object& p) {
    if (player.hurt_pending) {
        blink_tick(p);
    }
}

void Game::blink_tick(Object& p) {
    if (--player.hurt_pending) {
        if (p.state == 0x1E || p.state == 0x1A || !(player.hurt_pending & 0x02)) {
            p.display &= u8(~display_flag::hidden);
        } else {
            p.display |= display_flag::hidden;
        }
        return;
    }
    // 39F7
    player.hit = 0;
    p.touch = 0;
    p.display &= u8(~display_flag::hidden);
    p.flags &= u8(~0xC0);
    const u8 f = p.frame;
    if (p.type == 0x01) {
        if (f >= 0x06) {
            p.flags &= u8(~0x02);
        }
        return;
    }
    p.flags &= u8(~0x02);
    if (f >= 0x2F && f < 0x32) {
        p.flags |= 0x02;
    }
}

// 2216: unless objects run plainly, the player is dying or in state 44
// or 20, or in zone 5's act 3, the act's song again (D3ED); a frame wait.
Flow Game::restore_act_music() {
    if (request_restored_music()) {
        co_await next_frame(); // 084C
    }
}

// 2216's request; true when the frame's wait follows.
bool Game::request_restored_music() {
    if (act.object_mode) {
        return false;
    }
    const u8 s = objects[0].state;
    if (s == 0x44 || s == 0x20 || s == 0x1F || s == 0x28) {
        return false;
    }
    if (flow.zone == 5 && flow.act == 2) {
        return false;
    }
    sound_.request(act.music_playing, 2);
    act.music_request = act.music_playing;
    return true;
}

} // namespace triple_trouble_gg
