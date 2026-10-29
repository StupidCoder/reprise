// Bridge zone: collapsing bridges, the spiked walker, the seesaw, the rising
// platform and the Bridge boss. Also the shared nametable patch slot.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

// Queues a 2x2 tile patch of the background nametable (3282). The original has
// a single slot for this (D2AB-D2AF), released by the next video interrupt
// (0157-0161); ring collection, collapsing planks and the capsule all use it,
// and a plank waits until the slot is free. The tiles are stored as a
// persistent override of the level's tile map; only patches within the
// rendered area take effect, as in the original. Widescreen: that area grows
// by the view's margins, or the capsule's base appeared foot by foot in view.
void Game::queue_tile_patch(int x, int y, int table) {
    tile_patch.x = word(x);
    tile_patch.y = word(y);
    tile_patch.table = table;
    const int sx = (x & ~7) - (camera.x & ~7);
    const int sy = (y & ~7) - (camera.y & ~7);
    if (sx < 8 - wide_left || sx >= 256 + wide_right || sy < -wide_top || sy >= 192 + wide_bottom) {
        return;
    }
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            level.tile_overrides[(y / 8 + row) * level.columns * 4 + x / 8 + col] =
                level.word(table + (row * 2 + col) * 2);
        }
    }
}

// Terrain actions 24-26 (5AD8-5BA9): stepping on a bridge block replaces half
// of it by a falling plank object, but only if an object slot is free. Action
// 25 only collapses the right half, 26 only the left half, 24 either.
void Game::collapse_bridge(int action) {
    if (!sonic.grounded()) {
        return;
    }
    const int x = word(sonic.pixel_x() + 8);
    const int y = word(sonic.pixel_y() + 16);
    const bool right = (x & 31) >= 16;
    if ((action == 25 && !right) || (action == 26 && right)) {
        return;
    }
    // Allocation keeps the slot's other state bytes; the plank sets its own
    // random delay.
    if (auto* plank = allocate_object(type::collapsing_plank)) {
        plank->x = ((x & ~31) + (right ? 16 : 0)) * 256;
        plank->y = (y & ~31) * 256;
        plank->vx = plank->vy = 0;
        plank->flags = 0;
        auto& block = level.layout.at((y / 32) * level.columns + x / 32);
        // Collision changes at once, but the old bridge tiles stay visible until
        // the plank's timer erases them. Keep that picture as tile overrides.
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                const unsigned position =
                    ((y / 32) * 4 + row) * level.columns * 4 + (x / 32) * 4 + col;
                unsigned entry = level.blocks[block * 16 + row * 4 + col];
                if (level.block_attributes[block] & 128) {
                    entry |= 0x1000; // Foreground priority.
                }
                level.tile_overrides.try_emplace(position, entry);
            }
        }
        // Block 0x8A is a bridge without its right half, 0x89 one without its
        // left half; losing the second half leaves an empty block.
        block = right ? (block == 0x89 ? 0 : 0x8a) : (block == 0x8a ? 0 : 0x89);
    }
}

// The spiked walker (bank 2, 82FB-8388). It has two separate contacts: the
// spikes on its back (a 16x14 box) always hurt, the body below is an ordinary
// enemy contact. It walks left for 80 x 8 updates, then right for 80 x 8.
void Game::update_spiked_walker(Object& o) {
    auto& walk_timer = o.state[0];
    o.width = 16;
    o.height = 14;
    if (overlaps(o, 8, 6)) {
        sonic.take_hit(*this);
    }
    o.width = 20;
    o.height = 32;
    enemy_contact(o, 6, 16, 4, 4);
    o.vy = signed24(o.vy + 32);
    o.vx = walk_timer < 80 ? -64 : 64;
    animate_object(o, walk_timer < 80 ? rom::walker_walk_left : rom::walker_walk_right,
                   rom::walker_frames);
    if (!(ticks & 7) && ++walk_timer >= 160) {
        walk_timer = 0;
    }
}

// A collapsing bridge plank (bank 2, 83D6-8495). It waits a random 1..32
// updates; then, as soon as the tile patch slot is free, it erases the bridge
// tiles and falls (at most 4 px/update), still carrying Sonic. Flags: 0x01
// falling, 0x02 delay chosen.
void Game::update_collapsing_plank(Object& o) {
    auto& delay = o.state[0];
    o.flags |= no_terrain;
    o.width = 14;
    o.height = 12;
    int dy = 0;
    if (!(o.flags & 0x01)) {
        o.mapping = 0;
        if (!(o.flags & 0x02)) {
            delay = (next_random() & 31) + 1;
            o.flags |= 0x02;
        }
        if (!--delay) {
            delay = 1;
            if (tile_patch.x & 0x8000) { // Slot free.
                queue_tile_patch(o.pixel_x(), word(o.pixel_y() + 14), rom::plank_erase_patch);
                o.flags |= 0x01;
                sound(Effect::PlankFall);
            }
        }
    }
    if (o.flags & 0x01) {
        o.mapping = rom::plank_layout;
        o.vy = signed24(o.vy + 32);
        if (((o.vy >> 8) & 255) >= 4) {
            o.vy = (o.vy & ~0xff00) | 0x400;
        }
        dy = signed16(o.vy);
        if (o.pixel_y() >= word(bottom_edge(256))) {
            o.type = type::none;
            return;
        }
    }
    if (overlaps(o, 2, 4) && sonic.vy >= 0) {
        land_on_platform(o, 16, dy);
    }
}

// A platform accelerating up for 80 updates and down for 80, at most
// 2 px/update (bank 2, B4E8-B58E).
void Game::update_rising_platform(Object& o) {
    auto& timer = o.state[0];
    o.flags |= no_terrain;
    o.mapping = rom::rising_platform_layout;
    o.vy = signed24(o.vy + (timer < 80 ? 16 : -16));
    const int speed = signed16(o.vy);
    if (std::abs(speed) >= 512) {
        o.vy = speed < 0 ? -512 : 512;
    }
    if (sonic.vy >= 0) {
        o.width = 30;
        o.height = 28;
        if (overlaps(o, 2, 8)) {
            land_on_platform(o, 16, signed16(o.vy));
        }
    }
    if (++timer >= 160) {
        timer = 0;
    }
}

// The seesaw (bank 2, 8681-8844). Sonic lands on a plank whose other end
// carries a weight, simulated as a separate falling mass. Landing pushes the
// plank down by Sonic's falling speed; when the weight comes down, it throws
// him back up.
//
// State: the plank's depression (0..28 px), word 1 the weight's velocity,
// bytes 3..5 its 24-bit position. Flags: 0x01 initialised, 0x02 Sonic is on
// the plank.
void Game::update_seesaw(Object& o) {
    auto& s = o.state;
    auto& depression = o.state[0];
    o.flags |= no_terrain;
    if (o.first_update()) {
        depression = 28;
        o.set_pixel_x(o.pixel_x() - 16);
    }
    const int weight = signed24(s[3] + (s[4] << 8) + (s[5] << 16) + signed16(o.state_word(1)));
    s[3] = weight;
    s[4] = weight >> 8;
    s[5] = weight >> 16;
    const int velocity = word(o.state_word(1) + 56); // Gravity on the weight.
    o.set_state_word(1, velocity);
    if (!(velocity & 32768) && weight >= 0) {
        bool rest = !depression;
        if (depression) {
            if (o.flags & 0x02) {
                // The plank throws its rider.
                sonic.vy = velocity ? word(-velocity - sonic.previous_gravity) - 65536
                                    : -sonic.previous_gravity;
            }
            const int displacement = (weight >> 8) & 255;
            depression = 28 - displacement;
            rest = displacement >= 28;
            if (rest && (o.flags & 0x02)) {
                sound(Effect::Spring);
            }
        }
        if (rest) {
            depression = s[1] = s[2] = s[3] = s[5] = 0;
            s[4] = 28;
        }
    }
    // The renderer draws the pose captured here, before contact changes it.
    o.bridge_render = {depression, signed16(o.state_word(4))};
    o.flags &= ~0x02;
    o.width = 20;
    o.height = (depression + 8) & 255;
    int inset_y = (depression + 4) & 255;
    if (overlaps(o, 2, inset_y)) {
        o.flags |= 0x02;
        if (sonic.vy < 0) {
            return;
        }
        if (depression != 28) {
            // Landing pushes the plank down by Sonic's falling speed.
            o.set_state_word(1, -sonic.vy);
            depression += ((sonic.vy >> 8) & 255);
            if (depression < 28) {
                sonic.set_pixel_y(o.pixel_y() + 16 + ((inset_y - 4) & 255) - sonic.height);
                sonic.flags |= grounded;
                return;
            }
            depression = 28;
        }
    } else {
        // The raised end of the plank.
        if (sonic.vy < 0) {
            return;
        }
        o.width = 60;
        o.height = (36 - depression) & 255;
        inset_y = (32 - depression) & 255;
        if (!overlaps(o, 42, inset_y)) {
            return;
        }
    }
    sonic.vy = -sonic.previous_gravity;
    sonic.set_pixel_y(o.pixel_y() + 16 + ((inset_y - 4) & 255) - sonic.height);
    sonic.flags |= grounded;
}

// The Bridge boss (bank 2, 84AB-8646). It rises from one of two positions
// chosen at random, waits 100 updates, fires three shots in a spread toward
// Sonic's side, sinks and repeats.
void Game::update_bridge_boss(Object& o) {
    auto& phase = o.state[0];
    auto& wait_timer = o.state[1];
    o.flags |= no_terrain;
    o.width = 30;
    o.height = 28;
    o.flame_tile = -1;
    stop_streaming_in_arena();
    o.mapping = rom::bridge_boss_layout;
    if (o.first_update()) {
        start_boss_fight(1488, 776);
    }
    if (!phase) {
        // Pick a starting position.
        const int address = rom::bridge_boss_starts + (next_random() & 1) * 4;
        o.set_pixel_x(level.word(address));
        o.set_pixel_y(level.word(address + 2));
        ++phase;
    } else if (phase == 1) {
        // Rise.
        o.vy = -128;
        if (o.pixel_y() <= 880) {
            ++phase;
            wait_timer = 0;
        }
    } else if (phase == 2) {
        // Wait, then fire.
        o.vy = 0;
        if (++wait_timer == 100) {
            ++phase;
            if (boss.hits < 8) {
                const int address = sonic.pixel_x() < o.pixel_x() ? rom::bridge_boss_shots_left
                                                                  : rom::bridge_boss_shots_right;
                const int x = word(o.pixel_x() + level.word(address));
                const int y = word(o.pixel_y() + level.word(address + 2));
                for (unsigned i = 0; i < 3; ++i) {
                    if (auto* shot = allocate_object(type::projectile)) {
                        shot->x = x * 256;
                        shot->y = y * 256;
                        shot->state[0] = 0;
                        shot->state[2] = 16; // Gravity.
                        for (unsigned n = 3; n < 7; ++n) {
                            shot->state[n] = 0;
                        }
                        shot->vx = signed16(level.word(address + 4 + i * 4));
                        shot->vy = signed16(level.word(address + 6 + i * 4));
                    }
                }
                sound(Effect::Break);
            }
        }
    } else {
        // Sink.
        o.vy = 128;
        if (o.pixel_y() > 976) {
            phase = 0;
        }
    }
    update_boss_contact(o, 0xa2);
    submit_lift_jet(o);
}

} // namespace sonic1_gg
