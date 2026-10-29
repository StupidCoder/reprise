// The random generator, explosions, the Green Hill boss, the shared boss hit
// and defeat logic, and the animal capsule that ends each zone.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

// The game's random generator (0688): seed = seed * 3, then the two bytes are
// mixed and 0x54 added. Byte arithmetic matters: the two byte sums wrap
// independently, and the final addition may carry into the high byte. The
// result is the new high byte. The same generator is also advanced once per
// video interrupt (Game::video_interrupt), so the sequence depends on timing.
unsigned Game::next_random() {
    const unsigned tripled = (random_seed * 3) & 65535;
    const unsigned high = ((tripled & 255) + (tripled >> 8)) & 255;
    const unsigned low = (high + (tripled & 255)) & 255;
    random_seed = ((high << 8) + low + 0x54) & 65535;
    return random_seed >> 8;
}

// An explosion at a random offset of up to 31 pixels from `parent` (7A76). A
// failed allocation consumes neither random numbers nor the sound.
void Game::spawn_explosion(const Object& parent) {
    if (auto* o = allocate_object(type::explosion)) {
        const int x = next_random() & 31;
        const int y = next_random() & 31;
        o->x = word(parent.pixel_x() + x) * 256;
        o->y = word(parent.pixel_y() + y) * 256;
        o->state[0] = o->state[5] = o->state[6] = 0;
        o->vx = o->vy = 0;
        sound(Effect::Break);
    }
}

// The Green Hill boss (7065-7299).
//
// The craft follows a bytecode script: each byte is an action, 0 jumps to the
// position in the next byte. Flag bits 0x02/0x04 of boss_flags alternate the
// scripts chosen at the left and right turning points; the bob timer also
// advances the script every 16 updates; state word 3 is the current script.
//
// Actions: 1 fly left / 2 fly right until a turning point, 3/5 descend, 4/6
// swoop right/left at 4 px/update, 7/11 hover, 8 restart, 9 climb back up.
void Game::update_green_hill_boss(Object& o) {
    auto& boss_flags = o.state[0];
    auto& script_position = o.state[1];
    auto& bob_timer = o.state[2];
    o.flags |= no_terrain;
    o.width = 32;
    o.height = 28;
    o.flame_tile = -1;
    stop_streaming_in_arena();
    if (!(boss_flags & 0x01)) {
        o.set_pixel_y(280);
        start_boss_fight(1888, 256);
        script_position = 0;
        o.set_state_word(3, rom::gh_boss_fly_left);
        boss_flags |= 0x01;
    }

    // A gentle bob from the table at 72B0.
    o.vy = signed8(level.data(rom::gh_boss_bob + (bob_timer & 63)));

    int action = 0;
    for (int guard = 0; guard < 4; ++guard) {
        action = level.data(o.state_word(3) + script_position);
        if (action) {
            break;
        }
        script_position = level.data(o.state_word(3) + script_position + 1);
    }
    auto start_script = [&](int address) {
        script_position = 0;
        o.set_state_word(3, address);
    };
    switch (action) {
    case 1:
        o.vx = -256;
        if (o.pixel_x() <= word(level.camera_left + 34)) {
            start_script((boss_flags & 0x02) ? rom::gh_boss_descend_swoop_right
                                             : rom::gh_boss_fly_right);
            boss_flags ^= 0x02;
        }
        break;
    case 2:
        o.vx = 256;
        if (o.pixel_x() > word(level.camera_left + 186)) {
            start_script((boss_flags & 0x04) ? rom::gh_boss_descend_swoop_left
                                             : rom::gh_boss_fly_left);
            boss_flags ^= 0x04;
        }
        break;
    case 3:
    case 5:
        o.vx = 0;
        o.vy = 96;
        if (o.pixel_y() > word(camera.y + 108)) {
            start_script(action == 3 ? rom::gh_boss_swoop_right : rom::gh_boss_swoop_left);
        }
        break;
    case 4:
        o.vx = 1024;
        break;
    case 6:
        o.vx = -1024;
        break;
    case 7:
    case 11:
        o.vx = 0;
        break;
    case 8:
        o.vx = 0;
        start_script(rom::gh_boss_climb);
        bob_timer = 0;
        break;
    case 9:
        o.vx = 0;
        o.vy = -256;
        if (o.pixel_y() <= word(camera.y + 24)) {
            start_script(o.pixel_x() >= level.camera_left ? rom::gh_boss_fly_left
                                                          : rom::gh_boss_fly_right);
        }
        break;
    default:
        throw std::runtime_error("Unrecovered boss action " + std::to_string(action));
    }
    // Facing comes from the action that ran, even if it just switched scripts
    // (725D keeps the old action pointer).
    const int facing = level.data(rom::gh_boss_facing + action);
    o.flags = (o.flags & ~facing_left) | facing;
    o.mapping = facing ? rom::gh_boss_layout_left : rom::gh_boss_layout_right;
    update_boss_contact(o);
    submit_engine_flame(o);
    if ((++bob_timer & 15) == 0) {
        ++script_position;
    }
}

// --- Shared boss helpers ---------------------------------------------------------

// A boss arrives: the camera locks where it is and then eases to the arena
// position (Game::update_camera); the boss art and palette replace the act's
// sprites; the boss music starts and the hit count restarts. The Labyrinth boss
// keeps the act's sprite palette.
void Game::start_boss_fight(int arena_x, int arena_y, bool boss_palette) {
    lock_boss_arena(arena_x, arena_y);
    level.object_patterns = level.boss_patterns;
    if (boss_palette) {
        use_boss_palette();
    }
    boss.hits = 0;
    music(Music::Boss);
}

// Locks the camera limits where the camera is and sets the arena target.
void Game::lock_boss_arena(int arena_x, int arena_y) {
    boss.arena_x = arena_x;
    boss.arena_y = arena_y;
    level.camera_left = level.camera_right = camera.x;
    level.camera_top = level.camera_bottom = camera.y;
}

void Game::use_boss_palette() {
    sprite_palette_kind = 1;
    std::copy_n(level.boss_palette.begin() + 2, 30, level.palette.begin() + 34);
}

// Once the camera has reached the arena, new terrain stops streaming in (7CDA).
void Game::stop_streaming_in_arena() {
    if (boss.arena_x == camera.x && boss.arena_y == camera.y) {
        terrain_streaming = false;
    }
}

// The craft's engine flame behind it while it flies sideways, every second
// update; submitted before the craft moves (7A36).
void Game::submit_engine_flame(Object& o) {
    if ((o.vx & 65535) && !(ticks & 1)) {
        o.flame_tile = (ticks & 2) + (o.vx < 0 ? 0 : 4);
        o.flame_x = word(o.pixel_x() + (o.vx < 0 ? 40 : -8));
        o.flame_y = word(o.pixel_y() + 16);
    }
}

// The jet below the craft while it rises (9402), until it is defeated.
void Game::submit_lift_jet(Object& o) {
    if (boss.hits < 8 && o.vy < 0) {
        o.flame_tile = ticks & 2;
        o.flame_x = word(o.pixel_x() + 16);
        o.flame_y = word(o.pixel_y() + 48);
    }
}

// Hits and defeat, shared by the Green Hill, Bridge, Jungle and Labyrinth
// bosses (77FD). `mapping_offset` selects each boss's hit and defeat art.
//
// A curled Sonic touching the boss bounces off (his vertical speed reverses)
// and scores a hit; the boss flashes for 24 updates, during which it cannot be
// hit again. An uncurled Sonic is hurt instead. After eight hits the boss
// stops, explodes ten times (one every 24 updates), sinks, then flies off to
// the right; the level music resumes and the camera is released.
void Game::update_boss_contact(Object& o, int mapping_offset) {
    if (boss.hits >= 8) {
        o.vx = o.vy = 0;
        o.mapping = rom::boss_hit_layouts + mapping_offset + (o.facing_left() ? 54 : 36);
        if (boss.defeat_phase < 10) {
            boss.explosion_ticks = (boss.explosion_ticks - 1) & 255;
            if (!boss.explosion_ticks) {
                boss.explosion_ticks = 24;
                ++boss.defeat_phase;
                spawn_explosion(o);
            }
            return;
        }
        if (boss.defeat_phase < 58) {
            o.y = (o.y + 32) & 0xffffff; // Sinks.
        }
        if (boss.defeat_phase < 90) {
            ++boss.defeat_phase;
            return;
        }
        if (boss.defeat_phase == 90) {
            boss.defeat_phase = 91;
            sound_events.push_back({SoundEvent::Kind::Music, level.music});
            // The original also waits one extra video frame here (78F6); that is
            // a host timing detail, not a game update.
        }
        o.vx = 768;
        o.vy = -160;
        o.mapping = rom::boss_hit_layouts;
        // Widescreen: leave the presented view before disappearing.
        if (o.pixel_x() < word(right_edge(256))) {
            return;
        }
        o.type = type::none;
        level.camera_right = 8192;
        boss.arena_x = 0;
        terrain_streaming = true;
        level.object_patterns = level.capsule_patterns;
        return;
    }
    if (!flash.ticks) {
        if (!overlaps(o, 8, 12) || sonic.dead) {
            return;
        }
        if (!(sonic.flags & curled)) {
            sonic.take_hit(*this);
            return;
        }
        sonic.vy = signed24(-sonic.vy);
        sonic.vx = 0;
        flash.ticks = 24;
        flash.color = 143; // Sprite palette entry 15.
        flash.value = 0xfff;
        ++boss.hits;
        sound(Effect::Break);
    }
    o.mapping = rom::boss_hit_layouts + mapping_offset + (o.facing_left() ? 18 : 0);
    boss.explosion_ticks = 24;
    boss.defeat_phase = 0;
}

// The animal capsule (736B-754C).
//
// Solid on its sides; its top has a sloped button (heights in the table at
// 754D). Standing on the lowest part of the button opens it: Sonic is put
// into auto-run, the capsule explodes eight times, the act-clear music plays
// and twelve animals jump out, one every 16 updates. The early returns are
// preserved: standing on the sloped edge of the button does not advance the
// sequence, even once auto-run is on.
//
// Flags: 0x01 initialised, 0x02 opened.
void Game::update_capsule(Object& o) {
    auto& explosion_timer = o.state[0];
    auto& explosions = o.state[1];
    auto& animals_released = o.state[5];
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_pixel_y(o.pixel_y() + 16);
    }
    o.width = 28;
    o.height = 64;
    // Alternating tile patches animate the capsule. They share the single
    // pending patch slot with rings and falling planks (3282).
    const int patch =
        ((o.flags & 0x02) ? rom::capsule_open_patches : rom::capsule_patches) + (ticks & 1) * 12;
    const int x = word(o.pixel_x() + level.word(patch));
    const int y = word(o.pixel_y() + level.word(patch + 2));
    queue_tile_patch(x, y, patch + 4);
    o.mapping = (ticks & 16) ? rom::capsule_layout_blink : rom::capsule_layout;
    level.camera_left = camera.x;
    level.camera_right = word(o.pixel_x() - 112);

    if (overlaps(o, 2, 0) && sonic.vy >= 0) {
        if (sonic.pixel_y() >= o.pixel_y()) {
            // Side contact: push Sonic out.
            sonic.set_pixel_x(o.pixel_x() + (word(o.pixel_x() + 16) >= sonic.pixel_x() ? -14 : 29));
            sonic.vx = 0;
        } else {
            // On top: stand on the button's contour.
            const int center = word(sonic.pixel_x() + 8);
            if (center < o.pixel_x() || center > word(o.pixel_x() + 32)) {
                return;
            }
            const int height = level.data(rom::capsule_button_heights + (center & 31));
            sonic.set_pixel_y(o.pixel_y() - 32 + height);
            sonic.vy = -sonic.previous_gravity;
            sonic.flags |= grounded;
            if (height != 3) {
                return;
            }
            o.mapping = rom::capsule_pressed_layout; // Button pressed.
            if (!auto_run) {
                auto_run = true;
                sonic.vx = 0;
            }
        }
    }
    if (!auto_run) {
        return;
    }
    terrain_streaming = false;
    if (explosions < 8) {
        if (++explosion_timer < 20) {
            return;
        }
        explosion_timer = 0;
        spawn_explosion(o);
        ++explosions;
        return;
    }
    if (!(o.flags & 0x02)) {
        finish_ticks = 160;
        music(Music::ActClear);
        o.flags |= 0x02;
    }
    o.mapping = 0;
    if (ticks & 15) {
        return;
    }
    // Release an animal of a random kind with a random upward speed.
    const int kind = type::freed_animal_pausing + (next_random() & 1);
    if (auto* animal = allocate_object(kind)) {
        animal->state[5] = animal->state[6] = 0;
        animal->x = word(o.pixel_x() + 8) * 256;
        animal->y = word(o.pixel_y() + 26) * 256;
        const int fraction = next_random();
        const int speed = 2 + (next_random() & 1);
        animal->vy = -speed * 256 + fraction;
    }
    if (++animals_released >= 12) {
        o.type = type::none;
    }
}

// Freed animals (75D3/76D8) run off to the left. One kind bounces continuously;
// the other lands, pauses for eight updates and jumps again. Speeds and art
// differ by zone (scene).
void Game::update_animal(Object& o) {
    o.flags &= ~no_terrain;
    o.width = 12;
    if (o.type == type::freed_animal_bouncing) {
        o.height = 16;
        if (o.grounded()) {
            o.vy = -768;
        }
        o.vy = signed24(o.vy + (level.scene == 3 ? 56 : 18));
        if (o.vy >= 0 && ((o.vy >> 8) & 255) >= 2) {
            o.vy = 512;
        }
        o.vx = level.scene == 3 ? -384 : -512;
        animate_object(o,
                       level.scene == 0   ? rom::animal_bounce_green_hill
                       : level.scene == 3 ? rom::animal_bounce_labyrinth
                                          : rom::animal_bounce_other,
                       rom::animal_bounce_frames);
    } else {
        o.height = 32;
        const int scene = std::min(3u, level.scene);
        o.mapping = rom::animal_air_layouts + scene * 27;
        if (o.grounded()) {
            o.vy = 256;
            o.vx = 0;
            o.mapping = rom::animal_ground_layouts + scene * 27;
            if (++o.state[0] < 8) {
                return;
            }
            o.vy = level.scene == 0 ? -1024 : -512;
        }
        o.vy = signed24(o.vy + 40);
        if (o.vy >= 0 && ((o.vy >> 8) & 255) >= 2) {
            o.vy = 512;
        }
        o.vx = -384;
        o.state[0] = 0;
    }
    // Removed once it has left the screen on the left.
    if (word(o.pixel_x() - 32 + wide_left) < camera.x) {
        o.type = type::none;
    }
}

} // namespace sonic1_gg
