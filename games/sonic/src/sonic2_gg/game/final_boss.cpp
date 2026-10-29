// Crystal Egg Zone act 3, the last boss (55, bank 1E): it attacks with
// fireball clusters (53), falling fire (54) and electricity (Sonic is safe
// in the pipe, state 21); eight hits break it. Its remains then jump away
// and Sonic follows (states 31, 32); the scene completes the zone and the
// remains become object 5D (see ending.cpp).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
// The fire (53) keeps its speed limit in its angle and speed bytes (IX+0A as a
// word); the speed byte doubles as how far it has spread (8F95).
unsigned spread(const Object& o) {
    return o.angle | (o.speed << 8);
}

void set_spread(Object& o, unsigned v) {
    o.angle = u8(v);
    o.speed = u8(v >> 8);
}
} // namespace

namespace {
// Sonic's states in this scene.
constexpr u8 sonic_in_pipe = 0x21, sonic_follows = 0x31, sonic_watches = 0x32;
} // namespace

// 915E / 9164: palette cycle 1 (D481) kinds 9 and A (the boss's flashing).
void Game::boss_palette(u8 kind) {
    palette.cycle1[0] = kind;
}

// 916A / 9170: the electricity on (D447 here) or off.
void Game::final_boss_electric(bool on) {
    boss.throw_seconds = on ? 0xFF : 0x00;
}

// 0311 (64D8): breaks the map block at the object + (dx, dy) into four
// pieces, as Sonic does to breakable blocks.
void Game::break_block_at(Object& o, unsigned dx, unsigned dy) {
    probe_type(o, dx, dy); // 73F2
    break_block();         // FD, redrawn, debris
}

// 92AD (state 0): eight hits.
void Game::final_boss_start(Object& o) {
    player.power_up_hold = 0xFF;
    o.next_state = 1;
    o.display = u8((o.display | display_flag::keep_on_screen) & ~display_flag::offscreen);
    o.hits = 0x08;
}

// 92C3 (state 1): from camera X 180 the camera moves to (210, C0).
void Game::final_boss_wait(Object& o) {
    if (camera.x < 0x0180) {
        return;
    }
    script_camera(0x0210, 0x00C0); // 02F6
    o.next_state = 2;
}

// 92DB (state 2): there, the camera follows Sonic within the arena.
void Game::final_boss_arena(Object& o) {
    bumper_object(o); // 021B
    o.touch_sides = 0;
    if (camera.x != 0x0210) {
        return;
    }
    follow_sonic(); // 02F3
    camera.max_x = 0x0200;
    camera.min_x = 0x0200;
    camera.min_y = 0x0018;
    camera.max_y = 0x00C8;
    o.next_state = 3;
}

// 937E: bumper; a hit (while hits are left) makes it blink 64 frames (7);
// with none left it breaks (8) and the camera may go on to X 400.
void Game::final_boss_guard(Object& o) {
    bumper_object(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (o.hits) {
        --o.hits;
        o.next_state = 7;
        o.timer = 0x40;
        return;
    }
    o.next_state = 8;
    camera.max_x = 0x0400;
}

// 930B (state 3): the next attack (4 fireballs, 5 fire, 6 electricity)
// from table 932C by hits left and a counter.
void Game::final_boss_choose(Object& o) {
    final_boss_guard(o);
    ++o.counter;
    const unsigned list = rom.word(rom::final_boss_attacks, o.hits * 2u);
    o.next_state = rom.byte(0x1E, list + (o.counter & 0x07));
}

// 935A (state 6): while the electricity is on, Sonic outside the pipe is
// hurt.
void Game::final_boss_shock(Object& o) {
    final_boss_guard(o);
    if (!boss.throw_seconds || sonic().state == sonic_in_pipe) {
        return;
    }
    player.hit_request = 0xFF;
}

// 936E (state 7): blinking after a hit.
void Game::final_boss_blink(Object& o) {
    bumper_object(o);
    o.touch_sides = 0;
    if (!--o.timer) {
        o.next_state = 4;
    }
}

// 91F2 / 91F7: the remains, once, start 48 pixels higher.
void Game::final_boss_lift(Object& o) {
    if (o.timer) {
        return;
    }
    o.y = (unsigned(o.y) + 0xFFD0u) & 0xFFFF;
    o.timer = 0x01;
}

// 9443: Sonic on the ground at X 368 or more follows the remains (31).
void Game::final_boss_call_sonic() {
    Object& s = sonic();
    if (!(s.terrain & 0x02) || s.state == sonic_follows || s.state == sonic_watches) {
        return;
    }
    if (s.x < 0x0368) {
        return;
    }
    s.next_state = sonic_follows;
}

// 93A4 (states 9, B): the remains hop right (gravity 28 rising, 80
// falling, 1.5 pixels per frame); landed, the next state after 64 frames
// (state 9 waits for Sonic to follow).
void Game::final_boss_hop(Object& o) {
    final_boss_call_sonic();
    move_object(o);
    if (hi(o.vy) & 0x80) {
        fall(o, 0x0028, 0x0600);
        return;
    }
    fall(o, 0x0080, 0x0600);
    o.vx = 0x0180;
    object_floor(o); // 0260
    if (!(o.terrain & 0x02)) {
        return;
    }
    o.vx = 0;
    if (o.state == 0x09 && sonic().state != sonic_follows) {
        return;
    }
    o.next_state = u8(o.state + 1);
    o.timer = 0x40;
}

// 93F6 (state A): runs right to X 44C, then a high hop (B).
void Game::final_boss_run(Object& o) {
    o.vx = 0x0300;
    final_boss_call_sonic();
    o.vy = 0x0300;
    object_floor(o);
    move_object(o);
    if (o.x < 0x044C) {
        return;
    }
    o.next_state = 0x0B;
    o.vy = 0xFCA0;
    o.vx = 0x02C0;
}

// 9435 (state C): waits for Sonic to arrive (32).
void Game::final_boss_rest(Object& o) {
    final_boss_call_sonic();
    if (sonic().state == sonic_watches) {
        o.next_state = 0x0D;
    }
}

// 9288: a 224-frame wait (E).
void Game::final_boss_pause(Object& o) {
    o.timer = 0xE0;
}

// 9462 (state E; also 5D state 7): counts down, then the next state.
void Game::countdown_next(Object& o) {
    if (!--o.timer) {
        o.next_state = u8(o.state + 1);
    }
}

// 946E (state F): becomes object 5D, parameter 1, from its first state.
void Game::final_boss_transform(Object& o) {
    o.type = object_type::companion;
    o.param = 0x01;
    o.state = 0;
    o.next_state = 0;
    o.display = 0;
    o.frame_ticks = 0;
    o.restart_script();
}

// --- 53: a fireball cluster ----------------------------------------------------
// Four fireballs circling a centre that wobbles: its velocity swings by
// 40 per frame between growing limits (IX+0A/0B, a word here).

// 8E8C: the start velocity from table 8EBE by the clock and parameter.
void Game::fireballs_start(Object& o) {
    o.timer = 0x03;
    set_spread(o, 0x0080);
    const auto entry = rom::fireball_speeds + u8(u8((clock.seconds & 0x06) * 4) + o.param);
    o.vx = rom.word(entry);
    o.vy = rom.word(entry, 2);
}

// 8E7A: moves; hurts Sonic.
void Game::fireballs_move(Object& o) {
    move_object(o);
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
}

// 8EDE: the wobble, the circling (angle IX+1E + 4 per frame; the layout in
// D3CE: the centre and four fireballs at 64 steps apart, radius 1/16 of
// the sine), gone off screen once the limit reaches 900.
void Game::fireballs_circle(Object& o) {
    const unsigned limit = spread(o);
    auto swing = [&](std::uint16_t& v, u8 bit) {
        const unsigned value = (unsigned(v) + ((o.timer & bit) ? 0xFFC0u : 0x0040u)) & 0xFFFF;
        v = value;
        if (value & 0x8000) {
            if (((0x10000u - value) & 0xFFFF) >= limit) {
                o.timer &= u8(~bit);
            }
        } else if (value >= limit) {
            o.timer |= bit;
        }
    };
    swing(o.vx, 0x01);
    set_spread(o, limit + 0x0004);
    const unsigned limit2 = spread(o);
    {
        const unsigned value = (unsigned(o.vy) + ((o.timer & 0x02) ? 0xFFC0u : 0x0040u)) & 0xFFFF;
        o.vy = value;
        if (value & 0x8000) {
            if (((0x10000u - value) & 0xFFFF) >= limit2) {
                o.timer &= u8(~0x02);
            }
        } else if (value >= limit2) {
            o.timer |= 0x02;
        }
    }
    fireballs_move(o);
    o.counter = u8(o.counter + 0x04);
    sprites.run_layout[0].dy = 0;
    sprites.run_layout[0].dx = 0;
    for (unsigned b = 4, k = 1; b >= 1; --b, ++k) {
        // 8FA0
        const u8 angle = u8(((b - 1) << 6) + o.counter);
        const u8 sine = rom.byte(rom::sine, angle);
        const u8 cosine = rom.byte(rom::sine, u8(angle + 0xC0));
        // 1/16 of the sine, sign-extended to a word.
        auto radius = [](u8 v) {
            return std::uint16_t(std::int16_t(std::int8_t(v) >> 4));
        };
        sprites.run_layout[k].dy = radius(cosine);
        sprites.run_layout[k].dx = radius(sine);
    }
    if (!(o.display & display_flag::offscreen)) {
        return;
    }
    if (o.speed >= 0x09) {
        o.type = object_type::removed;
    }
}

// --- 54: falling fire ----------------------------------------------------------------

// 9043 (after 32 frames): starts its path (script velocities).
void Game::fire_start(Object& o) {
    o.next_state = 1;
}

// 9049: moves; hurts Sonic. 905B: gone.
void Game::fire_move(Object& o) {
    fireballs_move(o);
}

// --- Sonic ----------------------------------------------------------------------------

// A9F8 (state 31): runs right after the remains (2 pixels per frame,
// pressed to the floor) to X 450, then watches (32).
void Game::sonic_follow_remains() {
    Object& o = sonic();
    o.display &= u8(~display_flag::mirrored);
    o.vx = 0x0200;
    o.vy = 0x0300;
    object_floor(o); // 0260
    move_object(o);  // 0257
    if (o.x < 0x0450) {
        return;
    }
    o.next_state = sonic_watches;
}

// A9E8 (state 32): the camera moves to (3F8, 8D).
void Game::sonic_watch_camera() {
    script_camera(0x03F8, 0x008D); // 02F6
}

// A9F2 (state 32): the zone is complete.
void Game::sonic_watch_done() {
    flow.game_flags |= game_flag::zone_complete;
}

} // namespace sonic2_gg
