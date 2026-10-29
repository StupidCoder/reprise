// Objects and the player: the touch test (58AC), solid objects (51FF), an
// object's ground test (53CD), destroyed objects (51B1), points (2430) and
// what the player does on touching an object (37B5-39D3).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Bits of Object::touch_sides (offset 21) an object's touch test sets: where
// the player is. The player's own byte gets the opposite sides in its high
// nibble (5A3B adds them to its contact bits).
namespace side {
inline constexpr u8 above = 0x01, below = 0x02, right = 0x04, left = 0x08;
inline constexpr u8 vertical = above | below, horizontal = right | left;
} // namespace side

// 249B: 100 points (BCD, lowest byte first).
constexpr std::array<u8, 3> hundred_points{0x00, 0x01, 0x00};
} // namespace

// 58AC (0369): whether the player's frame box overlaps the object's
// (widths added; heights: the player's below the object's origin, the
// object's above it). The side kept is that of the smaller overlap. The
// player records the object's type (unless the object's flags bit 7) and
// the opposite side. Not while the player's flags bit 6, nor for objects
// off screen or with flags bit 6.
void Game::touch_test(Object& o) {
    player.touch_kind = 0x30;
    o.touch = 0;
    o.touch_sides &= 0xF0;
    auto& p = objects[0];
    if ((p.flags & 0x40) || (o.display & display_flag::offscreen) || (o.flags & 0x40)) {
        return;
    }
    const auto miss = [&] {
        o.touch_sides &= 0xF0;
    };
    // Horizontally (only within 256 pixels).
    const std::uint16_t dx = std::uint16_t(p.x - o.x);
    const u8 widths = u8(p.frame_width + o.frame_width);
    u8 x_overlap;
    if (!(dx & 0x8000)) {
        if (hi(dx) || lo(dx) > widths) {
            return miss();
        }
        x_overlap = u8(widths - lo(dx));
        o.touch_sides |= side::right;
    } else {
        const u8 d = u8(-lo(dx));
        if (hi(dx) != 0xFF || !d || d > widths) {
            return miss();
        }
        x_overlap = u8(widths - d);
        o.touch_sides |= side::left;
    }
    // Vertically.
    const std::uint16_t dy = std::uint16_t(p.y - o.y);
    u8 y_overlap;
    if (!(dy & 0x8000)) {
        if (hi(dy) || lo(dy) > p.frame_height) {
            return miss();
        }
        y_overlap = u8(p.frame_height - lo(dy));
        o.touch_sides |= side::below;
    } else {
        const u8 d = u8(-lo(dy));
        if (hi(dy) != 0xFF || !d || d > o.frame_height) {
            return miss();
        }
        y_overlap = u8(o.frame_height - d);
        o.touch_sides |= side::above;
    }
    o.touch_sides &= (x_overlap < y_overlap) ? side::horizontal : side::vertical;
    if (!(o.flags & 0x80)) {
        p.touch = o.type;
    }
    const u8 sides = o.touch_sides & 0x0F;
    const u8 opposite = sides ^ ((sides & side::vertical) ? side::vertical : side::horizontal);
    p.touch_sides = u8((p.touch_sides & 0x0F) | (opposite << 4));
}

// 51FF (036C): a solid object: the touch test, then the player is pushed
// out of it on the side found, unless already blocked on that side (its
// contact bits) or, sideways, at the screen's edge (32 pixels in).
void Game::solid_object(Object& o) {
    touch_test(o);
    auto& p = objects[0];
    switch (o.touch_sides & 0x0F) {
    case side::below: // 5239
        if (!(p.contact & 0x02)) {
            p.y = std::uint16_t(o.y + p.frame_height);
        }
        return;
    case side::above: // 5250
        if (!(p.contact & 0x01)) {
            p.y = std::uint16_t(o.y - o.frame_height);
        }
        return;
    case side::left: // 5268
        if (p.contact & 0x08) {
            return;
        }
        // Widescreen: the edge moves with the player's limits.
        if (std::uint16_t(camera.x + 0x20 - (0x38 - wide_screen_limits().left)) >= p.x) {
            return;
        }
        p.x = std::uint16_t(o.x - (o.frame_width + p.frame_width));
        return;
    case side::right: // 5297
        if (p.contact & 0x04) {
            return;
        }
        if (std::uint16_t(camera.x + 0xE0 + (wide_screen_limits().right - 0xC7)) < p.x) {
            return;
        }
        p.x = std::uint16_t(o.x + o.frame_width + p.frame_width);
        return;
    default:
        return;
    }
}

// 53CD (03A5): whether the object's origin is in a block without flags
// bits 6 and 7 (709A): nothing holds it up.
bool Game::nothing_below(const Object& o) {
    const u8 flags = probe_flags(o, 0, 0);
    return !(flags & 0xC0);
}

// 51B1 (0366): destroyed (sound B9); 51B8: when touched while the player
// rolls or has power-up 5 (sound CE). Points for types below 50, and the
// object becomes an explosion (type 07) where it is.
void Game::destroy_object(Object& o) {
    sound_.request(0xB9, 1);
    become_explosion(o);
}

void Game::destroy_if_attacked(Object& o) {
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (objects[0].power_up != 0x05 && !(objects[0].flags & 0x02)) {
        return;
    }
    sound_.request(0xCE, 1);
    become_explosion(o);
}

// 51D0
void Game::become_explosion(Object& o) {
    if (o.type < 0x50) {
        add_points(hundred_points); // 51F2: 2430 with 249B
    }
    o.type = 0x07;
    o.state = o.next_state = 0;
    o.display = 0;
    o.frame_ticks = 0;
    o.script = {};
    o.counter = 0;
    o.param &= 0x7F;
}

// 2430: points (BCD, not in demos). Each new 50000 (the ten thousands
// digit changing to 0 or 5): sound D1 and a reward counted.
void Game::add_points(const std::array<u8, 3>& add) {
    if (flow.demo) {
        return;
    }
    score.points_before = score.points;
    bool carry = false;
    for (unsigned i = 0; i < 3; ++i) {
        const auto sum = bcd_add_carry(score.points[i], add[i], carry);
        score.points[i] = sum.value;
        carry = sum.carry;
    }
    score.points_changed = carry ? 2 : 1;
    const u8 digit = score.points[2] & 0x0F;
    if (digit == (score.points_before[2] & 0x0F)) {
        return;
    }
    if (digit != 0 && digit != 5) {
        return;
    }
    sound_.request(0xD1, 1);
    ++score.point_rewards;
    if (flow.mode == 0x06) {
        draw_reward_emblems(); // 78C2: on the results screen, the emblems again
    }
}

// 3396 (0432): ten rings.
void Game::add_ten_rings() {
    score.rings_low = bcd_add(score.rings_low, 0x10);
    show_rings(); // 33BA
    if (score.rings_low < 0x10) {
        add_hundred_rings(); // 332A
    }
}

// 381B-39C5: the player touched an object (its type in Object::touch).
// Most are harmless here or handled by the object; with flags bit 1
// (rolling, jumping) the player bounces off item boxes and the like: from
// above up at 2.5 (1 under water) pixels per frame, from below down.
// Returns true for anything else: it hurts (384A).
bool Game::player_touch(Object& p) {
    const u8 t = p.touch;
    if (t == 0x25 || t == 0x2C || t == 0x4D || t == 0x27 || t == 0x43) {
        p.touch = 0;
        return false;
    }
    if (p.flags & 0x02) {
        if (p.touch_sides & 0x10) { // 3993: hit from below
            if (p.state != 0x09) {
                p.flags |= 0x01;
                p.vy = 0x0080;
            }
        } else if (p.touch_sides & 0x20) { // 39A6: landed on top
            p.flags |= 0x01;
            p.terrain &= u8(~0x02);
            p.contact &= u8(~0x02);
            p.touch_sides &= u8(~0x20);
            p.vy = (p.flags & 0x04) ? 0xFF00 : 0xFD80;
        }
        p.touch = 0;
        return false;
    }
    if (t == 0x06 || t == 0x22 || t == 0x2D) {
        p.touch = 0;
        return false;
    }
    return true;
}

} // namespace triple_trouble_gg
