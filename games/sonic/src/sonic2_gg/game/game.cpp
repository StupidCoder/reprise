// Game construction and the per-frame entry point.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

Game::Game(const Assets& assets, int zone, int act)
    : rom(assets), start_zone_(zone), start_act_(act) {
    program_.start(boot());
}

void Game::resume_main_loop() {
    interrupts_ = true;
    voice_frames_ = 0;
    program_.start(main_loop());
    program_.resume(); // up to the wait in 05C5
}

void Game::frame(std::uint8_t buttons) {
    buttons_ = std::uint8_t(buttons & 0xBF);
    voice_requested = false;
    display.changes.clear();
    if (voice_frames_) {
        // The voice routine runs with interrupts off: no frame interrupt, and
        // the main program is inside the routine.
        if (--voice_frames_) {
            return;
        }
        // 8056: XOR A; LD (DE04),A: the high byte of the first effect
        // channel's bytecode position.
        auto& position = sound_.state.channels[sound_channel::effect_tone2].position;
        position = std::uint16_t(position & 0x00FF);
        // EI: the interrupt that was held off runs now.
    } else if (!interrupts_) {
        program_.resume();
        return;
    }
    if (stall_frames_) {
        // A frame that passed while the original was still busy loading.
        --stall_frames_;
        return;
    }
    clock_ = 0;
    frame_interrupt();
    water_line_after_frame_interrupt(); // 05E8 at line 192
    update_wide_view();                 // Widescreen enhancement: margins of the presented view.
    program_.resume();
    water_line_interrupt(); // 05E8
}

// Bank 3, 8000: the sampled "SEGA" voice. The routine takes 168 + 15999 * 339
// + 57 CPU clocks; the main program and the frame interrupt are stopped for
// that long. The samples themselves are played by the audio side (voice.hpp).
Flow Game::play_voice() {
    constexpr unsigned clocks = 168 + 15999 * 339 + 57;
    constexpr unsigned frame_clocks = 262 * 228;
    voice_requested = true;
    voice_frames_ = int((clocks + frame_clocks - 1) / frame_clocks);
    co_await next_frame();
}

// Bank 2, 441E / 4000: the sound driver (audio/sound_driver.hpp).
void Game::sound_init() {
    sound_.init();
}

void Game::sound_tick() {
    sound_.tick();
}

// 53B6: clears the map, camera and scroll state (D15E-D290). The scripted
// camera target (D2CE) lies outside and is kept.
void Game::clear_map_state() {
    spend(6471);
    freeze_level_background();
    const auto scripted_x = camera.scripted_x, scripted_y = camera.scripted_y;
    camera = {};
    camera.scripted_x = scripted_x;
    camera.scripted_y = scripted_y;
    level.map_bank = 0;
    level.layout_bank = 0;
    level.block_table = 0;
    level.layout_source = 0;
    level.row_table = 0;
    level.attribute_table = 0;
}

// 10E7: asks the sound driver to fade the music out.
void Game::fade_music() {
    sound_.state.fade_steps = 0x0C;
    sound_.state.fade_counter = 0x01;
    sound_.state.fade_speed = 0x02;
}

// 22B7: a new game: three lives, no rings, score or emeralds, and (unless
// the zone select chose one) the first act.
void Game::new_game() {
    score.lives = 3;
    score.lives_at_start = 3;
    score.rings = 0;
    for (int i = 0; i < 3; ++i) {
        score.points[i] = 0;
        score.bonus[i] = 0;
        score.time_bonus[i] = 0;
    }
    score.emeralds = 0;
    if (flow.zone_select_used) {
        return;
    }
    flow.zone = 0;
    flow.act = 0;
}

// 212E: the clock's digits in the HUD sprites (tiles 10, 12, ... are 0-9).
void Game::update_timer_digits() {
    if (flow.attract_flags) {
        return;
    }
    const auto rlc = [](u8 v) {
        return u8((v << 1) | (v >> 7));
    };
    const auto rrc3 = [](u8 v) {
        return u8((v >> 3) | (v << 5));
    };
    sprites.table[hud_minutes].tile = u8((rlc(clock.minutes) & 0x1E) + 0x10);
    sprites.table[hud_seconds].tile = u8((rrc3(clock.seconds) & 0x1E) + 0x10);
    sprites.table[hud_seconds + 1].tile = u8((rlc(clock.seconds) & 0x1E) + 0x10);
}

// LD A,R stands in for randomness in the original (the refresh register
// counts instruction fetches). The reconstruction uses a xorshift generator
// instead (a documented deviation); the test harness can replay the
// original's values through random_source.
u8 Game::refresh_register() {
    if (random_source) {
        return random_source();
    }
    // xorshift32
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    return u8(random_state_ & 0x7F);
}

} // namespace sonic2_gg
