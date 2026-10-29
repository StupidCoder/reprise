// Sonic's behaviour: running, rolling, jumping and gravity, scripted poses,
// animation, rings, damage and death. Terrain actions are in terrain.cpp.

#include "sonic1_gg/game/sonic.hpp"
#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace sonic1_gg {

namespace flag = body_flag;

namespace {

// Sonic's animation sequence numbers (table at 5C5B).
namespace anim {
inline constexpr int walk = 1;
inline constexpr int run = 2;
inline constexpr int standing = 5;
inline constexpr int crouch = 7;
inline constexpr int spin = 9;
inline constexpr int skid = 10;
inline constexpr int dying = 11;
inline constexpr int look_up = 12;
inline constexpr int waiting = 13;
inline constexpr int teleport_end = 14;
inline constexpr int teleport_start = 15;
inline constexpr int hurt = 16;
inline constexpr int carried = 17;
inline constexpr int springing = 19;
inline constexpr int breathing = 20;
inline constexpr int drowning = 21;
inline constexpr int ending_drop = 22;
inline constexpr int ending_pose = 23;
inline constexpr int ending_stand = 24;
inline constexpr int ending_final_pose = 25;
} // namespace anim

} // namespace

// One update of Sonic's movement (4B63-4E3F). Positions are not changed here
// except for collision-box adjustments; move() follows.
void Sonic::update(Game& game, Input input) {
    auto& level = game.level;
    auto& camera = game.camera;
    if (skid_ticks) {
        --skid_ticks;
    }
    if (game.music_restore_ticks && !--game.music_restore_ticks) {
        if (level.music != 255) {
            game.sound_events.push_back({SoundEvent::Kind::Music, level.music});
        }
    }

    // In special stages Sonic is always curled (5374).
    if (game.special_mode) {
        flags |= flag::jumping;
    }

    // A water current (592C) overrides the directional controls and pushes Sonic
    // down; it is applied before the movement constants are chosen.
    if (current_ticks) {
        input.left = input.right = input.down = input.up = false;
        vy = (vy & 255) + 1024;
        flags &= ~flag::curled;
    }
    // 526C consumes air while the PREVIOUS update's underwater bit is set.
    if (flags & flag::underwater) {
        game.update_air_supply();
    }

    // --- Scripted states that replace normal movement ---------------------------

    // Teleporter (538B-5411): Sonic stands still in a pose, then either moves to
    // the destination cell or, for a room exit, triggers an area reload.
    if (game.teleport_ticks) {
        --game.teleport_ticks;
        if (game.teleport_ticks >= 20) {
            vx = vy = 0;
            animation = anim::teleport_start;
        } else if (game.teleport_ticks) {
            flags &= ~flag::facing_left;
            animation = anim::teleport_end;
        } else {
            const int destination = level.data(game.teleport_table + 2);
            if (destination) {
                if (destination < 128) {
                    game.area_transition = true;
                    game.area_target = destination;
                } else {
                    game.area_advance = true;
                }
                game.finish_ticks = 1;
                set_pixel_x(0);
                set_pixel_y(0);
            } else {
                x = (level.data(game.teleport_table) * 32 + 8) * 256;
                y = level.data(game.teleport_table + 1) * 32 * 256;
            }
        }
        finish_update(game);
        return;
    }

    // Ending: Sonic drops in from above and curls when the animation finishes.
    if (game.ending.drop) {
        vy = 0;
        animation = anim::ending_drop;
        if (animation_step >= 18) {
            game.ending.drop = false;
            flags |= flag::jumping;
        }
        finish_update(game);
        return;
    }

    // Good ending: Sonic walks back to x = 7008, stops, the emeralds appear and
    // he strikes two poses.
    if (game.ending.collect) {
        input = {pixel_x() >= 7008, false, false, false, false};
        if (pixel_x() < 7008 && (vx & 65535) == 0) {
            flags |= flag::facing_left;
            animation = anim::ending_stand;
            if (!game.ending.emerald_spawned) {
                game.ending.pose_ticks = 80;
                if (auto* emerald = game.allocate_object(object_type::ending_emerald)) {
                    emerald->state[0] = 0;
                    emerald->flags = 0;
                    emerald->x = pixel_x() * 256;
                    emerald->y = ((pixel_y() + 14) & 65535) * 256;
                    game.ending.emerald_spawned = true;
                }
            } else {
                if (!game.ending.emerald_pose && !--game.ending.pose_ticks) {
                    game.ending.emerald_pose = true;
                    game.ending.pose_ticks = 140;
                }
                if (game.ending.emerald_pose) {
                    animation = anim::ending_pose;
                    if (game.ending.pose_ticks) {
                        --game.ending.pose_ticks;
                    } else {
                        animation = anim::ending_final_pose;
                    }
                }
            }
            finish_update(game);
            return;
        }
    }

    // --- Movement constants (4B63-4B89) ------------------------------------------
    //
    // All speeds are in 1/256 pixel per update. On land Sonic accelerates by 16
    // up to 768 (3 px) and brakes by 48 when reversing; with speed shoes the top
    // speed doubles. Underwater everything is much slower. In the air during a
    // special stage, friction is low and the speed limit is very high.
    const bool water = (flags & flag::underwater) != 0;
    const int acceleration = water ? 4 : 16;
    const int braking = water ? 12 : 48;
    const int friction = water ? 2 : game.special_mode && !grounded() ? 2 : 8;
    const int max_run = water                              ? 256
                        : shoes_ticks                      ? 1536
                        : game.special_mode && !grounded() ? 3072
                                                           : 768;
    const int jump_velocity = water ? -576 : -768;
    const int gravity = water ? 16 : 56;
    const int terminal_velocity = water ? 512 : 2048;

    // The music tempo follows the speed shoes (on land only).
    if (!water) {
        game.sound_events.push_back({SoundEvent::Kind::Tempo, unsigned(shoes_ticks != 0)});
    }
    if (!water && shoes_ticks && (game.ticks & 3) == 0) {
        --shoes_ticks;
    }
    flags &= ~flag::no_terrain;

    // Pressing down while running on the ground curls into a roll (5335). This
    // happens BEFORE the terrain check, so that pressing down at a ramp's lip
    // already counts as rolling when the ramp decides whether to launch.
    if (input.down && !(flags & flag::curled) && grounded()) {
        flags |= flag::rolling;
        if (vx & 65535) {
            game.sound(Effect::Roll);
        }
    }

    // Terrain under Sonic's centre: springs, spikes, conveyors, water and so on.
    {
        const bool already_hurt = hurt;
        game.current_input = input;
        terrain_actions(game);
        input = game.current_input;
        if (current_ticks) {
            input.left = input.right = input.down = input.up = false;
        }
        // 4C3A: falling below the camera's lowest limit is always fatal, even
        // with rings, a shield or invincibility.
        if (pixel_y() + 36 > level.camera_bottom + 192) {
            die(game);
        }
        // Damage taken from the terrain just now removes control immediately
        // (3092), for the rest of this update.
        if (hurt && !already_hurt) {
            input = {};
        }
    }

    // Idle timer for the waiting animation: no buttons, no speed, on the ground.
    const bool idle = !input.left && !input.right && !input.jump && !input.down && !input.up &&
                      vx == 0 && grounded();
    idle_ticks = idle ? ((idle_ticks + 1) & 65535) : 0;

    // Acts with a water level: below the surface, Sonic is underwater.
    if (level.has_water()) {
        if (game.water.y < pixel_y()) {
            if (!(flags & flag::underwater)) {
                game.sound(Effect::WaterEnter);
            }
            flags |= flag::underwater;
        } else {
            air_ticks = 0;
            flags &= ~flag::underwater;
        }
    }

    animation = idle_ticks >= 360 ? anim::waiting : anim::standing;

    // Looking up: holding only up while standing still raises the view.
    if (input.up && !input.left && !input.right && !input.down && !input.jump) {
        if (vx == 0 && grounded()) {
            animation = anim::look_up;
            if (camera.vertical_bias <= 48) {
                ++camera.vertical_bias;
            }
        }
    } else if (!(flags & flag::rolling)) {
        camera.vertical_bias = approach(camera.vertical_bias, 0);
    }

    if (flags & flag::rolling) {
        roll(game, input, braking);
    } else {
        // Uncurled: restore the standing 16x32 collision box (4C99-4CB8, 5312).
        // Curling moved the box origin down by 11 pixels; undo that.
        if (height != 32 && !(flags & flag::jumping)) {
            y = wrap24(y - 11 * 256);
        }
        width = 16;
        height = 32;

        // Running (515E-522F). Pressing against the current direction brakes
        // harder than letting go. During a fast skid (>= 1 px/update) Sonic keeps
        // facing his direction of motion and shows the skid animation; below
        // that he turns immediately.
        int delta = 0;
        const int old_vx = vx;
        if (input.right) {
            flags &= ~flag::facing_left;
            if (vx >= 0) {
                animation = anim::walk;
                if (vx < max_run) {
                    delta = acceleration;
                } else {
                    vx = max_run;
                    animation = water ? anim::walk : anim::run;
                }
            } else {
                delta = braking;
                if (-vx >= 256) {
                    flags |= flag::facing_left;
                    animation = anim::skid;
                } else {
                    animation = anim::walk;
                }
            }
        } else if (input.left) {
            flags |= flag::facing_left;
            if (vx <= 0) {
                animation = anim::walk;
                if (-vx < max_run) {
                    delta = -acceleration;
                } else {
                    vx = -max_run;
                    animation = water ? anim::walk : anim::run;
                }
            } else {
                delta = -braking;
                if (vx >= 256) {
                    flags &= ~flag::facing_left;
                    animation = anim::skid;
                } else {
                    animation = anim::walk;
                }
            }
        } else if (vx != 0) {
            // Coasting: friction slows Sonic down. Above the top speed (after a
            // spring, for example) he slows by the acceleration value instead.
            const bool too_fast = std::abs(vx) >= max_run;
            animation = too_fast ? (water ? anim::walk : anim::run) : anim::walk;
            delta = (vx > 0 ? -1 : 1) * (too_fast ? acceleration : friction);
        }
        vx += delta;
        // 4D32-4D58: braking or friction never reverses the direction of
        // motion; crossing zero stops Sonic. (From zero he may accelerate
        // either way.)
        if ((old_vx < 0 && vx >= 0) || (old_vx > 0 && vx < 0)) {
            vx = 0;
        }
    }

    if (animation == anim::skid && !skid_ticks && grounded()) {
        game.sound(Effect::Skid);
        skid_ticks = 60;
    }

    // Landing from a jump: uncurl and restore the box origin.
    if (grounded() && (flags & flag::jumping)) {
        flags &= ~flag::jumping;
        if (!game.special_mode) {
            y = wrap24(y - 11 * 256);
        }
    }

    // --- Jumping and gravity (4D78-4DD3) --------------------------------------
    //
    // A jump sets the upward speed and keeps re-applying it for up to 16
    // updates while the button stays held, so a longer press jumps higher.
    // A new jump needs the button to have been released on the ground first:
    // `jump_released` is that latch. A rolling Sonic can jump too, which
    // uncurls him from the roll into the jump.
    const bool was_rolling = (flags & flag::rolling) != 0;
    bool may_hold = !was_rolling && jump_hold != 0;
    if (!may_hold && grounded()) {
        may_hold = (flags & flag::jump_released) || !input.jump;
    }
    previous_gravity = 0;
    if (may_hold && input.jump) {
        if (jump_hold == 0) {
            jump_hold = 16;
            game.sound(Effect::Jump);
        }
        if (was_rolling) {
            flags &= ~flag::rolling;
            vx &= ~7;
        }
        vy = jump_velocity;
        --jump_hold;
        flags |= flag::jumping;
    } else {
        if (may_hold && !input.jump) {
            flags |= flag::jump_released;
        } else {
            flags &= ~flag::jump_released;
        }
        if (!was_rolling) {
            jump_hold = 0;
        }
        // Gravity, up to the terminal falling speed. Platforms that Sonic
        // stands on cancel exactly this amount (see Game::land_on_platform).
        if (vy < terminal_velocity) {
            vy += gravity;
            previous_gravity = gravity;
        }
    }
    if (flags & flag::jumping) {
        animation = anim::spin;
    }
    // Moving fast vertically while uncurled: springing up or falling.
    if (std::abs(vy) > 256 && !(flags & (flag::grounded | flag::curled))) {
        animation = vy < 0 ? anim::springing : anim::walk;
    }

    // 4E3F collects rings before the horizontal clamp in finish_update(). In an
    // auto-scrolling act the other order would delay rings at the edge.
    collect_rings(game);

    finish_update(game);
}

// The common tail of Sonic's update (4EE0-4FFB): speed limits, bubbles,
// invincibility stars, the level's horizontal limits and pose overrides.
void Sonic::finish_update(Game& game) {
    const auto& level = game.level;

    // Speed caps: 10 px/update horizontally, 12 vertically (fast ramp launches
    // reach it). Only the integer byte is limited; the fraction byte is kept.
    // For negative speeds the integer byte is floor(value / 256), which is not
    // C++'s truncation toward zero, hence the explicit fraction handling.
    auto limit_speed = [](int speed, int limit) {
        const int fraction = speed & 255;
        return std::clamp((speed - fraction) / 256, -limit, limit) * 256 + fraction;
    };
    vx = limit_speed(vx, 10);
    vy = limit_speed(vy, 12);

    // Underwater, Sonic breathes out a bubble every 256 updates (4F0A).
    if (level.has_water() && (flags & flag::underwater) && !(game.ticks & 255)) {
        game.spawn_bubble(*this, 0);
    }
    if (invincible_ticks) {
        update_stars(game); // 4F11
    }
    if (game.ring_sparkle_ticks) {
        game.update_ring_sparkle(); // 4F1F
    }
    if (game.special.bumper_ticks) {
        --game.special.bumper_ticks; // 54B0
    }

    // Horizontal level limits (4F33-4F8C). Sonic may not leave the area the
    // camera can show: the camera's limits plus the LCD crop (48 px left, 208 px
    // right, minus his width). Widescreen enhancement: where the limits roll
    // along with the camera (auto-scrolling, or a goal sign or capsule that
    // stops the camera from scrolling back) they move out to the edges of the
    // presented view, so that there is no invisible wall inside it. Arena
    // walls stay, and the force field shows them.
    const bool scrolling = game.rolling_limits();
    const int left = level.camera_left + 48 - (scrolling ? game.wide_left : 0);
    const int right = level.camera_right + 208 - 16 - 1 + (scrolling ? game.wide_right : 0);
    if (!game.auto_run && pixel_x() < left) {
        set_pixel_x(left);
        if (vx < 0) {
            vx = 0;
        }
    }
    if (!game.auto_run && pixel_x() > right) {
        set_pixel_x(right);
        if (vx >= 256) {
            vx = 0;
        }
    }

    // Pose overrides, in the original order.
    if (current_ticks) {
        --current_ticks;
        animation = anim::carried;
    }
    if (hurt) {
        // Knock-back ends when Sonic lands.
        width = 20;
        animation = anim::hurt;
        if (vy >= 0 && grounded()) {
            hurt = false;
            vx = 0;
        }
    }
    if (breathing_ticks) {
        flags &= 0xff & ~flag::curled;
        animation = anim::breathing;
        --breathing_ticks;
    }
    // A curled Sonic is 25 pixels tall in the air. This runs after the breathing
    // override clears the curl bits, so jumping during that pose keeps the
    // standing box (4F2A/4FFB).
    if (flags & flag::jumping) {
        height = 25;
    }
    previous_flags = flags;
    animate(level);
}

void Sonic::update_stars(Game& game) {
    // 509F: both stars orbit the position Sonic will have after this update's
    // movement, (X + 5 + vx, Y + 9 + vy), in 16-bit words. The breathing pose
    // has already cleared the curl bits when the original reads D415.
    const int current = breathing_ticks ? flags & (0xff & ~flag::curled) : flags;
    const int base_x = (pixel_x() + 5 + (vx >> 8)) & 65535;
    const int base_y = (pixel_y() + 9 + (vy >> 8)) & 65535;
    static constexpr int tiles[2] = {0x94, 0x96};
    for (int star = 0; star < 2; ++star) {
        // 50CE: a table of signed (dx, dy) byte pairs at 510A. Its 24 entries
        // alternate between opposite points, so each sprite shows two stars by
        // flickering. Curled, the orbit is 4 pixels higher.
        int& phase = star_phase[star];
        const int dx = signed8(game.level.data(rom::star_offsets + phase));
        const int dy = signed8(game.level.data(rom::star_offsets + 1 + phase)) +
                       ((current & flag::curled) ? -4 : 0);
        game.queue_effect_sprite(tiles[star], base_x + dx, base_y + dy);
        phase += 2;
        if (phase >= 0x30) {
            phase = 0;
        }
    }
}

// Rolling (55C7-56D4). A rolling Sonic cannot accelerate on the ground: he
// slows down by 4/256 per update and can only brake. In the air he can steer.
void Sonic::roll(Game& game, Input input, int braking) {
    // Curling shortens the box from 32 to 21 pixels and moves its origin down by
    // 11, so the feet stay where they were.
    if (height != 21) {
        y = wrap24(y + 11 * 256);
    }
    width = 16;
    height = 21;

    if (vx == 0) {
        if (grounded()) {
            // Stopped: uncurl into a crouch, which lowers the view.
            animation = anim::crouch;
            flags &= ~flag::rolling;
            if (game.camera.vertical_bias > -40) {
                --game.camera.vertical_bias;
            }
        } else {
            animation = anim::spin;
        }
        return;
    }

    animation = anim::spin;
    int delta = vx < 0 ? 4 : -4; // Rolling friction.
    const int old_vx = vx;
    if (!input.down && (input.left || input.right)) {
        if (!grounded()) {
            // Steering in the air. Note: there is no speed limit here, only the
            // general 10 px cap, so a rolling jump can outrun the camera.
            delta = input.left ? -16 : 16;
        } else if ((input.left && vx >= 0) || (!input.left && input.right && vx < 0)) {
            // Pressing against the roll uncurls Sonic into a skid. Both original
            // branches end in the same code (5203, 5635), so this clears the
            // facing bit in either direction; preserved as executed.
            flags &= ~flag::rolling;
            flags &= ~flag::facing_left;
            animation = anim::skid;
            delta = -braking;
            if ((vx & 65535) < 256) {
                flags |= flag::facing_left;
                animation = anim::walk;
            }
        }
    }
    vx += delta;
    if ((old_vx < 0 && vx >= 0) || (old_vx > 0 && vx < 0)) {
        vx = 0;
    }
}

// Animation (549E, 4F99-4FD6). Each animation is a byte sequence of frame
// numbers; a byte >= 128 is a jump to another position in the sequence.
void Sonic::animate(const Level& level) {
    auto byte = [&](int address) {
        return int(level.animation_data.at(address - rom::sonic_animations));
    };
    const int table = rom::sonic_animations + animation * 2;
    const int sequence = byte(table) | (byte(table + 1) << 8);
    if (animation != previous_animation) {
        animation_step = 0;
    }
    for (int guard = 0;; ++guard) {
        if (guard > 4) {
            throw std::runtime_error("Invalid animation loop");
        }
        const int value = byte(sequence + animation_step);
        if (value < 128) {
            frame = value;
            break;
        }
        animation_step = byte(sequence + animation_step + 1);
    }
    previous_animation = animation;

    // Walking and spinning advance in proportion to the horizontal speed; the
    // fractional accumulator persists across animation changes. Everything
    // else advances one step per update.
    if (animation == anim::walk || animation == anim::spin) {
        const int base = animation == anim::walk ? 1 : 4;
        const int period = animation == anim::walk ? 48 : 70;
        const int speed = std::abs(vx) / 2;
        const int fraction = animation_fraction + (speed & 255);
        animation_fraction = fraction & 255;
        animation_step = (animation_step + base + speed / 256 + fraction / 256) & 255;
        if (animation_step >= period) {
            animation_step -= period;
        }
    } else {
        animation_step = (animation_step + 1) & 255;
    }
}

void Sonic::move(const Game& game) {
    game.integrate_body(*this);
}

// The death sequence. Sonic jumps up and falls through everything (no
// terrain), using the death or drowning animation. The life is taken once he
// has fallen below the screen. Game::tick counts death_ticks down.
void Sonic::update_dead(Game& game) {
    flags |= flag::no_terrain | flag::facing_left;
    animation = drowned ? anim::drowning : anim::dying;
    if (death_ticks != 96) {
        if (!life_debited && pixel_y() > game.camera.y + 192) {
            game.progress.ever_died = true;
            --game.lives;
            life_debited = true;
        } else {
            if (drowned) {
                vy = 128; // Sinks slowly.
            } else if (vy <= 1536) {
                vy += 32;
            }
            vx = 0;
        }
    }
    animate(game.level);
    previous_flags = flags;
    move(game);
}

// Ring collection (4E3F/5000). Rings are not objects: a ring block in the level
// layout (block number >= 0x79) holds up to two rings, one per 16-pixel half,
// recorded in bits 0 and 1 of the block number. Collecting one clears its bit
// and patches the visible tiles.
void Sonic::collect_rings(Game& game) {
    auto& level = game.level;
    const int cx = pixel_x() + 8;
    const int cy = pixel_y() + 8;
    if (cx >= level.columns * 32 || cy >= level.rows * 32 || pixel_y() - game.camera.y < 16) {
        return;
    }
    auto& block = level.layout[(cy / 32) * level.columns + cx / 32];
    if ((block & 127) < 0x79) {
        return;
    }
    const int mask = 1 << ((cx >> 4) & 1);
    if (block & mask) {
        block ^= mask;
        // Erasing the ring uses the single tile patch slot (5028-5050).
        game.queue_tile_patch(cx & ~15, (cy & ~31) + 8, rom::ring_erase_patch);
        game.add_rings(1);
        // The same position starts the six-update sparkle (502F-504A).
        game.ring_sparkle_x = cx & ~15;
        game.ring_sparkle_y = (cy & ~31) + 8;
        game.ring_sparkle_ticks = 6;
    }
}

// Death (2FF4): falling, drowning, time-out, or a hit without rings or shield.
void Sonic::die(Game& game) {
    game.music(Music::Death);
    dead = true;
    flags |= flag::grounded;
    vy = -1024;
    death_ticks = 96;
    hurt = shield = false;
    invincible_ticks = 0;
}

// A hit (2FD9-3099). Without rings or a shield it is fatal. Otherwise Sonic
// loses the shield or all of his rings and is knocked back up and away from the
// direction he faces. The scattered rings are a single animated object that
// cannot be collected again; while it runs, Sonic cannot be hurt.
void Sonic::take_hit(Game& game) {
    if (dead || lost_ring_guard || hurt || invincible_ticks) {
        return;
    }
    if (!shield && !game.rings) {
        die(game);
        return;
    }
    if (!shield) {
        game.rings_lost_this_tick = game.rings;
        game.ring_loss_x = x;
        game.ring_loss_y = y;
        game.rings = 0;
        if (auto* lost = game.allocate_object(object_type::lost_rings)) {
            lost->state[0] = 6;
            lost->state[1] = 0;
            lost->set_pixel_x(pixel_x());
            lost->set_pixel_y(pixel_y());
            lost->vy = -1024;
        }
    }
    vy = (flags & flag::underwater) ? -512 : -1024;
    vx = facing_left() ? 512 : -512;
    if (facing_left()) {
        // The original ORs in the literal 0x12 (3074), which also sets the
        // underwater bit. Preserved as executed.
        flags |= flag::facing_left | flag::underwater;
    }
    shield = false;
    hurt = true;
    game.sound(Effect::RingLoss);
}

} // namespace sonic1_gg
