// Jungle zone: the falling platform and the Jungle boss with its bombs.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

// A platform that falls a moment after being stood on (bank 1, 6A55-6B25).
// Once touched it counts 24 updates and then falls. Below the screen it
// returns to its stored origin (word 0) and stays hidden (flag 0x02) until
// that origin is above the view.
void Game::update_falling_platform(Object& o) {
    auto& touched = o.state[5];      // 1 once Sonic has stood on it
    auto& fall_counter = o.state[6]; // Counts up while touched
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_state_word(0, o.pixel_y());
    }
    if (o.flags & 0x02) {
        if (word(top_edge(-16)) < o.pixel_y()) {
            o.mapping = 0;
            return;
        }
        o.flags &= ~0x02;
    }
    fall_counter += touched;
    if (fall_counter >= 24) {
        o.vy = signed24(o.vy + 64);
    }
    o.width = 26;
    o.height = 16;
    if (sonic.vy >= 0 && overlaps(o, 6, 8)) {
        touched = 1;
        land_on_platform(o, 16, signed16(o.vy));
    }
    o.mapping = level.scene ? rom::platform_other_zones : rom::platform_green_hill;
    // Widescreen margins (zero on the LCD) extend the reset distance.
    if (o.pixel_y() > word(bottom_edge(168))) {
        o.vy = 0;
        touched = fall_counter = 0;
        o.y = o.state_word(0) * 256;
        o.flags |= 0x02;
    }
}

// The Jungle boss's bomb (bank 2, 822D-82D5). It slides with friction, falls,
// and explodes when its fuse runs out (the fuse counter advances every second
// update); the explosion still hurts. The friction can cross zero on the
// fraction byte; the ROM does not clamp it.
void Game::update_boss_bomb(Object& o) {
    auto& fuse_counter = o.state[0];
    auto& fuse_length = o.state[1];
    o.flags &= ~no_terrain;
    o.width = 12;
    o.height = 16;
    if (overlaps(o, 2, 2)) {
        sonic.take_hit(*this);
    }
    o.vx = signed24(o.vx + (o.vx < 0 ? 2 : -2));
    o.vy = signed24(o.vy + 32);
    if (((o.vy >> 8) & 255) >= 3) {
        o.vy = 768;
    }
    fuse_counter += ticks & 1;
    if (fuse_counter < fuse_length) {
        animate_object(o, rom::bomb_fuse_animation, rom::bomb_fuse_frames);
        return;
    }
    if (fuse_counter == fuse_length) {
        if (!(ticks & 1)) {
            return;
        }
        o.state[5] = 0; // Restart the animation timer for the explosion.
        sound(Effect::Break);
    }
    o.vx = 0;
    animate_object(o, rom::bomb_burst_animation, rom::bomb_frames);
    if (fuse_counter > ((fuse_length + 18) & 255)) {
        o.type = type::none;
    }
}

// The Jungle boss (bank 2, 806B-8208). It appears once Sonic stands on the
// ground above y = 224, descends, then patrols left and right; at each end it
// stops, bobs and drops a bomb with a random fuse. The early returns before
// the hit test, and the return when no slot is free for the bomb, are kept.
void Game::update_jungle_boss(Object& o) {
    auto& boss_flags = o.state[0]; // 0x01 descended, 0x02 heading right
    auto& pause_timer = o.state[1];
    o.flags |= no_terrain;
    o.width = 32;
    o.height = 28;
    o.flame_tile = -1;
    if (!(o.flags & 0x01)) {
        if (sonic.pixel_y() >= 224 || !sonic.grounded()) {
            return;
        }
        start_boss_fight(480, 80);
        o.flags |= 0x01;
    }
    stop_streaming_in_arena();
    if (!(boss_flags & 0x01)) {
        o.mapping = rom::jungle_boss_layout_left;
        o.vy = 128;
        if (o.pixel_y() < 112) {
            return;
        }
        o.vy = 0;
        boss_flags |= 0x01;
    }
    if (!pause_timer) {
        if (!(boss_flags & 0x02)) {
            o.mapping = rom::jungle_boss_layout_left;
            o.flags &= ~facing_left;
            o.vx = -128;
            if (o.pixel_x() < 540) {
                pause_timer = 87;
            }
        } else {
            o.mapping = rom::jungle_boss_layout_right;
            o.flags |= facing_left;
            o.vx = 128;
            if (o.pixel_x() >= 648) {
                pause_timer = 87;
            }
        }
    } else {
        o.vx = 0;
        --pause_timer;
        if (pause_timer == 52) {
            boss_flags ^= 0x02;
            if (boss.hits < 8) {
                auto* bomb = allocate_object(type::boss_bomb);
                if (!bomb) {
                    return;
                }
                bomb->x = word(o.pixel_x() + 11) * 256;
                bomb->y = word(o.pixel_y() + 48) * 256;
                bomb->vx = bomb->vy = 0;
                bomb->state[0] = bomb->state[5] = bomb->state[6] = 0;
                bomb->state[1] = (next_random() & 63) + 100;
            }
            // The vertical speed is left unchanged on the drop update (8192).
        } else {
            o.vy = !pause_timer ? 0 : pause_timer >= 56 ? 256 : pause_timer < 32 ? -256 : 0;
        }
    }
    update_boss_contact(o, 0x5a);
    submit_engine_flame(o);
}

} // namespace sonic1_gg
