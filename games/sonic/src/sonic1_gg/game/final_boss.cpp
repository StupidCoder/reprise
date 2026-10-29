// The final boss room (Sky Base 3), its orb and floor flames, and the two
// ending objects: the flyby after the escape and the emeralds of the good
// ending.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

// Submits a sprite layout of up to 3 rows x 6 tiles as direct sprites
// (tile 0xFE/0xFF = no sprite; a row starting with 0xFF ends the layout).
void Game::submit_object_mapping(Object& o, int address, int x, int y) {
    for (int row = 0; row < 3; ++row) {
        if (level.data(address + row * 6) == 255) {
            break;
        }
        for (int col = 0; col < 6; ++col) {
            const int tile = level.data(address + row * 6 + col);
            if (tile < 254) {
                o.extra_sprites.push_back({tile, word(x + col * 8), word(y + row * 16)});
            }
        }
    }
}

// The final boss (bank 2, B60E-BA2C).
//
// The villain sits in a machine behind a barrier at the right of the room and
// cannot be touched. Sonic damages it by hitting the wall at x = 1016 while
// curled, which bounces him back. Twelve hits open the room. The villain then
// flees right, matching Sonic's speed, jumps over obstacles and drops into the
// escape shaft; a fade follows.
//
// Attack phases: 0 idle, 1 hop, 2 fall, 3 fire an orb. State words 0 and 2
// hold the base position; after the defeat byte 0 counts the barrier tile
// patches instead. Flags: 0x01 initialised, 0x02 firing pose, 0x04 defeated
// (fleeing), 0x08 reached the shaft.
void Game::update_final_boss(Object& o) {
    auto& s = o.state;
    auto& attack_phase = o.state[4];
    auto& phase_timer = o.state[5];
    auto& pose = o.state[6];
    o.width = 30;
    o.height = 47;
    o.flags |= no_terrain;
    // Pose table at BA2D: (dx, dy, mapping word) relative to the base position.
    auto apply_pose = [&] {
        const int at = rom::final_boss_poses + pose * 4;
        o.mapping = level.word(at + 2);
        o.set_pixel_x(o.state_word(0) + level.data(at));
        o.set_pixel_y(o.state_word(2) + level.data(at + 1));
    };
    auto fall = [&] {
        o.vy = signed24(o.vy + 14);
        if (o.vy >= 512) {
            o.vy = (o.vy & 0xff0000) | 512;
        }
    };

    if (!(o.flags & 0x04)) {
        // --- The fight ---
        stop_streaming_in_arena();
        // The hit test uses this fixed wall position, not the sprite (B7CB).
        if (sonic.pixel_x() >= 1016) {
            sonic.vx = 0;
        }
        if (!flash.ticks && !sonic.dead && (sonic.flags & curled) && sonic.pixel_x() >= 1016) {
            sonic.vx = -768;
            flash.ticks = 24;
            flash.color = 0;
            flash.value = 0xfff;
            sound(Effect::Break);
            ++boss.hits;
        }
        if (o.first_update()) {
            level.object_patterns = level.boss_patterns;
            lock_boss_arena(864, 312);
            o.set_pixel_x(o.pixel_x() + 8);
            o.set_state_word(0, o.pixel_x());
            o.set_pixel_y(o.pixel_y() + 16);
            o.set_state_word(2, o.pixel_y());
            boss.hits = 0;
            music(Music::FinalBoss);
            boss.final_barrier = true;
        }
        switch (attack_phase) {
        case 0: // Idle, alternating two poses every 8 updates for 40 x 8 updates.
            apply_pose();
            if (!(ticks & 7)) {
                if (phase_timer >= 28 || ++pose >= 2) {
                    pose = 0;
                }
                if (++phase_timer >= 40) {
                    phase_timer = 0;
                    ++attack_phase;
                }
            }
            break;
        case 1: // Hop.
            o.vy = -448;
            ++attack_phase;
            o.set_pixel_x(o.state_word(0) + 4);
            o.mapping = rom::final_boss_hop_layout;
            break;
        case 2: // Fall back.
            fall();
            o.mapping = rom::final_boss_hop_layout;
            if (word(o.pixel_y() - 1) >= o.state_word(2)) {
                o.set_pixel_y(o.state_word(2));
                phase_timer = 0;
                o.vy = 0;
                ++attack_phase;
            }
            break;
        case 3: // Fire an orb (one at a time), then return to idle.
            o.set_pixel_x(o.state_word(0));
            if (!phase_timer && !boss.orb_active) {
                if (auto* orb = allocate_object(type::final_boss_orb)) {
                    orb->x = 968 * 256;
                    orb->y = 351 * 256;
                    orb->state[0] = 0;
                    orb->flags = 0;
                    orb->vx = orb->vy = 0;
                }
            }
            pose = 2;
            o.flags |= 0x02;
            apply_pose();
            if (++phase_timer >= 18) {
                o.flags &= ~0x02;
                attack_phase = phase_timer = 0;
            }
            break;
        }
        // The machine's two-tile light, drawn directly.
        const int at = (o.flags & 0x02) ? rom::final_boss_light_firing : rom::final_boss_light;
        const int x = level.word(at) + level.word(at + 4);
        const int y = level.word(at + 2) + level.word(at + 6);
        o.extra_sprites.push_back({level.data(at + 8), word(x), word(y)});
        o.extra_sprites.push_back({level.data(at + 9), word(x + 8), word(y)});
        if (boss.hits >= 12) {
            s[0] = phase_timer = pose = 0;
            o.flags |= 0x04;
            boss.final_barrier = false;
            music(Music::ScrapBrain);
            sound(Effect::BossDefeated);
        }
        return;
    }

    if (!(o.flags & 0x08)) {
        // --- The escape ---
        o.flags &= ~no_terrain;
        // Fifteen tile patches remove the barrier, one per update.
        auto& barrier_patches = s[0];
        if (barrier_patches < 15) {
            const int at = rom::barrier_patches + barrier_patches * 12;
            queue_tile_patch(level.word(at), level.word(at + 2), at + 4);
            if (++barrier_patches == 15) {
                terrain_streaming = true;
                level.camera_right = 1360;
            }
        }
        // Runs right just ahead of Sonic (same rule as the Scrap Brain chase).
        if (o.pixel_x() > 1504) {
            o.vx = 0;
        } else {
            const int speed =
                (sonic.vx & 32768) || word(o.pixel_x() - sonic.pixel_x()) >= 64 ? -128 : sonic.vx;
            o.vx = word(speed + 256);
        }
        if (pose == 6 && phase_timer == 1 && o.grounded()) {
            o.vy = -256;
        }
        // Jumps over terrain ahead.
        if ((level.block_attributes[level.block_at(o.pixel_x() + 54, o.pixel_y() + 23)] & 63) &&
            o.grounded()) {
            o.vy = -640;
        }
        if (level.block_at(o.pixel_x() + 8, o.pixel_y()) == 0x49 && o.grounded()) {
            // Reached the shaft: lock the camera and fade the music.
            phase_timer = pose = 0;
            o.vx = 0;
            o.set_state_word(0, 1504);
            o.set_state_word(2, 352);
            lock_boss_arena(1360, 288);
            sound_events.push_back({SoundEvent::Kind::Fade, 8});
            o.flags |= 0x08;
        } else {
            fall();
            animate_object(o, rom::villain_run_animation, rom::villain_frames);
            return;
        }
    }

    // --- In the shaft: a short animation, then the villain disappears ---
    current_input = {};
    apply_pose();
    if (phase_timer < 48) {
        const int old = phase_timer;
        if (!(ticks & 7)) {
            pose = (pose + 1) & 1;
            ++phase_timer;
        }
        if (old >= 44) {
            o.mapping = rom::villain_shaft_layout;
        }
    } else {
        o.mapping = 0;
        if (++phase_timer >= 112) {
            o.type = type::none;
        }
    }
}

// The boss's orb (BCF2-BE02). It waits 200 updates, then homes in on Sonic
// with a small acceleration on each axis, and disappears near the screen edges,
// on contact, or when the barrier falls. The right-bound test at BD38 adds the
// object's own X instead of the constant 00D0 it loaded; kept as executed.
void Game::update_final_boss_orb(Object& o) {
    auto& wait = o.state[0];
    o.flags |= no_terrain;
    o.width = o.height = 8;
    boss.orb_active = true;
    bool remove = false;
    if (overlaps(o, 4, 4)) {
        if (!sonic.dead) {
            sonic.take_hit(*this);
        }
        remove = true;
    } else if (wait < 200) {
        ++wait;
    } else {
        if (word(camera.x + 40) >= o.pixel_x() || camera.x + o.pixel_x() > 65535) {
            remove = true;
        } else {
            int vx = o.vx;
            if (sonic.pixel_x() < o.pixel_x()) {
                vx += vx < 0 ? -8 : -16;
            } else {
                vx += vx >= 0 ? 8 : 16;
            }
            o.vx = signed24(vx);
            if (word(camera.y + 16) >= o.pixel_y() || word(camera.y + 168) < o.pixel_y()) {
                remove = true;
            } else {
                int vy = o.vy;
                if (sonic.pixel_y() < o.pixel_y()) {
                    vy += vy < 0 ? -5 : -2;
                } else {
                    vy += vy >= 0 ? 5 : 2;
                }
                o.vy = signed24(vy);
            }
        }
    }
    if (!remove) {
        animate_object(o, rom::orb_animation, rom::orb_frames);
    }
    if (remove || !boss.final_barrier) {
        o.type = type::none;
        boss.orb_active = false;
    }
}

// Floor flames of the final room (BB95-BC3B): like the flame jets, but each
// one moves through three positions (table BC6C), one per 256-update cycle.
void Game::update_final_boss_flame(Object& o) {
    auto& cycle = o.state[0];
    auto& position = o.state[1];
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_pixel_x(o.pixel_x() + 12);
    }
    if (!boss.final_barrier) {
        o.type = type::none;
        return;
    }
    const int phase = cycle >> 4;
    o.width = 6;
    o.height = level.data(rom::floor_flame_heights + phase);
    int table = level.word(rom::floor_flame_sprites + phase * 2);
    if (table) {
        table += (cycle * 8) & 31;
        for (int i = 0; i < 4; ++i) {
            o.extra_sprites.push_back({level.data(table + i * 2), o.pixel_x(),
                                       word(o.pixel_y() + level.data(table + i * 2 + 1))});
        }
        if (o.height && overlaps(o, 2, 10)) {
            sonic.take_hit(*this);
        }
    }
    ++cycle;
    o.mapping = 0;
    if (!cycle) {
        o.flags &= ~0x01;
        if (++position >= 3) {
            position = 0;
        }
        o.set_pixel_x(level.word(rom::floor_flame_positions + position * 2));
    } else if (cycle == 112) {
        sound(Effect::FlameJet);
    }
}

// The emeralds of the good ending (BF53). After Sonic's pose they wait 80
// updates and then rise out of view, which ends the act.
void Game::update_ending_emerald(Object& o) {
    auto& timer = o.state[0];
    o.flags |= no_terrain;
    const unsigned offset = (ticks & 15) * 8;
    std::copy_n(level.emerald_patterns.begin() + offset, 8,
                level.object_patterns.begin() + 0xb80 + offset);
    if (!(o.flags & 0x01)) {
        o.mapping = 0;
        o.vx = 0;
        if (++timer >= 80) {
            o.flags |= 0x01;
            timer = 100;
        }
        return;
    }
    if (timer) {
        --timer;
    } else {
        o.vy = -128;
    }
    // Drawn through the reserved sprites on odd updates, not as a mapping.
    if (ticks & 1) {
        submit_object_mapping(o, rom::ending_emerald_layout, o.pixel_x(), o.pixel_y());
    }
    if (word(o.pixel_y() + 32) < camera.y) {
        finish_ticks = 1;
        area_advance = true;
    }
}

// The flyby after the escape (BE03). The villain's craft flies right; Sonic
// drops in from above and hits it, which is played out with the ordinary
// movement and collision code (not a movie). After ten explosions the good
// ending continues with the emeralds if all six were collected.
void Game::update_ending_flyby(Object& o) {
    auto& explosion_timer = o.state[0];
    auto& drop_delay = o.state[1];
    auto& explosions = o.state[2];
    o.flags |= no_terrain;
    o.flame_tile = -1;
    current_input = {};
    if (!(o.flags & 0x02)) {
        sprite_palette_kind = 1;
        ending.sonic_hidden = true;
        sonic.set_pixel_y(0);
        drop_delay = 255;
        ending.camera_scripted = true;
        o.flags |= 0x02;
    }
    if (!(ticks & 1) && drop_delay && !--drop_delay) {
        sonic.set_pixel_x(o.pixel_x() + 60);
        sonic.set_pixel_y(o.pixel_y() - 64);
        ending.sonic_hidden = false;
        ending.drop = true;
        sound(Effect::Roll);
    }
    o.width = 32;
    o.height = 28;
    o.vx = 256;
    o.vy = 0;
    if (ending.camera_scripted && word(camera.x + 64) < o.pixel_x()) {
        ++camera.x;
    }
    o.mapping = rom::flyby_layout;
    if (!(o.flags & 0x01) && overlaps(o, 8, 16)) {
        sonic.vy = signed24(-sonic.vy);
        ending.camera_scripted = false;
        o.flags |= 0x01;
        explosion_timer = 1;
        sound(Effect::Break);
    }
    if (!(ticks & 1)) {
        o.flame_tile = (ticks & 2) + 4;
        o.flame_x = word(o.pixel_x() - 8);
        o.flame_y = word(o.pixel_y() + 16);
    }
    if (!(o.flags & 0x01)) {
        return;
    }
    // Hit: sink slowly and explode every 24 updates.
    o.vy = 64;
    o.mapping = rom::flyby_hit_layout;
    if (--explosion_timer) {
        return;
    }
    spawn_explosion(o);
    explosion_timer = 24;
    if (++explosions < 10) {
        return;
    }
    if (progress.emerald_count >= 6) {
        ending.collect = true;
    } else if (!finish_ticks) {
        finish_ticks = 32;
        area_advance = true;
    }
}

} // namespace sonic1_gg
