// The player's state changes (bank 0, 3A3A-3F80; most through the vectors
// 03B7-0435).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr Table slope_forces{0x0C, 0xB79C};
} // namespace

// 3A3A (040E): standing, unless on a slope (a landing attribute other than
// 0, 12, 14, 0E, 10): then rolling down it at its force times 8 (3D7B).
// Standing checks for a ledge (B7D0).
void Game::to_standing(Object& o) {
    if (o.next_state == 0x30) {
        return;
    }
    const u8 a = probe.landed_attribute;
    if (a && a != 0x12 && a != 0x14 && a != 0x0E && a != 0x10) {
        if (o.state == 0x09) {
            return;
        }
        o.vx = std::uint16_t(rom.word(slope_forces, a) * 8);
        roll(o); // 3D7B
        return;
    }
    o.flags &= u8(~0x03);
    o.next_state = 0x01;
    o.vx = 0;
    player.top_speed = 0x0400;
    check_ledge(o); // B7D0
}

// B7D0: the floor 4 pixels left and right (7 below): a difference of 10
// or more: balancing (state 19) facing the edge.
void Game::check_ledge(Object& o) {
    probe_terrain(o, 0xFFFC, 0x0007);
    const u8 left = probe.floor;
    probe_terrain(o, 0x0004, 0x0007);
    const u8 right = probe.floor;
    u8 d = u8(right - left);
    if (right < left) {
        d = u8(-d);
    }
    if (d < 0x10) {
        return;
    }
    if (right == left) {
        o.next_state = 0x01;
    } else if (right < left) {
        o.next_state = 0x19;
        o.display &= u8(~display_flag::mirrored);
    } else {
        o.next_state = 0x19;
        o.display |= display_flag::mirrored;
    }
}

// 3A8D (03F6): walking (in the air only from state 31... not at all).
void Game::to_walking(Object& o) {
    if (!(o.terrain & 0x02) && o.state == 0x31) {
        return;
    }
    o.flags &= u8(~0x43);
    o.next_state = 0x05;
    player.top_speed = 0x0400;
    face_input(o); // B7B6
    camera.aim_y = 0x78;
}

// 3AB8 (03BA): a jump, unless already in the air, on blocks A7-A9, held by
// object 4D, or in state 11. Up at FB.C0 (FC.C0 under water); on landing
// attributes 0C and 0A a fixed sideways speed (and status bit 6).
void Game::to_jumping(Object& o) {
    if (o.flags & 0x01) {
        return;
    }
    const u8 b = terrain_state.block;
    if (b == 0xA7 || b == 0xA8 || b == 0xA9) {
        return;
    }
    if (o.touch == 0x4D) {
        return;
    }
    if (o.state == 0x11) {
        return;
    }
    jump(o);
}

// 3AD6 (0402): the jump itself.
void Game::jump(Object& o) {
    player.jump_ticks = 0;
    o.status &= u8(~0x40);
    o.flags |= 0x03;
    o.next_state = 0x0A;
    o.vy = (o.flags & 0x04) ? 0xFCC0 : 0xFBC0;
    o.y = std::uint16_t(o.y - 1);
    std::uint16_t vx = o.vx;
    if (!(o.flags & 0x04)) {
        if (probe.landed_attribute == 0x0C) {
            vx = 0x0200;
            o.status |= 0x40;
        } else if (probe.landed_attribute == 0x0A) {
            vx = 0xFE00;
            o.status |= 0x40;
        }
    }
    o.vx = vx;
    camera.aim_y = 0x78;
    o.terrain &= u8(~0x02);
    sound_.request((flow.zone == 3 && (o.status & 0x20)) ? 0xCD : 0xB1, 1);
}

// 3B47 (03B7): falling (state 0E), not from states 0A, 26, 16, 41, 43,
// 48, 23, 29; downward at 1 pixel per frame except from state 24.
void Game::to_falling(Object& o) {
    const u8 s = o.state;
    if (s == 0x0A || s == 0x26 || s == 0x16 || s == 0x41 || s == 0x43 || s == 0x48 || s == 0x23 ||
        s == 0x29) {
        return;
    }
    to_falling_from_flight(o);
}

// 3B62
void Game::to_falling_from_flight(Object& o) {
    if (o.state != 0x24) {
        o.vy = 0x0100;
    }
    to_falling_state(o);
}

// 3B6F
void Game::to_falling_state(Object& o) {
    o.next_state = 0x0E;
    o.flags = u8((o.flags | 0x01) & ~0x02);
    o.display &= u8(~display_flag::flicker);
    o.terrain &= u8(~0x02);
}

// 3D99: bounced away rolling (state 1B): in the air, spinning, off the
// floor; no longer a placement's; D41B cleared.
void Game::to_bounced_roll(Object& o) {
    auto& p = objects[0];
    p.placement = 0;
    p.next_state = 0x1B;
    p.flags |= 0x03;
    p.terrain &= u8(~0x02);
    terrain_state.pipe_block = 0;
    (void)o;
}

// 3BB8: from a roll into the air: spinning (state 0A) without speed up.
void Game::to_rolling_fall(Object& o) {
    o.flags |= 0x03;
    o.next_state = 0x0A;
    o.vy = 0;
    o.terrain &= u8(~0x02);
}

// 3BCF (03F9)
void Game::to_looking_up(Object& o) {
    o.vx = 0;
    o.next_state = 0x03;
}

// 3BDA (03FC): only on the floor.
void Game::to_crouching(Object& o) {
    if (!(o.contact & 0x02)) {
        return;
    }
    o.vx = 0;
    o.flags &= u8(~0x42);
    o.next_state = 0x04;
}

// 3BF2 (040B): Sonic's peel out charge (state 15; not in zone 2's water).
void Game::to_peel_out(Object& o) {
    if ((o.status & 0x20) && flow.zone == 2) {
        return;
    }
    o.vx = 0;
    o.flags &= u8(~0x40);
    o.next_state = 0x15;
    sound_.request(0xBA, 1);
}

// 3C4A (0414): a spin dash charge (state 0F).
void Game::to_spin_dash(Object& o) {
    if (flow.zone == 2 && (o.status & 0x20)) {
        return;
    }
    o.vx = 0;
    o.flags = u8((o.flags | 0x02) & ~0x40);
    o.next_state = 0x0F;
    sound_.request(0xBA, 1);
}

// 3C6E (0411): released at 7 pixels per frame (state 10).
void Game::release_spin_dash(Object& o) {
    player.top_speed = 0x0700;
    o.vx = (o.display & display_flag::mirrored) ? 0xF900 : 0x0700;
    o.flags |= 0x02;
    o.next_state = 0x10;
    sound_.request(0xC8, 1);
}

// 3C12 (0429): the peel out released (not on zone 2's slopes): off at 7
// the way he faces (top speed 7), state 1A, sound C8; rolling, untouchable
// and flags bit 5; a dust cloud (type 5, parameter FF).
void Game::release_peel_out(Object& o) {
    if (flow.zone == 2 && (o.status & 0x20)) {
        return;
    }
    player.top_speed = 0x0700;
    o.vx = (o.display & display_flag::mirrored) ? 0xF900 : 0x0700;
    o.next_state = 0x1A;
    sound_.request(0xC8, 1);
    o.flags |= 0xA2;
    spawn_object(o, 0x05, 0xFF); // 50E1
}

// 3D3B (0426)
void Game::to_running(Object& o) {
    o.flags &= u8(~0x03);
    o.next_state = 0x06;
}

// 3D48 (0420) / 3D5B (041D): skidding (states 7 and 8), not in the air.
void Game::to_skid_7(Object& o) {
    if (o.flags & 0x01) {
        return;
    }
    sound_.request(0xB0, 1);
    o.flags &= u8(~0x02);
    o.next_state = 0x07;
}

void Game::to_skid_8(Object& o) {
    if (o.flags & 0x01) {
        return;
    }
    sound_.request(0xB0, 1);
    o.flags &= u8(~0x02);
    o.next_state = 0x08;
}

// 3D6E (0423): rolling (not in state 23; standing still: crouching).
void Game::to_rolling(Object& o) {
    if (o.next_state == 0x23) {
        return;
    }
    if (!hi(o.vx)) {
        to_crouching(o);
        return;
    }
    roll(o);
}

// 3D7B: top speed 6, rolling (state 9, sound B4).
void Game::roll(Object& o) {
    player.top_speed = 0x0600;
    o.flags = u8((o.flags & ~0x01) | 0x02);
    if (o.state == 0x09) {
        return;
    }
    o.next_state = 0x09;
    sound_.request(0xB4, 1);
}

// 3E7A (03D5): the jump button in the air: once per jump (status bit 2)
// state 24, remembering the state it came from (byte 34).
void Game::special_move(Object& o) {
    if (o.status & 0x04) {
        return;
    }
    o.status |= 0x04;
    o.scratch[0] = o.state;
    o.next_state = 0x24;
    o.flags |= 0x03;
    o.display &= u8(~display_flag::hidden);
}

// 3EE3 (0435): kind 8 blocks in zones 0 and 1 (not act 3): floating
// (state 31), unless in state 16, with a power-up or rising.
void Game::to_float(Object& o) {
    if (flow.act == 2) {
        return;
    }
    if (flow.zone >= 2) {
        return;
    }
    if (o.state == 0x16) {
        return;
    }
    if (o.power_up) {
        return;
    }
    if (o.vy & 0x8000) {
        return;
    }
    o.flags &= u8(~0x03);
    o.next_state = 0x31;
    o.vy = 0x0100;
    o.vx = 0;
}

} // namespace triple_trouble_gg
