#pragma once
#include "sonic1_gg/audio/voice.hpp"
#include "sonic1_gg/frontend/session.hpp"

// Explicit snapshot field order. Changing fields requires a schema bump.
// Schema 2 appends invincibility-star and ring-sparkle state; schema 3 appends widescreen
// object staging; schema 4 appends the original death/game-over/continue flow and
// tile-screen scroll. Earlier saves remain readable.
// Immutable asset references are not saved; constructors rebind them on load.
namespace sonic1_gg {
struct StateAccess {
    template <class A>
    static void visit(A& a, Body& v) {
        a(v.x, v.y, v.vx, v.vy, v.width, v.height, v.flags, v.contact_class);
    }

    template <class A>
    static void visit(A& a, Sonic& v) {
        a(static_cast<Body&>(v));
        a(v.animation, v.animation_step, v.previous_animation, v.animation_fraction, v.jump_hold,
          v.idle_ticks, v.frame);
    }

    template <class A>
    static void visit(A& a, Object& v) {
        a(static_cast<Body&>(v));
        a(v.type, v.mapping, v.state, v.active, v.flame_tile, v.flame_x, v.flame_y, v.bridge_render,
          v.extra_sprites, v.flower_frames);
    }

    template <class A>
    static void visit(A& a, Camera& v) {
        a(v.x, v.y, v.left_gap, v.right_gap, v.top_gap, v.bottom_gap, v.vertical_bias);
    }

    template <class A>
    static void visit(A& a, Input& v) {
        a(v.left, v.right, v.jump, v.down, v.up);
    }

    template <class A>
    static void visit(A& a, Level& v) {
        a(v.act, v.scene, v.music, v.linked_area, v.initial_flags, v.columns, v.rows, v.camera_left,
          v.camera_right, v.camera_top, v.camera_bottom, v.spawn_x, v.spawn_y, v.layout, v.blocks,
          v.block_attributes, v.electric_palettes, v.palette_cycle, v.background_patterns,
          v.object_patterns, v.palette, v.player_patterns, v.left_contours, v.right_contours,
          v.ceiling_contours, v.floor_contours, v.floor_push, v.wall_push, v.vertical_tolerance,
          v.animation_data, v.placements, v.active_bounds, v.terrain_actions, v.boss_patterns,
          v.boss_palette, v.capsule_patterns, v.tile_overrides, v.common_patterns, v.monitor_icons,
          v.goal_patterns, v.goal_palette, v.ambient_patterns, v.lost_ring_patterns,
          v.emerald_patterns, v.object_tables);
    }

    template <class A>
    static void visit(A& a, Game& v) {
        // Two retired fields keep their place in the format: a "game over" flag
        // (always false; the Session owns game over) and a former
        // debugging switch (always true). Values read from a save are ignored.
        bool retired_game_over = false;
        bool retired_world_enabled = true;
        a(v.level, v.sonic, v.camera, v.objects, v.foreground_clouds, v.ticks, v.rings, v.score,
          v.lives, v.elapsed_ticks, v.death_count, v.next_life_score_bcd, v.flash.written,
          v.sprite_palette_kind, v.sonic.previous_flags, v.palette_ticks, v.palette_phase,
          v.ambient.ticks, v.ambient.phase, v.ambient.pending, v.ambient.uploaded,
          v.sonic.previous_gravity, v.pending_camera_top, v.pending_camera_bottom,
          v.sonic.shoes_ticks, v.sonic.invincible_ticks, v.sonic.death_ticks, v.finish_ticks,
          v.progress.emerald_acts, v.progress.emerald_count, v.music_restore_ticks, v.checkpoint_x,
          v.checkpoint_y, v.goal.rings, v.goal.outcome, v.goal.entry_speed, v.goal.bonus_table,
          v.sonic.hurt, v.sonic.dead, v.sonic.shield, v.sonic.lost_ring_guard, v.goal.started,
          v.complete, retired_game_over, v.progress.life_monitor_acts,
          v.progress.special_stage_pending, v.special_mode, v.special.time_out,
          v.progress.continue_collected, v.special.bumper_ticks, v.special.bumper_x,
          v.special.bumper_y, v.flash.color, v.flash.value, v.boss.final_barrier, v.boss.orb_active,
          v.ending.sonic_hidden, v.ending.camera_scripted, v.ending.drop, v.ending.collect,
          v.ending.emerald_spawned, v.ending.emerald_pose, v.progress.ever_died,
          v.ending.pose_ticks, v.progress.life_monitor_count, v.sway.offset, v.sway.velocity,
          v.sway.acceleration, v.sway.camera_fraction, v.teleport_ticks, v.teleport_table,
          v.electric.ticks, v.electric.palette, v.area_transition, v.area_advance, v.area_target,
          v.water.y, v.water.scanline, v.sonic.air_ticks, v.sonic.breathing_ticks,
          v.sonic.current_ticks, v.sonic.air_digit, v.sonic.drowned, v.water.boss_palette,
          v.progress.switch_acts, v.water.wave_sprites, v.tile_patch.x, v.tile_patch.y,
          v.tile_patch.table, v.horizontal_scroll, v.demo_mode, v.demo_ended, v.sonic.life_debited,
          v.warmup_ticks, retired_world_enabled, v.current_input, v.sound_events, v.boss.arena_x,
          v.boss.arena_y, v.boss.hits, v.flash.ticks, v.boss.explosion_ticks, v.boss.defeat_phase,
          v.random_seed, v.terrain_streaming, v.auto_run, v.shared_platform_y, v.sonic.skid_ticks);
        if constexpr (A::reading) {
            if (a.version < 2) {
                // Schema 1 predates D2F4/D2F5. Use the monitor's initial phases.
                v.sonic.star_phase = {0x18, 0};
                v.ring_sparkle_x = v.ring_sparkle_y = v.ring_sparkle_ticks = 0;
                v.effect_sprites.clear();
                return;
            }
        }
        a(v.sonic.star_phase, v.ring_sparkle_x, v.ring_sparkle_y, v.ring_sparkle_ticks,
          v.effect_sprites);
        if constexpr (A::reading) {
            if (a.version < 3) {
                // Schema 2 predates widescreen staging; objects restage on their next scan.
                for (auto& o : v.objects) {
                    o.staged = false;
                    o.staged_animation = {};
                }
                return;
            }
        }
        for (auto& o : v.objects) {
            a(o.staged, o.staged_animation);
        }
        if constexpr (A::reading) {
            if (a.version < 4) {
                v.death_finished = false; // Schema 3 respawned immediately.
                return;
            }
        }
        a(v.death_finished);
    }

    template <class A>
    static void visit(A& a, SoundEvent& v) {
        a(v.kind, v.value);
    }

    template <class A>
    static void visit(A& a, FrontEnd& v) {
        a(v.scene, v.screen, v.act, v.lives, v.demo_cycle, v.ticks, v.scene_ticks, v.demo,
          v.voice_requested, v.sound_events, v.animation_pointer, v.animation_remaining, v.mapping,
          v.blink, v.route_x, v.route_y, v.logo_x, v.logo_step, v.step_ticks, v.after_fade_,
          v.fade_target_, v.voice_ticks_, v.map_ticks_, v.map_intro_ticks_);
    }

    template <class A>
    static void visit(A& a, Results& v) {
        a(v.phase, v.screen, v.score, v.rings, v.lives, v.time_bonus, v.ring_value, v.continues,
          v.next_life_score_bcd, v.special, v.ticks, v.phase_ticks, v.sound_counter,
          v.sound_events);
    }

    template <class A>
    static void visit(A& a, Ending& v) {
        a(v.phase, v.ticks, v.phase_ticks, v.screen, v.score, v.lives, v.emeralds, v.bonus,
          v.tally_stage, v.tally_timer, v.sound_events, v.text_pointer, v.text_x, v.text_y,
          v.text_wait, v.scroll_rows, v.actor_pointer, v.actor_ticks, v.fade_target_, v.tally_step_,
          v.tally_wait_, v.pending_tile_, v.pending_letter_);
    }

    template <class A>
    static void visit(A& a, SoundDriver::Voice& v) {
        a(v.tone_latch, v.volume_latch, v.remaining, v.volume, v.period, v.detune, v.vibrato_offset,
          v.envelope, v.stage, v.shape, v.vibrato_delay, v.vibrato_rate, v.vibrato_steps,
          v.vibrato_delta, v.delay_left, v.rate_left, v.steps_left, v.delta, v.gate, v.octave,
          v.default_length, v.noise, v.unit, v.song_base, v.position, v.repeat_position, v.flags,
          v.index, v.target_volume, v.loop_depth, v.loop_initialized, v.loops);
    }

    template <class A>
    static void visit(A& a, SoundDriver& v) {
        a(v.voices_, v.output_, v.output_initialized_, v.flags_, v.effect_channel_, v.priority_,
          v.last_noise_, v.unit_, v.tempo_, v.normal_tempo_, v.effect_tempo_, v.fade_left_,
          v.fade_step_, v.writes_);
    }

    template <class A>
    static void visit(A& a, gg::TileSprite& v) {
        a(v.x, v.y, v.tile);
    }

    template <class A>
    static void visit(A& a, gg::TileScreen& v) {
        a(v.patterns, v.tiles, v.palette, v.sprites, v.enabled);
        if constexpr (A::reading) {
            if (a.version < 4) {
                v.scroll_y = 0;
                return;
            }
        }
        a(v.scroll_y);
    }

    template <class A>
    static void visit(A& a, GameOver& v) {
        a(v.phase, v.continued, v.screen, v.continues, v.digit, v.phase_ticks, v.sound_events);
    }

    template <class A>
    static void visit(A& a, gg::Psg& v) {
        v.serialize(a);
    }

    template <class A>
    static void visit(A& a, LogoVoice& v) {
        a(v.position_, v.clock_, v.playing_);
    }

    template <class A>
    static void visit(A& a, Session& v) {
        a(v.front, v.fading_game, v.paused, v.voice_requested, v.demo_pointer, v.sound_events,
          v.next_special, v.return_act, v.continues, v.special_attempts, v.special_successes,
          v.special_ring_value, v.pending_area_, v.advance_area_, v.carry_progress_,
          v.previous_start_);
        bool selected = v.selected_level_.has_value();
        a(selected);
        if constexpr (A::reading) {
            if (selected) {
                v.selected_level_.emplace();
            } else {
                v.selected_level_.reset();
            }
        }
        if (selected) {
            a(*v.selected_level_);
            if constexpr (A::reading) {
                // Immutable shield art was added after schema 1. Rebind it from
                // installed assets so older quick saves remain compatible.
                v.selected_level_->shield_patterns = v.green_hill_.shield_patterns;
            }
        }
        bool game = bool(v.game);
        a(game);
        if constexpr (A::reading) {
            if (game && !selected) {
                throw std::runtime_error("Save has no level source");
            }
            v.game = game ? std::make_unique<Game>(*v.selected_level_) : nullptr;
        }
        if (game) {
            a(*v.game);
        }
        bool results = bool(v.results);
        a(results);
        if constexpr (A::reading) {
            if (results && !game) {
                throw std::runtime_error("Save has no results game");
            }
            v.results =
                results ? std::make_unique<Results>(v.flow_assets_, 0, 0, 0, 3, 0, 0) : nullptr;
        }
        if (results) {
            a(*v.results);
        }
        bool ending = bool(v.ending);
        a(ending);
        if constexpr (A::reading) {
            if (ending && !game) {
                throw std::runtime_error("Save has no ending game");
            }
            v.ending = ending ? std::make_unique<Ending>(v.flow_assets_, 0, 3, 0, 0, 0, 0, false,
                                                         v.game->level.object_patterns)
                              : nullptr;
        }
        if (ending) {
            a(*v.ending);
        }
        bool game_over = bool(v.game_over);
        if constexpr (A::reading) {
            if (a.version < 4) {
                game_over = v.respawn_pending_ = false;
                v.game_over.reset();
            } else {
                a(game_over, v.respawn_pending_);
            }
            if (game_over && !game) {
                throw std::runtime_error("Save has no game-over game");
            }
            v.game_over = game_over ? std::make_unique<GameOver>(v.flow_assets_, 0) : nullptr;
        } else {
            a(game_over, v.respawn_pending_);
        }
        if (game_over) {
            a(*v.game_over);
        }
        if constexpr (A::reading) {
            if (v.front.scene == FrontEnd::Scene::GameOver && !game_over && !v.fading_game) {
                throw std::runtime_error("Save has no game-over screen");
            }
            if ((v.gameplay() || v.fading_game) && !game) {
                throw std::runtime_error("Save has no active game");
            }
            if (v.front.scene == FrontEnd::Scene::Results && !results && !ending) {
                throw std::runtime_error("Save has no result screen");
            }
        }
    }
};
} // namespace sonic1_gg
