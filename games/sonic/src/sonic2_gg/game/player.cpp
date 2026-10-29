// Sonic: the per-frame update (2F4F), the general movement states, physics
// and damage (bank 0, 2F72-3D2D). Sonic is object slot 0 (D500); his script
// (type 1, bank 1F) calls the handlers below through the vectors at 0266-0308.
//
// Units: positions are 16.8 fixed point (x_sub/x, y_sub/y), velocities 8.8
// signed (vx D516, vy D518; 0x100 = one pixel per frame).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
unsigned neg16(unsigned v) {
    return (0x10000 - v) & 0xFFFF;
}

// BIT 7,H; DEC HL; CPL; CPL: the magnitude of a signed 16-bit value.
unsigned abs16(unsigned v) {
    return (v & 0x8000) ? neg16(v) : v;
}

// Bits of Sonic's flags (D503).
namespace sonic_flag {
inline constexpr u8 airborne = 0x01;
inline constexpr u8 curled = 0x02;                   // rolling or jumping: hurts enemies
inline constexpr u8 riding = 0x04;                   // on a vehicle (cart, glider)
[[maybe_unused]] inline constexpr u8 special = 0x10; // touched a special block (74DF)
inline constexpr u8 hurt_once = 0x40;                // set with `hurt`
inline constexpr u8 hurt = 0x80;                     // invulnerable after a hit
} // namespace sonic_flag

// Bits of `terrain` (D522) and `contact` (D523).
namespace contact_bit {
inline constexpr u8 ceiling = 0x01, floor = 0x02, right_wall = 0x04, left_wall = 0x08;
} // namespace contact_bit

// Script states of Sonic (bank 1F, A51F).
namespace state {
inline constexpr u8 standing = 0x01, idle = 0x02, look_up = 0x03, crouch = 0x04;
inline constexpr u8 walking = 0x05, running = 0x06, skid_left = 0x07, skid_right = 0x08;
inline constexpr u8 rolling = 0x09, jumping = 0x0A, sprung = 0x0B;
inline constexpr u8 falling = 0x0E, intro = 0x0F, balancing = 0x19;
inline constexpr u8 slide = 0x1A, launched = 0x1B, bounced = 0x1C, floating = 0x1D;
inline constexpr u8 hurt = 0x1E, dying = 0x1F;
// Set elsewhere (player_support.cpp, the end sign).
[[maybe_unused]] inline constexpr u8 pipe = 0x0C, pipe_exit = 0x0D, act_end = 0x20;
} // namespace state
} // namespace

Object& Game::sonic() {
    return objects[0];
}

bool Game::jump_pressed() const {
    return input.pressed[0] & button::jump;
}

// 2F4F: Sonic's update: script, current handler, then power-up timers and
// monitor rewards (4820). Running out of time kills him (3828).
void Game::update_sonic() {
    Object& o = sonic();
    if (!o.type) {
        return;
    }
    if (clock.time_over) {
        die(); // 3828
    }
    animate_sonic();       // 5EFF
    run_object_handler(o); // 5FF9
    update_power_ups();    // 4820
}

// 5EFF: in states 1F (dying) and 28 the script only counts down: a requested
// state is not taken.
void Game::animate_sonic() {
    Object& o = sonic();
    if (o.state == 0x1F || o.state == 0x28) {
        if (--o.frame_ticks == 0) {
            run_script(o, 0x1F);
        }
        return;
    }
    animate_object(o, 0x1F);
}

// --- Entering states ----------------------------------------------------

// 2F72 (vector 0266): the first frame. In the intro (zone 9) Sonic is
// scripted (state 0F).
void Game::sonic_start() {
    Object& o = sonic();
    o.angle = 0x40;
    o.speed = 0;
    o.frame = 0x10;
    if (flow.zone == 9) {
        o.sprite_count = 4;
        camera.mode |= camera_mode::hold_vertical;
        o.next_state = state::intro;
        return;
    }
    o.sprite_count = 8;
    player.max_speed = 0x0400;
    halve_speed_under_water(); // 491C
    o.display &= u8(~display_flag::mirrored);
    ++o.next_state;
}

// 2FBD: standing still.
void Game::to_standing() {
    Object& o = sonic();
    o.flags &= u8(~(sonic_flag::airborne | sonic_flag::curled));
    o.next_state = state::standing;
    player.idle_ticks = 0;
    o.vx = 0;
    player.max_speed = 0x0400;
    halve_speed_under_water();
    camera.mode &= u8(~camera_mode::hold_vertical);
    check_balance(); // 38BE
}

// 2FE3: walking.
void Game::to_walking() {
    Object& o = sonic();
    o.flags &= u8(~(sonic_flag::airborne | sonic_flag::curled | sonic_flag::hurt_once));
    o.next_state = state::walking;
    player.max_speed = 0x0400;
    halve_speed_under_water();
    face_input(); // 318D
    camera.offset_y = 0x80;
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 300A / 3015: looking up / crouching (both stop him).
void Game::to_look_up() {
    sonic().vx = 0;
    sonic().next_state = state::look_up;
}

void Game::to_crouch() {
    Object& o = sonic();
    o.vx = 0;
    o.flags &= u8(~(sonic_flag::curled | sonic_flag::hurt_once));
    o.next_state = state::crouch;
}

// 3028: running at top speed.
void Game::to_running() {
    Object& o = sonic();
    o.flags &= u8(~(sonic_flag::airborne | sonic_flag::curled));
    o.next_state = state::running;
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 303A / 3052: skidding (not in the air).
void Game::to_skid(bool left) {
    Object& o = sonic();
    if (o.flags & sonic_flag::airborne) {
        return;
    }
    sound_.request(sound::skid);
    o.flags &= u8(~sonic_flag::curled);
    o.next_state = left ? state::skid_left : state::skid_right;
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 306A: rolling, if moving at least one pixel per frame; else crouching.
void Game::to_rolling() {
    Object& o = sonic();
    if (!hi(o.vx)) {
        to_crouch();
        return;
    }
    o.next_state = state::rolling;
    player.max_speed = 0x0600;
    halve_speed_under_water();
    o.flags = u8((o.flags & ~sonic_flag::airborne) | sonic_flag::curled);
    sound_.request(sound::roll);
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 3091: launched curled (state 1B).
void Game::to_launched() {
    Object& o = sonic();
    o.next_state = state::launched;
    o.flags |= sonic_flag::airborne | sonic_flag::curled;
    o.terrain &= u8(~contact_bit::floor);
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 30A7: thrown upward by a spring (vy), unless already moving up.
void Game::spring_up(unsigned vy) {
    Object& o = sonic();
    if (hi(o.vy) & 0x80) {
        return;
    }
    o.next_state = state::sprung;
    o.vy = vy;
    o.flags = u8((o.flags | sonic_flag::airborne) & ~(sonic_flag::curled | sonic_flag::riding));
    o.terrain &= u8(~contact_bit::floor);
    sound_.request(sound::spring);
}

// 30CC: bounced curled (state 1C), unless already moving up.
void Game::bounce_up(unsigned vy) {
    Object& o = sonic();
    if (hi(o.vy) & 0x80) {
        return;
    }
    o.next_state = state::bounced;
    o.vy = vy;
    o.flags |= sonic_flag::airborne | sonic_flag::curled;
    o.terrain &= u8(~contact_bit::floor);
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 30ED / 310C (two identical copies): sent rolling at speed vx.
void Game::launch_rolling(unsigned vx) {
    Object& o = sonic();
    o.next_state = state::rolling;
    o.vx = vx;
    player.max_speed = vx;
    o.flags = u8((o.flags & ~sonic_flag::airborne) | sonic_flag::curled);
    o.terrain &= u8(~contact_bit::floor);
    sound_.request(sound::spring);
}

// 312B: falling off a ledge (not while jumping).
void Game::to_falling() {
    Object& o = sonic();
    if (o.state == state::jumping) {
        return;
    }
    o.next_state = state::falling;
    o.vy = 0x0100;
    o.flags = u8((o.flags | sonic_flag::airborne) & ~sonic_flag::curled);
    o.terrain &= u8(~contact_bit::floor);
    camera.mode |= camera_mode::hold_vertical;
}

// 314F: floating down (state 1D).
void Game::to_floating() {
    Object& o = sonic();
    o.next_state = state::floating;
    o.vy = 0x0080;
    o.flags = u8((o.flags | sonic_flag::airborne) & ~sonic_flag::curled);
    o.terrain &= u8(~contact_bit::floor);
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 316D: sliding (state 1A).
void Game::to_slide() {
    Object& o = sonic();
    o.next_state = state::slide;
    o.flags &= u8(~sonic_flag::airborne);
    o.terrain |= contact_bit::floor;
    player.max_speed = 0x0400;
    halve_speed_under_water();
    camera.offset_y = 0x80;
    camera.mode &= u8(~camera_mode::hold_vertical);
}

// 318D: Sonic faces the way left/right is held.
void Game::face_input() {
    const u8 held = input.held[0];
    const u8 r = u8((held << 2) | (held >> 6)) & 0x30;
    if (!r) {
        return;
    }
    Object& o = sonic();
    o.display = u8((o.display & ~display_flag::mirrored) | (r & 0x10));
}

// 491C: top speed is halved under water.
void Game::halve_speed_under_water() {
    if (!water.underwater) {
        return;
    }
    player.max_speed = player.max_speed >> 1;
}

// --- The common physics (3A73) ------------------------------------------

// 3A73: input forces, movement, terrain and contact with objects; then a
// jump if a jump button was pressed.
void Game::sonic_physics() {
    Object& o = sonic();
    if (flow.zone == 2) {
        update_air(); // 48DF
    }
    apply_input_force(); // 3BF5
    if (o.state < 5) {
        player.slope_force = 0;
    }
    move_horizontally(); // 3AF9
    move_vertically();   // 3B66
    collide_terrain();   // 6AB6
    touch_objects();     // 3773
    if (jump_pressed()) {
        start_jump(); // 3A9D
    }
}

// 3A9D: a jump, unless sliding, already in the air or riding.
void Game::start_jump() {
    Object& o = sonic();
    if (o.state == state::slide) {
        return;
    }
    if (o.flags & (sonic_flag::airborne | sonic_flag::riding)) {
        return;
    }
    player.jump_ticks = 0;
    o.flags |= sonic_flag::airborne | sonic_flag::curled;
    o.next_state = state::jumping;
    o.vy = water.underwater ? 0xFCC0 : 0xFBC0;
    --o.y;
    camera.offset_y = 0x80;
    o.terrain &= u8(~contact_bit::floor);
    sound_.request(sound::jump);
}

// 3AE2: rolling off a ledge into a jump without speed (6CAE).
void Game::roll_off() {
    Object& o = sonic();
    o.flags |= sonic_flag::airborne | sonic_flag::curled;
    o.next_state = state::jumping;
    o.vy = 0;
    o.terrain &= u8(~contact_bit::floor);
}

// 3BF5: the horizontal force from the controller (D36F) and the slope
// (D371). Sonic is kept between screen X 38 and C7. The force tables give
// four bytes per state: the force for left and for right; under water a
// second pair of tables applies. Without left/right, a braking table is used
// while he moves. The force doubles below speed 0x80.
void Game::apply_input_force() {
    Object& o = sonic();
    if (widescreen()) {
        // Widescreen: see wide_screen_limits.
        const auto [left, right] = wide_screen_limits();
        const int screen = int(std::int16_t(std::uint16_t(o.x - camera.x)));
        if (screen < left) {
            pin_to_screen(unsigned(left)); // 3CF1
            return;
        }
        if (screen > right) {
            pin_to_screen(unsigned(right)); // 3CFE
            return;
        }
    } else {
        const unsigned screen = (o.x - camera.x) & 0xFF;
        if (screen < 0x38) {
            pin_to_screen(0x38); // 3CF1
            return;
        }
        if (screen >= 0xC8) {
            pin_to_screen(0xC7); // 3CFE
            return;
        }
    }
    if (o.state >= 0x1E) {
        return;
    }
    const unsigned index = (o.state * 4u) & 0xFF;
    const u8 held = input.held[0];
    unsigned table;
    unsigned side;
    if (!(held & 0x0C)) {
        // 3CD0: no direction held.
        player.input_force = 0;
        if (!o.vx) {
            return;
        }
        table = 0x3E2E;
        side = (hi(o.vx) & 0x80) ? 0 : 2;
    } else {
        const bool wet = water.underwater;
        if (hi(o.vx) & 0x80) {
            table = wet ? 0x3F2E : 0x3DAE;
        } else {
            table = wet ? 0x3EAE : 0x3D2E;
        }
        side = (held & 0x04) ? 0 : 2;
    }
    unsigned force = rom.word(0, table + index + side);
    if (!(((abs16(o.vx) + 0x80) & 0xFFFF) >> 8)) {
        force = (force * 2) & 0xFFFF;
    }
    player.input_force = force;
    player.slope_force = rom.word(rom::slope_forces, player.slope);
}

// 3CF1 / 3CFE: Sonic at the screen edge: placed there without fraction,
// moved back by his velocity, and stopped.
void Game::pin_to_screen(unsigned screen_x) {
    Object& o = sonic();
    o.x = camera.x + screen_x;
    o.x_sub = 0;
    // 3D09: subtract the velocity from the 24-bit position.
    const unsigned low = unsigned(o.x_low16());
    const unsigned v = o.vx;
    const unsigned r = low - v;
    o.x_sub = u8(r);
    const unsigned borrow = (low < v) ? 1u : 0u;
    const unsigned extend = (v & 0x8000) ? 0xFFu : 0u;
    o.x = std::uint16_t(((u8(hi(o.x) - extend - borrow)) << 8) | u8(r >> 8));
    player.input_force = 0;
    o.vx = 0;
}

// 3AF9: horizontal velocity (plus forces), limited to the top speed D36D, and
// zero against a wall in that direction (contact bits 2/3). Sets the facing
// angle for objects that read it (40 right, C0 left).
void Game::move_horizontally() {
    Object& o = sonic();
    unsigned v = (o.vx + player.input_force + player.slope_force) & 0xFFFF;
    if (!(v & 0x8000)) {
        if (o.contact & contact_bit::right_wall) {
            o.vx = 0;
            player.input_force = 0;
            return;
        }
        o.angle = 0x40;
        if ((v >> 8) >= hi(player.max_speed)) {
            v = player.max_speed;
        }
    } else {
        if (o.contact & contact_bit::left_wall) {
            o.vx = 0;
            player.input_force = 0;
            return;
        }
        o.angle = 0xC0;
        if ((v >> 8) < u8(-hi(player.max_speed))) {
            v = neg16(player.max_speed);
        }
    }
    o.vx = v;
    add_to_position(o.x_sub, o.x, v);
}

// 3B66: vertical velocity with gravity, limited, and fixed while on the
// floor. In the air gravity is 0x30 per frame (0x18 in state 0B, 0x24 in
// 1B; half under water), up to 7 (4 under water) pixels per frame.
void Game::move_vertically() {
    Object& o = sonic();
    unsigned v = o.vy;
    if (!(o.flags & sonic_flag::airborne)) {
        if (player.platform_slot) {
            return;
        }
        // 3B71: on the ground the velocity's magnitude is used.
        v = abs16(v);
    } else {
        const bool wet = water.underwater;
        unsigned gravity = wet ? 0x18 : 0x30;
        if (o.state == state::sprung) {
            gravity = wet ? 0x0C : 0x18;
        } else if (o.state == state::launched) {
            gravity = wet ? 0x12 : 0x24;
        }
        v = (v + gravity) & 0xFFFF;
        if (!(v & 0x8000)) {
            const unsigned limit = wet ? 0x04 : 0x07;
            if ((v >> 8) >= limit) {
                v = limit << 8;
            }
        }
    }
    if (o.terrain & contact_bit::floor) {
        v = 0x0700;
    }
    o.vy = v;
    add_to_position(o.y_sub, o.y, v);
}

// The 24-bit add of a signed 8.8 velocity to a 16.8 position
// (ADD HL,DE then ADC with the sign extension).
void Game::add_to_position(u8& sub, std::uint16_t& whole, unsigned v) {
    const unsigned low = sub | (lo(whole) << 8);
    const unsigned sum = low + v;
    const unsigned carry = sum >> 16;
    const unsigned extend = (v & 0x8000) ? 0xFFu : 0u;
    sub = u8(sum);
    whole = std::uint16_t((u8(hi(whole) + extend + carry) << 8) | u8(sum >> 8));
}

// --- Touching objects (3773) --------------------------------------------

// 3773: what touching an object (slot D520) does: while invulnerable after a
// hit, Sonic only blinks; invincible, nothing; curled, he bounces off;
// otherwise he is hurt (D3A8 also requests a hit).
void Game::touch_objects() {
    Object& o = sonic();
    if (o.flags & sonic_flag::hurt) {
        blink_after_hit(); // 388C
        return;
    }
    if (o.power_up == 2) { // D532: invincible
        player.hit_request = 0;
        o.touch = 0;
        return;
    }
    if (player.hit_request) {
        player.hit_request = 0;
        o.contact &= u8(~contact_bit::ceiling);
        hurt_sonic(); // 37B5
        return;
    }
    if (!o.touch) {
        return;
    }
    const u8 type = objects[o.touch - 1].type; // 6385
    if (type == object_type::glider || type == object_type::mine_cart) {
        o.touch = 0; // 38B9: harmless to touch
        return;
    }
    if (o.flags & sonic_flag::curled) {
        bounce_off_object(); // 3858
        return;
    }
    hurt_sonic();
}

// 37B5: loses the rings (up to seven scatter, objects 08) or dies without
// any.
void Game::hurt_sonic() {
    Object& o = sonic();
    if (!score.rings) {
        die(); // 3828
        return;
    }
    unsigned count = ((score.rings >> 4) & 0x0F) + 1;
    if (count >= 8) {
        count = 7;
    }
    for (unsigned h = 0; h < count; ++h) {
        spawn_object(object_type::lost_ring, u8(h));
    }
    o.flags |= sonic_flag::hurt | sonic_flag::hurt_once;
    player.invulnerable_ticks = 0x78;
    score.rings = 0;
    show_rings();
    sound_.request(sound::rings_lost);
    o.touch = 0;
    o.next_state = state::hurt;
    o.flags = u8((o.flags | sonic_flag::airborne) & ~sonic_flag::riding);
    o.terrain &= u8(~contact_bit::floor);
    o.vy = (o.terrain & contact_bit::ceiling) ? 0x0000 : 0xFC00;
    o.vx = (o.contact & contact_bit::left_wall) ? 0x0100 : 0xFF00;
    player.input_force = 0;
}

// 3828: dying: thrown up, falling through everything (game flag 3).
void Game::die() {
    Object& o = sonic();
    o.next_state = state::dying;
    o.flags |= sonic_flag::airborne;
    o.display = 0;
    o.vy = 0xFB00;
    player.input_force = 0;
    o.terrain &= u8(~contact_bit::floor);
    flow.game_flags |= game_flag::life_lost;
    clock.time_over = 0;
    sound_.request(sound::life_lost);
    camera.mode |= camera_mode::hold_vertical;
}

// 3858: curled into an object: bounced up off its top (side bit 5), pushed
// down below it (bit 4, except rolling), or nothing.
void Game::bounce_off_object() {
    Object& o = sonic();
    if (o.touch_sides & 0x10) {
        if (o.state != state::rolling) {
            o.flags |= sonic_flag::airborne;
            o.vy = 0x0080;
        }
    } else if (o.touch_sides & 0x20) {
        o.flags |= sonic_flag::airborne;
        o.terrain &= u8(~contact_bit::floor);
        o.vy = 0xFD00;
    }
    o.touch = 0;
}

// 388C: blinking for D3A9 frames after a hit, then vulnerable again.
void Game::blink_after_hit() {
    Object& o = sonic();
    if (player.invulnerable_ticks) {
        --player.invulnerable_ticks;
        if (player.invulnerable_ticks & 2) {
            o.display |= display_flag::hidden;
        } else {
            o.display &= u8(~display_flag::hidden);
        }
        return;
    }
    o.display &= u8(~display_flag::hidden);
    o.flags &= u8(~(sonic_flag::hurt | sonic_flag::hurt_once));
    player.hit_request = 0;
    o.touch = 0;
}

// 38BE: standing at a ledge: if the floor heights 4 pixels left and right
// differ by 16 or more, he balances (state 19), facing the drop.
void Game::check_balance() {
    Object& o = sonic();
    probe_terrain(0xFFFC, 0x0004); // 733E
    const u8 left = probe.floor;
    probe_terrain(0x0004, 0x0004);
    const u8 right = probe.floor;
    const int d = int(right) - int(left);
    if ((d < 0 ? -d : d) < 0x10) {
        return;
    }
    if (right == left) {
        o.next_state = state::standing;
        return;
    }
    o.next_state = state::balancing;
    if (right > left) {
        o.display |= display_flag::mirrored;
    } else {
        o.display &= u8(~display_flag::mirrored);
    }
}

// --- State handlers -----------------------------------------------------

// 31A2 (state 01): standing. After 512 frames he starts waiting (state 02).
void Game::sonic_standing() {
    Object& o = sonic();
    camera.offset_y = 0x80;
    sonic_physics();
    if (o.next_state != state::standing) {
        return;
    }
    const u8 held = input.held[0];
    if (o.vx || (held & 0x0C)) {
        to_walking();
        return;
    }
    if (held & button::up) {
        to_look_up();
        return;
    }
    if (held & button::down) {
        to_crouch();
        return;
    }
    if (++player.idle_ticks >= 0x0200) {
        o.next_state = state::idle;
    }
}

// 3205 (state 02): waiting.
void Game::sonic_idle() {
    sonic_physics();
    if (sonic().next_state != state::idle) {
        return;
    }
    const u8 held = input.held[0];
    if (held & button::jump) {
        start_jump();
    } else if (held & 0x0C) {
        to_walking();
    } else if (held & button::up) {
        to_look_up();
    } else if (held & button::down) {
        to_crouch();
    }
}

// 31DB (state 19): balancing at a ledge.
void Game::sonic_balancing() {
    Object& o = sonic();
    camera.offset_y = 0x80;
    sonic_physics();
    if (o.next_state != state::balancing) {
        return;
    }
    const u8 held = input.held[0];
    if (o.vx || (held & 0x0C)) {
        to_walking();
    } else if (held & button::up) {
        to_look_up();
    } else if (held & button::down) {
        to_crouch();
    }
}

// 3228 (state 03): looking up (the camera aims lower on screen).
void Game::sonic_look_up() {
    camera.offset_y = 0xA8;
    sonic_physics();
    if (sonic().next_state != state::look_up) {
        return;
    }
    const u8 held = input.held[0];
    if (held & 0x0C) {
        to_walking();
    } else if (held & button::down) {
        to_crouch();
    } else if (!(held & button::up)) {
        to_standing();
    }
}

// 324A (state 04): crouching.
void Game::sonic_crouch() {
    camera.offset_y = 0x30;
    sonic_physics();
    if (sonic().next_state != state::crouch) {
        return;
    }
    const u8 held = input.held[0];
    if (held & button::up) {
        to_look_up();
    } else if (!(held & button::down)) {
        to_standing();
    }
}

// 3266 (state 05): walking.
void Game::sonic_walking() {
    Object& o = sonic();
    camera.offset_y = 0x80;
    sonic_physics();
    if (o.next_state != state::walking) {
        return;
    }
    face_input();
    if (zone_triggers()) { // 367C
        return;
    }
    if (u8(hi(o.vx) + 1) >= 2 && (input.held[0] & button::down)) {
        to_rolling();
        return;
    }
    if (!water.underwater && (abs16(o.vx) >> 8) == hi(player.max_speed)) {
        to_running();
        return;
    }
    check_stop(); // 32AB
    check_skid(); // 3304
}

// 32AB: no direction held and slower than 0x20: stand.
void Game::check_stop() {
    if (input.held[0] & 0x0C) {
        return;
    }
    if (!(abs16(sonic().vx) & 0xFFE0)) {
        to_standing();
    }
}

// 3304: holding against the motion at speed 1-254 makes him skid.
void Game::check_skid() {
    const Object& o = sonic();
    const u8 speed = hi(o.vx);
    if (speed == 0 || speed == 0xFF) {
        return;
    }
    const u8 held = input.held[0];
    if (!(speed & 0x80)) {
        if (held & button::left) {
            to_skid(true);
        }
    } else if (held & button::right) {
        to_skid(false);
    }
}

// 3320 (state 06): running.
void Game::sonic_running() {
    Object& o = sonic();
    sonic_physics();
    if (o.next_state != state::running) {
        return;
    }
    if (zone_triggers()) {
        return;
    }
    if (input.held[0] & button::down) {
        to_rolling();
        return;
    }
    u8 speed = hi(o.vx);
    if (speed & 0x80) {
        speed = u8(-speed);
    }
    if (speed < 4) {
        to_walking();
        return;
    }
    check_skid();
}

// 3345 / 3368 (states 07, 08): skidding until the held direction no longer
// opposes the motion.
void Game::sonic_skid(u8 skid_state) {
    Object& o = sonic();
    sonic_physics();
    if (o.next_state != skid_state) {
        return;
    }
    check_stop();
    face_input();
    const u8 held = input.held[0];
    const u8 right = u8((held >> 4) | (held << 4)) & 0x80; // bit 3 to bit 7
    if (((hi(o.vx) & 0x80) ^ right) != 0) {
        to_walking();
    }
}

// 338B (state 09): rolling.
void Game::sonic_rolling() {
    Object& o = sonic();
    sonic_physics();
    if (o.next_state != state::rolling) {
        return;
    }
    if (zone_triggers()) {
        return;
    }
    // 32C7
    const u8 held = input.held[0];
    if (!(hi(o.vx) & 0x80)) {
        if (held & button::left) {
            to_skid(true);
            return;
        }
    } else if (held & button::right) {
        to_skid(false);
        return;
    }
    if (player.slope) {
        return;
    }
    if (abs16(o.vx) & 0xFFF0) {
        return;
    }
    if (held & button::down) {
        to_crouch();
    } else {
        to_standing();
    }
}

// 33CD (state 0A): jumping. Holding the button keeps the jump speed for up
// to 14 frames; releasing it ends that for this jump.
void Game::sonic_jumping() {
    Object& o = sonic();
    if (input.held[0] & button::jump) {
        if (++player.jump_ticks < 0x0E) {
            o.vy = water.underwater ? 0xFCC0 : 0xFBC0;
        }
    } else {
        player.jump_ticks = 0x20;
    }
    sonic_physics();
    if (o.next_state != state::jumping) {
        return;
    }
    if (!(o.contact & contact_bit::floor)) {
        return;
    }
    if (player.floor_type != 0x8D) {
        to_walking();
    }
}

// 339A (state 1B): launched; lands into walking or standing.
void Game::sonic_launched() {
    Object& o = sonic();
    sonic_physics();
    if (o.next_state != state::launched) {
        return;
    }
    if (!(o.contact & contact_bit::floor)) {
        return;
    }
    o.flags &= u8(~sonic_flag::airborne);
    if (zone_triggers()) {
        return;
    }
    if (jump_pressed()) {
        start_jump();
        return;
    }
    if (!(abs16(o.vx) & 0xFFC0)) {
        to_standing();
    }
}

// 3407 (state 0B): after a spring.
void Game::sonic_sprung() {
    Object& o = sonic();
    sonic_physics();
    if (o.next_state != state::sprung) {
        return;
    }
    if (o.contact & contact_bit::floor) {
        to_walking();
        return;
    }
    if (!(hi(o.vy) & 0x80)) {
        to_falling();
        return;
    }
    camera.mode |= camera_mode::hold_vertical;
    face_input();
}

// 3426 (state 1C): bounced.
void Game::sonic_bounced() {
    Object& o = sonic();
    sonic_physics();
    if (o.next_state != state::bounced) {
        return;
    }
    if (o.contact & contact_bit::floor) {
        to_walking();
        return;
    }
    if (!(hi(o.vy) & 0x80)) {
        to_falling();
    }
}

// 343E (state 0E): falling; 3470 (state 1E): hurt. Both land into walking.
void Game::sonic_falling() {
    Object& o = sonic();
    sonic_physics();
    if (o.next_state != state::falling) {
        return;
    }
    if (o.contact & contact_bit::floor) {
        to_walking();
    }
}

void Game::sonic_hurt() {
    sonic_physics();
    if (sonic().contact & contact_bit::floor) {
        to_walking();
    }
}

// 344F (state 1D): floating down; the X velocity is forced to -1 (an
// earlier -0.75 is overwritten).
void Game::sonic_floating() {
    Object& o = sonic();
    o.display &= u8(~display_flag::hidden);
    o.vx = 0xFF40;
    o.vx = 0xFF00;
    sonic_physics();
    if (o.contact & contact_bit::floor) {
        to_walking();
    }
}

// 349A (state 1A): sliding: the block under him steers.
void Game::sonic_slide() {
    const u8 block = player.floor_block;
    u8 force = 0;
    if (block < 0x07) {
        force = button::left;
    } else if (block >= 0x10 && block < 0x17) {
        force = button::right;
    }
    if (force) {
        input.held[0] = force;
        input.pressed[0] = 0;
        face_input();
    }
    sonic_physics();
    if (!player.floor_type) {
        to_falling();
        return;
    }
    if (!player.slope) {
        to_walking();
    }
}

// 34CB (state 1F): dying: only falls.
void Game::sonic_dying() {
    sonic().display &= u8(~display_flag::hidden);
    camera.mode |= camera_mode::hold_vertical;
    move_vertically();
}

// 3675 (state 0D): straight into running.
void Game::sonic_pipe_exit() {
    sonic().next_state = state::running;
    sonic_physics();
}

// 34F8 (state 20): after the end sign Sonic runs off to the right,
// accelerating up to 6 pixels per frame; 288 pixels right of the camera the
// act (D293 bit 5) or, in a zone's last act, the zone (bit 4) is complete.
// Gimmick Mt. act 1 ends on the left instead. Widescreen margins (zero on the
// LCD) extend the run until he has left the presented view.
void Game::sonic_act_end_run() {
    Object& o = sonic();
    auto complete = [&] {
        o.vx = 0;
        flow.game_flags |= flow.act < 2 ? game_flag::act_complete : game_flag::zone_complete;
    };
    o.vy = 0;
    if (flow.zone == 4 && flow.act == 0) {
        // 355E
        o.display |= display_flag::mirrored;
        // Widescreen: until he has left the presented view.
        if (((camera.x + 0x10u) & 0xFFFF) >= unsigned(o.x) + unsigned(wide_left)) {
            complete();
            return;
        }
        unsigned vx = o.vx;
        if (!(vx & 0x8000)) {
            vx = 0xFFC0;
            o.vx = vx;
        }
        if ((vx >> 8) >= 0xFB) {
            o.vx = (vx + 0xFFF0) & 0xFFFF;
        }
        move_object(o);
        return;
    }
    o.display &= u8(~display_flag::mirrored);
    if (((unsigned(o.x) - camera.x) & 0xFFFF) >= 0x0120u + unsigned(wide_right)) {
        complete();
        return;
    }
    unsigned vx = o.vx;
    if (vx & 0x8000) {
        vx = 0;
        o.vx = 0;
    }
    if ((vx >> 8) < 0x06) {
        o.vx = vx + 0x0010;
    }
    move_object(o);
}

// 3598 (state 0C): carried along a tube. The 24-bit distance D399 grows by
// the speed; its upper word indexes two offset tables (bank 9, B4C5 for Y and
// B933 for X) added to the entry point. Before distance B0 the speed drops by
// 7 per frame (Sonic slides back out when it runs out), afterwards it grows
// by 12; at 230 Sonic leaves running at the maximum speed.
void Game::sonic_tube() {
    Object& o = sonic();
    o.display &= u8(~display_flag::hidden);
    camera.mode |= camera_mode::hold_vertical;
    unsigned distance =
        player.tube_position[0] | (player.tube_position[1] << 8) | (player.tube_position[2] << 16);
    distance = (distance + unsigned(o.vx)) & 0xFFFFFF;
    player.tube_position[0] = u8(distance);
    player.tube_position[1] = u8(distance >> 8);
    player.tube_position[2] = u8(distance >> 16);
    const unsigned position = (distance >> 8) & 0xFFFF;
    const unsigned entry = (position * 2) & 0xFFFF;
    o.y = (unsigned(o.origin_y) + rom.word(rom::tube_path_y, entry)) & 0xFFFF;
    o.x = (unsigned(o.origin_x) + rom.word(rom::tube_path_x, entry)) & 0xFFFF;
    if (position < 0x00B0) {
        if (unsigned(o.vx) < 0x0007) {
            // 363E: too slow; drops out, nudged towards the entry.
            if (unsigned(o.origin_x) >= unsigned(o.x)) {
                o.x = (unsigned(o.x) + 0x0008) & 0xFFFF;
            } else {
                o.x = (unsigned(o.x) + 0xFFFE) & 0xFFFF;
            }
            to_floating();
            return;
        }
        o.vx = unsigned(o.vx) - 0x0007;
    } else {
        o.vx = (unsigned(o.vx) + 0x000C) & 0xFFFF;
    }
    if (position < 0x0230) {
        return;
    }
    o.next_state = state::running;
    o.vx = unsigned(player.max_speed);
    sonic_physics();
    player.tube_delay = 0x10;
}

namespace {
// 39D5 / 3A18 (bank 0): pairs of frames; the second of a pair is used when
// the first is already shown, so each step alternates between the two.
void pick_frame(const Assets& rom, Object& o, unsigned table, unsigned step) {
    unsigned entry = table + (step & 0xFF) * 2;
    if (rom.byte(0, entry) == o.frame) {
        ++entry;
    }
    o.frame = rom.byte(0, entry);
    o.frame_ticks = 0x06;
}
} // namespace

// 39F9: the tube frames, one pair per 27 units of distance.
void Game::sonic_tube_frames() {
    const unsigned position = player.tube_position[1] | (player.tube_position[2] << 8);
    pick_frame(rom, sonic(), 0x3A18, position / 0x1B);
}

// 39AC: frames by the height above the entry point, one pair per 16 pixels.
void Game::sonic_rise_frames() {
    Object& o = sonic();
    const unsigned height = (unsigned(o.origin_y) - unsigned(o.y)) & 0xFF;
    pick_frame(rom, o, 0x39D5, height >> 4);
}

// 3764 (state 10): slow movement at most 1.5 pixels per frame.
void Game::sonic_pushed() {
    player.max_speed = 0x0180;
    player.input_force = 0x0008;
    move_horizontally();
}

// 3495 (state 25) and the end of 347B (state 24): air count reset.
void Game::sonic_breathe() {
    water.air_count = 0;
}

} // namespace sonic2_gg

namespace sonic2_gg {

// 38FF (vector 02F9): the walking frames 1-6; the faster, the shorter each
// frame shows (table 3924 by speed).
void Game::sonic_walk_animation() {
    Object& o = sonic();
    u8& step = o.step; // D52F
    if (++step >= 6) {
        step = 0;
    }
    o.frame = u8(step + 1);
    u8 speed = hi(o.vx);
    if (speed & 0x80) {
        speed = u8(-speed);
    }
    o.frame_ticks = rom.byte(rom::walk_frame_ticks, speed);
}

// 3934 (vector 02FC): the rolling frames (table 396D, 20 steps). On the
// ground Sonic faces his motion and the speed sets the frame time (3981); in
// the air he faces the held direction, 3 frames per step.
void Game::sonic_roll_animation() {
    Object& o = sonic();
    u8& step = o.step;
    if (++step >= 0x14) {
        step = 0;
    }
    o.frame = rom.byte(rom::roll_frames, step);
    if (o.terrain & 0x02) {
        o.display &= u8(~display_flag::mirrored);
        u8 speed = hi(o.vx);
        if (speed & 0x80) {
            speed = u8(-speed);
            o.display |= display_flag::mirrored;
        }
        o.frame_ticks = rom.byte(rom::roll_frame_ticks, speed);
        return;
    }
    const u8 held = input.held[0];
    if (held & button::right) {
        o.display &= u8(~display_flag::mirrored);
    } else if (held & button::left) {
        o.display |= display_flag::mirrored;
    }
    o.frame_ticks = 3;
}

} // namespace sonic2_gg
