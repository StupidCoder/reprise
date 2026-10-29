// Objects of Under Ground Zone (bank 1C) and Sonic riding the mine cart.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
bool in_table(const Assets& rom, unsigned bank, unsigned table, unsigned count, u8 value) {
    for (unsigned i = 0; i < count; ++i) {
        if (rom.byte(bank, table + i) == value) {
            return true;
        }
    }
    return false;
}
} // namespace

// BD38 (bank 1C): whether a walking enemy must turn: a wall 16 pixels up 8
// pixels ahead (profile 81, 8D or 90, except the blocks listed at BD88), or
// no solid ground 16 pixels down 8 pixels ahead. Returns 1 to turn.
u8 Game::enemy_must_turn(Object& o) {
    const unsigned ahead = (hi(o.vx) & 0x80) ? 0xFFF8 : 0x0008;
    const u8 wall = probe_type(o, ahead, 0xFFF0);
    if (wall == 0x81 || wall == 0x8D || wall == 0x90) {
        if (!in_table(rom, 0x1C, 0xBD88, 8, probe.block)) {
            return 1;
        }
    }
    const u8 ground = probe_type(o, ahead, 0x0010);
    return (ground & 0x80) ? 0 : 1;
}

// BC2D / BC71 (bank 1C): Sonic within `range` pixels horizontally /
// vertically (distance below 256 and its low byte below `range`).
namespace {
bool within(unsigned a, unsigned b, unsigned range) {
    const unsigned d = a >= b ? a - b : b - a;
    return !(d >> 8) && (d & 0xFF) < range;
}
} // namespace

bool Game::sonic_near_x(const Object& o, unsigned range) {
    return within(o.x, sonic().x, range);
}

bool Game::sonic_near_y(const Object& o, unsigned range) {
    return within(o.y, sonic().y, range);
}

// --- 23: a walking enemy that throws two bombs (24) -----------------------

// 850C
void Game::bomb_walker_start(Object& o) {
    o.next_state = 1;
    o.counter = 0;
}

// 8515 / 852F: walking right / left (each turn counted in 1E).
void Game::bomb_walker_right(Object& o) {
    ++o.counter;
    o.angle &= 0x7F;
    o.vx = 0x00C0;
    o.vy = 0x0200;
}

void Game::bomb_walker_left(Object& o) {
    o.angle |= 0x80;
    o.vx = 0xFF40;
    o.vy = 0x0200;
}

// 8546 (state 1): walks; turns at walls and ledges (state 2); after four
// rounds it stops to throw (state 3).
void Game::bomb_walker_walk_right(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    move_object(o);
    object_floor(o);
    touch_sonic(o);
    if (enemy_must_turn(o) & 1) {
        o.next_state = 2;
    } else if (o.counter >= 4) {
        o.next_state = 3;
    }
    destroy_if_attacked(o); // 020C
}

// 8572 (state 2)
void Game::bomb_walker_walk_left(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    move_object(o);
    object_floor(o);
    touch_sonic(o);
    if (enemy_must_turn(o) & 1) {
        o.next_state = 1;
    }
    destroy_if_attacked(o);
}

// 858E (state 3): standing while throwing.
void Game::bomb_walker_throw(Object& o) {
    o.counter = 0;
    move_object(o);
    object_floor(o);
    touch_sonic(o);
    destroy_if_attacked(o);
}

// 85B1: a bomb thrown up and to one side (parameter 0 right, else left).
void Game::bomb_start(Object& o) {
    o.vx = o.param ? 0xFF80 : 0x0080;
    o.vy = 0xFC00;
    o.next_state = 1;
}

// 85D1: flies in an arc; hurts Sonic on touch.
void Game::bomb_fly(Object& o) {
    move_object(o);
    o.vy += 0x20;
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
    }
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
}

// --- 29: the mine cart ------------------------------------------------------

// 8952
void Game::cart_start(Object& o) {
    o.next_state = 1;
}

// 8AC2: touching the cart from above or the sides puts Sonic in it (state
// 16). After he jumps out, the cart ignores him for IX+1F frames (flag 6).
bool Game::cart_board(Object& o) {
    if (o.flags & 0x40) {
        if (!--o.timer) {
            o.flags &= 0xBF;
        }
    }
    touch_sonic(o);
    o.touch_sides &= 0x0D;
    if (!o.touch_sides) {
        return false;
    }
    player.riding = unsigned(0xD500 + unsigned(&o - &objects[0]) * 0x40);
    Object& s = sonic();
    s.flags &= 0xFC;
    s.next_state = 0x16;
    sound_.request(sound::thud);
    return true;
}

// 8957 (state 1): parked until Sonic boards; then it rolls the way its
// parameter says (state 2 right, 3 left) and is no longer re-placed.
void Game::cart_parked(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (!cart_board(o)) {
        cart_move(o); // 8AB3
        return;
    }
    cart_go(o); // 8962
}

// 8962
void Game::cart_go(Object& o) {
    o.placement = 0;
    o.next_state = o.param;
    o.vy = 0x0400;
}

// 8976 / 89B9 (states 2, 3): accelerating up to 4 pixels per frame. A rail
// end (blocks at 8B11) 16 pixels ahead crashes it while on the ground; off
// the ground it falls (state 4).
void Game::cart_roll(Object& o, bool left) {
    unsigned v = o.vx;
    if (left) {
        v = (0x10000 - v) & 0xFFFF;
    }
    if ((v >> 8) < 4) {
        v += 0x10;
        o.vx = left ? (0x10000 - v) & 0xFFFF : v;
    }
    probe_type(o, left ? 0xFFF0 : 0x0010, 0xFFF0); // 0263
    if (in_table(rom, 0x1C, 0x8B11, 0x1B, probe.block)) {
        // 89FC
        if (o.terrain & 0x02) {
            cart_crash(o); // 8A56
            return;
        }
        o.next_state = 4;
        o.vy = 0;
        return;
    }
    cart_board(o);
    if (o.terrain & 0x02) {
        cart_move(o);
        return;
    }
    o.next_state = 4;
    o.vy = 0;
}

// 8A10 (state 4): falling; it stops against rail ends and lands back on
// rails (previous block listed at 8AF7) or crashes.
void Game::cart_fall(Object& o) {
    const unsigned ahead = (hi(o.vx) & 0x80) ? 0xFFF0 : 0x0010;
    probe_type(o, ahead, 0xFFF0);
    if (in_table(rom, 0x1C, 0x8B11, 0x1B, probe.block)) {
        o.vx = 0;
    }
    fall(o, 0x0020, 0x0600);
    if (!(o.terrain & 0x02)) {
        cart_move(o);
        return;
    }
    if (in_table(rom, 0x1C, 0x8AF7, 0x1A, player.cart_block)) {
        cart_go(o);
        return;
    }
    cart_crash(o);
}

// 8A56: the cart breaks; a rider is thrown off (37EF).
void Game::cart_crash(Object& o) {
    sound_.request(sound::cart_crash);
    o.terrain &= 0xFD;
    // RES 7,(IY+4): IY is left over from earlier code. While Sonic rides,
    // his handler (4741) leaves IY at the cart, so the cart becomes visible
    // again; this reconstruction assumes that case. (Hypothesis.)
    o.display &= u8(~display_flag::hidden);
    o.vy = 0xFC00;
    o.vx = 0;
    o.next_state = 5;
    const unsigned self = 0xD500 + unsigned(&o - &objects[0]) * 0x40;
    if (player.riding != self) {
        return;
    }
    player.riding = 0;
    throw_off_sonic(); // 37EF
}

// 8A95 (state 5): the broken cart falls to the ground.
void Game::cart_broken(Object& o) {
    fall(o, 0x0040, 0x0600);
    if (!(o.terrain & 0x02)) {
        cart_move(o);
        return;
    }
    o.next_state = 6;
}

// 8AAA (state 6): blinks away for good.
void Game::cart_gone(Object& o) {
    o.placement = 0;
    o.param = 0;
}

// 8AB3
void Game::cart_move(Object& o) {
    move_object(o);
    object_floor(o);
    player.cart_block = probe.block;
    destroy_if_attacked(o); // 020C
}

// 37EF: Sonic thrown off without losing rings (the second half of 37B5).
void Game::throw_off_sonic() {
    Object& o = sonic();
    o.touch = 0;
    o.next_state = 0x1E;
    o.flags = u8((o.flags | 0x01) & ~0x04);
    o.terrain &= 0xFD;
    o.vy = (o.terrain & 0x01) ? 0x0000 : 0xFC00;
    o.vx = (o.contact & 0x08) ? 0x0100 : 0xFF00;
    player.input_force = 0;
}

// 4717 (state 16): in the cart: the cart's sprite is hidden and Sonic's
// frames show both.
void Game::sonic_board_cart() {
    Object& o = sonic();
    o.display &= u8(~display_flag::hidden);
    static constexpr u8 cart_tiles[8] = {0x00, 0x02, 0x04, 0x6A, 0x6C, 0x6E, 0x70, 0x72};
    for (int i = 0; i < 8; ++i) {
        player.cart_frame[i] = cart_tiles[i];
    }
    Object& cart = objects[(player.riding - 0xD500) / 0x40];
    follow_cart(cart); // 477E
    cart.display |= display_flag::hidden;
}

// 4712 (called by the script): riding.
void Game::sonic_ride() {
    sonic().next_state = 0x17;
}

// 4741 (state 17): riding; a jump (or getting hurt) leaves the cart rolling
// on without him.
void Game::sonic_riding() {
    Object& o = sonic();
    Object& cart = objects[(player.riding - 0xD500) / 0x40];
    follow_cart(cart);
    collect_map_items(); // 723C
    touch_objects();     // 3773
    if (o.next_state != 0x1E) {
        o.flags &= 0xFE;
        if (!(input.held[0] & button::jump)) {
            return;
        }
    }
    // 475F
    cart.display &= u8(~display_flag::hidden);
    cart.timer = 0x10;
    cart.flags |= 0x40;
    cart.next_state = cart.param;
    player.riding = 0;
    start_jump(); // 3A9D
}

// 477E: Sonic takes the cart's position and velocity, curled, and faces its
// direction (parameter 2 is right).
void Game::follow_cart(Object& cart) {
    Object& o = sonic();
    o.x = unsigned(cart.x);
    o.y = unsigned(cart.y);
    o.vx = unsigned(cart.vx);
    o.vy = unsigned(cart.vy);
    o.flags |= 0x02;
    collide_walls(); // 6FC6
    if (cart.param == 2) {
        o.display &= u8(~display_flag::mirrored);
    } else {
        o.display |= display_flag::mirrored;
    }
}

// 47BB (vector 02DB): the cart's rocking frames: the frame index steps with
// the motion; up and down select other frame sets (4808, 4814; 47FC).
void Game::sonic_cart_frames() {
    Object& o = sonic();
    const u8 held = input.held[0];
    unsigned table = 0x47FC;
    if (held & button::up) {
        table = 0x4808;
    } else if (held & button::down) {
        table = 0x4814;
    }
    u8 step = o.counter;
    step = (hi(o.vx) & 0x80) ? u8(step - 1) : u8(step + 1);
    step &= 3;
    o.counter = step;
    const unsigned e = table + step * 3u;
    o.frame = rom.byte(1, e);
    player.cart_frame[6] = rom.byte(1, e + 1);
    player.cart_frame[7] = rom.byte(1, e + 2);
    o.frame_ticks = 3;
}

// --- 34: an enemy pacing and charging --------------------------------------

namespace {
// The parts of 34's handlers that are the same: move, own floor, touch
// Sonic, and destruction when attacked (B30F -> 020C).
} // namespace

// B0F5 (state 0): starts when on screen.
void Game::pacer_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.next_state = 1;
}

// B0FF / B15F (states 1, 2): walking right / left at 1 pixel per frame;
// Sonic within 40 pixels ahead and level makes it charge (states 7 / 8);
// walls and ledges turn it.
void Game::pacer_walk(Object& o, bool left) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.vx = left ? 0xFF00 : 0x0100;
    o.vy = 0x0100;
    if (left) {
        touch_sonic(o);
        move_object(o);
        object_floor(o);
    } else {
        move_object(o);
        object_floor(o);
        touch_sonic(o);
    }
    if (sonic_near_y(o, 0x40)) {
        // Sonic ahead (no borrow) and less than 40 pixels away.
        const unsigned a = left ? unsigned(o.x) : unsigned(sonic().x);
        const unsigned b = left ? unsigned(sonic().x) : unsigned(o.x);
        if (a >= b && a - b < 0x40) {
            o.next_state = left ? 0x08 : 0x07;
            o.vx = 0;
        }
    }
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
        return;
    }
    face_motion(o); // 023C
    if (enemy_must_turn(o) & 1) {
        o.next_state = left ? 0x01 : 0x02;
    }
}

// B1C0 / B203 (states 3, 4): slowing to a turn (vx steps by 2 to -1 / +1).
void Game::pacer_turn(Object& o, bool to_left) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    face_motion(o);
    touch_sonic(o);
    if (enemy_must_turn(o) & 1) {
        o.next_state = to_left ? 0x05 : 0x06;
        return;
    }
    move_object(o);
    object_floor(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
        return;
    }
    const unsigned v = to_left ? (o.vx - 2u) & 0xFFFF : (o.vx + 2u) & 0xFFFF;
    o.vx = v;
    if (v == (to_left ? 0xFF00u : 0x0100u)) {
        o.next_state = to_left ? 0x02 : 0x01;
    }
}

// B246 / B26A (states 5, 6): creeping at -2 / +2 subpixels per frame.
void Game::pacer_creep(Object& o, bool left) {
    o.vx = left ? 0xFFFE : 0x0002;
    o.vy = 0x0100;
    touch_sonic(o);
    move_object(o);
    object_floor(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
    }
}

// B28E / B2CE (states 7, 8): charging, accelerating by 4 up to 2.5 pixels
// per frame, until it must turn (states 5 / 6).
void Game::pacer_charge(Object& o, bool left) {
    move_object(o);
    object_floor(o);
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        destroy_if_attacked(o);
        return;
    }
    face_motion(o);
    if (!(enemy_must_turn(o) & 1)) {
        if (!left) {
            o.vx += 4;
            if (o.vx < 0x0280) {
                return;
            }
        } else {
            o.vx -= 4;
            if (o.vx >= 0xFD80) {
                return;
            }
        }
    }
    o.vx = 0;
    o.next_state = left ? 0x06 : 0x05;
}

// --- 35: a turret firing at Sonic (shots 3C) ------------------------------

// B402
void Game::turret_start(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    o.display |= display_flag::mirrored;
    o.next_state = 1;
}

// B410 (state 1): fires when Sonic is within 128 pixels both ways.
void Game::turret_watch(Object& o) {
    if (!sonic_near_x(o, 0x80)) {
        return;
    }
    if (!sonic_near_y(o, 0x80)) {
        return;
    }
    o.next_state = 2;
}

// B424 (state 3): can be destroyed for good while firing.
void Game::turret_fire(Object& o) {
    touch_sonic(o);
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    o.param = 0x40;
    destroy_if_attacked(o);
}

// B431 (state 5)
void Game::turret_remove(Object& o) {
    o.type = object_type::removed;
    o.placement = 0;
}

// BDAB: a shot flying left at 1.4 pixels per frame.
void Game::shot_start(Object& o) {
    o.vx = 0xFE90;
    o.vy = 0;
    o.next_state = 1;
}

// BDC1: hurts Sonic on touch; gone when off screen.
void Game::shot_fly(Object& o) {
    if (o.display & display_flag::offscreen) {
        o.type = object_type::removed;
        return;
    }
    touch_sonic(o);
    move_object(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
}

} // namespace sonic2_gg
