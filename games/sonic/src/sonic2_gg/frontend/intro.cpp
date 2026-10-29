// The intro scene (0ED8), the title screen (0FD4) and the attract demos
// (24EE). The intro and title are played as acts of a ninth zone (zone 9),
// with scripted objects.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 10B0: Start pressed during the attract sequence (D292 bit 7) ends the
// calling scene: the original pops its own return address and returns from
// its caller. The scenes below test it and co_return.
bool Game::attract_aborted() const {
    return flow.attract_flags & attract_flag::start;
}

// 0ED8: Sonic and Tails race along a cliff (zone 9, act 0).
Flow Game::intro() {
    di();              // 0ED8
    clear_map_state(); // 53B6
    flow.zone = 9;
    flow.act = 0;
    co_await init_act(); // 2206
    camera.offset_x = 0x7C;
    camera.offset_y = 0x74;
    reset_camera_window(); // 5D26
    di();                  // 0EF7
    load_tiles(16, rom::intro_tiles);
    load_tiles(64, rom::intro_sky_tiles);
    load_tiles(90, rom::intro_cliff_tiles);
    load_tiles(124, rom::intro_figure_tiles);
    camera.scroll_y = 0x2C;
    objects[8].type = object_type::intro_tails;  // D700: Tails
    objects[9].type = object_type::intro_runner; // D740
    objects[11].type = object_type::intro_cloud; // D7C0: a cloud
    objects[12].type = object_type::intro_star;  // D800-D880: stars
    objects[13].type = object_type::intro_star;
    objects[14].type = object_type::intro_star;
    fade_in_act(); // 7578
    flow.game_flags = game_flag::playing;
    enable_map(); // 4A0F
    sound_.request(sound::intro);
    // 0F70: Sonic walks right until X = 170.
    do {
        co_await wait_frame();
        if (attract_aborted()) {
            co_return;
        }
        game_frame(); // 09BB
        ++sonic().x;
    } while (sonic().x < 0x0170);
    objects[10].type = object_type::intro_hills; // D780: the horizon hills
    for (int i = 0; i < 0x0564; ++i) {
        if (attract_aborted()) {
            co_return;
        }
        co_await wait_frame();
        game_frame();
    }
    sonic().next_state = 0x10;
    disable_map(); // 4A15
    for (int i = 0; i < 0x0078 + 0x003C; ++i) {
        if (attract_aborted()) {
            co_return;
        }
        co_await wait_frame();
        game_frame();
    }
    objects[8].type = 0;
}

// 0FD4: the title screen (zone 9, act 1), about 20 seconds.
Flow Game::title_screen() {
    clear_map_state();
    clear_level_state();
    flow.zone = 9;
    flow.act = 1;
    co_await init_act();
    co_await next_frame(); // 17EE
    clear_screen();
    load_tiles(256, rom::title_tiles);
    draw_rect({6, 3}, rom::title_map, 18, 20);
    camera.scroll_y = 0x2C;
    camera.scroll_x = 0xF8;
    for (auto& slot : sprites.table) {
        slot.y = slot.x = slot.tile = 0;
    }
    objects[0].type = 0;
    load_tiles(16, rom::title_figures);
    load_tiles(46, rom::title_figures2);
    spawn_object(object_type::title_sonic, 0); // Sonic and Tails
    spawn_object(object_type::title_tails, 0);
    fade_in_act();
    flow.game_flags = game_flag::playing;
    sound_.request(sound::title);
    video.frame_counter = 0;
    for (int i = 0; i < 0x1E; ++i) {
        co_await wait_frame();
        game_frame();
        blink_press_start(); // 1094
    }
    for (int i = 0; i < 0x0474; ++i) {
        if (attract_aborted()) {
            co_return;
        }
        co_await wait_frame();
        game_frame();
        blink_press_start();
    }
}

// 1094: "PRESS START BUTTON" shown every other 32 frames.
void Game::blink_press_start() {
    draw_rect({6, 18}, (video.frame_counter & 0x20) ? rom::title_map : rom::press_start_text, 1,
              20);
}

// 24EE: a recorded demo (eight in turn, table 258F), up to 1500 frames.
// Afterwards the attract sequence starts again at the SEGA logo.
Flow Game::demo() {
    // 255C
    flow.demo_number = u8((flow.demo_number + 1) & 7);
    const auto demo = rom::demos + flow.demo_number * 4u;
    flow.zone = rom.byte(demo);
    flow.demo_bank = rom.byte(demo, 1);
    flow.demo_start = rom.word(demo, 2);
    flow.act = flow.zone == 1 ? 1 : 0;
    flow.game_flags = game_flag::playing;
    // 25C9
    flow.game_flags |= game_flag::act_start;
    co_await next_frame(); // 17EE
    clear_screen();
    co_await title_card();      // 269D
    co_await hold_title_card(); // 2970
    start_fade_out();
    fade_music();
    co_await wait_frames(42);
    co_await init_act();
    fade_in_act();
    enable_map();
    score.rings = 0x20;
    flow.game_flags &= u8(~game_flag::act_start);
    flow.demo_input = flow.demo_start;
    // 25AF
    for (int i = 0; i < 0x05DC; ++i) {
        co_await wait_frame();
        game_frame();
        if (flow.attract_flags & attract_flag::start) {
            break;
        }
        if (flow.game_flags != game_flag::playing) {
            break;
        }
    }
    // 25F8: a dying Sonic falls off the screen first.
    for (;;) {
        co_await wait_frame();
        update_sonic(); // 2F4F
        if (sonic().state != 0x1F || hi(sonic().screen_y) == 0x02) {
            break;
        }
    }
    sound_.request(sound::stop);
    sound_tick();          // the driver is called directly once (bank 2, 4000)
    stop_palette_cycles(); // 7C26
    start_fade_out();
    co_await wait_frames(42);
    flow.attract_flags &= u8(~attract_flag::start);
    flow.demo_input = 0;
    flow.game_flags = 0;
    flow.zone = 0;
    flow.act = 0;
    // 251E: bit 7 was just cleared, so this always returns to the logo.
    flow.attract_flags =
        u8((flow.attract_flags & ~(attract_flag::start | attract_flag::demo)) | attract_flag::logo);
}

} // namespace sonic2_gg

namespace sonic2_gg {

// 9883 (bank 1E) / A738 (bank 1F): counts 1E/1F (a word) up; true when it
// reaches `limit`.
bool Game::count_to(Object& o, unsigned limit) {
    o.set_long_count(o.long_count() + 1u);
    return o.long_count() == limit;
}

namespace {
void reset_count(Object& o) {
    o.set_long_count(0);
}
} // namespace

// --- 5E: the intro's second runner -----------------------------------------

// 96BB
void Game::intro_runner_start(Object& o) {
    o.tile_base = 0x40;
    o.y = 0x007E;
    o.x = (sonic().x + 0x20u) & 0xFFFF;
    reset_count(o);
    o.next_state = 1;
}

// 971B: keeps 32 pixels ahead of Sonic for 224 frames.
void Game::intro_runner_follow(Object& o) {
    o.x = (sonic().x + 0x20u) & 0xFFFF;
    if (!count_to(o, 0x00E0)) {
        return;
    }
    o.next_state = 2;
    reset_count(o);
}

// 973C: then moves on its own.
void Game::intro_runner_away(Object& o) {
    move_object(o);
    if (!count_to(o, 0x0218)) {
        return;
    }
    o.next_state = 3;
    reset_count(o);
}

// --- 5F: Tails ---------------------------------------------------------------

// 9767
void Game::intro_tails_start(Object& o) {
    o.tile_base = 0x5A;
    o.y = 0x006A;
    o.x = (sonic().x + 0x30u) & 0xFFFF;
    reset_count(o);
    o.next_state = 1;
}

// 97DB (state 1): 48 pixels ahead of Sonic for 128 frames.
void Game::intro_tails_follow(Object& o) {
    o.x = (sonic().x + 0x30u) & 0xFFFF;
    if (!count_to(o, 0x0080)) {
        return;
    }
    o.next_state = 2;
    reset_count(o);
}

// 97FD / 9815 / 982C (states 2-4): moving with the script's velocity for
// 536, 96 and 104 frames.
void Game::intro_tails_move(Object& o, unsigned frames, u8 next) {
    move_object(o);
    if (!count_to(o, frames)) {
        return;
    }
    o.next_state = next;
    reset_count(o);
}

// 9843 (state 5): waits 540 frames.
void Game::intro_tails_wait(Object& o) {
    if (!count_to(o, 0x021C)) {
        return;
    }
    o.next_state = 6;
    reset_count(o);
}

// 9857 (state 6): flies off, accelerating: vx = n * 16, vy = 256 - n * 32.
void Game::intro_tails_leave(Object& o) {
    o.set_long_count(o.long_count() + 0x10u);
    const unsigned c = o.long_count();
    o.vx = std::uint16_t(c);
    const unsigned twice = (c * 2) & 0xFFFF;
    o.vy = ((~(twice - 1)) + 0x0100u) & 0xFFFF;
    move_object(o);
}

// --- Sonic in the intro --------------------------------------------------

// A706 (state 0F): on even demo numbers Sonic stands for 264 frames, then
// runs off right (state 34) with the camera stopped; otherwise he only
// stands (35).
void Game::sonic_intro_stand() {
    Object& o = sonic();
    if (flow.demo_number & 1) {
        o.next_state = 0x35;
        return;
    }
    if (!count_to(o, 0x0108)) {
        return;
    }
    o.next_state = 0x34;
    disable_map(); // 0275
    reset_count(o);
}

// A729 (state 34)
void Game::sonic_intro_run() {
    Object& o = sonic();
    move_object(o);
    if (!count_to(o, 0x0298)) {
        return;
    }
    o.next_state = 0x35;
}

// --- 50, 51: the title screen's figures ------------------------------------

// 8DC5
void Game::title_sonic_start(Object& o) {
    o.x = 0x0082;
    o.y = 0x007C;
    o.tile_base = 0x10;
    o.next_state = 1;
}

// 8E21
void Game::title_tails_start(Object& o) {
    o.x = 0x009A;
    o.y = 0x006B;
    o.tile_base = 0x2E;
    o.next_state = 1;
}

// 8E3D: holding buttons B6 (Start, both buttons, left, down) on a certain
// frame of Tails' blink sets the zone select code (D12C) with a chime.
void Game::title_tails_secret(Object& o) {
    (void)o;
    if ((input.held[0] & 0xB6) != 0xB6) {
        return;
    }
    input.controller2_code = 0xB6;
    sound_.request(sound::ring);
}

} // namespace sonic2_gg
