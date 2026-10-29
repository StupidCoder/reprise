// Platforms (bank 1C, type 20 and the falling block 2D). The low digit of
// a platform's parameter picks its kind: 0 moves back and forth, 1 falls
// after being stood on, 2 swings; the high digit is its frame.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
// IX+0A holds the direction of a moving platform: bit 4 set = left.
constexpr u8 moving_left = 0x10;
} // namespace

// 814F: a platform carries Sonic standing on it: he moves with it and is
// held at its top (81B9). D3B9 notes the carrying platform; D100 keeps
// Sonic's touch bits meanwhile.
void Game::platform_carry(Object& o) {
    Object& s = sonic();
    probe.block_attribute = s.touch_sides; // D100 as scratch
    touch_sonic(o);                        // 025A
    const u8 sides = o.touch_sides & 0x0F;
    if (sides && !(sides & 0x02) && !(hi(s.vy) & 0x80) && !(s.terrain & 0x02)) {
        move_object(o);
        o.touch_sides = 0x01;
        s.touch_sides = 0x20;
        s.contact |= 0x02;
        const u8 slot = u8(slot_number(o)); // 0230
        if (player.platform_slot && player.platform_slot != slot) {
            return;
        }
        player.platform_slot = slot;
        // 81B9: on top, moved along by the platform's vx (24-bit add).
        s.y = (unsigned(o.y) + 1 - o.height) & 0xFFFF;
        const unsigned vx = o.vx;
        unsigned position = s.x_sub | (unsigned(s.x) << 8);
        position = (position + vx + ((vx & 0x8000) ? 0xFF0000u : 0)) & 0xFFFFFF;
        s.x_sub = u8(position);
        s.x = position >> 8;
        return;
    }
    move_object(o);
    o.touch_sides = 0;
    s.touch_sides = probe.block_attribute;
    if (player.platform_slot == slot_number(o)) {
        player.platform_slot = 0;
    }
}

// 8067 (state 0): once on screen, its kind's state: parameter digit * 8
// (state 1 for 0).
void Game::platform_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.flags |= 0x80;
    const u8 state = u8((o.param & 0x0F) << 3);
    o.next_state = state ? state : 1;
}

// 81EE / 81E7: the frame from the parameter's high digit (held E0 or 20
// ticks).
void Game::platform_frame(Object& o, u8 ticks) {
    o.frame_ticks = ticks;
    o.frame = u8((o.param >> 4) + 1);
}

// 8080 (states 1-7): moves 1 pixel per frame, turning at walls, and (off
// the 256-pixel picture) when 192 pixels or more from the screen's middle.
// In Gimmick Mt. it is gone when off screen. Widescreen margins (zero on the
// LCD) move the turning points beyond the presented view.
void Game::platform_move(Object& o) {
    object_wall(o); // 0239
    if (o.terrain & 0x0C) {
        o.angle = u8((o.angle & ~moving_left) | ((o.terrain & 0x04) << 2));
    } else if (hi(o.screen_x)) {
        const unsigned middle = (camera.x + 0x0080u) & 0xFFFF;
        if (middle >= unsigned(o.x)) {
            if (((middle - o.x) & 0xFFFF) >= 0x00C0u + unsigned(wide_left)) {
                o.angle &= u8(~moving_left);
            }
        } else if (((unsigned(o.x) - middle) & 0xFFFF) >= 0x00C0u + unsigned(wide_right)) {
            o.angle |= moving_left;
        }
    }
    o.vx = with_hi(o.vx, (o.angle & moving_left) ? 0xFF : 0x01);
    platform_carry(o);
    if (flow.zone == 4 && (o.display & display_flag::offscreen)) {
        o.type = object_type::removed;
    }
}

// 80F6 (state 8): still; stood on, it shakes (state 9) and falls (A).
void Game::platform_wait(Object& o) {
    platform_carry(o);
    if (o.touch_sides & 0x0F) {
        o.next_state = 9;
    }
}

// 8104 (state 9)
void Game::platform_shake(Object& o) {
    platform_carry(o);
}

// 8108 (state A, and the block 2D): falls, removed once below the screen.
// Widescreen: once below the presented view.
void Game::platform_fall(Object& o) {
    o.display |= 0x01;
    fall(o, 0x0020, 0x0600); // 020F
    platform_carry(o);
    if (widescreen()) {
        const int y = int(std::int16_t(std::uint16_t(o.screen_y)));
        if (y >= 0xC0 + wide_bottom || y < -wide_top - 64) {
            o.type = object_type::waiting;
        }
        return;
    }
    if (lo(o.screen_y) >= 0xC0) {
        o.type = object_type::waiting;
    }
}

// 8123 (state 10): swings: the angle runs from 80 to 0 and back (8131),
// at speed 30 along it.
void Game::platform_swing(Object& o) {
    o.speed = 0x30;
    if (o.angle) {
        o.angle = u8(o.angle + ((o.counter & 0x01) ? 0xFF : 0x01));
    } else {
        o.angle = 0x80;
        ++o.counter;
    }
    velocity_from_angle(o); // 0236
    platform_carry(o);
}

// 8EC4 (2D): placed in the 16 x 32 cell under Sonic.
void Game::falling_block_start(Object& o) {
    o.flags |= 0x80;
    o.tile_base = 0x62;
    o.x = (sonic().x & 0xFFF0) | 0x0008;
    o.y = ((((unsigned(sonic().y) + 0x0020) & 0xFFE0)) + 0xFFF4u) & 0xFFFF;
}

} // namespace sonic2_gg

namespace sonic2_gg {

// --- 21: a platform that rides a scripted path ------------------------------
// It waits at its origin (state 5) until Sonic stands on it, then follows
// the path of state parameter + 1 (velocities set by the script).

// 835C (state 0): parameter 1 waits even off screen; others are kept.
void Game::path_platform_start(Object& o) {
    o.flags |= 0x80;
    if (o.param != 0x01) {
        if (o.display & display_flag::offscreen) {
            return;
        }
        o.display |= display_flag::keep_on_screen;
    }
    o.next_state = 5;
}

// 838D (state 5): at the origin; stood on, it starts.
void Game::path_platform_wait(Object& o) {
    if (o.param != 0x01 && (o.display & display_flag::offscreen)) {
        return;
    }
    o.vx = 0;
    o.vy = 0;
    o.x = o.origin_x;
    o.y = o.origin_y;
    platform_carry(o); // 814F
    if (o.touch_sides & 0x0F) {
        o.next_state = u8(o.param + 1);
    }
}

// 8389: carrying along the path.
void Game::path_platform_ride(Object& o) {
    platform_carry(o);
}

// 8374: the same, no longer kept on screen; once off screen it returns to
// its origin (5).
void Game::path_platform_ride_free(Object& o) {
    platform_carry(o);
    o.display &= u8(~(display_flag::keep_on_screen | display_flag::hidden));
    if (o.display & display_flag::offscreen) {
        o.next_state = 5;
    }
}

// --- 22: a platform moving back and forth ------------------------------------

// 83D9 (state 0)
void Game::shuttle_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 1;
    o.flags |= 0x80;
}

// 83ED (state 1): 1 pixel per frame between its origin and parameter
// pixels right of it.
void Game::shuttle_move(Object& o) {
    o.vx = (o.counter & 0x01) ? 0xFF00 : 0x0100;
    platform_carry(o);
    if (unsigned(o.x) < unsigned(o.origin_x)) {
        o.counter &= u8(~0x01);
        return;
    }
    const unsigned d = (unsigned(o.x) - unsigned(o.origin_x)) & 0xFFFF;
    if (!(d >> 8) && (d & 0xFF) < o.param) {
        return;
    }
    o.counter |= 0x01;
}

} // namespace sonic2_gg
