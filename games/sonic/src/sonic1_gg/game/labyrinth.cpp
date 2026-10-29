// Labyrinth zone: water, air and drowning, bubbles, the Labyrinth enemies and
// traps, and the Labyrinth boss.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

namespace {

// True when the body is outside the camera-relative rectangle. Widescreen
// margins (zero on the LCD) widen the rectangle to the presented view.
bool outside(const Body& o, const Game& g, int left, int right, int top, int bottom) {
    return word(g.left_edge(left)) >= o.pixel_x() || word(g.right_edge(right)) < o.pixel_x() ||
           word(g.top_edge(top)) >= o.pixel_y() || word(g.bottom_edge(bottom)) < o.pixel_y();
}

// Bubbles disappear off screen and when they reach the water surface.
bool bubble_gone(const Game& g, const Object& o) {
    return o.pixel_x() <= std::max(0, g.left_edge(-8)) || o.pixel_x() > word(g.right_edge(256)) ||
           o.pixel_y() <= g.water.y || o.pixel_y() <= word(g.top_edge(-16)) ||
           o.pixel_y() > word(g.bottom_edge(192));
}

} // namespace

// Creates a small bubble near `parent` (bank 2, 91F5-9260). A bubble source
// sometimes produces a large air bubble instead (table at 9261). The object is
// allocated before any random number is drawn, so a failed allocation leaves
// the generator untouched; fields not set here keep their old values.
void Game::spawn_bubble(const Body& parent, int source_type) {
    auto* b = allocate_object(type::small_bubble);
    if (!b) {
        return;
    }
    if (source_type == type::bubble_source) {
        b->type = level.data(rom::bubble_kinds + (next_random() & 15));
    }
    b->x = word(parent.pixel_x() + (next_random() & 15)) * 256;
    b->y = word(parent.pixel_y() + (next_random() & 15)) * 256;
    b->state[0] = b->state[1] = 0;
    b->flags = 0;
    b->vx = 0;
}

// Air supply underwater (bank 1, 526C-52F9). After 768 updates underwater a
// countdown from 5 to 0 appears, one number per 256 updates, with warning
// chimes at increasing frequency (table 52FA). Below zero Sonic drowns. The
// boss act (11) deliberately has no air limit.
void Game::update_air_supply() {
    if (level.scene != 3 || level.act == 11) {
        return;
    }
    sonic.air_ticks = word(sonic.air_ticks + 1);
    if (sonic.air_ticks < 768) {
        return;
    }
    int digit = 5 - ((sonic.air_ticks - 768) >> 8);
    if (digit < 0) {
        sonic.shield = sonic.hurt = false;
        sonic.invincible_ticks = 0;
        sonic.drowned = sonic.dead = true;
        sonic.death_ticks = 192;
        music(Music::Death);
        for (int i = 0; i < 4; ++i) {
            spawn_bubble(sonic, 0);
        }
        digit = 0;
    }
    sonic.air_digit = (ticks & 1) ? digit : -1; // The number blinks.
    if (!(ticks & level.data(rom::air_warning_rates + digit))) {
        sound(Effect::Chime);
    }
}

// A pressure switch (A3C1-A453). Standing on it toggles this act's switch bit
// once per press; doors and the Labyrinth 1 life monitor read that bit. Flags:
// 0x01 initialised, 0x02 pressed.
void Game::update_pressure_switch(Object& o) {
    o.width = 10;
    o.height = 17;
    if (o.first_update()) {
        o.set_pixel_x(o.pixel_x() + 8);
    }
    if (overlaps(o, 1, 0) && sonic.vy >= 0) {
        o.mapping = level.scene == 3 ? rom::switch_pressed_labyrinth : rom::switch_pressed;
        land_on_platform(o, 6, 0);
        if (!(o.flags & 0x02)) {
            o.flags |= 0x02;
            progress.switch_acts ^= 1u << level.act;
            sound(Effect::Chime);
        }
    } else {
        o.flags &= ~0x02;
        o.mapping = level.scene == 3 ? rom::switch_released_labyrinth : rom::switch_released;
    }
    o.vy = 512;
}

// A slow fish (884C-88C8). It swims right for 128 x 8 updates, then left; its
// hit box is wider at the front. Now and then it releases a bubble.
void Game::update_swimmer(Object& o) {
    auto& swim_timer = o.state[0];
    o.flags |= no_terrain;
    const bool right = swim_timer < 128;
    o.vx = right ? 32 : -32;
    o.width = right ? 20 : 12;
    o.height = 12;
    enemy_contact(o, 2, right ? 10 : 2, right ? 8 : 0, 0);
    animate_object(o, right ? rom::swimmer_right : rom::swimmer_left, rom::swimmer_frames);
    if (!(ticks & 7)) {
        ++swim_timer;
        if (!(next_random() & 30)) {
            spawn_bubble(o, o.type);
        }
    }
}

// A hazard on a looping path (8910-8998): 180 signed (dx, dy) samples relative
// to its stored origin (words 1 and 3).
void Game::update_path_hazard(Object& o) {
    auto& path_position = o.state[0];
    o.flags |= no_terrain;
    o.width = 8;
    o.height = 12;
    if (o.first_update()) {
        o.set_state_word(1, o.pixel_x() + 8);
        o.set_state_word(3, o.pixel_y() + 8);
    }
    o.set_pixel_x(o.state_word(1) + signed8(level.data(rom::hazard_path + path_position * 2)));
    o.set_pixel_y(o.state_word(3) + signed8(level.data(rom::hazard_path + 1 + path_position * 2)));
    if (overlaps(o, 4, 2)) {
        sonic.take_hit(*this);
    }
    o.mapping = rom::hazard_layout;
    if (++path_position >= 180) {
        path_position = 0;
    }
}

// Spikes on a chain (8B08-8BCD). A 256-update cycle in four phases: extend,
// stay out, retract, stay in. The pose table gives up to three spike sprites and
// the height of the damaging area.
void Game::update_spike_chain(Object& o) {
    o.flags |= no_terrain;
    if (o.first_update()) {
        o.set_pixel_x(o.pixel_x() + 12);
    }
    const int t = ticks & 255;
    const int phase = t >> 6;
    int table;
    if (phase == 0) {
        table = t % 64 < 8 ? rom::spike_chain_extend + (t & 63) : rom::spike_chain_out;
    } else if (phase == 1) {
        table = rom::spike_chain_out;
    } else if (phase == 2) {
        table = (t & 63) < 8 ? rom::spike_chain_retract + (t & 63) : rom::spike_chain_in;
    } else {
        table = rom::spike_chain_in;
    }
    const int pose = rom::spike_chain_poses + level.data(table) * 8;
    for (int i = 0; i < 3; ++i) {
        if (level.data(pose + i * 2) < 128) {
            emit_sprite(o, level.data(pose + i * 2), 0, level.data(pose + i * 2 + 1));
        }
    }
    o.mapping = 0;
    o.width = 1;
    o.height = level.data(pose + 7);
    if (overlaps(o, 4, level.data(pose + 6))) {
        sonic.take_hit(*this);
    }
    if (t == 128) {
        sound(Effect::SpikeChain);
    }
}

// A spear in a wall (8C28-8D49). Every 150 updates it flies out horizontally
// (to the right unless the wall block is 0x52) until it hits terrain or leaves
// the screen, then resets to its origin (words 1 and 3).
void Game::update_spear(Object& o) {
    auto& wait_timer = o.state[0];
    o.flags &= ~no_terrain;
    o.width = 4;
    o.height = 10;
    if (!(o.flags & 0x01)) {
        o.set_pixel_x(o.pixel_x() + 10);
        o.set_pixel_y(o.pixel_y() + 8);
        o.set_state_word(1, o.pixel_x());
        o.set_state_word(3, o.pixel_y());
        wait_timer = 150;
        o.flags |= 0x01;
        if (level.block_at(o.pixel_x(), o.pixel_y()) != 0x52) {
            o.flags |= 0x02;
        }
    }
    if (wait_timer) {
        if (--wait_timer) {
            o.mapping = 0;
            o.vx = 0;
            return;
        }
        sound(Effect::Bolt);
    }
    o.vx = (o.flags & 0x02) ? 256 : -256;
    o.mapping = (o.flags & 0x02) ? rom::spear_right : rom::spear_left;
    o.vy = 0;
    bool reset = (o.flags & (wall_contact | grounded)) != 0;
    if (!reset) {
        if (overlaps(o, 2, 4)) {
            sonic.take_hit(*this);
        }
        reset = outside(o, *this, -16, 272, -16, 208);
    }
    if (reset) {
        o.set_pixel_x(o.state_word(1));
        o.set_pixel_y(o.state_word(3));
        wait_timer = 150;
        o.mapping = 0;
        o.vx = 0;
    }
}

// The water surface (8D59-8E3D). It moves the water level by a 32-step table
// (one step every 16 updates), classifies the surface's screen row for the
// renderer, and draws four drifting wave sprites along it.
void Game::update_water_level(Object& o) {
    auto& level_step = o.state[0];
    auto& wave_timer = o.state[1];
    auto& wave_scroll = o.state[2];
    o.flags |= no_terrain;
    o.y = (o.y + signed8(level.data(rom::water_level_steps + level_step))) & 0xffffff;
    water.y = o.pixel_y();
    if (!(ticks & 15) && ++level_step >= 32) {
        level_step = 0;
    }
    const int delta = water.y - camera.y;
    water.scanline = delta <= 12 ? 255 : delta > 180 ? 0 : delta;
    if (!water.scanline || water.scanline == 255) {
        return;
    }
    for (int i = 0; i < 2; ++i) {
        const int dx = (wave_scroll + level.data(rom::wave_offsets + wave_timer * 2 + i)) & 255;
        const int y = camera.y + ((water.scanline + 9) & 255);
        water.wave_sprites.push_back({0, camera.x + dx, y});
        water.wave_sprites.push_back({2, camera.x + dx + 8, y});
    }
    if (++wave_timer >= 6) {
        wave_timer = 0;
        wave_scroll += 2;
        if (wave_scroll >= 16) {
            wave_scroll = 0;
        }
    }
}

// A bubble source (8E6A). Every 128 updates it may release a bubble.
void Game::update_bubble_source(Object& o) {
    auto& timer = o.state[1];
    o.flags |= no_terrain;
    if (!(timer & 127) && (level.data(rom::bubble_chances + (next_random() & 7)) & 1)) {
        spawn_bubble(o, type::bubble_source);
    }
    animate_object(o, rom::bubble_source_animation, rom::bubble_source_frames);
    ++timer;
}

// A small bubble (8ECF-8F70). It rises and drifts sideways in a random
// direction, chosen again every 16 updates.
void Game::update_small_bubble(Object& o) {
    auto& age = o.state[0];
    o.flags |= no_terrain;
    o.mapping = 0;
    if (!(age & 15)) {
        o.vx = (next_random() & 63) < 32 ? 40 : -40;
    }
    o.vy = -160;
    if (bubble_gone(*this, o)) {
        o.type = type::none;
    }
    emit_sprite(o, 12);
    ++age;
}

// A fish that jumps at Sonic (8F72-9049). It uses terrain collision. When Sonic
// is within 48 pixels horizontally it jumps (flag 0x01 until it lands);
// otherwise it swims toward him.
void Game::update_jumping_swimmer(Object& o) {
    o.width = 12;
    o.height = 32;
    enemy_contact(o, 2, 2, 0, 8);
    o.vy = signed24(o.vy + 16);
    if (o.vy >= 0 && ((o.vy >> 8) & 255) >= 4) {
        o.vy = 768;
    }
    if (!(o.flags & 0x01)) {
        if (word(o.pixel_x() - 48) < sonic.pixel_x() && word(o.pixel_x() + 48) >= sonic.pixel_x()) {
            o.flags |= 0x01;
            o.vy = -640;
        }
        const bool left = o.pixel_x() >= sonic.pixel_x();
        o.vx = left ? -64 : 64;
        animate_object(o, left ? rom::jumper_swim_left : rom::jumper_swim_right,
                       rom::jumper_frames);
        if (left) {
            o.flags |= facing_left;
        } else {
            o.flags &= ~facing_left;
        }
    } else {
        animate_object(o, o.flags & facing_left ? rom::jumper_jump_left : rom::jumper_jump_right,
                       rom::jumper_frames);
        if (o.grounded()) {
            o.flags &= ~0x01;
        }
    }
}

// A platform that floats up once touched (90C5-91E7). It accelerates to
// 1 px/update until stopped by terrain class 30, and returns to its origin
// (words 0 and 2) when off screen. Flags: 0x01 rising, 0x02 initialised.
void Game::update_buoyant_platform(Object& o) {
    o.flags |= no_terrain;
    o.width = 30;
    o.height = 28;
    o.mapping = rom::buoyant_platform_layout;
    if (!(o.flags & 0x02)) {
        o.set_state_word(0, o.pixel_x());
        o.set_pixel_y(o.pixel_y() - 1);
        o.set_state_word(2, o.pixel_y());
        o.flags |= 0x02;
    }
    if ((level.block_attributes[level.block_at(o.pixel_x() + 16, o.pixel_y() + 32)] & 63) == 30) {
        o.vy = 0;
    } else if (o.flags & 0x01) {
        o.vy = signed24(o.vy - 8);
        if (((-((o.vy >> 8) & 255)) & 255) >= 2) {
            o.vy = -256;
        }
    }
    if (outside(o, *this, -32, 256, -32, 224)) {
        o.x = o.state_word(0) * 256;
        o.y = o.state_word(2) * 256;
        o.vy = 0;
        o.flags &= ~0x01;
        return;
    }
    if (!overlaps(o, 2, 14)) {
        return;
    }
    o.flags |= 0x01;
    // Not while Sonic is moving up faster than 1 px/update.
    const int high = (sonic.vy >> 8) & 255;
    if (high >= 128 && ((-high) & 255) >= 2) {
        return;
    }
    if (!sonic.hurt) {
        land_on_platform(o, 16, signed16(o.vy));
    }
}

// A large air bubble (96FA-9867). Touching it with the top of the head
// refills the air: Sonic stops and breathes for 32 updates, and the bubble pops.
void Game::update_air_bubble(Object& o) {
    auto& age = o.state[0];
    auto& pop_timer = o.state[1];
    o.flags |= no_terrain;
    o.mapping = 0;
    if (pop_timer && pop_timer < 8) {
        emit_sprite(o, 12, 4);
    } else {
        emit_sprite(o, 64);
        emit_sprite(o, 66, 8);
    }
    o.width = 10;
    o.height = 12;
    if (pop_timer) {
        o.vy = 0;
        if (!--pop_timer) {
            o.type = type::none;
        }
        o.vx = 0;
    } else {
        if (overlaps(o, 6, 2) && word(o.pixel_y() - 8) < sonic.pixel_y() &&
            word(o.pixel_y() + 6) >= sonic.pixel_y()) {
            sonic.vy = 0;
            sonic.jump_hold = 0;
            sonic.air_ticks = 0;
            sonic.breathing_ticks = 32;
            pop_timer = 16;
            sound(Effect::AirBubble);
        }
        o.vy = -104;
        if (!(age & 15)) {
            o.vx = (next_random() & 63) < 32 ? 32 : -32;
        }
    }
    if (bubble_gone(*this, o)) {
        o.type = type::none;
    }
    ++age;
}

// Smoke left by the homing shot (96AA-96F6): three frames, 12 updates each.
void Game::update_homing_trail(Object& o) {
    auto& frame_timer = o.state[0];
    auto& frame = o.state[1];
    o.flags |= no_terrain;
    o.mapping = 0;
    emit_sprite(o, level.data(rom::homing_trail_tiles + frame));
    if (++frame_timer >= 12) {
        frame_timer = 0;
        if (++frame >= 3) {
            o.type = type::none;
        }
    }
}

// The Labyrinth boss's homing shot (94A7-9689). It drops slowly until it is
// below Sonic, then steers toward him on both axes (at most 1 px/update),
// leaving a trail every 16 updates. Flags: 0x01 homing, 0x02 initialised,
// 0x04 started left of Sonic; word 0 drives the animation.
void Game::update_homing_shot(Object& o) {
    o.flags |= no_terrain;
    o.width = 8;
    o.height = 10;
    if (overlaps(o, 4, 4)) {
        sonic.take_hit(*this);
    }
    if (!(o.flags & 0x02)) {
        o.flags |= 0x02;
        if (word(o.pixel_x() + 8) < word(sonic.pixel_x() + 8)) {
            o.flags |= 0x04;
        }
    }
    if (!(o.flags & 0x01)) {
        o.vy = 64;
        o.mapping = o.flags & 0x04 ? rom::homing_shot_right : rom::homing_shot_left;
        if (sonic.pixel_y() < o.pixel_y()) {
            o.flags |= 0x01;
        }
        return;
    }
    if (word(o.pixel_x() - 16 + wide_left) < camera.x ||
        word(o.pixel_x() - 16) >= word(right_edge(256)) ||
        word(o.pixel_y() - 16 + wide_top) < camera.y || o.pixel_y() > word(bottom_edge(192))) {
        o.type = type::none;
    }
    o.vx = signed24(o.vx + (o.flags & 0x04 ? 2 : -2));
    if (std::abs(signed16(o.vx)) >= 256) {
        o.vx = o.vx < 0 ? -256 : 256;
    }
    const bool up = word(o.pixel_y() + 8) >= word(sonic.pixel_y() + 16);
    o.vy = signed24(o.vy + (up ? (o.vy < 0 ? -2 : -4) : (o.vy < 0 ? 4 : 2)));
    if (std::abs(signed16(o.vy)) >= 256) {
        o.vy = o.vy < 0 ? -256 : 256;
    }
    o.set_state_word(0, o.state_word(0) + std::abs(signed16(o.vx)));
    o.mapping =
        (o.vx < 0 ? rom::homing_shot_left : rom::homing_shot_right) + ((o.state_word(0) >> 8) & 8);
    if (!(ticks & 15)) {
        if (auto* trail = allocate_object(type::homing_trail)) {
            trail->x = word(o.pixel_x() + (o.vx < 0 ? 15 : -7)) * 256;
            trail->y = o.pixel_y() * 256;
            trail->state[0] = trail->state[1] = 0;
            trail->vx = trail->vy = 0;
        }
    }
}

// A ball with four orbiting spikes (AE4E-AFF6). It drifts slowly toward Sonic;
// state bytes 0..3 are the spikes' positions in a 164-step orbit table (AFF7),
// 254 meaning shed. After 200 updates of warm-up, and with Sonic level with it
// in Labyrinth, a spike passing position 74 is thrown as a loose spike. The
// body is an enemy contact; every spike always hurts.
void Game::update_spike_ball(Object& o) {
    auto& s = o.state;
    auto& warm_up = o.state[4];
    auto& orbit_step = o.state[5];
    o.flags |= no_terrain;
    if (o.first_update()) {
        s[0] = 0;
        s[1] = 42;
        s[2] = 82;
        s[3] = 124;
    }
    const bool left = o.pixel_x() >= sonic.pixel_x();
    o.vx = left ? -8 : 8;
    o.mapping = left ? rom::spike_ball_left : rom::spike_ball_right;
    if (warm_up == 200 && level.scene == 3 && word(o.pixel_y() - 48) < sonic.pixel_y() &&
        word(o.pixel_y() + 44) >= sonic.pixel_y()) {
        for (int i = 0; i < 4; ++i) {
            if (s[i] == 74) {
                s[i] = 254;
                if (auto* spike = allocate_object(type::loose_spike)) {
                    spike->x = word(o.pixel_x() + 18) * 256;
                    spike->y = word(o.pixel_y() + 30) * 256;
                    spike->vx = left ? -128 : 128;
                    spike->vy = 0;
                }
            }
        }
    }
    orbit_step = left ? 1 : 255;
    o.width = o.height = 28;
    enemy_contact(o, 18, 18, 16, 16);
    for (int i = 0; i < 4; ++i) {
        if (s[i] != 254) {
            const int at = rom::spike_orbit + (s[i] & 254);
            const int x = level.data(at);
            const int y = level.data(at + 1);
            emit_sprite(o, 36, x, y);
            o.width = (x + 6) & 255;
            o.height = (y + 6) & 255;
            if (overlaps(o, (x + 2) & 255, (y + 2) & 255)) {
                sonic.take_hit(*this);
            }
            const int next = (s[i] + orbit_step) & 255;
            s[i] = next == 255 ? 163 : next == 164 ? 0 : next;
        }
    }
    if ((ticks & 7) && warm_up < 200) {
        ++warm_up;
    }
}

// A thrown spike (B0BA-B131): it flies straight and ignores terrain.
void Game::update_loose_spike(Object& o) {
    o.flags |= no_terrain;
    o.mapping = 0;
    o.width = o.height = 4;
    if (overlaps(o, 2, 6)) {
        sonic.take_hit(*this);
    }
    if (outside(o, *this, -16, 272, -16, 208)) {
        o.type = type::none;
    } else {
        emit_sprite(o, 36);
    }
}

// The Labyrinth boss (9271-947C). It visits three fixed stations in turn: it
// moves into place, waits 100 updates and attacks, then moves out. At the
// first station it fires two arcing shots; at the others it drops a homing
// shot.
void Game::update_labyrinth_boss(Object& o) {
    auto& phase = o.state[0];
    auto& wait_timer = o.state[1];
    auto& station = o.state[2];
    o.flags |= no_terrain;
    o.width = 32;
    o.height = 28;
    o.flame_tile = -1;
    stop_streaming_in_arena();
    o.mapping = rom::lab_boss_layout;
    if (o.first_update()) {
        start_boss_fight(1376, 512, false);
    }
    if (!phase) {
        o.set_pixel_x(level.word(rom::lab_boss_stations + station * 4));
        o.set_pixel_y(level.word(rom::lab_boss_stations + 2 + station * 4));
        ++phase;
    } else if (phase == 1) {
        o.vy = station ? 128 : -128;
        if (o.pixel_y() == level.word(rom::lab_boss_arrive_y + station * 2)) {
            ++phase;
            wait_timer = 0;
        }
    } else if (phase == 2) {
        o.vy = 0;
        if (++wait_timer == 100) {
            ++phase;
            if (!station) {
                for (int i = 0; i < 2; ++i) {
                    if (auto* shot = allocate_object(type::projectile)) {
                        shot->x = word(o.pixel_x() + (i ? 32 : 4)) * 256;
                        shot->y = word(o.pixel_y() - 6) * 256;
                        shot->state[0] = 0;
                        shot->state[2] = 4; // Gravity.
                        for (int n = 3; n < 7; ++n) {
                            shot->state[n] = 0;
                        }
                        shot->vx = i ? 256 : -256;
                        shot->vy = -256;
                    }
                }
                sound(Effect::Break);
            } else if (boss.hits < 8) {
                if (auto* shot = allocate_object(type::homing_shot)) {
                    shot->x = word(o.pixel_x() + 15) * 256;
                    shot->y = word(o.pixel_y() + 34) * 256;
                    shot->flags = 0;
                    shot->vx = shot->vy = 0;
                }
            }
        }
    } else {
        o.vy = station ? -128 : 128;
        if (o.pixel_y() == level.word(rom::lab_boss_leave_y + station * 2)) {
            phase = 0;
            if (++station >= 3) {
                station = 0;
            }
        }
    }
    update_boss_contact(o, 0xa2);
    submit_lift_jet(o);
}

} // namespace sonic1_gg
