// Sonic's zone triggers (367C), water and air (48DF), power-ups and monitor
// rewards (4820).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
// `value` in [base, base + 256) with (value - base) & mask == 0: the SBC,
// high byte and AND tests the routines use.
bool in_window(unsigned value, unsigned base, unsigned mask) {
    if (value < base) {
        return false;
    }
    const unsigned d = value - base;
    return !(d >> 8) && !(d & mask);
}
} // namespace

// 367C: places where Sonic enters a tube (state 0C) or a pipe run (0D).
// Returns true when it switched state; the original then returns from the
// calling handler too (POP AF; RET). D39C delays the next check.
bool Game::zone_triggers() {
    if (player.tube_delay) {
        --player.tube_delay;
        return false;
    }
    const Object& o = sonic();
    bool enter = false;
    if (flow.zone == 2) {
        if (flow.act != 0) {
            return false;
        }
        enter = in_window(o.x, 0x075E, 0xF8) && in_window(o.y, 0x0160, 0xE0);
    } else if (flow.zone == 3) {
        if (flow.act == 0) {
            enter = (in_window(o.x, 0x0D5E, 0xF8) && o.y >= 0x0140) ||
                    (in_window(o.x, 0x0F3E, 0xF8) && o.y >= 0x0160);
        } else if (flow.act == 1) {
            enter = in_window(o.x, 0x0AFE, 0xF8) && o.y >= 0x0160;
        }
    }
    if (!enter) {
        return false;
    }
    enter_tube(); // 372B
    return true;
}

// 372B: facing right (angle below 80) starts the tube path (state 0C) from
// here; otherwise the exit (0D).
void Game::enter_tube() {
    Object& o = sonic();
    if (o.angle >= 0x80) {
        o.next_state = 0x0D;
        return;
    }
    o.next_state = 0x0C;
    o.origin_x = o.x;
    o.origin_y = o.y;
    player.tube_position[0] = 0;
    player.tube_position[1] = 0;
    player.tube_position[2] = 0;
}

// 48DF (zone 2): under water, every 120 frames a bubble may rise and the
// air count goes up; at 11 the countdown appears (object 09).
void Game::update_air() {
    check_water(); // 4948
    if (!water.underwater) {
        water.air_count = 0;
        return;
    }
    if (++water.air_ticks < 0x78) {
        return;
    }
    water.air_ticks = 0;
    // 4910: a bubble (object 0A, parameter 3) on a random half of these.
    if (!(refresh_register() & 0x04)) {
        spawn_object(object_type::bubble, 0x03);
    }
    ++water.air_count;
    if (water.air_count == 0x0B) {
        spawn_object(object_type::air_countdown, 0x00);
    }
}

// 4948: whether Sonic is under the water line of Aqua Lake (zone 2): fixed
// lines per area in acts 1 and 3, the rising water (D47F) in act 2. Going
// under makes a splash (object 0B).
void Game::check_water() {
    if (flow.zone != 2) {
        return;
    }
    const Object& o = sonic();
    unsigned line;
    if (flow.act == 0) {
        line = o.x < 0x0650 ? 0x01B8 : 0x0218;
    } else if (flow.act == 1) {
        line = water.rise_y;
    } else {
        line = o.x < 0x0880 ? 0x0118 : o.x < 0x0900 ? 0x0158 : 0x0138;
    }
    if (o.y < line) {
        water.underwater = 0; // 49D7
        return;
    }
    if (!water.underwater) {
        spawn_object(object_type::splash, 0x00);
    }
    water.underwater = 0xFF;
}

// 4820: the invincibility timer (1200 frames, then the act music returns)
// or, without it, one monitor reward per frame from D39D.
void Game::update_power_ups() {
    Object& o = sonic();
    if (o.power_up) {
        if (++player.power_up_ticks < 0x04B0) {
            return;
        }
        o.power_up = 0;
        player.power_up_ticks = 0;
        if (player.power_up_hold) {
            return;
        }
        if (flow.game_flags & u8(~game_flag::playing)) {
            return;
        }
        if (o.state == 0x20) {
            return;
        }
        play_act_music(); // 10B8
        return;
    }
    u8& items = player.item_flags;
    if (items & 0x01) { // 4875: ten rings
        items &= 0xFE;
        unsigned r = score.rings + 0x10u;
        if (r > 0x99) {
            r += 0x60;
        }
        score.rings = u8(r);
        sound_.request(sound::ten_rings);
        show_rings();
        if (score.rings < 0x10) {
            ++score.lives;
            sound_.request(sound::extra_life);
            show_lives();
        }
    } else if (items & 0x02) { // 489D: extra life
        items &= 0xFD;
        ++score.lives;
        sound_.request(sound::extra_life);
        show_lives();
    } else if (items & 0x10) { // 48AE: a continue
        items &= 0xEF;
        ++score.continues;
    } else if (items & 0x20) {
        items &= 0xDF;
    } else if (items & 0x04) {
        items &= 0xFB;
    } else if (items & 0x08) { // 48BE: invincibility (stars: objects 05)
        items &= 0xF7;
        sound_.request(sound::invincibility);
        o.power_up = 2;
        player.power_up_ticks = 0;
        spawn_object(object_type::invincibility_star, 0);
        spawn_object(object_type::invincibility_star, 1);
    } else if (items & 0x40) {
        items &= 0xBF;
    }
}

} // namespace sonic2_gg
