// The player character: buttons, movement, collision with the act's
// ground, animation choice and drawing (08041FF8-080478F8).

#include "sonic_advance_gba/game/player.hpp"
#include "gba/bios.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/moon_zone.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <algorithm>

namespace sonic_advance_gba {

namespace {

constexpr u32 start_points = 0x084A6DEC;         // per act: x, y (pixels)
constexpr u32 state_animations = 0x084A6F0C;     // per state: animation, variant
constexpr u32 character_animations = 0x084A6F04; // per character: its first animation
constexpr u32 wrap_points = 0x084A6E74; // per act: x, y from which the act wraps (negative: never)
constexpr u32 wrap_shifts = 0x084A6EBC; // per act: how far (x in pixels, y in 256 pixels)

// States (+40) the code below names. The others only pick animations.
namespace state {
inline constexpr u8 standing = 0x00, crouch_wait = 0x02, crouch_end = 0x03, walking = 0x04,
                    jumping = 0x05;
inline constexpr u8 spin_dash = 0x06, spin_dash_release = 0x07, skidding = 0x09, look_up = 0x0A,
                    look_end = 0x0B;
inline constexpr u8 balance_front = 0x0C, balance_back = 0x0D, pushing = 0x0E, special = 0x0F;
inline constexpr u8 riding = 0x10, falling = 0x12, turning = 0x16, swinging = 0x17;
} // namespace state

[[noreturn]] void not_yet(const char* what) {
    throw Unrecovered(std::string("player: ") + what);
}

// The sine table's sine and cosine of an 8-bit angle, 8.8.
s32 sin8(const Assets& a, unsigned angle) {
    return sine(a, (angle & 0xFF) * 4) >> 6;
}

s32 cos8(const Assets& a, unsigned angle) {
    return sine(a, (angle & 0xFF) * 4 + 0x100) >> 6;
}

// A speed times a sine, as the code computes it: (product >> 8) in 16 bits.
s16 scaled(s32 speed, s32 factor) {
    return s16((speed * factor) >> 8);
}

// The quadrant (0 floor, 1 wall on the left of the body, 2 ceiling, 3 wall
// on the right) an angle falls in, rounded as the ground-following code
// does (0804386C).
unsigned follow_quadrant(s32 a) {
    const s32 v = (a > -0x20 && a <= 0) ? a + 0x20 : a + 0x1F;
    return (u32(v) & 0xFF) >> 6;
}

// The same for the probe ahead (08043B74), which rounds differently.
unsigned ahead_quadrant(u8 q) {
    u32 v = (q + 0x20u) & 0xFF;
    const bool low = v == 0 || v >= 0x80;
    if (low ? s8(q) < 1 : s8(q) > 0) {
        v = (q + 0x1Fu) & 0xFF;
    }
    return v >> 6;
}

// An angle seen with gravity reversed.
u8 mirrored(u8 a) {
    return u8(s8(-(s32(s8(a)) + 0x40)) - 0x40);
}

class Movement {
public:
    Movement(Game& g, Player& p) : g(g), a(g.assets()), s(g.stage()), p(p) {}

    // 08043AA4 for other parts.
    s32 room_towards(u8 q) { return room(q); }

    // 08047378: Sonic's movement for the frame, by situation.
    void sonic() {
        switch (p.flags & (player_flag::in_air | player_flag::rolling)) {
        case 0: // on the ground
            if (p.flags & player_flag::riding) {
                ride();
                return;
            }
            if (p.move == 0) {
                if (g.game_mode != 6 && sonic_ground_move() != 0) {
                    return;
                }
                if (spin_dash()) {
                    return;
                }
                if (jump()) {
                    return;
                }
                slope();
                walk();
            } else {
                slope();
                if (g.game_mode != 6) {
                    sonic_ground_move();
                }
            }
            break;
        case player_flag::rolling: // rolling on the ground
            reset_look();
            recentre_camera();
            if (jump()) {
                return;
            }
            roll_slope();
            roll();
            break;
        case player_flag::in_air:
            in_air(false);
            return;
        default: // in the air, rolled up
            in_air(true);
            return;
        }
        limits();
        move();
        follow_ground();
        slip();
    }

    // 08045CC8: the buttons for this frame.
    void read_buttons() {
        const u16 before = p.held;
        if (g.game_mode >= 2) {
            not_yet("multiplayer buttons");
        }
        if (!(p.flags & player_flag::frozen)) {
            p.held = g.controller().held;
            remember_buttons(p.held);
        } else {
            remember_buttons(0);
        }
        p.pressed = u16((before ^ p.held) & p.held);
        // With left or right held, up and down do not count.
        if (p.held & (button::left | button::right)) {
            p.held &= 0xFF3F;
        }
        if (p.pressed & (button::left | button::right)) {
            p.pressed &= 0xFF3F;
        }
    }

    // 08044A38: water and the speeds that depend on it. Entering or leaving
    // the water splashes (sound BEh; not Knuckles gliding, at most every 10
    // frames) and halves the speeds going in (doubling the fall coming
    // out). Under water the air lasts 30 seconds: from 12 left the drowning
    // music, at 5-0 the countdown numbers; then the player drowns.
    void water() {
        const u32 before = p.flags;
        p.flags &= ~player_flag::left_water;
        const bool may_splash = (p.character != 2 || p.special != 9) && p.splash_timer < 1;
        const auto splash = [&] {
            p.splash_timer = 10;
            create_water_splash(g, s16(p.x >> 8), s.water_level);
            g.sound().start(0xBE);
        };
        if (s.water_level < 0 || (p.y >> 8) - 4 < s.water_level) {
            if (p.flags & player_flag::underwater) {
                p.flags = (p.flags & ~player_flag::underwater) | player_flag::left_water;
                p.speed_y = s16(p.speed_y << 1);
                if (may_splash) {
                    splash();
                }
            }
            if (p.flags & player_flag::speed_shoes) {
                set_speeds(0x780, 0x24, 0xC0);
            } else {
                set_speeds(0x480, 8, 0x60);
            }
            p.air_ticks = 0x3C;
            p.air = 0x1E;
            if (p.index == 0) {
                g.sound().stop(0x1B); // the drowning music
            }
        } else {
            if (!(before & player_flag::underwater)) {
                p.flags = (before & ~player_flag::left_water) | player_flag::underwater |
                          player_flag::left_water;
                p.speed_x = s16(p.speed_x >> 1);
                p.speed_y = s16(p.speed_y >> 2);
                if (may_splash) {
                    splash();
                }
            }
            if (p.flags & player_flag::speed_shoes) {
                set_speeds(0x3C0, 0x12, 0x60);
            } else {
                set_speeds(0x240, 4, 0x30);
            }
            if (s8(--p.air_ticks) < 1) {
                const s8 air = s8(p.air--);
                switch (s8(air - 2)) {
                case 0:
                    create_air_countdown(g, p, 0);
                    break;
                case 2:
                    create_air_countdown(g, p, 1);
                    break;
                case 4:
                    create_air_countdown(g, p, 2);
                    break;
                case 6:
                    create_air_countdown(g, p, 3);
                    break;
                case 8:
                    create_air_countdown(g, p, 4);
                    break;
                case 9:
                    if (p.index == 0) {
                        s.music_drowning = 0x10;
                    }
                    break;
                case 10:
                    create_air_countdown(g, p, 5);
                    break;
                default:
                    break;
                }
                if (s8(p.air) < 0) {
                    // Drowned.
                    p.flags |= player_flag::dead;
                    p.speed_y = 0;
                    create_bubble(g, p.x, p.y - 0xC00, 0, true);
                    start_drowning_bubbles(g, p);
                }
                p.air_ticks = 0x3C;
            }
            if ((s.ticks & 0xF) == 0 && (s.next_random() & 0x300) == 0) {
                create_bubble(g, p.x + ((p.flags & player_flag::facing_left) ? -0x400 : 0x400),
                              p.y - 0x400, 0, false);
            }
        }
        if (p.power & player_power::fast) {
            p.top_speed <<= 1;
            p.acceleration <<= 1;
            p.deceleration <<= 1;
        } else if (p.power & player_power::slow) {
            p.top_speed >>= 2;
            p.acceleration >>= 2;
            p.deceleration >>= 2;
        }
        if (p.splash_timer != 0) {
            --p.splash_timer;
        }
    }

    // 08045FD4: the animation for the state.
    void choose_animation(TransformedSprite& t) {
        Sprite& sp = t.sprite;
        if (p.flags & player_flag::pushing) {
            p.move = 0;
            p.move_input = 0;
            p.state = state::pushing;
        }
        const u16 base = a.u16(character_animations + 2u * p.character);
        const u32 entry = state_animations + u32(s32(s8(p.state)) * 4);
        p.animation = a.u16(entry);
        if (s8(p.state) < 0x2B) {
            p.animation = u16(p.animation + base);
        }
        p.variant = a.u16(entry + 2);
        sp.speed = 0x10;
        if (p.ground_speed != 0 || (p.held & 0xF3)) {
            p.idle_timer = 0x168;
        }
        const s32 speed = p.ground_speed < 0 ? -p.ground_speed : p.ground_speed;
        auto speed_by_ground_speed = [&] {
            const s32 v = speed >> 4;
            sp.speed = u8(v < 8 ? 8 : v > 0x10 ? 0x10 : v);
        };
        switch (p.state) {
        case state::riding:
            if ((p.angle + 0x20u) & 0xC0) {
                p.animation = u16(base + 0x2C);
                p.variant = 1;
                p.flags &= ~player_flag::facing_left;
            }
            break;
        case state::standing:
            if (p.idle_timer == 0) {
                p.animation = u16(base + 1); // waiting
                p.variant = 0;
            } else {
                --p.idle_timer;
            }
            break;
        case state::skidding:
            if (speed < 0x180) {
                p.animation = u16(base + 7);
                p.variant = 0;
            }
            if ((s.ticks & 3) == 0) {
                const s32 r = upside_down() ? -p.radius_y : p.radius_y;
                create_skid_dust(g, s16(p.x >> 8), s16((p.y >> 8) + r));
            }
            break;
        case state::walking:
            if (speed >= 0x480) {
                p.animation = u16(base + 5); // running
                p.variant = 0;
                sp.speed = 0x10;
            } else {
                speed_by_ground_speed();
            }
            break;
        case 0x18:
        case 0x19:
        case 0x1A:
            speed_by_ground_speed();
            break;
        case 0x15:
        case 0x55:
            if (p.speed_y > -0x181) {
                p.state = state::falling;
            }
            break;
        default:
            break;
        }
        if (p.flags & player_flag::underwater) {
            sp.speed = u8(sp.speed - (sp.speed >> 1));
        }
        set_flips(sp);
        if (g.game_mode >= 2) {
            p.unknown_6c = 0;
        }
        if (sp.animation != p.animation || sp.variant != p.variant) {
            sp.animation = p.animation;
            sp.variant = u8(p.variant);
            sp.previous_variant = 0xFF;
            sp.hitboxes[0].index = 0xFFFFFFFF;
            sp.hitboxes[1].index = 0xFFFFFFFF;
            if (g.game_mode >= 2) {
                p.unknown_6c = 1;
            }
        }
        p.previous_state = p.state;
    }

    // 080462C8: animates and draws the body. An animation that ends changes
    // the state, which picks the next animation at once.
    void draw_body(TransformedSprite& t) {
        Sprite& sp = t.sprite;
        for (;;) {
            sp.x = t.x = s16((p.x >> 8) - s.camera.x);
            sp.y = t.y = s16((p.y >> 8) - s.camera.y);
            const u8 st = p.state;
            if (st == state::walking || st == state::swinging || st == 0x28) {
                // Turned with the ground.
                t.angle = u16(p.angle << 2);
                break;
            }
            if (st == 0x20) {
                break; // turned by its own code
            }
            t.angle = 0;
            sp.flags &= ~u32(sprite_flag::rotation_set | sprite_flag::rotated);
            if (!(p.flags & player_flag::facing_left)) {
                sp.flags |= sprite_flag::flip_x;
            } else {
                sp.flags &= ~sprite_flag::flip_x;
                ++sp.x;
            }
            if (upside_down()) {
                sp.flags |= sprite_flag::flip_y;
            } else {
                sp.flags &= ~sprite_flag::flip_y;
            }
            if (animate(g, sp) != 0) {
                draw_if_visible(sp);
                return;
            }
            animation_ended();
            if (p.state == st) {
                draw_if_visible(sp);
                return;
            }
            choose_animation(t);
        }
        // Rotated and scaled, with rotation set 0 (the player) or 1.
        sp.flags = (sp.flags &
                    ~u32(sprite_flag::flip_x | sprite_flag::flip_y | sprite_flag::rotation_set)) |
                   u32(s32(s8(p.index | 0x20)));
        t.scale_x = (p.flags & player_flag::facing_left) ? 0x100 : -0x100;
        if (upside_down()) {
            t.scale_x = s16(-t.scale_x);
        }
        if (t.scale_x < 0) {
            --t.x;
        }
        t.scale_y = 0x100;
        if (upside_down()) {
            t.angle = u16((-(t.angle + 0x100) - 0x100) & 0x3FF);
        }
        t.scale_x = s16((p.scale_x * t.scale_x) >> 8);
        t.scale_y = s16((t.scale_y * p.scale_y) >> 8);
        animate(g, sp);
        if (g.game_mode >= 2) {
            not_yet("multiplayer player drawing");
        }
        transform(g, t);
        draw_if_visible(sp);
    }

    // 08046CB8 (inlined in several places): the moves end.
    void end_moves() {
        p.flags &= ~u32(player_flag::pushing | player_flag::jumped | player_flag::spin_dash);
        p.special = 0;
        p.move = 0;
        p.move_input = 0;
        p.flags &= ~u32(0x8000);
        if (p.character == 1) {
            g.sound().stop(0x78);
        }
        if (p.character == 3) {
            p.flags &= ~u32(0x6000000);
        }
    }

    // 08042814: landing.
    void land() {
        if (!(p.flags & player_flag::no_jump_cap)) {
            if (!(p.flags & player_flag::keep_state)) {
                p.state = state::walking;
            }
            if (p.flags & player_flag::rolling) {
                p.flags &= ~player_flag::rolling;
                set_radius_y(0xE);
            }
            p.radius_x = 6;
            p.radius_y = 0xE;
        }
        end_moves();
        p.chain = 0;
        reset_look();
        p.flags &= ~u32(player_flag::jump_cancel | player_flag::in_air);
    }

    void radius(s8 rx, s8 ry) {
        set_radius_y(ry);
        p.radius_x = rx;
        p.radius_y = ry;
    }

private:
    Game& g;
    const Assets& a;
    StageState& s;
    Player& p;

    bool upside_down() const { return s.flags & stage_flag::upside_down; }

    s32 probe(s32 pos, s32 other, u8 layer, s32 step, u8* angle, Sensor which) const {
        return sonic_advance_gba::probe(a, s.map, pos, other, layer, step, angle, which);
    }

    void set_speeds(s32 top, s32 accel, s32 decel) {
        p.top_speed = top;
        p.acceleration = accel;
        p.deceleration = decel;
    }

    void set_flips(Sprite& sp) const {
        if (!(p.flags & player_flag::facing_left)) {
            sp.flags |= sprite_flag::flip_x;
        } else {
            sp.flags &= ~sprite_flag::flip_x;
        }
        if (upside_down()) {
            sp.flags |= sprite_flag::flip_y;
        } else {
            sp.flags &= ~sprite_flag::flip_y;
        }
    }

    // Drawn unless hidden or blinking after a hit (two frames of four).
    void draw_if_visible(Sprite& sp) {
        if (g.game_mode >= 2) {
            not_yet("multiplayer player drawing");
        }
        if ((p.flags & player_flag::dead) ||
            (!(p.flags & player_flag::hidden) && (p.hurt_timer == 0 || (s.ticks & 2) == 0))) {
            draw(g, sp);
        }
    }

    // 0804D108: the last 60 frames' buttons, newest first.
    void remember_buttons(u16 held) {
        auto& h = s.held_history;
        auto& n = s.pressed_history;
        for (std::size_t i = h.size() - 1; i > 0; --i) {
            h[i] = h[i - 1];
            n[i] = n[i - 1];
        }
        h[0] = held;
        n[0] = u16(held & ~h[1]);
    }

    // 0804D02C: a direction tapped now that was tapped before too, more
    // than four and less than `frames` frames ago, with no other direction
    // held since: -1 left, 1 right, 0 none.
    s32 double_tap(s32 frames) const {
        const auto& h = s.held_history;
        const auto& n = s.pressed_history;
        u16 tap, others;
        switch (n[0] & (button::left | button::right)) {
        case button::left:
            tap = button::left;
            others = button::right | button::up | button::down;
            break;
        case button::right:
            tap = button::right;
            others = button::left | button::up | button::down;
            break;
        default:
            return 0;
        }
        for (s32 i = 0; i < frames; ++i) {
            if (h[std::size_t(i)] & others) {
                break;
            }
            if (i > 3 && (n[std::size_t(i + 1)] & tap)) {
                return tap == button::left ? -1 : 1;
            }
        }
        return 0;
    }

    // 08045138: a new height keeps the feet where they are.
    void set_radius_y(s8 r) {
        if (p.radius_y == r) {
            return;
        }
        u32 q = p.angle;
        if (upside_down()) {
            q = mirrored(u8(q));
        }
        if (q != 0) {
            q = (q + 0x1F) & 0xFF;
        }
        const s32 d = (r - p.radius_y) * 0x100;
        switch (q >> 6) {
        case 0:
            p.y -= d;
            break;
        case 1:
            p.x += d;
            break;
        case 2:
            p.y += d;
            break;
        default:
            p.x -= d;
            break;
        }
    }

    // 08047238
    void reset_look() {
        if (p.index == 0) {
            p.look_timer = 0x78;
        }
    }

    // 0804724C: the camera's look offset returns to the middle.
    void recentre_camera() {
        if (p.index != 0) {
            return;
        }
        s16& c = s.camera.look_y;
        if (c >= 1) {
            c = s16(c - 2);
        } else if (c < 0) {
            c = s16(c + 2);
        }
    }

    // The nearer of the probes at the two corners (x - 2 - radius_x and
    // x + 2 + radius_x); `other` gets the farther.
    static s32 nearer(s32 left, u8 left_angle, s32 right, u8 right_angle, u8* angle, s32* other) {
        u8 dummy_angle;
        s32 dummy_other;
        u8& ang = angle ? *angle : dummy_angle;
        s32& o = other ? *other : dummy_other;
        if (left < right) {
            ang = left_angle;
            o = right;
            return left;
        }
        ang = right_angle;
        o = left;
        return right;
    }

    // 0804273C: the ceiling above both corners. Rising slower than 3
    // pixels a frame passes through tiles marked passable.
    s32 ceiling(u8* angle, s32* other) const {
        const u8 l = u8(p.layer | (p.speed_y < 0x300 ? 0x80 : 0));
        u8 a1 = 0, a2 = 0;
        const s32 top = (p.y >> 8) - p.radius_y;
        const s32 left = probe(top, ((p.x >> 8) - 2) - p.radius_x, l, -8, &a1, Sensor::floor);
        const s32 right = probe(top, (p.x >> 8) + 2 + p.radius_x, l, -8, &a2, Sensor::floor);
        return nearer(left, a1, right, a2, angle, other);
    }

    // 08042590 / 08042668: the wall left / right of the top and bottom
    // edges.
    s32 wall(s32 step, u8* angle, s32* other) const {
        const u8 l = u8(p.layer | (p.speed_y < 0x300 ? 0x80 : 0));
        u8 a1 = 0, a2 = 0;
        const s32 x = step < 0 ? ((p.x >> 8) - 2) - p.radius_x : (p.x >> 8) + 2 + p.radius_x;
        const s32 top = probe(x, (p.y >> 8) - p.radius_y, l, step, &a1, Sensor::wall);
        const s32 bottom = probe(x, (p.y >> 8) + p.radius_y, l, step, &a2, Sensor::wall);
        return nearer(top, a1, bottom, a2, angle, other);
    }

    // 08046EAC: the floor below both corners (passable tiles stay
    // passable while rising).
    s32 floor(u8* angle, s32* other) const {
        const u8 l = u8(p.layer | (p.speed_y < 0 ? 0x80 : 0));
        u8 a1 = 0, a2 = 0;
        const s32 bottom = (p.y >> 8) + p.radius_y;
        const s32 left = probe(bottom, ((p.x >> 8) - 2) - p.radius_x, l, 8, &a1, Sensor::floor);
        const s32 right = probe(bottom, (p.x >> 8) + 2 + p.radius_x, l, 8, &a2, Sensor::floor);
        return nearer(left, a1, right, a2, angle, other);
    }

    // 08046E00 / 08046DB4: floor / ceiling; a tile angle with bit 0 set
    // counts as flat, and reversed gravity mirrors the angle.
    u8 surface_angle(u8 angle) const {
        if (angle & 1) {
            return 0;
        }
        return upside_down() ? u8(0x80 - angle) : angle;
    }

    s32 floor_surface(u8& angle, s32* other) const {
        const s32 r = floor(&angle, other);
        angle = surface_angle(angle);
        return r;
    }

    s32 ceiling_surface(u8& angle, s32* other) const {
        const s32 r = ceiling(&angle, other);
        angle = surface_angle(angle);
        return r;
    }

    // The walls at the centre line on both sides, as the air collision
    // routines probe them.
    void walls_in_air() {
        const u8 l = u8(p.layer | (p.speed_y < 0x300 ? 0x80 : 0));
        s32 d = probe(((p.x >> 8) - 3) - p.radius_x, p.y >> 8, l, -8, nullptr, Sensor::wall);
        if (d < 1) {
            p.x -= d * 0x100;
            p.speed_x = 0;
        }
        d = probe((p.x >> 8) + 3 + p.radius_x, p.y >> 8, l, 8, nullptr, Sensor::wall);
        if (d < 1) {
            p.x += d * 0x100;
            p.speed_x = 0;
        }
    }

    // 080428CC: moving mostly down.
    void air_collide_down() {
        walls_in_air();
        u8 ang = 0;
        s32 other = 0;
        s32 d = upside_down() ? ceiling_surface(ang, nullptr) : floor_surface(ang, &other);
        if (d >= 0) {
            return;
        }
        // Lands only when one corner is near enough for the speed.
        const s32 limit = -((p.speed_y >> 8) + 6);
        if (d < limit && other < limit) {
            return;
        }
        p.angle = ang;
        if (upside_down()) {
            d = -d;
        }
        p.y += d * 0x100;
        land();
        if (((ang + 0x20u) & 0x40) == 0) {
            if (((ang + 0x10u) & 0x20) == 0) {
                // Nearly flat: the horizontal speed carries on.
                p.speed_y = 0;
                p.ground_speed = p.speed_x;
                return;
            }
            p.speed_y = s16(p.speed_y >> 1);
        } else {
            p.speed_x = 0;
            if (p.speed_y > 0xBD0) {
                p.speed_y = 0xBD0;
            }
        }
        // Sloped: the fall turns into speed along the slope.
        s16 v = p.speed_y < 0 ? s16(-p.speed_y) : p.speed_y;
        if (s8(ang) < 0) {
            v = s16(-v);
        }
        p.ground_speed = v;
    }

    // 08042A34: moving mostly up.
    void air_collide_up() {
        walls_in_air();
        u8 ang = 0;
        s32 dummy = 0;
        s32 d = upside_down() ? floor_surface(ang, nullptr) : ceiling_surface(ang, &dummy);
        if (d >= 1) {
            return;
        }
        if (upside_down()) {
            d = -d;
        }
        p.y -= d * 0x100;
        if (((ang + 0x20u) & 0x40) == 0 || s8(ang) - 0x40 < 1) {
            p.speed_y = 0;
            return;
        }
        // A steep ceiling: the player lands on it.
        p.angle = ang;
        land();
        p.ground_speed = p.speed_y < 0 ? s16(-p.speed_y) : p.speed_y;
    }

    // 08042B5C / 08042CB8: moving mostly left / right.
    void air_collide_sideways(bool right) {
        u8 l = p.layer;
        if (!upside_down() && p.speed_y < 0x300) {
            l |= 0x80;
        }
        const s32 x = right ? (p.x >> 8) + 3 + p.radius_x : ((p.x >> 8) - 3) - p.radius_x;
        s32 d = probe(x, p.y >> 8, l, right ? 8 : -8, nullptr, Sensor::wall);
        if (d < 1) {
            p.x += right ? d * 0x100 : -d * 0x100;
            p.speed_x = 0;
            p.ground_speed = p.speed_y;
        }
        u8 ang = 0;
        s32 o = 0;
        d = upside_down() ? floor_surface(ang, &o) : ceiling_surface(ang, &o);
        if (d < 1) {
            if (upside_down()) {
                d = -d;
            }
            p.y -= d * 0x100;
            if (p.speed_y < 0) {
                p.speed_y = 0;
            }
            return;
        }
        if (p.speed_y < 0) {
            return;
        }
        d = upside_down() ? ceiling_surface(ang, &o) : floor_surface(ang, &o);
        if (d < 1) {
            if (upside_down()) {
                d = -d;
            }
            p.y += d * 0x100;
            p.angle = ang;
            land();
            p.speed_y = 0;
            p.ground_speed = p.speed_x;
        }
    }

    // 08042E08: the air collision for the direction of movement.
    void air_collide() {
        u32 dir = upside_down() ? 0x80 : 0;
        if (p.speed_x != 0 || p.speed_y != 0) {
            dir = (u32(gba::arctan2(p.speed_x, p.speed_y)) & 0xFFFF) >> 8;
        }
        switch (((dir - 0x20) & 0xC0) >> 6) {
        case 0:
            air_collide_down();
            break;
        case 1:
            air_collide_sideways(false);
            break;
        case 2:
            air_collide_up();
            break;
        default:
            air_collide_sideways(true);
            break;
        }
    }

    // 080470B4: the position moves by the speed.
    void move() {
        p.x += p.speed_x;
        if ((s.flags ^ s.previous_flags) & stage_flag::upside_down) {
            p.speed_y = s16(-p.speed_y);
        }
        p.y += upside_down() ? -s32(p.speed_y) : s32(p.speed_y);
    }

    // 08047100
    void gravity() {
        p.speed_y = s16(p.speed_y + ((p.flags & player_flag::underwater) ? 0xC : 0x2A));
    }

    // 080471F0: in the air the angle returns to level, 2 a frame.
    void level_out() {
        if (p.state == state::swinging) {
            p.angle = u8((p.flags & player_flag::facing_left) ? p.angle - 4 : p.angle + 4);
            return;
        }
        s32 v = s8(p.angle);
        if (v < 0) {
            v += 2;
            if (v >= 1) {
                v = 0;
            }
        } else if (v >= 1) {
            v -= 2;
            if (v < 0) {
                v = 0;
            }
        }
        p.angle = u8(v);
    }

    // The air's routines in order (both air cases of 08047378).
    void in_air(bool rolled) {
        reset_look();
        recentre_camera();
        if (rolled && g.game_mode != 6) {
            sonic_air_dash();
        }
        jump_release();
        air_control();
        limits();
        move();
        gravity();
        level_out();
        air_collide();
    }

    // 0804463C: releasing jump caps the rise; jump pressed again in the
    // air starts the character's air move.
    void jump_release() {
        const s16 cap = (p.flags & player_flag::underwater) ? -0x180 : -0x300;
        if (!(p.flags & player_flag::jumped)) {
            if (!(p.flags & player_flag::no_jump_cap) && p.speed_y < -0xBD0) {
                p.speed_y = -0xBD0;
            }
            return;
        }
        if (p.speed_y < cap) {
            if (!(s.jump_buttons & p.held)) {
                p.speed_y = cap;
            }
            return;
        }
        if (g.game_mode != 6 && p.special == 0 && (s.jump_buttons & p.pressed) &&
            p.state != state::special) {
            p.flags &= ~player_flag::jump_cancel;
            p.special = 1;
            switch (p.character) {
            case 0:
                sonic_air_attack();
                break;
            case 1:
                not_yet("Tails' flight (08047FF0)");
            case 2:
                not_yet("Knuckles' glide (08048114)");
            default:
                break;
            }
        }
    }

    // 080478AC: Sonic's attack in the air, with its effect (080478F8).
    void sonic_air_attack() {
        p.state = 0x2B;
        g.sound().start(0x71);
        create_air_attack_effect(g, p);
    }

    // 080476E0: a direction tapped twice in a jump boosts Sonic forward.
    void sonic_air_dash() {
        if (p.special != 0 || !(p.flags & player_flag::jumped)) {
            return;
        }
        const s32 dir = double_tap(10);
        if (dir == 0) {
            return;
        }
        p.special = 2;
        p.state = 0x2C;
        g.sound().start(0x70);
        if (dir > 0) {
            p.speed_x = s16(p.speed_x + (p.top_speed >> 2));
        } else {
            p.speed_x = s16(p.speed_x - (p.top_speed >> 2));
        }
        p.speed_y = 0;
    }

    // 0804456C: steering in the air (twice the ground acceleration), and a
    // drag on the horizontal speed near the top of a jump.
    void air_control() {
        const s32 accel = p.acceleration, top = p.top_speed;
        if (p.state == state::special) {
            return;
        }
        if (!(p.flags & player_flag::jump_cancel)) {
            s16 v = p.speed_x;
            if (p.held & button::left) {
                if (p.state != state::swinging) {
                    p.flags |= player_flag::facing_left;
                }
                v = s16(v - accel * 2);
                if (v < -top) {
                    // Faster than the top speed already: no more.
                    v = s16(v + accel * 2);
                    if (v > -top) {
                        v = s16(-top);
                    }
                }
            } else if (p.held & button::right) {
                if (p.state != state::swinging) {
                    p.flags &= ~player_flag::facing_left;
                }
                v = s16(v + accel * 2);
                if (v > top) {
                    v = s16(v - accel * 2);
                    if (v < top) {
                        v = s16(top);
                    }
                }
            }
            p.speed_x = v;
        }
        if (u16(p.speed_y) < 0xBD00) {
            return;
        }
        const s32 sx = p.speed_x;
        const s32 drag = sx >> 5;
        if (drag < 0) {
            const s16 n = s16(sx - drag);
            p.speed_x = n < 1 ? n : 0;
        } else if (drag >= 1) {
            const s16 n = s16(sx - drag);
            p.speed_x = n >= 0 ? n : 0;
        }
    }

    // 08043E8C: the act's wrap points, the bottom (falling out kills) and
    // the camera's limits.
    void limits() {
        s32 x = p.x, y = p.y;
        auto& c = s.camera;
        if (p.index == 0) {
            const u32 act = u32(s32(s.zone)) * 4;
            const s16 wx = a.s16(wrap_points + act);
            if (wx >= 0 && wx * 0x100 <= x && c.max_step_x != 0 && !(c.locks & 1)) {
                const s16 d = a.s16(wrap_shifts + act);
                x += d * 0x100;
                if (s.players == 2) {
                    s.player[1].x += d * 0x100;
                }
                c.x = s16(c.x + d);
                c.offset_x = s16(c.offset_x + d);
            }
            const s16 wy = a.s16(wrap_points + act + 2);
            if (wy >= 0 && wy * 0x100 <= y && c.max_step_y != 0 && !(c.locks & 2)) {
                const s16 d = a.s16(wrap_shifts + act + 2);
                y += d * 0x10000;
                if (s.players == 2) {
                    s.player[1].y += d * 0x10000;
                }
                c.y = s16(c.y + d * 0x100);
                c.offset_y = s16(c.offset_y + d * 0x100);
            }
        }
        const u32 f = p.flags;
        if ((f & 0x80000080) == player_flag::dead) {
            return;
        }
        if (!(f & 0x80000000)) {
            const bool fell =
                upside_down() ? !(c.min_y * 0x100 < p.y) : !(p.y < c.max_y * 0x100 - 1);
            if (fell) {
                p.flags = f | player_flag::dead;
                p.speed_y = s16((f & player_flag::underwater) ? -0x2A0 : -0x4E0);
                y = upside_down() ? s32(c.min_y) << 8 : c.max_y * 0x100 - 1;
            }
        }
        s32 cx = c.min_x * 0x100;
        if (cx <= x) {
            cx = x;
            if (c.max_x * 0x100 - 1 < x) {
                cx = c.max_x * 0x100 - 1;
            }
        }
        s32 cy = c.min_y * 0x100;
        if (cy <= y) {
            cy = y;
            if (c.max_y * 0x100 - 1 < y) {
                cy = c.max_y * 0x100 - 1;
            }
        }
        if (cx != x) {
            p.speed_x = 0;
            p.ground_speed = 0;
        }
        if (cy != y) {
            p.speed_y = 0;
            p.ground_speed = 0;
        }
        p.x = cx;
        p.y = cy;
    }

    // 0804342C (floor), 08043654 (wall at x - radius), 08043544
    // (ceiling), 08043764 (wall at x + radius): keeping to the ground. Two
    // probes at the corners; the nearer surface within reach (up to 11
    // pixels, less when slow) is followed and its angle taken; ground
    // farther away means the player runs off into the air.
    void follow(unsigned quadrant) {
        const s32 x = p.x >> 8, y = p.y >> 8;
        const u8 l = p.layer;
        s32 d1, d2;
        switch (quadrant) {
        case 0:
            d1 = probe(y + p.radius_y, (x - 2) - p.radius_x, l, 8, &p.sensor_angle[0],
                       Sensor::floor);
            d2 = probe(y + p.radius_y, x + 2 + p.radius_x, l, 8, &p.sensor_angle[1], Sensor::floor);
            break;
        case 1:
            d1 = probe(x - p.radius_y, (y - 2) - p.radius_x, l, -8, &p.sensor_angle[0],
                       Sensor::wall);
            d2 = probe(x - p.radius_y, y + 2 + p.radius_x, l, -8, &p.sensor_angle[1], Sensor::wall);
            break;
        case 2:
            d1 =
                probe(y - p.radius_y, x + 2 + p.radius_x, l, -8, &p.sensor_angle[0], Sensor::floor);
            d2 = probe(y - p.radius_y, (x - 2) - p.radius_x, l, -8, &p.sensor_angle[1],
                       Sensor::floor);
            break;
        default:
            d1 = probe(x + p.radius_y, y + 2 + p.radius_x, l, 8, &p.sensor_angle[0], Sensor::wall);
            d2 =
                probe(x + p.radius_y, (y - 2) - p.radius_x, l, 8, &p.sensor_angle[1], Sensor::wall);
            break;
        }
        const bool vertical = quadrant == 0 || quadrant == 2;
        const s32 sign = (quadrant == 0 || quadrant == 3) ? 1 : -1;
        s32 pos = vertical ? p.y : p.x;
        const s32 d = d1 < d2 ? d1 : d2;
        u8 ang = d1 < d2 ? p.sensor_angle[0] : p.sensor_angle[1];
        if (d != 0) {
            if (d < 0) {
                if (d < -0xB) {
                    return; // deep inside: left alone
                }
            } else {
                s32 v = vertical ? p.speed_x : p.speed_y;
                if (v < 0) {
                    v = -v;
                }
                v = (v >> 8) + 3;
                if (v > 0xB) {
                    v = 0xB;
                }
                if (v < d) {
                    if (quadrant != 0 || !(p.flags & player_flag::control_locked)) {
                        p.flags = (p.flags & ~player_flag::pushing) | player_flag::in_air;
                        return;
                    }
                    // Held to the floor: snapped down, the angle kept.
                    ang = p.angle;
                }
            }
            pos += sign * d * 0x100;
        }
        (vertical ? p.y : p.x) = pos;
        if ((ang & 1) == 0) {
            p.angle = ang;
            if (upside_down()) {
                p.angle = u8(-0x40 - (s8(p.angle) + 0x40));
            }
        }
    }

    // 0804386C: following the ground for the current angle.
    void follow_ground() {
        if (p.flags & player_flag::on_object) {
            p.sensor_angle[1] = 0;
            p.sensor_angle[0] = 0;
            return;
        }
        const s32 a8 = s8(upside_down() ? mirrored(p.angle) : p.angle);
        follow(follow_quadrant(a8));
    }

    // 080471A0: too slow on a steep slope: the player slips off and has no
    // control for half a second.
    void slip() {
        if (p.flags & player_flag::control_locked) {
            return;
        }
        if (p.slip_timer != 0) {
            --p.slip_timer;
            return;
        }
        if (((p.angle + 0x20u) & 0xC0) == 0) {
            return;
        }
        const s32 v = p.ground_speed < 0 ? -p.ground_speed : p.ground_speed;
        if (v >= 0x1E0) {
            return;
        }
        p.ground_speed = 0;
        p.flags |= player_flag::in_air;
        p.slip_timer = 0x1E;
    }

    // 08046F78: slopes speed up and slow down a moving player.
    void slope() {
        if (((p.angle + 0x60u) & 0xFF) < 0xC0 && p.ground_speed != 0) {
            p.ground_speed = s16(p.ground_speed + ((sin8(a, p.angle) * 3) >> 5));
        }
    }

    // 08046FE4: slopes while rolling: full effect downhill, a quarter
    // uphill.
    void roll_slope() {
        const s16 v = p.ground_speed;
        if (((p.angle + 0x60u) & 0xFF) >= 0xC0 || v == 0) {
            return;
        }
        const s32 k = sin8(a, p.angle) * 0x3C;
        s32 d = k >> 8;
        if (v < 1 ? d >= 0 : d <= 0) {
            d = k >> 10;
        }
        p.ground_speed = s16(v + d);
    }

    // 08043DA8: rolling on the ground: the direction buttons only brake
    // (or turn the player round when stopped), friction is half the
    // acceleration; stopped, the player stands up, unless forced to roll
    // (flag 200), which pushes on at 300h.
    void roll() {
        if (p.slip_timer == 0) {
            switch (p.held & (button::left | button::right)) {
            case button::right:
                if (p.ground_speed < 0) {
                    s32 v = p.ground_speed + 0x18;
                    if (v > 0) {
                        v = 0x60;
                    }
                    p.ground_speed = s16(v);
                } else {
                    p.flags &= ~player_flag::facing_left;
                }
                break;
            case button::left:
                if (p.ground_speed > 0) {
                    s32 v = p.ground_speed - 0x18;
                    if (v < 0) {
                        v = -0x60;
                    }
                    p.ground_speed = s16(v);
                } else {
                    p.flags |= player_flag::facing_left;
                }
                break;
            default:
                break;
            }
        }
        const s32 friction = p.acceleration >> 1;
        s32 v = p.ground_speed;
        if (v < 0) {
            v = std::min(v + friction, 0);
            p.ground_speed = s16(v);
        } else if (v >= 1) {
            v = std::max(v - friction, 0);
            p.ground_speed = s16(v);
        }
        if (v == 0) {
            if (!(p.flags & player_flag::no_jump_cap)) {
                p.flags &= ~player_flag::rolling;
                p.state = (p.held & button::down) ? state::crouch_wait : state::walking;
                radius(6, 0xE);
            } else {
                p.ground_speed = s16((p.flags & player_flag::facing_left) ? -0x300 : 0x300);
                g.sound().start(0x6D);
            }
        }
        p.speed_x = scaled(p.ground_speed, cos8(a, p.angle));
        p.speed_y = scaled(p.ground_speed, sin8(a, p.angle));
        wall_ahead();
    }

    // 0804393C: left on the ground.
    void press_left() {
        s32 v = p.ground_speed;
        if (v < 1) {
            if (!(p.flags & player_flag::facing_left)) {
                p.flags &= ~player_flag::pushing;
                p.state = state::turning;
                radius(6, 0xE);
            }
            p.flags |= player_flag::facing_left;
            v -= p.acceleration;
            if (v < -p.top_speed) {
                v += p.acceleration;
                if (v > -p.top_speed) {
                    v = -p.top_speed;
                }
            }
            p.ground_speed = s16(v);
            if ((p.flags & 0x808000) || p.state == state::turning) {
                return;
            }
            p.state = state::walking;
        } else {
            // Braking.
            v -= p.deceleration;
            if (v < 0) {
                v = -0x60;
            }
            p.ground_speed = s16(v);
            if (v < 0x300) {
                return;
            }
            g.sound().start(0x73);
            p.state = state::skidding;
        }
        radius(6, 0xE);
    }

    // 080439F8: right on the ground.
    void press_right() {
        s32 v = p.ground_speed;
        if (v < 0) {
            v += p.deceleration;
            if (v > 0) {
                v = 0x60;
            }
            p.ground_speed = s16(v);
            if (v > -0x300) {
                return;
            }
            g.sound().start(0x73);
            p.state = state::skidding;
        } else {
            if (p.flags & player_flag::facing_left) {
                p.flags &= ~player_flag::pushing;
                p.state = state::turning;
                radius(6, 0xE);
            }
            p.flags &= ~player_flag::facing_left;
            v += p.acceleration;
            if (v > p.top_speed) {
                v -= p.acceleration;
                if (v < p.top_speed) {
                    v = p.top_speed;
                }
            }
            p.ground_speed = s16(v);
            if ((p.flags & 0x808000) || p.state == state::turning) {
                return;
            }
            p.state = state::walking;
        }
        radius(6, 0xE);
    }

    // 08044E14: standing still at a ledge: balancing, facing the edge or
    // away from it.
    void balance() {
        const s32 x = p.x >> 8, y = p.y >> 8;
        const u8 l = p.layer;
        if (probe(y + p.radius_y, x, l, 8, nullptr, Sensor::floor) < 9) {
            return; // ground under the middle
        }
        s32 left, right;
        if (!upside_down()) {
            left = probe(y + p.radius_y, (x - 2) - p.radius_x, l, 8, nullptr, Sensor::floor);
            right = probe(y + p.radius_y, x + 2 + p.radius_x, l, 8, nullptr, Sensor::floor);
        } else {
            left = probe(y - p.radius_y, (x - 2) - p.radius_x, l, -8, nullptr, Sensor::floor);
            right = probe(y - p.radius_y, x + 2 + p.radius_x, l, -8, nullptr, Sensor::floor);
        }
        bool edge_on_left;
        if (left >= 9 && right == 0) {
            edge_on_left = true;
        } else if (left == 0 && right >= 9) {
            edge_on_left = false;
        } else {
            return;
        }
        const bool facing_left = p.flags & player_flag::facing_left;
        p.state = facing_left == edge_on_left ? state::balance_front : state::balance_back;
    }

    // 08044D40: balancing at the edge of the object stood on (its first
    // hitbox's sides); between them a balance just begun goes on.
    void balance_on_object() {
        const Sprite& o = *p.standing_on;
        const s32 ox = s16(u16(o.x) + u16(s.camera.x));
        const s32 left = o.hitboxes[0].left, right = o.hitboxes[0].right;
        const bool facing_left = p.flags & player_flag::facing_left;
        if (p.x < (ox + left - p.radius_x + 1) * 0x100 && left != 0) {
            p.state = facing_left ? state::balance_front : state::balance_back;
            return;
        }
        if (p.x > (ox + right + p.radius_x - 2) * 0x100 && right != 0) {
            p.state = facing_left ? state::balance_back : state::balance_front;
            return;
        }
        if ((ox + left - p.radius_x + 2) * 0x100 <= p.x &&
            p.x <= (ox + right + p.radius_x - 3) * 0x100) {
            return;
        }
        if (u8(p.previous_state - state::balance_front) < 2) {
            p.state = p.previous_state;
        }
    }

    // 0804489C: up and down on the ground (looking up, crouching) and the
    // camera's look offset.
    void up_down() {
        if (u8(p.state - state::balance_front) < 2) {
            if (p.index != 0) {
                return;
            }
            p.look_timer = 0x78;
            recentre_camera();
            return;
        }
        switch (p.held & (button::up | button::down)) {
        case button::up:
            if (p.ground_speed == 0) {
                p.state = state::look_up;
                look(true);
                return;
            }
            reset_look();
            recentre_camera();
            return;
        case button::down:
            crouch();
            return;
        case 0:
            reset_look();
            recentre_camera();
            if (p.state == state::crouch_wait) {
                p.state = state::crouch_end;
            } else if (p.state == state::look_up) {
                p.state = state::look_end;
            }
            return;
        default:
            return;
        }
    }

    // 0804471C (up: the view rises to 47h) and the standing part of
    // 080447A4 (down: it sinks to 3Bh): after two seconds held the camera's
    // look offset moves (mirrored with reversed gravity).
    void look(bool up) {
        if (p.look_timer != 0 && --p.look_timer != 0) {
            recentre_camera();
            return;
        }
        if (p.index != 0) {
            return;
        }
        s16& c = s.camera.look_y;
        const s32 seen = c + s.camera.shift_y;
        const s32 limit = up ? 0x47 : 0x3B;
        if (up != upside_down()) {
            if (seen >= -limit) {
                c = s16(c - 2);
            }
        } else if (seen <= limit) {
            c = s16(c + 2);
        }
    }

    // 080447A4: down: crouching while standing, rolling while moving.
    void crouch() {
        p.state = state::crouch_wait;
        if (p.ground_speed == 0) {
            look(false);
            return;
        }
        if (p.character == 3) {
            return;
        }
        g.sound().start(0x6D);
        p.flags |= player_flag::rolling;
        radius(6, 9);
        p.state = state::jumping;
        reset_look();
        recentre_camera();
    }

    // 08044F48: walking: the buttons, standing still, balancing, friction,
    // and the speed along the ground.
    void walk() {
        if (p.slip_timer == 0 && p.state != 0x40 && p.state != 0x1B) {
            const u8 st = p.state;
            switch (p.held & (button::left | button::right)) {
            case button::right:
                press_right();
                break;
            case button::left:
                press_left();
                break;
            case button::left | button::right:
                break;
            default:
                if (p.ground_speed == 0) {
                    if (!(p.flags & player_flag::keep_state) &&
                        (p.character != 3 || !(p.flags & player_flag::no_jump_cap))) {
                        if (st == state::crouch_wait) {
                            p.state = state::crouch_end;
                        } else if (st == state::look_up) {
                            p.state = state::look_end;
                        } else if (st != state::crouch_end && st != state::look_end) {
                            p.state = state::standing;
                        }
                        radius(6, 0xE);
                    }
                } else if (u8(p.state - 8) < 2 || st == state::standing) {
                    p.state = state::walking;
                    radius(6, 0xE);
                }
                break;
            }
            if (p.character == 3) {
                not_yet("Amy's walking (08044F48)");
            }
            if (((p.angle + 0x20u) & 0xC0) == 0 && p.ground_speed == 0) {
                const u32 before = p.flags;
                p.flags &= ~player_flag::pushing;
                if (!(before & player_flag::keep_state)) {
                    if (p.state != state::crouch_end && p.state != state::look_end) {
                        p.state = state::standing;
                    }
                    if (!(p.flags & player_flag::on_object)) {
                        balance();
                    } else {
                        balance_on_object();
                    }
                }
            }
        }
        up_down();
        if ((p.held & (button::left | button::right)) == 0) {
            // Friction.
            s32 v = p.ground_speed;
            if (v >= 1) {
                v -= 8;
                if (v < 0) {
                    v = 0;
                }
            } else if (v < 0) {
                v += 8;
                if (v > 0) {
                    v = 0;
                }
            }
            p.ground_speed = s16(v);
        }
        p.speed_x = scaled(p.ground_speed, cos8(a, p.angle));
        p.speed_y = scaled(p.ground_speed, sin8(a, p.angle));
        wall_ahead();
    }

    // 08043B74: the distance to a surface in direction q (an angle).
    s32 probe_ahead(u8 q) {
        const s32 x = p.x >> 8, y = p.y >> 8;
        p.sensor_angle[1] = q;
        p.sensor_angle[0] = q;
        switch (ahead_quadrant(q)) {
        case 0:
            return probe(y + 2 + p.radius_x, x, p.layer, 8, nullptr, Sensor::floor);
        case 1:
            return probe((x - 2) - p.radius_x, y, p.layer, -8, nullptr, Sensor::wall);
        case 2:
            return probe((y - 2) - p.radius_x, x, p.layer, -8, nullptr, Sensor::floor);
        default:
            return probe(x + 2 + p.radius_x, y, p.layer, 8, nullptr, Sensor::wall);
        }
    }

    // 08043C64: running into a wall stops the player (pushing).
    void wall_ahead() {
        if (p.ground_speed == 0) {
            return;
        }
        const u8 q = u8(p.angle + (p.ground_speed >= 0 ? 0xC0 : 0x40));
        const s32 d = probe_ahead(q);
        if (d * 0x100 > 0) {
            return;
        }
        switch (((q + 0x20u) & 0xC0) >> 6) {
        case 0:
            p.y += d * 0x100;
            p.speed_y = 0;
            break;
        case 2:
            p.y -= d * 0x100;
            p.speed_y = 0;
            p.flags |= player_flag::in_air;
            break;
        default:
            p.x += ((q + 0x20u) & 0xC0) == 0x40 ? -d * 0x100 : d * 0x100;
            p.speed_x = 0;
            p.flags = (p.flags & ~player_flag::rolling) | player_flag::pushing;
            radius(6, 0xE);
            p.ground_speed = 0;
            break;
        }
        p.move = 0;
        p.move_input = 0;
    }

    // 08043AA4: the room in direction q (an angle): how far the surface a
    // jump would meet is.
    s32 room(u8 q) {
        p.sensor_angle[1] = q;
        p.sensor_angle[0] = q;
        u8 ang = 0;
        s32 other = 0;
        switch (((q + 0x20u) & 0xC0) >> 6) {
        case 0:
            return floor(&ang, &other);
        case 1:
            return wall(-8, &ang, &other);
        case 2:
            return ceiling(&ang, &other);
        default:
            return wall(8, &ang, &other);
        }
    }

    // 0804421C: jumping, away from the ground at whatever angle.
    bool jump() {
        if (!(s.jump_buttons & p.pressed)) {
            return false;
        }
        const u8 ang = upside_down() ? mirrored(p.angle) : p.angle;
        if (room(u8(ang - 0x80)) <= 3) {
            return false; // a ceiling too close
        }
        p.flags = (p.flags & 0xFEFFF7DF) | player_flag::jumped | player_flag::in_air;
        if (p.character >= 3) {
            not_yet("Amy's jump (0804421C)");
        }
        p.state = state::jumping;
        const s32 j = (p.flags & player_flag::underwater) ? 0x2A0 : 0x4E0;
        const u8 out = u8(p.angle - 0x40);
        p.speed_x = s16(p.speed_x + scaled(j, cos8(a, out)));
        p.speed_y = s16(p.speed_y + scaled(j, sin8(a, out)));
        g.sound().start(0x74);
        if (!(p.flags & player_flag::rolling)) {
            p.flags |= player_flag::rolling;
            radius(6, 9);
        } else {
            p.flags |= player_flag::jump_cancel;
        }
        return true;
    }

    // 08044070: the spin dash (jump while crouching).
    bool spin_dash() {
        if (!(p.flags & player_flag::spin_dash)) {
            if (p.state != state::crouch_wait || !(s.jump_buttons & p.pressed)) {
                return false;
            }
        }
        if (!(p.flags & player_flag::spin_dash)) {
            p.state = state::spin_dash;
            g.sound().start(0x6D);
            start_spin_dash_dust(g);
            p.flags |= player_flag::spin_dash;
            p.spin_charge = 0;
            radius(6, 9);
        } else {
            if (!(p.held & button::down)) {
                // Released: off at the speed the charge reached.
                p.flags &= ~player_flag::spin_dash;
                const u32 speeds = (p.flags & player_flag::speed_shoes) ? 0x084A7096 : 0x084A7084;
                s16 v = a.s16(speeds + 2u * u32(s32(p.spin_charge) >> 8));
                if (p.index == 0) {
                    s.camera.hold = 10;
                }
                if (p.flags & player_flag::facing_left) {
                    v = s16(-v);
                }
                p.ground_speed = v;
                p.flags |= player_flag::rolling;
                g.sound().start(0x6E);
                p.state = state::jumping;
            } else {
                // Charging: the charge runs down by 1/32 a frame; each jump
                // press adds 200h (and makes it at least 800h) and raises
                // the sound's pitch.
                s16 c = p.spin_charge;
                if (c != 0) {
                    c = s16(c - (c >> 5));
                    if (c <= 0) {
                        c = 0;
                    }
                }
                if (p.state != state::spin_dash_release) {
                    p.state = state::spin_dash;
                }
                if (s.jump_buttons & p.pressed) {
                    g.sound().start(0x6D);
                    g.sound().restart_tracks(3);
                    g.sound().set_pitch(3, 0xFFFF, s16(c & 0xFF80));
                    if (g.game_mode != 6 && (p.character == 0 || p.character == 2)) {
                        p.state = state::spin_dash_release;
                    }
                    c = s16(c + 0x200);
                    if (c < 0x800) {
                        c = 0x800;
                    }
                }
                p.spin_charge = c;
            }
            reset_look();
            recentre_camera();
        }
        limits();
        follow_ground();
        return true;
    }

    // 08047634: Sonic's ground move with the other button, by stage;
    // returns the stage.
    s8 sonic_ground_move() {
        switch (p.move) {
        case 0: {
            // 080474D4: starting it.
            const u32 f = p.flags;
            if (!(f & player_flag::spin_dash) && s32(u32(p.angle + 0x40) << 24) > 0 &&
                !(f & player_flag::pushing) && (s.other_buttons & p.pressed)) {
                p.ground_speed = (f & player_flag::facing_left) ? -0x200 : 0x200;
                p.speed_x = 0;
                ++p.move;
                p.state = 0x2D;
                g.sound().start(0x6D);
            }
            return s8(p.move);
        }
        case 1:
            // 08047538: the first kick slows down; the other button again
            // kicks on (at 300h).
            brake_move(8);
            if (p.ground_speed == 0) {
                p.state = state::standing;
                p.move = 0;
            }
            if (s.other_buttons & p.pressed) {
                p.ground_speed = (p.flags & player_flag::facing_left) ? -0x300 : 0x300;
                ++p.move;
                p.move_input = 0;
                p.state = 0x2E;
                g.sound().start(0x79);
            }
            move_wall();
            return s8(p.move);
        case 2:
            // 080475C8: the second kick; a button pressed now picks the
            // follow-up when its animation ends (1 the other button, 2
            // jump).
            brake_move(8);
            if (p.move_input == 0) {
                if (s.other_buttons & p.pressed) {
                    p.move_input = 1;
                }
                if (p.move_input == 0 && (s.jump_buttons & p.pressed)) {
                    p.move_input = 2;
                }
            }
            move_wall();
            return s8(p.move);
        case 3:
            // The third kick slides to a stop.
            brake_move(0x20);
            if (p.ground_speed == 0) {
                p.state = 0x30;
                ++p.move;
            }
            move_wall();
            return s8(p.move);
        case 4:
            return s8(p.move);
        default:
            return 0;
        }
    }

    // The ground speed slows by `step` towards 0, then the speeds follow
    // it (08047078).
    void brake_move(s32 step) {
        s32 v = p.ground_speed;
        if (v >= 1) {
            p.ground_speed = s16(std::max(v - step, 0));
        } else if (v < 0) {
            p.ground_speed = s16(std::min(v + step, 0));
        }
        p.speed_x = scaled(p.ground_speed, cos8(a, p.angle));
        p.speed_y = scaled(p.ground_speed, sin8(a, p.angle));
    }

    // 08043D0C: a ground move runs into a surface ahead: against a wall it
    // stops (standing up from a roll), against the floor or a ceiling the
    // player is moved out.
    void move_wall() {
        if (p.ground_speed == 0) {
            return;
        }
        const u8 q = u8(p.angle + (p.ground_speed >= 0 ? 0xC0 : 0x40));
        const s32 d = probe_ahead(q);
        if (d * 0x100 > 0) {
            return;
        }
        switch (((q + 0x20u) & 0xC0) >> 6) {
        case 0:
            p.y += d * 0x100;
            p.speed_y = 0;
            return;
        case 2:
            p.y -= d * 0x100;
            p.speed_y = 0;
            p.flags |= player_flag::in_air;
            return;
        default:
            p.x += ((q + 0x20u) & 0xC0) == 0x40 ? -d * 0x100 : d * 0x100;
            p.speed_x = 0;
            p.flags &= ~player_flag::rolling;
            radius(6, 0xE);
            p.ground_speed = 0;
            return;
        }
    }

    // 08047284: carried along a path: only jumping, the ride's speed and
    // the ground.
    void ride() {
        if (ride_jump()) {
            return;
        }
        ride_control();
        limits();
        move();
        follow_ground();
    }

    // 08044400: jumping off a ride, onto the other collision layer.
    bool ride_jump() {
        if (!(s.jump_buttons & p.pressed)) {
            return false;
        }
        const u8 ang = upside_down() ? mirrored(p.angle) : p.angle;
        p.layer ^= 1;
        if (room(u8(ang - 0x80)) < 4) {
            p.layer ^= 1;
            return false;
        }
        g.sound().stop(s.player[0].character == 3 ? 0x86 : 0x72);
        p.flags = (p.flags & 0xFEFFF7DF) | player_flag::jumped | player_flag::in_air;
        s.player[0].power &= 0x7F;
        if (p.character < 3) {
            p.state = state::jumping;
        } else if (p.character == 3) {
            p.state = 0x55;
        }
        const s32 j = (p.flags & player_flag::underwater) ? 0x2A0 : 0x4E0;
        p.speed_y = scaled(j, sin8(a, u8(p.angle - 0x40)));
        g.sound().start(0x74);
        p.body->sprite.flags = (p.body->sprite.flags & ~sprite_flag::priority) | 0x2000;
        p.extra->sprite.flags = (p.extra->sprite.flags & ~sprite_flag::priority) | 0x2000;
        if (p.character != 3) {
            if (!(p.flags & player_flag::rolling)) {
                p.flags |= player_flag::rolling;
                radius(6, 9);
            } else {
                p.flags |= player_flag::jump_cancel;
            }
        }
        return true;
    }

    // 080449A4: a ride's speed: downhill with right or down held speeds it
    // up, left slows it, and it never drops below 0xC0.
    void ride_control() {
        if ((p.held & (button::right | button::down)) && ((p.angle + 0x60u) & 0xFF) < 0xC0) {
            p.ground_speed = s16(p.ground_speed + ((sin8(a, p.angle) * 5) >> 5));
        }
        s32 v = p.ground_speed;
        if (p.held & button::left) {
            v -= 8;
            if (v < 0xC0) {
                v = 0xC0;
            }
        }
        v -= 8;
        if (v < 0xC0) {
            v = 0xC0;
        }
        p.ground_speed = s16(v);
        p.state = state::riding;
        p.speed_x = scaled(p.ground_speed, cos8(a, p.angle));
        p.speed_y = scaled(p.ground_speed, sin8(a, p.angle));
    }

    // The state changes of 080462C8 when an animation has run to its end.
    void animation_ended() {
        const u8 walking_or_standing = p.ground_speed != 0 ? state::walking : state::standing;
        switch (p.state) {
        case state::spin_dash_release:
            p.state = state::spin_dash;
            break;
        case state::crouch_end:
            p.state = walking_or_standing;
            break;
        case state::look_end:
            p.state = state::standing;
            break;
        case state::turning:
            p.state = state::walking;
            break;
        case 0x11:
        case 0x12:
            p.state = 0x13;
            break;
        case 0x1B:
            p.state = state::walking;
            break;
        case 0x4A:
            p.x += ((p.flags & player_flag::facing_left) ? -1 : 1) * (p.radius_x + 3) * 0x100;
            p.radius_x = 6;
            p.radius_y = 0xE;
            p.y += upside_down() ? 0xE00 : -0xE00;
            p.state = 0x4B;
            ++p.move;
            break;
        case 0x4B:
            p.angle = 0;
            land();
            break;
        case 0x40:
            land();
            break;
        case 0x2D:
        case 0x30:
            end_moves();
            p.state = walking_or_standing;
            break;
        case 0x2E:
            // The second kick's follow-up: a third kick, a jump back, or
            // the end.
            if (p.move_input == 1) {
                p.ground_speed = s16((p.flags & player_flag::facing_left) ? -0x400 : 0x400);
                ++p.move;
                p.move_input = 0;
                p.state = 0x2F;
                g.sound().start(0x6F);
            } else if (p.move_input == 2) {
                end_moves();
                p.speed_x = s16(-p.speed_x);
                p.speed_y = (p.flags & player_flag::underwater) ? -0x2A0 : -0x4E0;
                p.flags |= player_flag::in_air | player_flag::rolling;
                radius(6, 9);
                p.state = 0x31;
                g.sound().start(0x74);
            } else {
                p.move = 0;
                p.move_input = 0;
                p.state = state::standing;
                p.chain = 0;
                p.ground_speed = 0;
            }
            break;
        case 0x36:
            p.state = 0x35;
            break;
        case 0x3A:
            p.state = 0x39;
            break;
        case 0x3C:
            p.move = 0;
            p.move_input = 0;
            p.state = walking_or_standing;
            p.chain = 0;
            break;
        case 0x46:
            p.state = 0x47;
            break;
        case 0x4C:
        case 0x4D:
        case 0x57:
            not_yet("Amy's hammer moves (080462C8)");
        case 0x58:
            p.move = 0;
            p.state = walking_or_standing;
            break;
        case 0x5D:
            p.move = 0;
            p.move_input = 0;
            p.state = state::standing;
            p.chain = 0;
            break;
        default:
            break;
        }
    }
};

// The player's process (080454D8; 08045B04 for a partner).
class PlayerProcess : public Process {
public:
    // `super`: act 13's Super Sonic (08049864).
    PlayerProcess(Player& p, bool super)
        : Process(p.index == 0 ? 0x3000 : 0x3001), p_(p), super_(super) {
        p.process = this;
    }

    void on_destroy() override { p_.process = nullptr; }

    void update(Game& g) override {
        if (p_.index != 0) {
            throw Unrecovered("the partner's process (08045B04)");
        }
        if (phase_ == Phase::falling) {
            fall(g);
            return;
        }
        if (phase_ == Phase::waiting) {
            wait(g);
            return;
        }
        if (super_) {
            if (super_sonic_frame(g, p_)) {
                phase_ = Phase::falling;
            }
            return;
        }
        Movement m(g, p_);
        if (p_.flags & player_flag::dead) {
            start_dying(g);
        } else {
            m.read_buttons();
            m.water();
            if (!(p_.flags & player_flag::no_movement)) {
                switch (p_.character) {
                case 0:
                    m.sonic();
                    break;
                case 1:
                    throw Unrecovered("Tails' movement (08047B6C)");
                case 2:
                    throw Unrecovered("Knuckles' movement (08048FCC)");
                case 3:
                    throw Unrecovered("Amy's movement (08049778)");
                default:
                    break;
                }
            }
        }
        show(g);
        if (p_.state != 0x0F && p_.hurt_timer > 0) {
            --p_.hurt_timer;
        }
        if (p_.power != 0) {
            // Speed changes end with the music's normal tempo.
            if ((p_.power & player_power::fast) && --p_.power_timer == 0) {
                g.sound().set_tempo(0, 0x100);
                p_.power &= ~player_power::fast;
            }
            if ((p_.power & player_power::slow) && --p_.power_timer == 0) {
                g.sound().set_tempo(0, 0x100);
                p_.power &= ~player_power::slow;
            }
            if ((p_.power & player_power::invincible) && --p_.invincible_timer == 0) {
                p_.power &= ~player_power::invincible;
                g.sound().stop(0x1C);
            }
            if ((p_.power & 0x20) && --p_.power_timer_20 == 0) {
                p_.power &= ~0x20;
                auto& v = g.video().settings;
                v.sprite_window = false;
                v.outside = 0x3F;
            }
        }
    }

private:
    // After death: falling off the screen (080453E4), then a second's
    // wait (080452D0).
    enum class Phase : u8 { alive, falling, waiting };

    void show(Game& g) {
        Movement m(g, p_);
        m.choose_animation(*p_.body);
        m.draw_body(*p_.body);
        if (p_.character == 1) {
            throw Unrecovered("Tails' tails (08046AC0)");
        }
    }

    // 080454D8 with the dead flag: the dying jump (the body in front of
    // everything, the view held, the clock stopped).
    void start_dying(Game& g) {
        StageState& s = g.stage();
        phase_ = Phase::falling;
        p_.state = 1;
        p_.speed_x = 0;
        p_.hurt_timer = 2;
        p_.power = 0;
        p_.flags &= ~(player_flag::on_object | player_flag::pushing);
        p_.standing_on = nullptr;
        s.camera.locks |= 3;
        if (g.game_mode < 2) {
            s.flags |= stage_flag::paused;
        }
        Sprite& body = p_.body->sprite;
        body.flags = (body.flags & ~sprite_flag::priority) | 0x1000;
        p_.scale_x = p_.scale_y = 0x100;
        g.sound().stop(0x1C);
        g.sound().stop(0x1B);
        g.sound().stop(0x78);
        g.sound().start(s8(p_.air) < 0 ? 0xC0 : 0x77);
    }

    // 080453E4: falls (gravity reversed with the act's) until below the
    // view.
    void fall(Game& g) {
        StageState& s = g.stage();
        const bool upside_down = s.flags & stage_flag::upside_down;
        if (!(p_.flags & 0x80000000)) {
            const bool gone = upside_down ? p_.y <= (s.camera.y - 0x50) * 0x100
                                          : p_.y >= s.camera.y * 0x100 + 0xEFFF;
            if (gone) {
                timer_ = 0x3C;
                phase_ = Phase::waiting;
                return;
            }
        }
        p_.x += p_.speed_x;
        if ((s.flags ^ s.previous_flags) & stage_flag::upside_down) {
            p_.speed_y = s16(-p_.speed_y);
        }
        p_.y += upside_down ? -p_.speed_y : p_.speed_y;
        p_.speed_y = s16(p_.speed_y + ((p_.flags & player_flag::underwater) ? 0xC : 0x2A));
        show(g);
    }

    // 080452D0
    void wait(Game& g) {
        if (timer_ != 0) {
            --timer_;
            return;
        }
        if (g.game_mode >= 2) {
            throw Unrecovered("the multiplayer respawn (080452D0)");
        }
        g.scheduler().destroy(*this);
        player_died(g);
    }

    Player& p_;
    bool super_;
    Phase phase_ = Phase::alive;
    s32 timer_{}; // +04
};

// 080420D0: the body sprite.
void set_up_body(Game& g, Player& p, TransformedSprite& t) {
    Sprite& sp = t.sprite;
    sp.tiles.to = p.index == 0 ? 0x10000 : g.sprite_tiles.allocate(0x40);
    sp.tiles.bytes = 0;
    sp.animation = p.animation;
    sp.variant = u8(p.variant);
    sp.previous_variant = 0xFF;
    sp.x = s16(p.x >> 8);
    sp.y = s16(p.y >> 8);
    sp.layer = 0x400;
    sp.timer = 0;
    sp.speed = 0x10;
    sp.palette = 0;
    sp.flags = (g.game_mode >= 2 ? 0xC2000 : 0x2000) | sprite_flag::rotated | p.index;
    sp.hitboxes[0].index = 0xFFFFFFFF;
    sp.hitboxes[1].index = 0xFFFFFFFF;
    t.angle = 0;
    t.scale_x = t.scale_y = 0x100;
    t.x = t.y = 0;
}

// 08042178: Tails' tails.
void set_up_tails(const Player& p) {
    if (p.character == 1) {
        throw Unrecovered("Tails' tails sprite (08042178)");
    }
}

// 080423F4: the player at its start point, standing, the buttons ignored.
void reset_player(Game& g, Player& p) {
    StageState& s = g.stage();
    p.x = p.start_x << 8;
    p.y = p.start_y << 8;
    p.held = p.pressed = u16(s.jump_buttons | s.other_buttons);
    p.speed_x = p.speed_y = p.ground_speed = 0;
    p.flags = player_flag::frozen;
    p.angle = 0;
    p.radius_x = 6;
    p.radius_y = 0xE;
    p.look_timer = 0x78;
    p.spin_charge = 0;
    p.sensor_angle[0] = p.sensor_angle[1] = 0;
    p.layer = 1;
    p.top_speed = 0x480;
    p.acceleration = 8;
    p.deceleration = 0x60;
    p.state = p.previous_state = 0;
    p.animation = p.variant = 0xFFFF;
    p.hurt_timer = p.invincible_timer = p.power_timer = p.confused_timer = 0;
    p.standing_on = nullptr;
    p.power = 0;
    p.slip_timer = 0;
    p.idle_timer = 0x168;
    p.unknown_52 = p.unknown_50 = 0;
    p.scale_y = p.scale_x = 0x100;
    p.chain = 0;
    p.special = p.move = p.move_input = 0;
    p.air = 0x1E;
    p.air_ticks = 0x3C;
    p.splash_timer = 10;
    p.back_from_special = 0;
    p.unknown_6f.fill(0);
    p.tube_stage = 0x7F;
    if (p.index == 0 && g.game_mode < 2) {
        // The clock restarts from the checkpoint's time, or from 0 when it
        // had run out (10 minutes).
        if (s.clock < 36000) {
            s.frames = s.clock = p.checkpoint_time;
        } else {
            s.frames = s.clock = 0;
            p.checkpoint_time = 0;
        }
    }
    switch (p.character) {
    case 0:
        p.extra_80 = 0;
        p.extra_82 = 0;
        p.extra_84 = 0;
        break;
    case 1:
        p.extra_80 = 0;
        p.extra_84 = 0;
        p.extra_81 = 0;
        break;
    case 2:
        p.extra_80 = 0;
        p.extra_81 = 0;
        p.extra_82 = 0;
        break;
    case 3:
        p.extra_80 = 0;
        break;
    default:
        break;
    }
}

} // namespace

void place_player(Game& g, Player& p, u8 character, u8 zone, u8 index) {
    p.index = index;
    p.character = character;
    if (g.game_mode >= 4) {
        throw Unrecovered("multiplayer start points (080421E8)");
    }
    p.start_x = g.assets().s16(start_points + zone * 4u);
    p.start_y = g.assets().s16(start_points + zone * 4u + 2);
    p.unknown_6c = 0;
    p.checkpoint_time = 0;
    StageState& s = g.stage();
    p.body = index == 0 ? &s.player_sprites[2] : &s.player_sprites[3];
    p.extra = index == 0 ? &s.player_sprites[1] : &s.player_sprites[0];
}

void start_player(Game& g, Player& p, u8 zone) {
    if (p.index != 0) {
        throw Unrecovered("the partner (08045B04)");
    }
    g.scheduler().create<PlayerProcess>(p, zone == 0xD);
    reset_player(g, p);
    start_player_effects(g);
    start_lost_rings(g);
    if (g.demo_mode() != DemoMode::off) {
        // 0804CC78: the recording or playback starts over with the player.
        g.restart_demo_input();
    }
    set_up_body(g, p, *p.body);
    set_up_tails(p);
    if (zone == 0xD) {
        start_super_sonic(g, p);
    }
}

void set_button_layout(Game& g, u8 layout) {
    StageState& s = g.stage();
    if (layout == 0 || (s.flags & stage_flag::demo)) {
        s.jump_buttons = button::a;
        s.other_buttons = button::b;
    } else if (layout == 1) {
        s.jump_buttons = button::b;
        s.other_buttons = button::a;
    }
}

void land_player(Game& g, Player& p) {
    Movement(g, p).land();
}

void end_player_moves(Game& g, Player& p) {
    Movement(g, p).end_moves();
}

void set_player_radius(Game& g, Player& p, s8 radius_x, s8 radius_y) {
    Movement(g, p).radius(radius_x, radius_y);
}

s32 player_room(Game& g, Player& p, u8 direction) {
    return Movement(g, p).room_towards(direction);
}

} // namespace sonic_advance_gba
