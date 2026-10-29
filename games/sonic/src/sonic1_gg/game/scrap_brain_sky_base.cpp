// Scrap Brain and Sky Base: room exits, doors, electricity, flames, the
// mechanical enemies and guns, Sky Base's clouds and platforms, and the chase
// that ends Scrap Brain 3. These handlers keep the cartridge's order of
// update, contact and sprite submission and its byte-sized timers.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

// A room exit (9B4A): an invisible 30x96 sensor. Its cell position selects the
// linked room to load from the table at 9BAE.
void Game::update_area_exit(Object& o) {
    o.flags |= no_terrain;
    o.width = 30;
    o.height = 96;
    if (overlaps(o, 24, 0)) {
        for (int i = 0; i < 5; ++i) {
            const int entry = rom::area_exits + i * 3;
            if (level.data(entry) == ((o.pixel_x() >> 5) & 255) &&
                level.data(entry + 1) == ((o.pixel_y() >> 5) & 255)) {
                area_target = level.data(entry + 2);
                area_transition = true;
                finish_ticks = 1;
                break;
            }
        }
    }
    o.mapping = 0;
}

// An electric bolt (9BBD/9C45). It travels right or left for 100 updates,
// disappears, and starts again from its origin (word 0) after 240.
void Game::update_electric_bolt(Object& o) {
    auto& timer = o.state[2];
    o.vx = o.type == type::electric_right ? 384 : -384;
    o.mapping = o.type == type::electric_right ? rom::bolt_right : rom::bolt_left;
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_state_word(0, o.pixel_x());
        sound(Effect::Bolt);
    }
    o.width = 6;
    o.height = 8;
    if (timer < 100 && overlaps(o, 0, 4)) {
        sonic.take_hit(*this);
    }
    if (++timer >= 100) {
        if (timer >= 240) {
            o.x = o.state_word(0) * 256;
            timer = 0;
            sound(Effect::Bolt);
        } else {
            o.mapping = 0;
            o.vx = 0;
        }
    }
}

// A burst of fire (9C63). A 256-update cycle starting at a random point; each
// 16-update phase has a flame height (table 9D33) and four flame sprites.
void Game::update_flame_jet(Object& o) {
    auto& cycle = o.state[0];
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_pixel_x(o.pixel_x() + 12);
        cycle = next_random();
    }
    const int phase = cycle >> 4;
    o.height = level.data(rom::flame_heights + phase);
    o.width = 6;
    int table = level.word(rom::flame_sprites + phase * 2);
    if (table) {
        table += (cycle * 8) & 31;
        for (int i = 0; i < 4; ++i) {
            emit_sprite(o, level.data(table + i * 2), 0, level.data(table + i * 2 + 1));
        }
        if (o.height && overlaps(o, 2, 2)) {
            sonic.take_hit(*this);
        }
    }
    ++cycle;
    o.mapping = 0;
    if (cycle == 112) {
        sound(Effect::FlameJet);
    }
}

// Vertical doors (9DC3/9F2B/9FEE/A474, shared code 9E9D/9E47). They slide up
// while Sonic stands near them on their side, or, for the switch door, while
// the act's switch bit is set. The opening runs 0..48; below 40 the door is
// solid. Flag 0x02 marks a door whose block above is not 0xA3: it is aligned
// differently and, for the switch door, works the other way round.
void Game::update_door(Object& o) {
    auto& opening = o.state[0];
    o.flags |= no_terrain;
    o.width = 4;
    o.height = (3 - ((opening >> 4) & 3)) * 16;
    if (o.first_update()) {
        int dx = 20;
        if (level.block_at(o.pixel_x(), o.pixel_y() - 16) != 0xa3) {
            dx = 4;
            o.flags |= 0x02;
        }
        o.set_pixel_x(o.pixel_x() + dx);
        o.set_state_word(1, o.pixel_y());
    }
    if (opening < 40 && overlaps(o, 5, 0)) {
        sonic.set_pixel_x(o.pixel_x() + (sonic.vx < 0 ? 5 : -12));
        sonic.vx = 0;
    }
    bool open;
    if (o.type == type::switch_door) {
        open = bool(progress.switch_acts & (1u << level.act)) != bool(o.flags & 0x02);
    } else {
        const int left = o.type == type::door_from_right ? -12 : -48;
        const int right = o.type == type::door_from_left ? 0 : 36;
        open = word(o.pixel_x() + left) < sonic.pixel_x() &&
               word(o.pixel_x() + right) >= sonic.pixel_x() &&
               word(o.pixel_y() - 32) < sonic.pixel_y() &&
               word(o.pixel_y() + 80) >= sonic.pixel_y();
    }
    if (open) {
        if (opening < 48) {
            opening += o.type == type::switch_door ? 2 : 1;
            if (o.type != type::switch_door && opening == 1) {
                sound(Effect::Door);
            }
        }
    } else if (opening) {
        opening -= o.type == type::switch_door ? 2 : 1;
        if (o.type != type::switch_door && opening == 47) {
            sound(Effect::Door);
        }
    }
    o.set_pixel_y(o.state_word(1) - (opening & 15));
    const int base = o.type == type::door_from_left    ? rom::door_from_left_layouts
                     : o.type == type::door_from_right ? rom::door_from_right_layouts
                     : o.type == type::door_both_sides ? rom::door_both_sides_layouts
                                                       : rom::switch_door_layouts;
    o.mapping = base + ((opening >> 4) & 3) * 18;
}

// An electric arc between two poles (A0B1). A 160-update cycle: off (0-69),
// warning flicker (70-99), live (100-159).
void Game::update_electric_arc(Object& o) {
    auto& cycle = o.state[0];
    o.flags |= no_terrain;
    o.width = 42;
    o.height = 12;
    if (o.first_update()) {
        o.set_pixel_x(o.pixel_x() + 24);
        o.set_pixel_y(o.pixel_y() + 16);
    }
    if (cycle >= 100) {
        if (cycle == 100) {
            sound(Effect::Electric);
        }
        if (overlaps(o, 3, 2)) {
            sonic.take_hit(*this);
        }
        animate_object(o, rom::arc_live, rom::arc_frames);
    } else if (cycle < 70) {
        o.mapping = 0;
    } else {
        animate_object(o, rom::arc_warning, rom::arc_frames);
    }
    if (++cycle >= 160) {
        cycle = 0;
    }
}

// A bomb thrower (A173). It stands on the ground facing Sonic and throws a
// bouncing bomb toward him every 256 updates (when the timer reaches 237).
void Game::update_bomb_thrower(Object& o) {
    auto& s = o.state;
    auto& timer = o.state[0];
    o.width = 10;
    o.height = 32;
    enemy_contact(o, 3, 8, 0, 14);
    o.vy = 256;
    const bool left = word(sonic.pixel_x() + 8) < word(o.pixel_x() + 10);
    if (timer == 235) {
        s[5] = 0;
    }
    animate_object(o, timer >= 235 ? rom::thrower_throw : rom::thrower_idle,
                   left ? rom::thrower_frames_left : rom::thrower_frames_right);
    if (timer == 237) {
        if (auto* bomb = allocate_object(type::bouncing_bomb)) {
            bomb->x = o.pixel_x() * 256;
            bomb->y = word(o.pixel_y() + 6) * 256;
            bomb->state[0] = bomb->state[5] = bomb->state[6] = 0;
            bomb->vx = left ? -256 : 256;
            bomb->vy = 256;
        }
    }
    ++timer;
}

// A bouncing bomb (A305). It bounces along the terrain and explodes after 130
// updates; the explosion still hurts until it disappears at 165.
void Game::update_bouncing_bomb(Object& o) {
    auto& s = o.state;
    auto& age = o.state[0];
    o.flags &= ~no_terrain;
    o.width = 10;
    o.height = 15;
    if (overlaps(o, 1, 1)) {
        sonic.take_hit(*this);
    }
    if (o.grounded()) {
        o.vy = -768;
    }
    o.vy += 31;
    if (age >= 130) {
        if (age == 130) {
            s[5] = 0;
            sound(Effect::Break);
        }
        animate_object(o, rom::bouncing_bomb_burst, rom::bomb_frames);
    } else {
        animate_object(o, rom::bouncing_bomb_animation, rom::bomb_frames);
    }
    if (++age >= 165) {
        o.type = type::none;
    }
}

// A two-ended crawler (A51A). It crawls along a path of 22 steps, each end
// leading in turn; every 20 passes it turns around (flag 0x02). The spiked end
// always hurts; the other end is an enemy contact. Four direct sprites draw its
// segments.
void Game::update_crawler(Object& o) {
    auto& s = o.state;
    auto& step = o.state[0];
    auto& passes = o.state[4];
    o.width = 6;
    o.height = 16;
    if (!(ticks & 1)) {
        const int i = step * 2;
        o.x = (o.x + signed16(level.word(
                         (o.flags & 0x02 ? rom::crawler_steps_reverse : rom::crawler_steps) + i))) &
              0xffffff;
        const int v = word(o.state_word(1) + level.word(rom::crawler_lift + i));
        o.set_state_word(1, v);
        s[3] = (v & 32768) ? 255 : 0;
    }
    const bool reverse = o.flags & 0x02;
    const int table = (reverse ? rom::crawler_segments_reverse : rom::crawler_segments) + step;
    const int dy = signed16(o.state_word(2));
    for (int i = 0; i < 4; ++i) {
        emit_sprite(o, reverse ? (i ? 40 : 42) : (i ? 38 : 36), level.data(table + i * 22),
                    i % 2 ? 0 : dy);
    }
    if (!reverse) {
        o.width = 6;
        if (overlaps(o, 2, 8)) {
            enemy_contact(o, 2, 8);
        } else {
            o.width = 22;
            if (overlaps(o, 6, 8)) {
                sonic.take_hit(*this);
            }
        }
    } else {
        o.width = 16;
        if (overlaps(o, 1, 4)) {
            sonic.take_hit(*this);
        } else {
            o.width = 22;
            enemy_contact(o, 16, 4);
        }
    }
    o.vy = (o.vy & 0xff0000) | 0x100 | (o.vy & 255);
    if (!(ticks & 1) && ++step >= 22) {
        step = 0;
        if (++passes >= 20) {
            passes = 0;
            o.flags ^= 0x02;
        }
    }
}

// A Sky Base foreground cloud (A98D). Clouds use up to three reserved sprite
// rows per update, drift left at 3 px/update and reappear at a random
// position ahead of the camera.
void Game::update_cloud(Object& o) {
    o.flags |= no_terrain;
    if (foreground_clouds++ < 3) {
        for (int col = 0; col < 6; ++col) {
            const int tile = level.data(rom::cloud_tiles + col);
            if (tile < 254) {
                o.extra_sprites.push_back(
                    {tile, word(o.pixel_x() + col * 8), pixel(o.y + sway.offset)});
            }
        }
    }
    // Widescreen margins (zero on the LCD) recycle clouds out of view.
    if (word(o.pixel_x() + wide_left) < word(camera.x - 32)) {
        o.set_pixel_x(right_edge(436) + next_random() * 2);
    }
    o.vx = -768;
    o.mapping = 0;
}

// Two sparks cycling through four poses (AA30).
void Game::update_spark_pair(Object& o) {
    auto& pose = o.state[0];
    o.flags |= no_terrain;
    o.width = 5;
    o.height = 20;
    if (o.first_update()) {
        o.set_pixel_x(o.pixel_x() + 15);
        o.set_pixel_y(o.pixel_y() - 6);
    }
    int at = level.word(rom::spark_poses + pose);
    for (int i = 0; i < 2; ++i, at += 3) {
        if (level.data(at + 2) < 128) {
            emit_sprite(o, level.data(at + 2), level.data(at), level.data(at + 1));
        }
    }
    if (overlaps(o, 2, 2)) {
        sonic.take_hit(*this);
    }
    o.mapping = 0;
    pose += 2;
    if (pose >= 8) {
        pose = 0;
    }
}

// A mine (AAE7). It wanders slowly through its phases (one every 64 updates).
// When Sonic comes within range it primes itself (phase 100) and bursts into
// four fragments, which only happens if all four slots are free.
void Game::update_mine(Object& o) {
    auto& phase = o.state[0];
    o.width = 8;
    o.height = 16;
    if (phase < 100 && sonic.pixel_x() >= word(o.pixel_x() - 48) &&
        sonic.pixel_x() < word(o.pixel_x() + 40)) {
        phase = 100;
    }
    bool gravity = true;
    if (phase < 30) {
        o.vx = -8;
        animate_object(o, rom::mine_wander_left, rom::mine_frames);
    } else if (phase < 100) {
        if (phase < 35) {
            o.vx = 0;
            animate_object(o, rom::mine_pause, rom::mine_frames);
        } else if (phase < 65) {
            o.vx = 8;
            animate_object(o, rom::mine_wander_right, rom::mine_frames);
        } else {
            o.vx = 0;
            animate_object(o, rom::mine_pause_again, rom::mine_frames);
        }
    } else {
        o.vx = 0;
        if (phase < 102) {
            animate_object(o, rom::mine_primed, rom::mine_frames);
        } else {
            o.mapping = rom::mine_burst_layout;
            if (phase == 103) {
                gravity = false;
                bool all = true;
                for (int i = 0; i < 4; ++i) {
                    auto* fragment = allocate_object(type::projectile);
                    if (!fragment) {
                        all = false;
                        break;
                    }
                    fragment->x = word(o.pixel_x() + (i & 1 ? 8 : 0)) * 256;
                    fragment->y = word(o.pixel_y() + (i & 2 ? 8 : 0)) * 256;
                    fragment->state[0] = 0;
                    for (int j = 2; j < 7; ++j) {
                        fragment->state[j] = 0;
                    }
                    fragment->state[2] = 36; // Gravity.
                    fragment->vx = (i & 1 ? 3 : -2) * 256;
                    fragment->vy = (i & 2 ? -2 : -4) * 256;
                }
                if (all) {
                    o.type = type::none;
                    sound(Effect::MineBurst);
                }
            }
        }
    }
    if (gravity) {
        o.vy = 128;
    }
    if (overlaps(o, 2, 4)) {
        sonic.take_hit(*this);
    }
    if (!(ticks & 63) && ++phase == 70) {
        phase = 0;
    }
}

// A ceiling launcher (AD32). It fires a fast shot every 256 updates, starting
// at a random point of its cycle. The new shot keeps the slot's unused bytes.
void Game::update_ceiling_launcher(Object& o) {
    auto& s = o.state;
    auto& timer = o.state[0];
    auto& fire_animation = o.state[1];
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_pixel_x(o.pixel_x() - 4);
        timer = next_random();
    }
    if (timer == 100) {
        if (auto* shot = allocate_object(type::launcher_shot)) {
            shot->x = word(o.pixel_x() + 4) * 256;
            shot->y = word(o.pixel_y() + 16) * 256;
            sound(Effect::LauncherFire);
            fire_animation = 24;
            s[5] = s[6] = 0;
        }
    }
    if (fire_animation) {
        animate_object(o, rom::launcher_animation, rom::launcher_frames);
        --fire_animation;
    } else {
        o.mapping = 0;
    }
    ++timer;
}

// The launcher's shot (ADFB): it flies right at 2.5 px/update.
void Game::update_launcher_shot(Object& o) {
    o.flags |= no_terrain;
    o.width = o.height = 12;
    if (o.pixel_x() > word(right_edge(272))) {
        o.type = type::none;
    }
    if (overlaps(o, 2, 2)) {
        sonic.take_hit(*this);
    }
    o.vx = 640;
    o.vy = 0;
    o.mapping = rom::launcher_shot_layout;
}

// A rotating gun (B132). It turns through eight directions (one step every 64
// updates) and fires in the current direction every 26 updates.
void Game::update_rotating_gun(Object& o) {
    auto& direction = o.state[0];
    auto& fire_timer = o.state[1];
    o.flags |= no_terrain;
    if (o.first_update()) {
        direction = next_random() & 7;
    }
    o.mapping = 0;
    for (int i = 0; i < 2; ++i) {
        const int at = rom::gun_sprites + direction * 8 + i * 4;
        if (level.data(at + 2) != 255) {
            emit_sprite(o, level.data(at + 2), signed8(level.data(at)),
                        signed8(level.data(at + 1)));
        }
    }
    if (!(ticks & 63)) {
        direction = (direction + 1) & 7;
    }
    if (++fire_timer == 26) {
        fire_timer = 0;
        const int at = rom::gun_shots + direction * 6;
        spawn_cannon_shot(o, signed16(level.word(at)), signed16(level.word(at + 2)),
                          signed8(level.data(at + 4)), signed8(level.data(at + 5)));
    }
}

// A Sky Base riding platform (B25D). It bobs with the swaying view
// (sky_offset added to its stored 24-bit origin in bytes 1..3) and slowly
// carries its rider right until the platform reaches terrain.
void Game::update_sky_platform(Object& o) {
    auto& s = o.state;
    auto& propeller = o.state[0];
    o.flags |= no_terrain;
    if (o.first_update()) {
        s[1] = o.y;
        s[2] = o.y >> 8;
        s[3] = o.y >> 16;
    }
    o.y = ((s[1] | (s[2] << 8) | (s[3] << 16)) + sway.offset) & 0xffffff;
    if (sonic.vy >= 0) {
        o.width = 30;
        o.height = 16;
        if (overlaps(o, 2, 10)) {
            land_on_platform(o, 16, 0);
            if (!(level.block_attributes[level.block_at(o.pixel_x() + 32, o.pixel_y() + 16)] &
                  63)) {
                o.x = (o.x + 128) & 0xffffff;
                sonic.x = (sonic.x + 128) & 0xffffff;
            }
        }
    }
    // The propeller below it.
    for (int i = 0; i < 2; ++i) {
        const int at = rom::propeller_sprites + propeller + i * 2;
        if (level.data(at + 1) != 255) {
            emit_sprite(o, level.data(at + 1), -8, level.data(at));
        }
    }
    o.mapping = rom::sky_platform_layout;
    propeller += 4;
    if (propeller >= 16) {
        propeller = 0;
    }
}

// A crushing wall (B372). It pushes right at 0.5 px/update while Sonic is level
// with it, up to x = 1408, and returns to its origin (word 0) once it is far
// above the view.
void Game::update_crusher(Object& o) {
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_state_word(0, o.pixel_x());
    }
    o.width = 12;
    o.height = 46;
    o.mapping = rom::crusher_layout;
    if (overlaps(o, 2, 2)) {
        sonic.take_hit(*this);
    }
    o.extra_sprites.push_back({22, pixel(o.x + 128), word(o.pixel_y() - 16)});
    o.extra_sprites.push_back({24, word(pixel(o.x + 128) + 8), word(o.pixel_y() - 16)});
    o.vx = 0;
    if (o.pixel_x() >= 1408) {
        return;
    }
    if (word(o.pixel_y() + 64 + wide_top) < camera.y) {
        o.set_pixel_x(o.state_word(0));
    }
    if (word(o.pixel_y() - 32) < sonic.pixel_y() && word(o.pixel_y() + 44) >= sonic.pixel_y()) {
        o.vx = 128;
    }
}

// A concealed gun (B447). Its direction (0-3) comes from the terrain block it
// is placed on. Every 16 updates, during half of each 128-update cycle, it
// fires if Sonic is in the 256-pixel square its table names for that direction.
void Game::update_hidden_gun(Object& o) {
    auto& direction = o.state[0];
    auto& timer = o.state[1];
    o.flags |= no_terrain;
    if (!(o.flags & 0x01)) {
        const int kind = (level.block_at(o.pixel_x(), o.pixel_y()) - 60) & 255;
        if (kind >= 4) {
            return;
        }
        direction = kind;
        o.flags |= 0x01;
    }
    ++timer;
    if ((timer & 64) || (timer & 15)) {
        return;
    }
    const int at = rom::hidden_guns + direction * 10;
    if ((word(sonic.pixel_x() - o.pixel_x()) >> 8) != level.data(at + 8) ||
        (word(sonic.pixel_y() - o.pixel_y()) >> 8) != level.data(at + 9)) {
        return;
    }
    spawn_cannon_shot(o, signed16(level.word(at)), signed16(level.word(at + 2)),
                      signed16(level.word(at + 4)), signed16(level.word(at + 6)));
}

// The end of Scrap Brain 3 (A7B6-A97C), in stages (flags):
// - pursuit: the villain runs right ahead of Sonic, matching his speed, until
//   x = 1440;
// - 0x02: the camera locks and controls are ignored for 192 updates while the
//   villain stands on a lift;
// - 0x04: the villain is gone; the lift comes back down;
// - 0x08: Sonic can ride the lift up 192 pixels; at the top the act ends with
//   auto-run;
// - 0x10: done; the lift settles.
void Game::update_scrap_brain_chase(Object& o) {
    auto& lift = o.state[0];
    o.width = 30;
    o.height = 47;
    if (o.first_update()) {
        level.camera_left = 832;
        level.camera_right = 1344;
        level.camera_top = level.camera_bottom = camera.y;
        boss.arena_y = 544;
        level.object_patterns = level.boss_patterns;
        use_boss_palette();
        music(Music::Boss);
    }
    if (!(o.flags & 0x02)) {
        level.camera_left = camera.x;
        animate_object(o, rom::chase_run_animation, rom::villain_frames);
        const int speed =
            (sonic.vx & 32768) || word(o.pixel_x() - sonic.pixel_x()) >= 64 ? -128 : sonic.vx;
        o.vx = word(speed + 256);
        if (o.pixel_x() >= 1440) {
            o.vx &= 0xff0000;
            sonic.vx = 0;
            o.flags |= 0x02;
        }
    } else if (!(o.flags & 0x04)) {
        boss.arena_x = 1328;
        boss.arena_y = 544;
        level.camera_left = level.camera_right = camera.x;
        level.camera_top = level.camera_bottom = camera.y;
        current_input = {};
        o.x = 1440 * 256;
        o.mapping = rom::villain_frames;
        if (++lift >= 192) {
            o.flags |= 0x04;
        }
    } else if (!(o.flags & 0x08)) {
        current_input = {};
        o.mapping = 0;
        if (!--lift) {
            o.flags |= 0x08;
        }
    } else if (!(o.flags & 0x10)) {
        const int x = sonic.pixel_x();
        if (x > 1430 && x <= 1472 && (lift || sonic.pixel_y() >= 653)) {
            if (!lift) {
                sonic.vx = 0;
            }
            sonic.flags = grounded;
            sonic.set_pixel_x(1440);
            current_input = {};
            sonic.y = (654 - lift) * 256;
            sonic.vy = -sonic.previous_gravity;
            if (++lift == 192) {
                sonic.set_pixel_x(camera.x + 256);
                o.flags |= 0x10;
                music(Music::ActClear);
                finish_ticks = 160;
                auto_run = true;
                return;
            }
        }
    } else if (lift) {
        --lift;
    }
    o.y = (640 - lift) * 256;
    // The lift is a second sprite row, separate from the character (A96B).
    for (int col = 0; col < 6; ++col) {
        const int tile = level.data(rom::lift_tiles + col);
        if (tile < 254) {
            o.extra_sprites.push_back({tile, 1440 + col * 8, 687 - lift});
        }
    }
    if ((lift & 31) == 15) {
        sound(Effect::Door);
    }
}

// A shot from a gun (B59C), at an offset from the gun. The reused slot keeps
// its state byte 1; the shot has no gravity.
void Game::spawn_cannon_shot(const Object& gun, int vx, int vy, int dx, int dy) {
    if (fire_projectile(word(gun.pixel_x() + dx), word(gun.pixel_y() + dy), vx, vy, 0)) {
        sound(Effect::Break);
    }
}

} // namespace sonic1_gg
