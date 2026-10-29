// Helpers the object handlers call through the vectors at 0200-0263 (bank 1,
// 6127-6703).

#include "sonic2_gg/game/game.hpp"
#include <stdexcept>
#include <string>

namespace sonic2_gg {

namespace {
unsigned neg16(unsigned v) {
    return (0x10000 - v) & 0xFFFF;
}
} // namespace

// A byte the code reads through a pointer that may lead into work RAM (as
// the frame tables of Sonic in the cart do, D3A0) or into the ROM.
u8 Game::data_byte(unsigned bank, unsigned address) const {
    address &= 0xFFFF;
    if (address >= 0xC000) {
        return ram_data(address);
    }
    return rom.byte(bank, address);
}

// Some sprite frames in the cartridge point into work RAM instead: their
// tile numbers at D3A0 (Sonic in the cart, player.cart_frame) or their
// layout at D3CE (sprites.run_layout).
u8 Game::ram_data(unsigned address) const {
    if (address >= 0xD3A0 && address < 0xD3A8) {
        return player.cart_frame[address - 0xD3A0];
    }
    if (address >= 0xD3CE && address < 0xD3EE) {
        const unsigned at = address - 0xD3CE;
        const auto& offset = sprites.run_layout[at / 4];
        const unsigned word = (at & 2) ? offset.dx : offset.dy;
        return u8((at & 1) ? word >> 8 : word);
    }
    if (address >= 0xD3EE && address < 0xD3FE) {
        const unsigned at = address - 0xD3EE;
        const auto& offset = sprites.run_layout[at / 2];
        return (at & 1) ? offset.dx_fraction : offset.dy_fraction;
    }
    throw std::runtime_error("Sprite data read from unmapped RAM address " +
                             std::to_string(address));
}

unsigned Game::data_word(unsigned bank, unsigned address) const {
    return data_byte(bank, address) | (data_byte(bank, address + 1) << 8);
}

// 6127 (0209): the object at Sonic's position.
void Game::move_to_sonic(Object& o) {
    o.x = unsigned(sonic().x);
    o.y = unsigned(sonic().y);
}

// 613C (020C): an object touched by a curled Sonic is destroyed.
void Game::destroy_if_attacked(Object& o) {
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (!(sonic().flags & 0x02)) {
        return;
    }
    o.param = 0x80;
    explode(o); // 614C
}

// 614C (025D): points for an enemy (types 20-3F, table 617D: 1D14 gives 100),
// and the object turns into an explosion (type 0F).
void Game::explode(Object& o) {
    const u8 t = u8(o.type - 0x20);
    if (o.type >= 0x20 && t < 0x20) {
        if (rom.word(rom::enemy_points, t * 2u) == rom::points_100_routine) {
            add_score(rom::points_100);
        }
    }
    o.type = object_type::explosion;
    o.state = 0;
    o.next_state = 0;
    o.display = 0;
    o.frame_ticks = 0;
    o.restart_script();
}

// 61BE (020F): gravity: vy += dy while below `limit` (a vy that would reach
// the limit is left unchanged rather than clamped).
void Game::fall(Object& o, unsigned dy, unsigned limit) {
    const unsigned v = (o.vy + dy) & 0xFFFF;
    if (!(v & 0x8000) && v >= limit) {
        return;
    }
    o.vy = v;
}

// 61DA (0212): an object falling onto solid ground (profile type with bit 7)
// at `speed` or less stops (returns 0); falling faster, it bounces up with
// vy = speed - vy (returns 1); no ground or rising: -1.
int Game::land_object(Object& o, unsigned speed) {
    const u8 type = probe_type(o, 0, 0);
    if (!(type & 0x80) || (hi(o.vy) & 0x80)) {
        return -1;
    }
    if (speed < unsigned(o.vy)) {
        o.vy = (speed - o.vy) & 0xFFFF; // 6204
        return 1;
    }
    o.vy = 0;
    return 0;
}

// 620F (0218): touch test and the solid-object response.
void Game::solid_object(Object& o) {
    touch_sonic(o);    // 6704
    push_sonic_out(o); // 62B1
}

// 6215 (021B) / 621B (021E): the same, then a curled Sonic (states 09, 0A,
// 1B) touching it bounces off (4 pixels per frame away, launched).
void Game::bumper_object(Object& o) {
    touch_sonic(o);
    push_sonic_out(o);
    bounce_sonic_off(o);
}

void Game::bounce_sonic_off(Object& o) {
    Object& s = sonic();
    if (s.state != 0x0A && s.state != 0x09 && s.state != 0x1B) {
        return;
    }
    const u8 sides = o.touch_sides & 0x0F;
    if (!sides) {
        return;
    }
    if ((sides & 0x01) || (!(sides & 0x02) && s.vy)) {
        s.vy = 0xFC00;
    }
    s.next_state = 0x1B;
    sound_.request(sound::boss_hit);
    s.vx = (s.x >= o.x) ? 0x0400 : 0xFC00;
}

// 6269 (0221): a bumper-like object that is hit (bumper_object) blinks for
// IX+1F frames.
void Game::hit_object_bumper(Object& o) {
    bumper_object(o);
    blink_or_reset(o, false);
}

// 6280 (0224): a solid object that takes hits: each hit (64 frames apart)
// lowers its hit count (IX+24).
void Game::hit_object_solid(Object& o) {
    solid_object(o);
    blink_or_reset(o, true);
}

void Game::blink_or_reset(Object& o, bool count) {
    u8& ticks = o.timer;
    if (ticks) {
        // 62A5
        --ticks;
        o.display |= display_flag::flicker;
        o.touch_sides = 0;
        return;
    }
    o.display &= u8(~(display_flag::flicker | display_flag::hidden));
    if (!count) {
        o.touch_sides = 0;
        return;
    }
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    ticks = 0x40;
    --o.hits;
    sound_.request(sound::boss_hit);
}

// 62B1 (0227): moves Sonic out of a solid object on the side he touched
// (table 62C4); sideways only while he is within screen X 20-E0.
// Widescreen margins (zero on the LCD) widen that to the presented view.
void Game::push_sonic_out(Object& o) {
    Object& s = sonic();
    switch (o.touch_sides & 0x0F) {
    case 0x01: // 62FC: on top
        if (s.contact & 0x01) {
            return;
        }
        s.y = unsigned(o.y - o.height);
        return;
    case 0x02: // 62E5: below
        if (s.contact & 0x02) {
            return;
        }
        s.y = unsigned(o.y + s.height);
        return;
    case 0x04: // 6343: at its right
        if (s.contact & 0x04) {
            return;
        }
        if (((camera.x + 0xE0u + unsigned(wide_right)) & 0xFFFF) < s.x) {
            return;
        }
        s.x = unsigned(o.x + o.width + s.width);
        return;
    case 0x08: // 6314: at its left
        if (s.contact & 0x08) {
            return;
        }
        if (int((camera.x + 0x20u) & 0xFFFF) - wide_left >= int(unsigned(s.x))) {
            return;
        }
        s.x = unsigned(o.x - (o.width + s.width));
        return;
    default:
        return;
    }
}

// 6371 (022D)
void Game::clear_blink(Object& o) {
    o.timer = 0;
}

// 6394 (0236): the velocity for direction `angle` at `speed`: the sine table
// at 0330 (signed bytes) times the speed, divided by 16 (arithmetically).
void Game::velocity_from_angle(Object& o) {
    o.vx = 0;
    o.vy = 0;
    if (!o.speed) {
        return;
    }
    auto component = [&](u8 angle) {
        const int s = std::int8_t(rom.byte(rom::sine, angle));
        unsigned hl = 0;
        for (int i = 0; i < o.speed; ++i) {
            hl = (hl + unsigned(s)) & 0xFFFF;
        }
        return unsigned(std::int16_t(hl) >> 4) & 0xFFFF;
    };
    o.vx = component(o.angle);
    o.vy = component(u8(o.angle + 0xC0));
}

// 6404 (0239): a wall 17 pixels up at the object's leading edge sets terrain
// bit 2 (moving right) or 3.
void Game::object_wall(Object& o) {
    o.terrain = 0;
    unsigned dx = o.width;
    const bool left = hi(o.vx) & 0x80;
    if (left) {
        dx = neg16(dx);
    }
    const u8 type = probe_type(o, dx, 0xFFEF);
    if (type != 0x81 && type != 0x8D) {
        return;
    }
    o.terrain |= left ? 0x08 : 0x04;
}

// 643B (023C): faces the way it moves.
void Game::face_motion(Object& o) {
    if (!o.vx) {
        return;
    }
    if (hi(o.vx) & 0x80) {
        o.display |= display_flag::mirrored;
    } else {
        o.display &= u8(~display_flag::mirrored);
    }
}

// 6452 (023F) / 6456 (0242): faces Sonic (or the X in DE).
void Game::face_x(Object& o, unsigned x) {
    if (o.x == x) {
        return;
    }
    if (o.x < x) {
        o.display &= u8(~display_flag::mirrored);
    } else {
        o.display |= display_flag::mirrored;
    }
}

// 6479 (0248): vx = speed toward target X (+ offset, mirrored with the
// object), comparing 8-pixel columns; 0 when there.
void Game::move_toward_x(Object& o, unsigned target, unsigned offset, unsigned speed) {
    if (o.display & display_flag::mirrored) {
        offset = (~offset + 1) & 0xFFFF; // DEC DE; CPL; CPL
    }
    const unsigned t = ((target + offset) & 0xFFF8) & 0xFFFF;
    const unsigned x = o.x & 0xFFF8;
    unsigned v = speed;
    if (t < x) {
        v = neg16(speed);
    }
    if (t == x) {
        v = 0;
    }
    o.vx = v;
}

// 64AF (024B): the same vertically with 4-pixel rows.
void Game::move_toward_y(Object& o, unsigned target, unsigned offset, unsigned speed) {
    const unsigned t = ((target + offset) & 0xFFFC) & 0xFFFF;
    const unsigned y = o.y & 0xFFFC;
    unsigned v = speed;
    if (t < y) {
        v = neg16(speed);
    }
    if (t == y) {
        v = 0;
    }
    o.vy = v;
}

// 66C3 (0257): position += velocity (both 24-bit additions).
void Game::move_object(Object& o) {
    add_to_position(o.x_sub, o.x, o.vx);
    add_to_position(o.y_sub, o.y, o.vy);
}

// 6704 (025A): whether Sonic touches the object (boxes of half width
// IX+2C + Sonic's D52C, height IX+2D below / Sonic's D52D above). Sets
// touch_sides bits 0-3 (Sonic above / below / right / left; only the axis of
// the larger overlap), `touch`, D520 (the slot touched) and Sonic's own
// side bits in D521's high nibble. Ignored while Sonic is dying (state 1F,
// 28) or hurt, and for objects with flag bit 6 unless D503 bit 6 says
// otherwise.
void Game::touch_sonic(Object& o) {
    Object& s = sonic();
    if (s.power_up == 2) {
        // Invincible: every touch counts as an attack and cannot hurt.
        s.flags |= 0x82;
    }
    o.touch = 0;
    o.touch_sides &= 0xF0;
    if (s.state == 0x1F || s.state == 0x28) {
        return;
    }
    if (o.flags & 0x40) {
        return;
    }
    if (!(o.flags & 0x80) && (s.flags & 0x40)) {
        return;
    }
    const auto miss = [&] {
        o.touch_sides &= 0xF0;
    }; // 67F3
    const unsigned dx = (s.x - o.x) & 0xFFFF;
    unsigned c;
    if (!(dx & 0x8000)) {
        if ((dx >> 8) || u8(s.width + o.width) < (dx & 0xFF)) {
            miss();
            return;
        }
        c = dx & 0xFF;
        o.touch_sides |= 0x04;
    } else {
        const u8 l = u8(-int(dx & 0xFF));
        if ((dx >> 8) != 0xFF || !l || u8(s.width + o.width) < l) {
            miss();
            return;
        }
        c = l;
        o.touch_sides |= 0x08;
    }
    const unsigned dy = (s.y - o.y) & 0xFFFF;
    unsigned l;
    if (!(dy & 0x8000)) {
        if ((dy >> 8) || s.height < (dy & 0xFF)) {
            miss();
            return;
        }
        l = dy & 0xFF;
        o.touch_sides |= 0x02;
    } else {
        const u8 n = u8(-int(dy & 0xFF));
        if ((dy >> 8) != 0xFF || !n || o.height < n) {
            miss();
            return;
        }
        l = n;
        o.touch_sides |= 0x01;
    }
    // 67AB: only the sides of the axis with the larger distance remain (the
    // AND also clears the high nibble).
    o.touch_sides &= (l < c) ? 0x0C : 0x03;
    if (!(s.flags & 0x80)) {
        o.touch = 1;
    }
    if (!(o.flags & 0x80)) {
        s.touch = u8(slot_number(o)); // D520
    }
    // 67D4: Sonic's side of the contact (the opposite one) into the high
    // nibble of his D521.
    const u8 b = o.touch_sides & 0x0F;
    const u8 swap = (b & 0x03) ? 0x03 : 0x0C;
    const u8 a = u8(b ^ swap);
    const u8 high = u8((a << 4) | (a >> 4));
    s.touch_sides = u8((s.touch_sides & 0x0F) | high);
}

} // namespace sonic2_gg
