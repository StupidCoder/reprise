// Objects found in several zones: monitors, the chaos emerald, the goal sign,
// lost rings, explosions, shots, the common enemies and the platforms.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <cstdlib>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

// Monitors (5E0C-6089): solid boxes that break when Sonic rolls into them or
// lands on them while jumping. Hitting one from below while jumping bumps it up.
void Game::update_monitor(Object& o) {
    o.width = 20;
    o.height = 24;
    // First update: a monitor on block 0xB0 in Green Hill sits at a different
    // alignment; all others move four pixels right (6089).
    if (o.first_update()) {
        const bool offset = level.scene == 0 && level.block_at(o.pixel_x(), o.pixel_y()) == 0xb0;
        o.set_pixel_x(o.pixel_x() + (offset ? 22 : 4));
        if (offset) {
            o.set_pixel_y(o.pixel_y() + 22);
        }
    }
    // A life monitor can be collected once per act; linked rooms of the same act
    // share the flag (5EE8).
    if (o.type == type::life_monitor &&
        (progress.life_monitor_acts & (std::uint64_t{1} << level.act))) {
        o.type = type::none;
        o.mapping = 0;
        return;
    }

    bool broken = false;
    if (overlaps(o, 3, 0)) {
        if (sonic.flags & rolling) {
            broken = true;
        } else if (sonic.pixel_x() > o.pixel_x() - 10 && sonic.pixel_x() <= o.pixel_x() + 16) {
            // Above or below the monitor.
            if (!(sonic.flags & jumping)) {
                land_on_platform(o, 0, 0);
                sonic.jump_hold = 0;
            } else if (sonic.vy >= 0) {
                broken = true; // Jumped onto it.
            } else {
                // Hit from below: the monitor hops and Sonic bounces down.
                o.vy = -384;
                sonic.vy = 1024;
                sonic.jump_hold = 0;
                o.flags |= 0x02;
            }
        } else {
            // From the side: it acts as a wall.
            sonic.x = (o.pixel_x() + (o.pixel_x() + 10 >= sonic.pixel_x() + 8 ? -13 : 21)) * 256;
            sonic.vx = 0;
        }
    }

    if (broken) {
        const int kind = o.type;
        // 60CC destroys the monitor BEFORE granting its reward; the order matters
        // because both play a sound and only one sound wins.
        kill_object(o, 4, 8);
        switch (kind) {
        case type::ring_monitor:
            add_rings(10);
            break;
        case type::shoes_monitor:
            sonic.shoes_ticks = 240;
            sound(Effect::Ring);
            break;
        case type::life_monitor:
            if (o.flags & 0x04) {
                add_rings(10); // Disguised as a ring monitor (Labyrinth 1).
            } else {
                ++lives;
                if (level.act < 28) {
                    ++progress.life_monitor_count;
                }
                progress.life_monitor_acts |= std::uint64_t{1} << level.act;
                sound(Effect::ExtraLife);
            }
            break;
        case type::shield_monitor:
            sonic.shield = true;
            break;
        case type::invincibility_monitor: // 5FF2-6003
            sonic.invincible_ticks = 240;
            sonic.star_phase[0] = 0x18;
            sonic.star_phase[1] = 0;
            music(Music::Invincibility);
            break;
        case type::continue_monitor: // 607C sets D209 bit 3.
            progress.continue_collected = true;
            break;
        case type::checkpoint_monitor:
            checkpoint_x = ((o.pixel_x() - 4) / 32) * 32;
            checkpoint_y = ((o.pixel_y() - 8) / 32 - 1) * 32;
            break;
        }
        return;
    }

    // Special life monitors, before the common icon code:
    if (o.type == type::life_monitor && level.act == 4) {
        // Bridge 2: it floats up and down. The first leg down lasts 60 updates,
        // later legs alternate every 20 (5F32, 5F4B-5F72).
        o.vy = o.state[2] < 60 ? 64 : -64;
        if (++o.state[2] >= 80) {
            o.state[2] = 40;
        }
    }
    if (o.type == type::life_monitor && level.act == 9) {
        // Labyrinth 1: it looks like a ring monitor until the pressure switch
        // toggles this act's bit (5F74-5F8D). Contact above used the old state.
        o.flags |= 0x04;
        if (progress.switch_acts & (1u << level.act)) {
            o.flags &= ~0x04;
        }
    }
    if (o.type == type::life_monitor && level.act == 12) {
        // Scrap Brain 1: it drifts right and falls with bounce gravity (5F90).
        o.flags |= 0x02;
        o.vx = 128;
    }
    // Sky Base 3: the monitor only exists after seventeen earlier life monitors
    // (5FA2). The original empties the slot but still draws the life icon this
    // update (5FA9 -> 5F45), so the icon must not be looked up from type FF.
    const int icon_type = o.type;
    if (o.type == type::life_monitor && level.act == 17 && progress.life_monitor_count < 17) {
        o.type = type::none;
    }

    // The monitor's icon is streamed into tiles 0x5C-0x5F, eight bytes per update
    // (5E0C / 0BA8). The screen flickers between the icon and the plain monitor.
    const unsigned icon = icon_type == type::continue_monitor                   ? 7
                          : icon_type == type::checkpoint_monitor               ? 6
                          : icon_type == type::life_monitor && (o.flags & 0x04) ? 0
                                                                                : icon_type - 1;
    const unsigned offset = (ticks & 15) * 8;
    std::copy_n(level.monitor_icons.begin() + icon * 128 + offset, 8,
                level.object_patterns.begin() + 0xb80 + offset);
    o.mapping = (ticks & 7) < 5 ? rom::monitor_with_icon : rom::monitor_flicker;
    // The original returns before applying the bounce gravity on three of every
    // eight updates (the flicker frames).
    if ((ticks & 7) < 5 && (o.flags & 0x02)) {
        o.vy += 64;
    }
}

// A chaos emerald (6183). Collecting it plays the emerald jingle (the act music
// returns after 254 updates); the bit per act survives restarts, so a collected
// emerald stays gone.
void Game::update_chaos_emerald(Object& o) {
    if (progress.emerald_acts & (1u << level.act)) {
        o.type = type::none;
        return;
    }
    o.width = 12;
    o.height = 17;
    if (o.first_update()) {
        const bool offset = level.scene == 0 && level.block_at(o.pixel_x(), o.pixel_y()) == 0xb0;
        o.set_pixel_x(o.pixel_x() + (offset ? 22 : 4));
        if (offset) {
            o.set_pixel_y(o.pixel_y() + 22);
        }
    }
    o.mapping = 0;
    if (overlaps(o, 2, 2)) {
        progress.emerald_acts |= 1u << level.act;
        ++progress.emerald_count;
        music_restore_ticks = 254;
        music(Music::Emerald);
        o.type = type::none;
    } else {
        // Blinks every other update and falls onto the ground. Its graphics are
        // streamed into tiles 0x5C-0x5F eight bytes per update (0BA8).
        if (!(ticks & 1)) {
            o.mapping = rom::emerald_layout;
        }
        o.vy += 32;
        const unsigned offset = (ticks & 15) * 8;
        std::copy_n(level.emerald_patterns.begin() + offset, 8,
                    level.object_patterns.begin() + 0xb80 + offset);
    }
}

// The rings scattered by a hit (7BD1). They fall off the screen while blinking
// through six frames. As long as they exist Sonic cannot be hurt again.
void Game::update_lost_rings(Object& o) {
    auto& frame_timer = o.state[0];
    auto& frame = o.state[1];
    o.flags |= no_terrain;
    sonic.lost_ring_guard = true;
    o.mapping = (ticks & 1) ? level.word(rom::lost_ring_frames + frame * 3) : 0;
    o.vy += 32;
    if (o.pixel_y() > bottom_edge(256)) {
        o.type = type::none;
        sonic.lost_ring_guard = false;
        return;
    }
    o.vx = 0;
    if (--frame_timer == 0) {
        frame_timer = 4;
        frame = (frame + 1) % 6;
    }
}

// A destroyed enemy (693E). On its second update the explosion bounces a
// falling Sonic back up, which is what lets him chain jumps off enemies; not in
// boss arenas (terrain streaming off) or in act 18.
void Game::update_explosion(Object& o) {
    auto& age = o.state[0];
    auto& initialised = o.state[4]; // 0xAA once set up
    o.flags |= no_terrain;
    if (initialised != 0xaa) {
        age = o.state[5] = o.state[6] = 0;
        initialised = 0xaa;
        o.vx = o.vy = 0;
    }
    if (age == 1 && terrain_streaming && level.act != 18 && !sonic.grounded() && sonic.vy >= 0) {
        sonic.vy = -sonic.vy - sonic.previous_gravity;
    }
    animate_object(o, rom::explosion_animation, rom::explosion_frames);
    if (++age >= 24) {
        o.type = type::none;
    }
}

// The shared enemy and boss shot (6B26). It only hurts; its gravity is the
// state word its creator stored (see fire_projectile).
void Game::update_projectile(Object& o) {
    o.flags |= no_terrain;
    o.width = o.height = 2;
    if (overlaps(o, 3, 3)) {
        sonic.take_hit(*this);
    }
    o.vy += o.state_word(2);
    o.mapping = 0;
    // Drawn as a direct sprite, before the shot moves; Bridge 3 and Labyrinth 3
    // (the boss acts 5 and 11) use the boss shot art (6B7A-6B95).
    o.extra_sprites.push_back(
        {level.data((level.act == 5 || level.act == 11 ? rom::boss_shot_tiles : rom::shot_tiles) +
                    (ticks & 1)),
         o.pixel_x(), o.pixel_y()});
    // Removed off screen. Widescreen margins (zero on the LCD) keep shots until
    // they leave the presented view.
    if (o.pixel_x() < left_edge(8) || o.pixel_x() > right_edge(256) ||
        o.pixel_y() + 16 < top_edge(0) || o.pixel_y() > bottom_edge(192)) {
        o.type = type::none;
    }
}

// The crab (65F9). It follows a looping action script at 66D0: 1 walk right,
// 2 walk left, 3 stop, 4 stop and fire two arcing shots (left and right).
// The state word 0 is a timer advancing by 8 per update; its high byte is
// also the script position, so each action lasts 32 updates and the shots
// leave when the low byte reaches 32.
void Game::update_crab(Object& o) {
    auto& timer_low = o.state[0];
    auto& script_position = o.state[1];
    o.width = 16;
    o.height = 31;
    int action = level.data(rom::crab_script + script_position);
    if (!action) {
        script_position = 0;
        action = level.data(rom::crab_script);
    }
    if (action == 1) {
        o.vx = 40;
    } else if (action == 2) {
        o.vx = -40;
    } else if (action == 3) {
        o.vx = 0;
    } else if (action == 4 && timer_low == 32) {
        for (int direction : {-1, 1}) {
            const int x = o.pixel_x() + (direction < 0 ? 0 : 14);
            if (fire_projectile(x, o.pixel_y(), direction * 256, -1024, 36) && direction == 1) {
                sound(Effect::Projectile);
            }
        }
    }
    o.set_state_word(0, o.state_word(0) + 8);
    o.vy += 32;
    // On a firing update 6646 overwrites the animation table pointer with FFFC;
    // the mapper byte there is zero in this ROM, which selects animation entry
    // 0 for one update. Preserved.
    const int animation = (action == 4 && timer_low == 40) ? 0 : action;
    animate_object(o, level.word(rom::crab_animations + animation * 2), rom::crab_frames);
    enemy_contact(o, 4, 10, 4, 8);
}

// The flying shooter (6BD9). It waits off screen to the right, then flies left
// by its action script at 6D3C (1 fly, 2 hover, 3 hover and fire downward),
// and returns to its origin once it has left the screen on the left. Coming
// within 64 pixels of Sonic jumps to the attack part of the script.
// State: word 0 timer (high byte = script position), word 3 the origin X;
// flag 0x01 armed.
void Game::update_flying_shooter(Object& o) {
    auto& timer_low = o.state[0];
    auto& script_position = o.state[1];
    o.flags |= no_terrain;
    if (!(o.flags & 0x01)) {
        o.set_state_word(3, o.pixel_x());
        o.mapping = 0;
        script_position = 0;
        o.vx = 0;
        // It only arms itself while beyond the visible right edge.
        if (right_edge(256) >= o.pixel_x()) {
            return;
        }
        o.flags |= 0x01;
    }
    o.width = 20;
    o.height = 32;
    if (((o.pixel_x() - sonic.pixel_x()) & 65535) < 64 && script_position < 5) {
        script_position = 5;
    }
    int action = level.data(rom::flying_shooter_script + script_position);
    if (!action) {
        script_position = 0;
        action = level.data(rom::flying_shooter_script);
    }
    if (action == 1) {
        if (o.pixel_x() + 48 < left_edge(0)) {
            o.mapping = 0;
            o.set_pixel_x(o.state_word(3));
            o.flags &= ~0x01;
            return;
        }
        o.vx = -512;
    } else if (action == 2) {
        o.vx = 0;
    } else if (action == 3 && timer_low == 32) {
        if (fire_projectile(o.pixel_x(), o.pixel_y() + 32, -256, 384, 0)) {
            sound(Effect::Projectile);
        }
        o.vx = 0;
    }
    o.set_state_word(0, o.state_word(0) + 8);
    animate_object(o, level.word(rom::flying_shooter_animations + action * 2),
                   rom::flying_shooter_frames);
    enemy_contact(o, 0, 16, 4, 16);
}

// The ground bug (6E65). It crawls by the action script at 6EEF (1 left,
// 2 right, otherwise stop). A constant downward speed keeps it on the terrain,
// since it uses normal collision. Word 0: timer, high byte = script position.
void Game::update_ground_bug(Object& o) {
    auto& script_position = o.state[1];
    o.flags &= ~no_terrain;
    o.width = 10;
    o.height = 16;
    int action = level.data(rom::ground_bug_script + script_position);
    if (!action) {
        script_position = 0;
        action = level.data(rom::ground_bug_script);
    }
    o.vx = action == 1 ? -256 : action == 2 ? 256 : 0;
    o.set_state_word(0, o.state_word(0) + 8);
    o.vy = 512;
    animate_object(o, level.word(rom::ground_bug_animations + action * 2), rom::ground_bug_frames);
    enemy_contact(o, 3, 2);
}

// The wall shooter (6F61). Four phases: blinking in (60 updates), visible and
// firing left on update 100 (120 updates), blinking out (60), hidden (256).
void Game::update_wall_shooter(Object& o) {
    auto& phase = o.state[0];
    auto& phase_timer = o.state[6];
    o.flags |= no_terrain;
    o.width = 12;
    o.height = 20;
    if (phase == 0 || phase == 2) {
        o.mapping = (ticks & 1) ? 0 : rom::wall_shooter_layout;
        if (++phase_timer >= 60) {
            phase_timer = 0;
            ++phase;
        }
    } else if (phase == 1) {
        if (++phase_timer == 100) {
            if (fire_projectile(o.pixel_x(), o.pixel_y() + 6, -512, 0, 0)) {
                sound(Effect::Projectile);
            }
        }
        o.mapping = rom::wall_shooter_layout;
        if (phase_timer >= 120) {
            phase_timer = 0;
            ++phase;
        }
    } else {
        o.mapping = 0;
        if (++phase_timer == 0) {
            phase = 0;
        }
    }
    enemy_contact(o, 2, 2);
}

// The leaping fish (7D25). It jumps up out of the water at 4 px/update, falls
// back under gravity 16 (at most 4 px/update), then hides for 30 updates.
// Flag 0x01: in the air; 0x02: origin stored (word 1, the lowest point).
void Game::update_leaping_fish(Object& o) {
    auto& splash_ticks = o.state[0]; // A splash shows for the first 3 updates.
    auto& hide_ticks = o.state[3];
    o.flags |= no_terrain;
    o.width = 8;
    o.height = 12;
    if (hide_ticks) {
        --hide_ticks;
        o.mapping = 0;
        return;
    }
    if (!(o.flags & 0x01)) {
        if (!(o.flags & 0x02)) {
            o.set_state_word(1, o.pixel_y() - 12);
            o.set_pixel_x(o.pixel_x() + 8);
            o.flags |= 0x02;
        }
        o.vy = -1024;
        o.flags |= 0x01;
        splash_ticks = 3;
        sound(Effect::WaterEnter);
    } else {
        o.vy = std::min(o.vy + 16, 1024);
        if (o.pixel_y() >= o.state_word(1)) {
            o.y = o.state_word(1) * 256;
            o.vy = 0;
            hide_ticks = 30;
            o.flags &= ~0x01;
        }
    }
    animate_object(o, rom::fish_animation, rom::fish_frames);
    if (splash_ticks) {
        --splash_ticks;
        o.mapping = rom::fish_splash_layout;
    }
    enemy_contact(o, 4, 2);
}

// --- Platforms -----------------------------------------------------------------

// A platform following 112 signed (dx, dy) offsets from its origin (table
// 682E), forwards and then backwards (flag 0x02). Riders move along
// horizontally. Words 1 and 3 hold the origin.
void Game::update_path_platform(Object& o) {
    auto& position = o.state[0]; // Byte offset into the table, 0..224.
    o.flags |= no_terrain;
    pending_camera_top = pending_camera_bottom = 64;
    if (o.first_update()) {
        o.set_state_word(1, o.pixel_x());
        o.set_state_word(3, o.pixel_y());
        position = 224;
        o.flags |= 0x02; // Starts at the far end, moving back.
    }
    o.width = 26;
    o.height = 16;
    const int old_x = o.pixel_x();
    o.set_pixel_x(o.state_word(1) + signed8(level.data(rom::platform_path + position)));
    o.set_pixel_y(o.state_word(3) + signed8(level.data(rom::platform_path + 1 + position)));
    if (sonic.vy >= 0 && overlaps(o, 6, 8)) {
        sonic.set_pixel_x(sonic.pixel_x() + o.pixel_x() - old_x);
        land_on_platform(o, 16, 0);
    }
    o.mapping = level.scene ? rom::platform_other_zones : rom::platform_green_hill;
    if (o.flags & 0x02) {
        position -= 2;
        if (!position) {
            o.flags &= ~0x02;
        }
    } else {
        position += 2;
        if (position >= 224) {
            o.flags |= 0x02;
        }
    }
}

// A platform that sinks while ridden until it is half a block down, and rises
// back to block alignment when empty (69ED).
void Game::update_sinking_platform(Object& o) {
    o.flags |= no_terrain;
    o.width = 26;
    o.height = 16;
    o.mapping = level.scene ? rom::platform_other_zones : rom::platform_green_hill;
    if (sonic.vy >= 0 && overlaps(o, 6, 8)) {
        o.vy = (o.pixel_y() & 31) < 16 ? 128 : 0;
        land_on_platform(o, 16, o.vy);
    } else {
        o.vy = (o.pixel_y() & 31) ? -64 : 0;
    }
}

// A platform moving one pixel per update, reversing every 160 updates, which
// carries its rider (6DCA). Word 1 counts the updates; byte 3 the legs.
void Game::update_sliding_platform(Object& o) {
    auto& legs = o.state[3];
    o.flags |= no_terrain;
    if (level.act != 7) {
        pending_camera_top = pending_camera_bottom = 64;
    }
    o.width = 26;
    o.height = 16;
    const bool ridden = sonic.vy >= 0 && overlaps(o, 6, 8);
    if (ridden) {
        land_on_platform(o, 16, 0);
    }
    int count = o.state_word(1) + 1;
    if (count >= 160) {
        count = 0;
        ++legs;
    }
    o.set_state_word(1, count);
    const int dx = (legs & 1) ? -1 : 1;
    o.set_pixel_x(o.pixel_x() + dx);
    if (ridden) {
        sonic.set_pixel_x(sonic.pixel_x() + dx);
    }
    o.mapping = level.scene == 0   ? rom::platform_green_hill
                : level.scene == 1 ? rom::platform_bridge
                                   : rom::platform_other_zones;
}

// A platform moving down at 0.5 px/update that snaps back to its origin (word
// 1) after 192 x 4 updates (7E31). It publishes its height for the paired
// platform.
void Game::update_cycling_platform(Object& o) {
    auto& timer = o.state[0];
    auto& period = o.state[3];
    o.flags |= no_terrain;
    o.width = 12;
    o.height = 16;
    o.mapping = rom::cycling_platform_layout;
    shared_platform_y = o.pixel_y();
    if (o.first_update()) {
        o.set_state_word(1, o.pixel_y());
        period = 192;
        o.vy = 128;
    }
    if (sonic.vy >= 0 && overlaps(o, 6, 8)) {
        land_on_platform(o, 16, o.vy);
    }
    if (!(ticks & 3) && ++timer >= period) {
        timer = 0;
        o.y = o.state_word(1) * 256;
    }
}

// A platform 40 pixels above the cycling platform (7EC8).
void Game::update_paired_platform(Object& o) {
    o.flags |= no_terrain;
    o.width = 26;
    o.height = 16;
    o.mapping = rom::paired_platform_layout;
    o.set_pixel_y(shared_platform_y - 40);
    if (sonic.vy >= 0 && overlaps(o, 6, 8)) {
        land_on_platform(o, 16, o.vy);
    }
}

// A floating log (7EFC-8030). It bobs up and down on a 40-update cycle. A
// rider walking on it rolls it along at half his speed, unless a wall is in
// the way; the log's spin animation (word 1) follows the rider's speed.
void Game::update_floating_log(Object& o) {
    auto& bob_timer = o.state[0];
    o.flags |= no_terrain;
    o.width = 10;
    o.height = 16;
    if (o.first_update()) {
        o.set_pixel_y(o.pixel_y() - 24);
    }
    o.vy = bob_timer >= 20 ? -64 : 64;
    o.mapping = rom::log_layout;
    if (sonic.vy >= 0 && overlaps(o, 6, 8)) {
        land_on_platform(o, 16, o.vy);
        int delta = sonic.vx >> 1;
        const int ahead_x = sonic.pixel_x() + (sonic.vx >= 0 ? 18 : -2);
        if (sonic.vx && (level.block_attributes[level.block_at(ahead_x, sonic.pixel_y())] & 63)) {
            delta = 0;
        }
        o.x = (o.x + delta) & 0xffffff;
        sonic.x = o.x;
        int spin = (o.state_word(1) + std::abs(sonic.vx)) & 65535;
        if ((spin >> 8) >= 9) {
            spin -= 9 * 256;
        }
        o.set_state_word(1, spin);
        o.mapping = rom::log_layout + level.data(rom::log_spin_frames + (spin >> 8));
    }
    if (++bob_timer >= 40) {
        bob_timer = 0;
    }
}

// An invisible 64x64 area that throws a grounded Sonic up and to the right in
// the knocked-back state, used as a scripted launch (7AE3).
void Game::update_launch_trigger(Object& o) {
    o.flags |= no_terrain;
    o.width = o.height = 64;
    if (!sonic.hurt && sonic.grounded() && overlaps(o, 0, 0)) {
        sonic.vy = -1280;
        sonic.vx = 768;
        sonic.flags &= ~facing_left;
        sonic.hurt = true;
        sound(Effect::RingLoss);
    }
}

// Animated flower tiles in the background (7B29). Each phase (table 7BC1, four
// bytes per phase) names the frames for even and odd updates and its length.
void Game::update_flowers(Object& o) {
    auto& phase_timer = o.state[0]; // Counted in pairs of updates.
    auto& phase = o.state[1];
    o.flags |= no_terrain;
    if (o.first_update()) {
        phase_timer = 50;
        phase = 0;
    }
    const int parity = ticks & 1;
    o.flower_frames[parity] = level.data(rom::flower_phases + phase * 4 + parity);
    if (!parity && --phase_timer == 0) {
        phase_timer = level.data(rom::flower_phases + 2 + phase * 4);
        phase = (phase + 1) & 3;
    }
}

// The goal sign (61F8-64A7).
//
// Touching it starts a spin whose speed depends on how fast Sonic was running.
// The spin also lifts the sign, which slows down, falls and lands. Once it has
// landed, Sonic runs out of the act by himself and the face the sign stopped on
// is awarded (see Game::tick). The face depends on rings and deaths, not chance:
// 50 rings or more (outside the last two zones) gives a special stage.
//
// Sign flags: 0x01 initialised, 0x02 spinning, 0x04 falling, 0x08 landed,
// 0x80 speed bonus paid. State word 3 is the spin speed; bytes 5 and 6
// accumulate the spin.
void Game::update_goal_sign(Object& o) {
    auto& s = o.state;
    auto& sign_flags = o.state[0];
    auto& frame = o.state[1];
    auto& sonic_was_grounded = o.state[2];
    o.width = 24;
    o.height = 48;
    if (!(sign_flags & 0x01)) {
        sign_flags |= 0x01;
        horizontal_scroll = false;
        goal.rings = rings;
        level.object_patterns = level.goal_patterns;
        sprite_palette_kind = 2;
        std::copy(level.goal_palette.begin() + 34, level.goal_palette.end(),
                  level.palette.begin() + 34);
    }
    // The camera stops 112 pixels before the sign.
    level.camera_left = camera.x;
    level.camera_right = o.pixel_x() - 112;
    pending_camera_top = 112;
    pending_camera_bottom = 120;

    // Sonic landing near the sign makes it hop.
    const int old_ground = sonic_was_grounded;
    sonic_was_grounded = sonic.flags & grounded;
    if (sonic_was_grounded && sonic_was_grounded != old_ground && o.grounded() &&
        std::abs(sonic.pixel_x() - o.pixel_x()) < 100) {
        o.vy = -512;
    }
    o.vy += 26;

    if (!(sign_flags & 0x08)) {
        if (sign_flags & 0x04) {
            if (o.grounded()) {
                sign_flags = (sign_flags & ~0x04) | 0x08;
                finish_ticks = 160;
                auto_run = true;
                music(Music::ActClear);
                sound(Effect::SignLand);
            }
        } else if (overlaps(o, 10, 10) && o.vy >= 0 && !(sign_flags & 0x02)) {
            // Touched: spin at twice Sonic's speed (at most 3 px/update).
            goal.entry_speed = std::abs(sonic.vx);
            o.set_state_word(3, std::min(goal.entry_speed, 768) * 2);
            frame = 0;
            sign_flags |= 0x02;
            goal.started = true;
        }
    }

    int sequence = (sign_flags & 0x06) ? rom::goal_spin : rom::goal_idle;
    if ((sign_flags & 0x08) && !(sign_flags & 0x06)) {
        // Landed: choose the face (6345-636D) from the death count (180E) and
        // the rings held when the sign first became active.
        goal.outcome = level.act >= 28                      ? 1
                       : level.act < 12 && goal.rings >= 50 ? 4
                       : death_count == 2                   ? 3
                       : death_count >= 3                   ? 2
                                                            : 1;
        sequence = goal.outcome == 4   ? rom::goal_face_special
                   : goal.outcome == 3 ? rom::goal_face_rings
                   : goal.outcome == 2 ? rom::goal_face_life
                                       : rom::goal_face_none;
        // Speed bonus for touching the sign within certain speed windows. The
        // tables hold the points as packed BCD.
        const int integer = goal.entry_speed / 256;
        const int fraction = goal.entry_speed & 255;
        goal.bonus_table = integer >= 4   ? rom::goal_speed_bonus(1)
                           : integer == 1 ? (fraction < 0x60   ? 0
                                             : fraction < 0xa0 ? rom::goal_speed_bonus(0)
                                                               : rom::goal_speed_bonus(3))
                           : integer == 2 ? (fraction < 0x80   ? rom::goal_speed_bonus(1)
                                             : fraction < 0xa0 ? rom::goal_speed_bonus(2)
                                                               : 0)
                                          : 0;
        if (goal.bonus_table && !(sign_flags & 0x80)) {
            auto decimal = [](int bcd) {
                return (bcd >> 4) * 10 + (bcd & 15);
            };
            add_score(decimal(level.data(goal.bonus_table + 4)) * 10000 +
                      decimal(level.data(goal.bonus_table + 6)) * 100 +
                      decimal(level.data(goal.bonus_table + 5)));
            sign_flags |= 0x80;
        }
    }
    if (level.data(sequence + frame) == 255) {
        frame = level.data(sequence + frame + 1);
    }
    o.mapping = rom::goal_frames + 18 * level.data(sequence + frame);
    if (!(sign_flags & 0x02)) {
        ++frame;
        return;
    }

    // Spinning: advance the animation by the spin speed, rise by speed / 32,
    // and slow the spin by 16/256 per update until it stops.
    const int speed = o.state_word(3);
    const int low = (speed & 255) + s[5];
    s[5] = low;
    const int high = (speed >> 8) + s[6] + (low >> 8);
    s[6] = high;
    const int next_frame = frame + (speed >> 8) + (low >> 8);
    frame = next_frame >= 24 ? 0 : next_frame;
    if (o.vy <= -1024) {
        return;
    }
    o.vy -= speed / 32;
    if (o.pixel_y() >= 8) {
        o.set_state_word(3, speed - 16);
        if (speed >= 16) {
            return;
        }
    }
    o.vy = 0;
    sign_flags = (sign_flags & ~0x02) | 0x04;
    frame = 0;
}

} // namespace sonic1_gg
