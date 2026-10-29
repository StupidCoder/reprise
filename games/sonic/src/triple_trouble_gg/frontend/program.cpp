// The program: power-on, the mode loop (0DFE) and the SEGA logo (mode 0).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 0064: a cartridge byte; with bit 0 set the logo is followed by mode 18
// instead of the intro (a development switch; clear in this cartridge).
constexpr Table logo_switch{0x00, 0x0064};
// The logo's art: tiles (bank 10, 8000) at tile C0, and its map (bank 12,
// BBAE), 18 rows of 20 entries from name table address 38CC.
constexpr Table logo_tiles{0x10, 0x8000};
constexpr Table logo_map{0x12, 0xBBAE};
// The palettes of the logo (table 0E:B6FC).
constexpr u8 logo_background_palette = 0x01, logo_sprite_palette = 0x0F;
} // namespace

// 0000, 053A: power-on. The work RAM and the chip's memory are cleared, the
// chip set up, the sound driver silenced; then the program modes run from
// the SEGA logo.
Flow Game::boot() {
    power_on();
    flow.mode = mode::sega_logo;
    // Power-on is at the top of the picture, 70 lines after the frame
    // interrupt's time; the start-up code above runs with interrupts off up
    // to the logo's first EI (252C) 506181 CPU clocks later (measured).
    clock_ = 70 * 228;
    co_await busy(506181);
    co_await run_modes();
}

void Game::power_on() {
    di();
    video.loading = 0;
    sound_.init();            // 508F
    init_vdp_registers();     // 2641
    clear_cram();             // 7C08
    clear_vram();             // 7BCF
    enable_frame_interrupt(); // 26F2
    display_on();             // 26D6
    flow.frame_work = 0;
    flow.demo = 0;
}

// The app's level select (not the cartridge's): after power-on a new game
// set up as the character select does (1AD0: 3 lives, no emeralds, no
// restart point), then the act's title card (mode 4).
Flow Game::boot_to_act(LevelStart start) {
    power_on();
    clear_game_state(); // 327E
    score.lives = 0x03;
    score.point_rewards = 0x01;
    flow.emeralds = 0;
    flow.restart_point = 0;
    flow.special_entry = 0;
    flow.character = start.character;
    flow.zone = start.zone;
    flow.act = start.act;
    flow.mode = start.direct ? 0x05 : 0x04; // play, or the title card first
    if (start.kind == LevelStart::Kind::special_stage) {
        // 12F3: the special stage by the emeralds so far (table 1317: mode
        // and act); the act left is kept to come back to.
        flow.emeralds = start.number;
        flow.saved_zone = start.zone;
        flow.saved_act = start.act;
        const Table next{0x00, std::uint16_t(0x1317 + flow.emeralds * 2u)};
        flow.mode = rom.byte(next, 0);
        flow.act = rom.byte(next, 1);
    } else if (start.kind == LevelStart::Kind::ending) {
        flow.emeralds = start.number;
        flow.mode = 0x14;
    }
    co_await busy(506181);
    co_await run_modes();
}

// 0DFE: each mode routine ends by naming the next mode.
Flow Game::run_modes() {
    for (;;) {
        stop_music();   // 2288
        reset_camera(); // 3310
        di();
        switch (flow.mode) {
        case mode::sega_logo:
            co_await sega_logo();
            break;
        case mode::intro:
            co_await intro();
            break;
        case mode::title:
            co_await title();
            break;
        case 0x03:
            co_await demo_mode();
            break;
        case 0x04:
            co_await title_card();
            break;
        case 0x05:
            co_await play_mode();
            break;
        case 0x06:
            co_await act_cleared();
            break;
        case 0x07:
            co_await life_lost();
            break;
        case 0x08:
        case 0x09:
            co_await special_stage();
            break;
        case 0x0A:
            co_await special_results();
            break;
        case 0x0B:
        case 0x0C:
        case 0x0D:
            co_await flight_stage();
            break;
        case 0x12:
        case 0x13:
        case 0x14:
            co_await ending();
            break;
        case 0x16:
            co_await after_select();
            break;
        case 0x18:
            co_await character_select();
            break;
        case 0x19:
            co_await title_sound_test();
            break;
        default:
            throw Unrecovered("program mode " + std::to_string(flow.mode));
        }
    }
}

// 2288: the sound driver's "stop" request.
void Game::stop_music() {
    sound_.request(0xE0, 2);
}

// 21BA / 252C: `count` frame waits.
Flow Game::wait_frames(unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        co_await next_frame();
    }
}

// 21C5: up to `count` frame waits, ending early on button 1, 2 or Start.
Flow Game::wait_frames_or_button(unsigned count) {
    while (count) {
        co_await next_frame();
        --count;
        if (input.pressed & 0xB0) {
            co_return;
        }
    }
}

// 0E53: mode 0. The screen fades to black and everything is cleared; the
// logo appears from white while Sonic runs across it, the voice says
// "SEGA", and the screen fades to black again. Next: the intro.
Flow Game::sega_logo() {
    flow.frame_work = 0;
    palette.fade_period = 0;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x18);
    di();
    clear_sprites();    // 2757
    clear_map_state();  // 428B
    clear_game_state(); // 327E
    set_fades(0, 0x80, 3, 0x80, 3);
    co_await wait_frames(0x18);
    flow.zone = 0x10;
    co_await clear_screen(); // 273B
    // 341C (7795): the logo's art and map; 2C3B ends with EI.
    di();
    co_await busy(17 + 17 + 183 + load_tiles(0x1800, logo_tiles.bank, logo_tiles.address));
    co_await busy(7 + 53 + 30 + draw_rect(0x38CC, logo_map.bank, logo_map.address, 0x12, 0x14));
    ei();
    fill_white(); // 340C
    set_fades(3, 0x90, logo_background_palette, 0x90, logo_sprite_palette);
    co_await wait_frames(0x14);
    objects[0].type = 1; // Sonic runs across (state 3A, 0C:9833)
    flow.frame_work = frame_work::build_sprites;
    for (unsigned i = 0; i < 0xB4; ++i) {
        co_await next_frame();
        co_await update_player();
    }
    flow.frame_work = 0;
    di();
    video.busy = 0xFF;
    co_await play_voice();
    co_await next_frame();
    video.busy = 0;
    co_await wait_frames(0x1E);
    palette.fade_period = 1;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x3C);
    reset_game(); // 32A8
    flow.zone = 0;
    flow.act = 0;
    flow.character = 1;
    flow.mode = (rom.byte(logo_switch) & 1) ? 0x18 : mode::intro;
}

// Bank 1A, 8000: the sampled "SEGA" voice, 7568 samples of 703 CPU clocks
// after 178 clocks of set-up, and 57 to return; the program and the frame
// interrupt are stopped meanwhile (interrupts are off). The samples are
// played by the audio side (voice.hpp). At its end it clears the first
// sound request slot (DE04).
Flow Game::play_voice() {
    constexpr unsigned clocks = 178 + 7568 * 703 + 57;
    voice_requested = true;
    co_await busy(clocks);
    sound_.state.requests[0] = 0;
}

// 32A8: a new game's score; unless D168 is 80 the first zone and act.
void Game::reset_game() {
    score.points = {};
    if (flow.next_mode_hint != 0x80) {
        flow.zone = 0;
        flow.act = 0;
    }
    clear_continue_state(); // 32F4
    clear_rings_and_time(); // 32C3
}

// 32C3: no rings; the clock at 0, or (D17A = FE) the one saved in D17B.
void Game::clear_rings_and_time() {
    score.rings_low = 0;
    score.rings_high = 0;
    if (flow.special_entry == 0xFE) {
        // 32DC: the clock kept for a special stage's return (D17B).
        score.seconds = flow.checkpoint_time[0];
        score.minutes = flow.checkpoint_time[1];
        score.clock_extra = flow.checkpoint_time[2];
        return;
    }
    score.seconds = 0;
    score.minutes = 0;
    score.clock_extra = 0;
}

// 32F4
void Game::clear_continue_state() {
    flow.restart_point = 0;
    flow.checkpoint = {};
}

} // namespace triple_trouble_gg
