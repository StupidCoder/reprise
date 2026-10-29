// The act as a whole: initial state, the main update (in the original order),
// palette effects, the camera, and effect sprites. Sonic's own code is in
// sonic.cpp, terrain collision in terrain.cpp, the objects in objects.cpp and
// the zone files.

#include "sonic1_gg/game/game.hpp"
#include "sonic1_gg/game/arithmetic.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace sonic1_gg {

using namespace body_flag;

Game::Game(const Level& assets) : source(assets), level(assets) {
    reset();
}

// Puts the act into its initial state. A few values deliberately survive (see
// the comments); the Session restores others when it moves between acts.
void Game::reset() {
    level = source;
    // Sonic starts afresh, except for the invincibility star positions (D2F4/
    // D2F5), which the original does not reset between acts or restarts.
    const auto star_phase = sonic.star_phase;
    sonic = {};
    sonic.star_phase = star_phase;
    camera = {};

    // The state groups.
    progress = {};
    goal = {};
    flash = {};
    ambient = {};
    tile_patch = {};
    electric = {};
    sway = {};
    sway.acceleration = -2; // The sway starts upward.
    special = {};
    ending = {};
    boss = {};
    boss.final_barrier = level.final_barrier();
    // Labyrinth 3 (act 11, the boss act) starts underwater.
    water = {};
    water.y = level.act == 11 ? 32 : 2048;
    water.scanline = level.act == 11 ? 255 : 0;
    water.boss_palette = level.underwater_boss();

    // The act's music starts with the act.
    sound_events.clear();
    if (level.music != 255) {
        sound_events.push_back({SoundEvent::Kind::Music, level.music});
    }
    effect_sprites.clear();
    ring_sparkle_ticks = 0;

    // Level features.
    horizontal_scroll = level.auto_scrolls();
    terrain_streaming = true;
    teleport_ticks = teleport_table = 0;
    area_transition = area_advance = false;
    area_target = 0;

    // Score, clock and the end of the act.
    ticks = warmup_ticks = 0;
    rings = score = elapsed_ticks = death_count = 0;
    lives = 3;
    next_life_score_bcd = 5;
    random_seed = 0;
    pending_camera_top = pending_camera_bottom = 0;
    checkpoint_x = checkpoint_y = 0;
    finish_ticks = music_restore_ticks = 0;
    auto_run = complete = death_finished = demo_ended = false;

    // Palettes.
    sprite_palette_kind = 0;
    palette_ticks = level.palette_cycle[0];
    palette_phase = 0;

    // Special stages (acts 28..35): the first four last two minutes, the rest one
    // minute (1B51-1B68). The clock counts down.
    special_mode = level.act >= 28;
    if (special_mode) {
        elapsed_ticks = (level.act < 32 ? 120 : 60) * 60;
    }

    reset_objects();
    sonic.x = level.spawn_x * 256;
    sonic.y = level.spawn_y * 256;
    camera.x = std::max(int(level.camera_left), int(level.spawn_x) - 88);
    camera.y = std::clamp(int(level.spawn_y) - 64, int(level.camera_top), int(level.camera_bottom));
}

void Game::start_act(bool demo) {
    reset();
    demo_mode = demo;
    // The loader (185D/1AB3) places Sonic with a zeroed collision box and then
    // runs sixteen updates without input (1472) while the screen fades in; those
    // updates drop Sonic onto the floor and settle the camera.
    warmup_ticks = 16;
    sonic.width = sonic.height = sonic.flags = 0;
    sonic.animation = sonic.previous_animation = 0;
    // The camera starts three 32-pixel cells up and left of the spawn cell.
    camera.x = std::max(0, int(level.spawn_x) / 32 - 3) * 32;
    camera.y = std::max(0, int(level.spawn_y) / 32 - 3) * 32;
    if (horizontal_scroll) {
        level.camera_left = level.camera_right = camera.x; // 1668
    }
}

void Game::restart_from_checkpoint(int remaining_lives) {
    // Called by the Session after a death. Everything is reloaded like a fresh
    // act start (185D/1AB3 -> 1472), except the progress the cartridge keeps
    // across a restart. Whether a life was debited is decided by the caller:
    // the original's 1812 check reads D240 as it stands, and a life is only
    // taken once Sonic has fallen below the view (5701).
    const int saved_monitors = progress.life_monitor_count;
    const bool saved_died = progress.ever_died;
    const auto used_life_monitors = progress.life_monitor_acts;
    const auto switches = progress.switch_acts;
    const auto emeralds = progress.emerald_acts;
    const int saved_emerald_count = progress.emerald_count;
    const auto threshold = next_life_score_bcd;
    const int saved_score = score;
    const int saved_checkpoint_x = checkpoint_x;
    const int saved_checkpoint_y = checkpoint_y;
    const int saved_deaths = death_count;

    start_act(false);

    next_life_score_bcd = threshold;
    lives = remaining_lives;
    score = saved_score;
    death_count = saved_deaths;
    checkpoint_x = saved_checkpoint_x;
    checkpoint_y = saved_checkpoint_y;
    // Dying in a linked room restarts its clock at 1:30 (18D8/1B4E).
    if (source.linked_area) {
        elapsed_ticks = 90 * 60;
    }
    progress.life_monitor_count = saved_monitors;
    progress.ever_died = saved_died;
    progress.life_monitor_acts = used_life_monitors;
    progress.switch_acts = switches;
    progress.emerald_acts = emeralds;
    progress.emerald_count = saved_emerald_count;

    // A Labyrinth checkpoint restart begins fully submerged (18AC-18CF).
    if (saved_checkpoint_x && level.act >= 9 && level.act <= 10) {
        water.y = 32;
        water.scanline = 255;
    }
    if (saved_checkpoint_x) {
        sonic.x = saved_checkpoint_x * 256;
        sonic.y = saved_checkpoint_y * 256;
        // The camera starts three cells before the spawn cell (195C-1997).
        camera.x = std::max(0, saved_checkpoint_x / 32 - 3) * 32;
        camera.y = std::max(0, saved_checkpoint_y / 32 - 3) * 32;
        if (horizontal_scroll) {
            level.camera_left = level.camera_right = camera.x;
        }
        checkpoint_x = checkpoint_y = 0; // The checkpoint is used up (193E-1942).
    }
}

// One update of the act's main loop (281E-28D2). The order matters and follows
// the cartridge: palette work, the camera (following Sonic's PREVIOUS position),
// all objects, then Sonic himself.
void Game::tick(Input input) {
    rings_lost_this_tick = 0;
    sound_events.clear();
    water.wave_sprites.clear();
    effect_sprites.clear();
    sonic.air_digit = -1;

    // The first video interrupt of the update releases the tile patch slot.
    tile_patch.x |= 0xff00;

    if (complete || death_finished) {
        return;
    }

    // Animated background tiles (waterfalls, Jungle's water). 161E uploads the
    // frame selected by the PREVIOUS update.
    if (level.animated_tiles()) {
        if (ambient.pending >= 0 && ambient.pending != ambient.uploaded) {
            std::copy_n(level.ambient_patterns.begin() + ambient.pending * 128, 128,
                        level.background_patterns.begin() + 0x1f80);
            ambient.uploaded = ambient.pending;
        }
        if (level.scene == 0 && ambient.ticks == 5) {
            std::copy_n(level.ambient_patterns.begin() + 0x300 + (ambient.phase & 1) * 128, 128,
                        level.background_patterns.begin() + 0x180);
        }
    }

    // The act clock. Normal acts allow ten minutes (36,000 updates).
    if (!warmup_ticks && !goal.started && !sonic.dead) {
        if (special_mode) {
            update_special_clock();
        } else if (++elapsed_ticks >= 36000) {
            elapsed_ticks = 0;
            sonic.die(*this);
        }
    }

    // After the goal Sonic runs right by himself until 66 pixels past the right
    // edge of the LCD (274 = 48 + 160 + 66 in camera coordinates). In a wider
    // view the stop moves out by the extra margin, so he still leaves the picture.
    if (auto_run) {
        input = {false, true, false, false, false};
        if (sonic.pixel_x() >= level.camera_left + 274 + wide_right) {
            input = {};
            sonic.vx = sonic.vy = 0;
        }
    }

    // Scrap Brain's electric floors (16E4): a 171-step cycle on every second
    // update. Steps 100 and 130 switch to the warning and live palettes; the
    // floor is dangerous from step 130 (see terrain_interaction()).
    if (!warmup_ticks && level.electric_cycle() && (ticks & 1)) {
        const bool change = electric.ticks > 170 || electric.ticks == 0 || electric.ticks == 100 ||
                            electric.ticks == 130;
        if (electric.ticks > 170) {
            electric.ticks = 0;
        }
        if (change) {
            electric.palette = electric.ticks == 100 ? 1 : electric.ticks == 130 ? 2 : 0;
            palette_ticks = 2;
            palette_phase = 0;
            if (electric.ticks == 130) {
                sound(Effect::Electric);
            }
        }
        ++electric.ticks;
    }

    // Background palette cycling (1B1E / 0586), independent of the sprites. The
    // current phase is shown before its timer advances.
    if (!warmup_ticks) {
        if (level.electric_cycle()) {
            std::copy_n(level.electric_palettes.begin() + electric.palette * 128 +
                            palette_phase * 32,
                        32, level.palette.begin());
        } else {
            std::copy_n(level.palette_cycle.begin() + 2 + palette_phase * 32, 32,
                        level.palette.begin());
        }
        // Sprite colour 0 mirrors background colour 0 (the border colour).
        level.palette[32] = level.palette[0];
        level.palette[33] = level.palette[1];
        if (--palette_ticks == 0) {
            palette_ticks = level.electric_cycle() ? 2 : level.palette_cycle[0];
            palette_phase =
                (palette_phase + 1) % (level.electric_cycle() ? 4 : level.palette_cycle[1]);
        }
    }

    // Single-colour flashes: a hit boss, special stage bumpers. The interrupt
    // reloads the base palette every frame (0172/0586) and 1696 then overrides
    // one colour on odd updates. This must come AFTER the background copy above,
    // or the special stage bumper flashes would disappear entirely.
    flash.written = false;
    if (!warmup_ticks) {
        // flash_color: low nibble = palette entry, bit 7 = sprite palette.
        const unsigned offset = (flash.color & 15) * 2 + (flash.color & 128 ? 32 : 0);
        const auto& colors = sprite_palette_kind == 1   ? level.boss_palette
                             : sprite_palette_kind == 2 ? level.goal_palette
                                                        : source.palette;
        const unsigned source_offset = sprite_palette_kind == 1 ? 30 : offset;
        if (flash.color & 128) {
            level.palette[offset] = colors[source_offset];
            level.palette[offset + 1] = colors[source_offset + 1];
        }
        if ((ticks & 1) && flash.ticks) {
            flash.written = true;
            --flash.ticks;
            const bool lit = flash.ticks & 1;
            level.palette[offset] = lit ? flash.value & 255 : colors[source_offset];
            level.palette[offset + 1] = lit ? flash.value >> 8 : colors[source_offset + 1];
        }
    }

    // During the fade-in warm-up there is no input and the camera uses fixed gaps.
    const bool warming = warmup_ticks != 0;
    if (warmup_ticks) {
        input = {};
        camera.left_gap = 80;
        camera.right_gap = 88;
        camera.top_gap = 64;
        camera.bottom_gap = 72;
        --warmup_ticks;
    }
    current_input = input;
    ++ticks;

    // Auto-scrolling acts push both horizontal camera limits right every second
    // update (1672; it runs after the tick counter increments, not in warm-up).
    if (horizontal_scroll && !warming && (ticks & 1)) {
        level.camera_right = ++level.camera_left;
    }

    if (!sonic.dead) {
        update_camera();
    }
    update_wide_view(); // Widescreen enhancement: margins of the presented view.
    pending_camera_top = pending_camera_bottom = 0;

    // 1AF1 picks the next ambient tile frame, then advances its ten-update timer.
    static constexpr int ambient_frames[] = {5, 4, 3, 2, 1, 0};
    if (level.animated_tiles()) {
        ambient.pending = ambient_frames[ambient.phase];
        if (++ambient.ticks >= 10) {
            ambient.ticks = 0;
            ambient.phase = (ambient.phase + 1) % 6;
        }
    }

    update_objects();

    if (sonic.dead) {
        sonic.update_dead(*this);
        if ((ticks & 1) && --sonic.death_ticks <= 0) {
            // The Session continues with the fade, game over or restart (17F2).
            if (demo_mode) {
                demo_ended = true;
            } else {
                death_finished = true;
            }
        }
        return;
    }

    // Invincibility counts down every fourth update; the act music returns when
    // it ends (unless the act is already finishing).
    if ((ticks & 3) == 0 && sonic.invincible_ticks > 0 && --sonic.invincible_ticks == 0 &&
        !finish_ticks) {
        if (level.music != 255) {
            sound_events.push_back({SoundEvent::Kind::Music, level.music});
        }
    }

    input = current_input;
    if (sonic.hurt) {
        // Knock-back: no control, and Sonic uncurls.
        input = {};
        sonic.flags &= 0xff & ~curled;
    }
    if (!ending.sonic_hidden) {
        sonic.update(*this, input);
        sonic.move(*this);
    }
    // A death started by the terrain during this update ticks once already
    // (1591-15A6).
    if (sonic.dead && (ticks & 1) && sonic.death_ticks) {
        --sonic.death_ticks;
    }

    // After the goal sign lands: count down to the end of the act. At count 136
    // the sign's face pays out (173D / 17C7).
    if (finish_ticks && !(ticks & 1)) {
        --finish_ticks;
        if (finish_ticks == 136) {
            if (goal.outcome == 1) {
                sound(Effect::SignReward);
            } else if (goal.outcome == 2) {
                ++lives;
                sound(Effect::ExtraLife);
            } else if (goal.outcome == 3) {
                add_rings(10);
            } else if (goal.outcome == 4) {
                sound(Effect::SpecialStage);
                progress.special_stage_pending = special_mode = true;
            }
        }
        if (!finish_ticks) {
            complete = true;
        }
    }
}

// The camera (286F-2B0D).
//
// Sonic moves freely inside a dead zone; when he leaves it, the camera follows
// at up to 8 pixels per update. The dead zone's edges ease toward their targets
// one pixel per update: facing left moves the zone right so that more of the
// level ahead is visible. Objects can request a different vertical zone (moving
// platforms), and bosses ease the camera limits into their arena.
void Game::update_camera() {
    auto& c = camera;
    if (ending.camera_scripted) {
        return;
    }
    // 2B16 / 2B34: a boss arena moves both limits one pixel toward its target.
    if (boss.arena_x && boss.arena_x != level.camera_left) {
        level.camera_right = level.camera_left = approach(level.camera_left, boss.arena_x);
    }
    if (boss.arena_y && boss.arena_y != level.camera_top) {
        level.camera_bottom = level.camera_top = approach(level.camera_top, boss.arena_y);
    }

    c.left_gap = approach(c.left_gap, sonic.facing_left() ? 144 : 80);
    c.right_gap = approach(c.right_gap, sonic.facing_left() ? 152 : 88);
    c.top_gap = approach(c.top_gap, pending_camera_top ? pending_camera_top : 64);
    c.bottom_gap = approach(c.bottom_gap, pending_camera_bottom ? pending_camera_bottom
                                          : level.sways()       ? 128
                                                                : 96);

    // Moves the camera at most 8 pixels toward keeping `position` inside
    // [camera + low, camera + high]. 29DF / 2A9E reject a move that would wrap
    // the 16-bit camera position, before the level limits are applied.
    auto follow = [](int position, int camera_position, int low, int high) {
        if (position < camera_position + low) {
            const int next = camera_position - std::min(8, camera_position + low - position);
            return next < 0 ? camera_position : next;
        }
        if (position > camera_position + high) {
            const int next = camera_position + std::min(8, position - camera_position - high);
            return next > 65535 ? camera_position : next;
        }
        return camera_position;
    };

    // Auto-scrolling acts move the camera only through the limits.
    const int follow_x =
        horizontal_scroll ? c.x : follow(sonic.pixel_x(), c.x, c.left_gap, c.right_gap);
    c.x = std::clamp(follow_x, int(level.camera_left), int(level.camera_right));

    if (level.sways()) {
        // Sky Base (2B58): the whole view sways up and down. sky_velocity is a
        // signed 16-bit speed with an acceleration of +-2 that reverses once the
        // accumulated offset passes +-32 pixels; it is capped at +-2 pixels per
        // update. The cap tests the magnitude of the integer byte first.
        sway.velocity = (sway.velocity + sway.acceleration) & 65535;
        if (sway.velocity & 32768) {
            sway.velocity -= 65536;
        }
        int high = (sway.velocity >> 8) & 255;
        if (high >= 128) {
            high = (-high) & 255;
        }
        if (high >= 2) {
            sway.velocity = sway.velocity < 0 ? -512 : 512;
        }
        // The camera Y and the riding platforms share this 24-bit displacement.
        const int camera_fixed = (c.y * 256 + sway.camera_fraction + sway.velocity) & 0xffffff;
        c.y = camera_fixed >> 8;
        sway.camera_fraction = camera_fixed & 255;
        sway.offset = (sway.offset + sway.velocity) & 0xffffff;
        const int offset = signed16(sway.offset >> 8);
        if (offset < -32) {
            sway.acceleration = 2;
        } else if ((offset & 65535) >= 32) {
            sway.acceleration = -2;
        }

        // Vertical following uses its own dead zone here, and moves at most 7
        // pixels when Sonic is less than 256 pixels away.
        const int top = c.y + 32;
        const int bottom = c.y + c.bottom_gap;
        const int sonic_y = sonic.pixel_y();
        if (sonic_y <= top) {
            const int distance = top - sonic_y;
            const int next = c.y - std::min(distance, distance < 256 ? 7 : 8);
            if (next >= 0) {
                c.y = next;
            }
        } else if (sonic_y > bottom) {
            const int distance = sonic_y - bottom;
            c.y += std::min(distance, distance < 256 ? 7 : 8);
        }
        c.y = std::clamp(c.y, int(level.camera_top), int(level.camera_bottom));
    } else {
        // Looking up or down shifts the vertical dead zone (vertical_bias).
        c.y = std::clamp(follow(sonic.pixel_y(), c.y, c.top_gap + c.vertical_bias,
                                c.bottom_gap + c.vertical_bias),
                         int(level.camera_top), int(level.camera_bottom));
    }
}

void Game::update_ring_sparkle() {
    // 5063: the timer is decremented first. Two tiles while it is at least 3,
    // then a single smaller tile at the ring's position.
    const int left = --ring_sparkle_ticks;
    if (left >= 3) {
        queue_effect_sprite(0xb2, ring_sparkle_x, ring_sparkle_y - 2);
        queue_effect_sprite(0x5a, ring_sparkle_x + 8, ring_sparkle_y + 2);
    } else {
        queue_effect_sprite(0x5a, ring_sparkle_x, ring_sparkle_y - 2);
    }
}

void Game::queue_effect_sprite(int tile, int x, int y) {
    // 2F5D queues a sprite only when its camera-relative X is 0..255 and Y is
    // -16..191, in 16-bit words. World positions are kept so that a widescreen
    // view (whose horizontal window grows by its margins) can draw them too.
    const int sx = (x - camera.x + wide_left) & 65535;
    const int sy = (y - camera.y) & 65535;
    if (sx < 256 + wide_left + wide_right && (sy >= 0xfff0 || sy < 0xc0)) {
        effect_sprites.push_back(
            {tile, camera.x - wide_left + sx, camera.y + (sy >= 0xfff0 ? sy - 65536 : sy)});
    }
}

} // namespace sonic1_gg
