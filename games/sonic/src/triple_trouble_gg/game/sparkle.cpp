// The sparkle around the player (type 5, bank 1E, 8050-81A4): with
// power-up 5 (parameter 0, state 1) for as long as it lasts, and after a
// peel out (parameter FF, state 2) for 30 frames (D3D4) or while he is
// untouchable. It keeps him untouchable (flags bit 7) and follows him (4
// lower); its sprites' offsets turn through a table (8185, 8195) into
// D483.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr Table sparkle_y{0x1E, 0x8185}, sparkle_x{0x1E, 0x8195};
} // namespace

// 519B (03A2): at the player's place, `dy` lower.
void Game::follow_player(Object& o, std::uint16_t dy) {
    o.x = objects[0].x;
    o.y = std::uint16_t(objects[0].y + dy);
}

bool Game::run_sparkle_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::sparkle_follow_start: { // 806C
        o.tile_base = o.tile_base_mirrored = 0;
        o.timer = 0;
        o.counter = 0;
        o.frame = o.param ? 0x04 : 0x00;
        o.display |= 0x03;
        u8 state = 0x01;
        if (o.param == 0xFF) {
            ++state;
            player.sparkle_ticks = 0x001E;
        }
        o.next_state = state;
        objects[0].flags |= 0x80;
        follow_player(o, 0x0004);
        return true;
    }
    case Routine::sparkle_turn: // 813B: frames 0-2; the offsets' next step (32 steps)
        if (++o.timer >= 0x03) {
            o.timer = 0;
        }
        o.frame = o.timer;
        o.frame_ticks = 0x01;
        for (unsigned i = 0; i < 4; ++i) {
            animation.sparkle_offsets[i] = rom.byte(sparkle_y, o.counter * 4u + i);
            animation.sparkle_offsets[4 + i] = rom.byte(sparkle_x, o.counter * 4u + i);
        }
        if (++o.counter >= 0x20) {
            o.counter = 0;
        }
        return true;
    case Routine::sparkle_peel: // 80DF
        sparkle_peel(o);
        return true;
    default:
        return false;
    }
}

// 80AA: with power-up 5 it follows; ended: gone, the player no longer
// flagged (bit 5), and (outside boss acts, not leaving the act, not
// running off or flying) the act's song again (21DC).
Flow Game::sparkle_power(Object& o) {
    if (objects[0].power_up == 0x05) {
        objects[0].flags |= 0x80;
        follow_player(o, 0x0004);
        co_return;
    }
    o.type = 0xFF;
    objects[0].flags &= u8(~0x20);
    if (act.variant || (flow.frame_work & 0xBF)) {
        co_return;
    }
    if (objects[0].state == 0x20 || objects[0].next_state == 0x11) {
        co_return;
    }
    co_await play_act_music(); // 21DC (034E)
}

// 80DF: during the peel out (15) it follows; during its run (1A), or
// while a power-up lasts, it counts its 30 frames and lasts while he is
// untouchable (D3C2); then gone: the player's flags 5, 7, 1 and hidden
// bit cleared, no hurt, no touches (rolling again if airborne).
void Game::sparkle_peel(Object& o) {
    auto& p = objects[0];
    bool end = act.rings_gone;
    if (!end && p.state != 0x15) {
        if (p.state != 0x1A && !player.power_up_ticks) {
            end = true;
        } else {
            end = !--player.sparkle_ticks || !player.hurt_pending;
        }
    }
    if (!end) {
        follow_player(o, 0x0004);
        return;
    }
    p.flags &= u8(~0xA2);
    p.display &= u8(~display_flag::hidden);
    player.hurt_pending = 0;
    player.hit = 0;
    p.touch = 0;
    p.touch_sides = 0;
    o.type = 0xFF;
    if (p.flags & 0x01) {
        p.flags |= 0x02;
    }
}

} // namespace triple_trouble_gg
