// The top-level game flow; see session.hpp.

#include "sonic1_gg/frontend/session.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {
Session::Session(const FlowAssets& flow, const Level& green_hill, const Level& jungle,
                 const Level* act2, const Level* act3, const std::vector<Level>* special,
                 const std::map<unsigned, Level>* additional)
    : front(flow), flow_assets_(flow), green_hill_(green_hill), jungle_(jungle), act2_(act2),
      act3_(act3), special_(special), additional_(additional) {}

void Session::reset() {
    pending_area_ = -1;
    advance_area_ = false;
    next_special = 28;
    return_act = continues = special_attempts = special_successes = special_ring_value = 0;
    front.reset();
    game.reset();
    results.reset();
    ending.reset();
    game_over.reset();
    respawn_pending_ = false;
    carry_progress_ = false;
    fading_game = paused = previous_start_ = false;
    sound_events = front.sound_events;
}

// Creates the Game for front.act (or the pending linked room). The cartridge's
// level loader leaves some RAM untouched, so a few values survive from the
// previous act even without carry_progress_ (the random seed, the pending tile
// patch, Sonic's animation accumulator D2E1, the boss flash and the pending
// camera gaps). Moving on to the next act (carry_progress_) also keeps the
// score, emeralds, switch bits and life monitor history.
void Session::begin_act() {
    int patch_y = game ? game->tile_patch.y : 0, patch_table = game ? game->tile_patch.table : 0;
    int fraction = game ? game->sonic.animation_fraction : 0;
    int flash_ticks = game ? game->flash.ticks : 0, flash_color = game ? game->flash.color : 0,
        flash_value = game ? game->flash.value : 0;
    int top = game ? game->pending_camera_top : 0, bottom = game ? game->pending_camera_bottom : 0;
    unsigned threshold = game && carry_progress_ ? game->next_life_score_bcd : 5;
    int score = game && carry_progress_ ? game->score : 0;
    unsigned counter = game && carry_progress_ ? game->ticks : 0;
    unsigned rng = game ? game->random_seed : 0;
    unsigned switches = game && carry_progress_ ? game->progress.switch_acts : 0;
    unsigned emeralds = game && carry_progress_ ? game->progress.emerald_acts : 0;
    int count = game && carry_progress_ ? game->progress.emerald_count : 0;
    auto life_bits = game && carry_progress_ ? game->progress.life_monitor_acts : 0;
    int monitors = game && carry_progress_ ? game->progress.life_monitor_count : 0;
    bool died = game && carry_progress_ && game->progress.ever_died;
    bool shield =
        game && carry_progress_ && game->sonic.shield; // 1A76 preserves the prior shield bit.
    int old_flags = game ? game->sonic.previous_flags : 0;
    Camera previous_camera = game ? game->camera : Camera{};
    auto previous_objects = game ? game->objects : std::array<Object, 32>{};
    if (front.act == 1 && !act2_) {
        throw std::runtime_error("Act 2 assets are required for progression");
    }
    if (front.act == 2 && !act3_) {
        throw std::runtime_error("Act 3 assets are required for progression");
    }
    if (front.act >= 28 && (!special_ || front.act >= 28 + special_->size())) {
        throw std::runtime_error("Special stage assets are required for progression");
    }
    const Level* selected = nullptr;
    const bool area_reload = pending_area_ >= 0;
    const int old_rings = game ? game->rings : 0, old_elapsed = game ? game->elapsed_ticks : 0;
    const auto previous_layout = game ? game->level.layout : Bytes{};
    if (front.act >= 28) {
        selected = &special_->at(front.act - 28);
    } else if (additional_ && additional_->contains(front.act)) {
        selected = &additional_->at(front.act);
    } else if (front.act == 6 && front.demo) {
        selected = &jungle_;
    } else if (front.act <= 2) {
        selected = front.act == 2 ? act3_ : front.act == 1 ? act2_ : &green_hill_;
    }
    if (area_reload) {
        if (!additional_ || !additional_->contains(pending_area_)) {
            throw std::runtime_error("Linked area is not reconstructed yet");
        }
        selected = &additional_->at(pending_area_);
    }
    if (!selected) {
        throw std::runtime_error("This act is not reconstructed yet");
    }
    selected_level_ = *selected;
    // 141C selects the linked-area header while D238 retains the logical act.
    // 0A73 writes only the compressed payload; area 21 leaves C800–CFFF intact.
    if (selected_level_->layout.size() < 4096) {
        if (previous_layout.size() != 4096) {
            throw std::runtime_error("Partial area requires its preceding layout");
        }
        auto payload = selected_level_->layout;
        selected_level_->layout = previous_layout;
        std::copy(payload.begin(), payload.end(), selected_level_->layout.begin());
    }
    selected_level_->linked_area = selected->act >= 20 && selected->act < 28;
    selected_level_->act = front.act;
    game = std::make_unique<Game>(*selected_level_);
    game->start_act(front.demo);
    game->lives = front.lives;
    if (area_reload) {
        game->rings = old_rings;
        game->elapsed_ticks = old_elapsed;
        pending_area_ = -1;
        advance_area_ = false;
    }
    if (carry_progress_) {
        // 1A80 resets placed records, but the unused tail keeps its old body
        // bytes with type FF. Later temporary-object allocation reuses them.
        for (unsigned i = game->level.placements.size() / 3 + 1; i < 32; ++i) {
            game->objects[i] = previous_objects[i];
            game->objects[i].type = 255;
            game->objects[i].active = false;
        }
        game->camera.left_gap = previous_camera.left_gap;
        game->camera.right_gap = previous_camera.right_gap;
        game->camera.top_gap = previous_camera.top_gap;
        game->camera.bottom_gap = previous_camera.bottom_gap;
    }
    game->progress.life_monitor_acts = life_bits;
    game->progress.life_monitor_count = monitors;
    game->progress.ever_died = died;
    game->tile_patch.y = patch_y;
    game->tile_patch.table = patch_table;
    game->progress.switch_acts = switches;
    game->random_seed = rng;
    game->score = score;
    game->next_life_score_bcd = threshold;
    game->ticks = counter;
    game->progress.emerald_acts = emeralds;
    game->progress.emerald_count = count;
    game->sonic.shield = shield;
    game->sonic.previous_flags = old_flags;
    game->flash.ticks = flash_ticks;
    game->flash.color = flash_color;
    game->flash.value = flash_value;
    game->sonic.animation_fraction = fraction;
    game->pending_camera_top = top;
    game->pending_camera_bottom = bottom;
    demo_pointer = (front.demo_cycle & 1) ? 0x2a : 0;
    sound_events.insert(sound_events.end(), game->sound_events.begin(), game->sound_events.end());
    paused = fading_game = carry_progress_ = false;
    results.reset();
}

bool Session::supports_level(unsigned act) const {
    return act == 0 || (act == 1 && act2_) || (act == 2 && act3_) ||
           (additional_ && additional_->contains(act));
}

void Session::start_level(unsigned act) {
    if (!supports_level(act)) {
        throw std::runtime_error("Requested act has not been enabled");
    }
    front.show_map(act, 3, false);
    front.scene = FrontEnd::Scene::Black;
    front.scene_ticks = 59;
    tick({}, false);
}

void Session::start_special(unsigned index, unsigned resume) {
    return_act = resume;
    next_special = index + 1;
    carry_progress_ = true;
    front.act = index;
    front.lives = game ? game->lives : 3;
    front.demo = false;
    front.scene = FrontEnd::Scene::Gameplay;
    begin_act();
}

void Session::start_ending(bool all_emeralds) {
    carry_progress_ = false;
    front.act = 18;
    front.lives = 3;
    front.demo = false;
    front.scene = FrontEnd::Scene::Gameplay;
    begin_act();
    if (all_emeralds) {
        game->progress.emerald_count = 6;
    }
    // Area 18 has no music of its own: in play, the escape's song (Sky Base
    // 3's) carries over into it.
    if (game->level.music == 255 && additional_ && additional_->contains(17)) {
        sound_events.push_back({SoundEvent::Kind::Music, additional_->at(17).music});
    }
}

void Session::restart_act() {
    if (ending) {
        reset();
        return;
    }
    // Native shell action. A death in progress (fade, black hold, game over
    // or its map) restarts the act like an ordinary in-act request.
    const bool losing_life =
        respawn_pending_ || game_over || (fading_game && game && game->death_finished);
    if (game && (gameplay() || results || losing_life) && !front.demo) {
        front.scene = FrontEnd::Scene::Gameplay;
        results.reset();
        game_over.reset();
        respawn_pending_ = fading_game = false;
        game->start_act(false);
        sound_events = game->sound_events;
        paused = false;
    } else {
        reset();
    }
}

Bytes Session::death_fade_palette() const {
    // 0AB7 first reloads the D22C/D22D palettes (0B1E): background cycle
    // phase 0 and the unflashed stage, boss or goal sprite colours.
    auto palette = game->level.palette;
    std::copy_n(game->level.palette_cycle.begin() + 2, 32, palette.begin());
    palette[32] = palette[0];
    palette[33] = palette[1];
    if (game->sprite_palette_kind == 1) {
        std::copy_n(game->level.boss_palette.begin() + 2, 30, palette.begin() + 34);
    } else {
        const auto& colors =
            game->sprite_palette_kind == 2 ? game->level.goal_palette : game->source.palette;
        std::copy(colors.begin() + 34, colors.end(), palette.begin() + 34);
    }
    return palette;
}

// A death has run its course (Game::death_finished): fade out, then either
// restart from the checkpoint after a black hold or go to GAME OVER.
void Session::finish_life() {
    // 17F2-1818, after the death timer (D281) expires outside a demo.
    // 1802/1832: water raster colours are disabled before the fade.
    if (game->level.has_water()) {
        game->level.initial_flags[1] &= 0x7f;
        game->water.scanline = 0;
    }
    ++game->death_count; // 180E: D300 counts deaths for the goal sign.
    // 1812: D240 as it stands decides; zero lives leads to 1819 / 11E1.
    front.lives = game->lives;
    front.finish_life(death_fade_palette(), !game->lives);
    fading_game = true;
    respawn_pending_ = game->lives != 0;
}

void Session::end_demo() {
    front.screen.palette = game->level.palette;
    front.finish_demo();
    fading_game = true;
    sound_events = front.sound_events;
}

// One update of whichever part is active: ending, results, game over, the
// frontend screens, or the act itself.
void Session::tick(Input input, bool start) {
    if (ending) {
        ending->tick();
        sound_events = ending->sound_events;
        game->score = ending->score;
        game->lives = ending->lives;
        game->progress.emerald_count = ending->emeralds;
        if (ending->phase == Ending::Phase::Done) {
            reset();
        }
        return;
    }
    if (game) {
        game->video_interrupt();
    }
    sound_events.clear();
    voice_requested = false;
    bool pressed = start && !previous_start_;
    previous_start_ = start;
    if (results && front.scene == FrontEnd::Scene::Results) {
        results->tick(start || input.jump);
        sound_events = results->sound_events;
        game->score = results->score;
        game->rings = results->rings;
        game->lives = results->lives;
        game->next_life_score_bcd = results->next_life_score_bcd;
        // After the tally: back from a special stage, into a special stage
        // (the goal sign's special face), or on to the next act's map.
        if (results->phase == Results::Phase::Done) {
            if (front.act >= 28) {
                carry_progress_ = true;
                front.show_map(return_act + 1, game->lives, false);
            } else if (game->progress.special_stage_pending) {
                return_act = front.act;
                carry_progress_ = true;
                front.screen = results->screen;
                front.enter_special(next_special++, game->lives);
            } else if (supports_level(front.act + 1)) {
                carry_progress_ = true;
                front.show_map(front.act + 1, game->lives, false);
            }
        }
        return;
    }
    if (game_over) {
        game_over->tick(start, input.jump);
        sound_events = game_over->sound_events;
        if (game_over->phase != GameOver::Phase::Done) {
            return;
        }
        const bool continued = game_over->continued;
        game_over.reset();
        // 1827 / 1402: without a continue the cartridge starts a new game
        // (135B, logo first). 13C8 does the same for logical acts from 19.
        if (!continued || front.act >= 19) {
            // 12A9's last tick request precedes 135B's stop in the same frame.
            auto events = std::move(sound_events);
            reset();
            events.insert(events.end(), sound_events.begin(), sound_events.end());
            sound_events = std::move(events);
            return;
        }
        // 12B5-12C4: forget this act's checkpoint bit (D312) and spend a
        // credit. 182A: three lives. 13CD/0BDD: replay the act's map.
        // 0BDD clears D300 as the map is shown.
        --continues;
        game->checkpoint_x = game->checkpoint_y = 0;
        game->lives = 3;
        game->death_count = 0;
        respawn_pending_ = true;
        front.show_map(front.act, 3, false);
        return;
    }
    if (!gameplay()) {
        front.tick(start, input.jump, input.left || input.right || input.up || input.down);
        voice_requested = front.voice_requested;
        sound_events = front.sound_events;
        if (fading_game) {
            if (front.scene == FrontEnd::Scene::Logo) {
                fading_game = false;
            } else if (front.scene == FrontEnd::Scene::Results) {
                // The act has faded out: start the ending (after area 19),
                // load a linked room, or show the results.
                if (pending_area_ == 19) {
                    front.act = 19;
                    fading_game = false;
                    ending = std::make_unique<Ending>(
                        flow_assets_, game->score, game->lives, game->progress.emerald_count,
                        game->progress.life_monitor_count, special_attempts, special_successes,
                        game->progress.ever_died, game->level.object_patterns);
                    pending_area_ = -1;
                    return;
                }
                if (pending_area_ >= 0) {
                    if (advance_area_) {
                        ++front.act;
                    }
                    front.scene = FrontEnd::Scene::Gameplay;
                    fading_game = false;
                    begin_act();
                    return;
                }
                fading_game = false;
                sound_events.push_back({SoundEvent::Kind::Stop, 0});
                if (game->level.act >= 28) {
                    ++special_attempts;
                    if (!game->special.time_out) {
                        ++special_successes;
                        special_ring_value += 100;
                    } else {
                        game->rings = 0;
                        game->progress.continue_collected = false;
                    }
                    if (game->progress.continue_collected) {
                        ++continues;
                    }
                    game->special.time_out = game->progress.continue_collected =
                        false; // 464D / 465A clear result flags.
                }
                results = std::make_unique<Results>(
                    flow_assets_, game->level.act, game->score, game->rings, game->lives,
                    game->elapsed_ticks, game->progress.emerald_count,
                    game->level.act >= 28 ? special_ring_value : 100, continues,
                    game->next_life_score_bcd);
            } else if (front.scene == FrontEnd::Scene::GameOver) {
                fading_game = false;
                game_over = std::make_unique<GameOver>(flow_assets_, continues);
            } else {
                game->level.palette = front.screen.palette;
            }
        }
        if (gameplay() && respawn_pending_) {
            // 1414 after the hold: the native reload keeps checkpoint, score
            // and progress (Game::restart_from_checkpoint).
            respawn_pending_ = fading_game = paused = false;
            game->restart_from_checkpoint(game->lives);
            sound_events.insert(sound_events.end(), game->sound_events.begin(),
                                game->sound_events.end());
        } else if (gameplay()) {
            begin_act();
        }
        return;
    }
    if (front.demo && !game->warmup_ticks) {
        unsigned b = flow_assets_.demo_inputs.at(demo_pointer);
        if (start || !b || game->demo_ended) {
            end_demo();
            return;
        }
        // Attract demo: one recorded controller byte (active low) per 32
        // updates. 12E9 reads the current byte before advancing on ticks / 32.
        if (!(game->ticks & 31)) {
            ++demo_pointer;
        }
        input = {!(b & 4), !(b & 8), !(b & 32), !(b & 2), !(b & 1)};
    } else if (pressed && !game->warmup_ticks) {
        paused = !paused;
    }
    if (paused) {
        return;
    }
    game->tick(input);
    sound_events = game->sound_events;
    if (game->death_finished && !front.demo) {
        finish_life();
    } else if (front.demo && game->demo_ended) {
        end_demo();
    } else if (game->complete && !front.demo) {
        // 1758 -> 0AB7/0B1E reloads the base palette before fading. It does
        // not freeze the currently displayed background cycle phase.
        if (game->area_transition || game->area_advance) {
            pending_area_ = game->area_advance ? front.act + 1 : game->area_target;
            advance_area_ = game->area_advance;
            carry_progress_ = true;
            front.lives = game->lives;
        }
        auto palette = game->level.palette;
        std::copy_n(game->level.palette_cycle.begin() + 2, 32, palette.begin());
        palette[32] = palette[0];
        palette[33] = palette[1];
        front.finish_act(std::move(palette));
        fading_game = true;
    }
}
} // namespace sonic1_gg
