// The intro (mode 1, 0F3E).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

// 0F3E: zone 7, act 1 as a scene: the camera fixed (it moves while Sonic
// runs), the objects of the intro.cpp story, for 0546 frames or until
// button 1, 2 or Start; then a fade to white. Next: the title.
Flow Game::intro() {
    flow.special_entry = 0;
    flow.restart_point = 0;
    flow.zone = 7;
    flow.act = 0;
    flow.frame_work = 0;
    co_await clear_screen();   // 273B
    clear_map_state();         // 428B
    co_await load_act();       // 3167
    enable_map();              // 4CC3
    fade_in_act();             // 7650
    co_await play_act_music(); // 21DC
    objects[0].type = 0x01;    // Sonic
    objects[8].type = 0x30;    // the first emerald
    objects[9].type = 0x35;    // Knuckles
    flow.frame_work = frame_work::build_sprites;
    disable_map(); // 4CCE
    camera.max_y = camera.y;
    camera.min_y = camera.y;
    bool skipped = false;
    for (unsigned n = 0x0546; n; --n) {
        co_await next_frame();
        if (!(camera.map_flags & map_flag::update_pending)) {
            update_map();              // 3F81
            co_await update_player();  // 3431
            co_await update_objects(); // 4F3F
            stream_object_art();       // 7C15
        }
        if (input.pressed & 0xB0) {
            skipped = true;
            break;
        }
    }
    co_await wait_frames(8);
    if (skipped) {
        stop_music(); // 2288
    }
    set_fades(2, 0x50, palette.fades[0].palette, 0x50, palette.fades[1].palette);
    co_await wait_frames(0x78);
    flow.restart_point = 0;
    flow.special_entry = 0;
    flow.mode = mode::title;
}

// 21DC: the act's song (table 2259 by zone * 3 + act; 227D by zone when
// D3E4 is set), then a frame. Not with D3EB, nor while the player is in
// states 1F, 20, 28 or 44.
Flow Game::play_act_music() {
    if (request_act_music()) {
        co_await next_frame();
    }
}

// 21DC's request; true when the frame's wait follows.
bool Game::request_act_music() {
    if (act.object_mode) {
        return false;
    }
    const u8 s = objects[0].state;
    if (s == 0x44 || s == 0x20 || s == 0x1F || s == 0x28) {
        return false;
    }
    if (act.variant) {
        // 2244: (a boss act) the zone's song (table 227D) on the first
        // request slot, no frame's wait.
        const u8 song = rom.byte(Table{0x00, 0x227D}, flow.zone);
        sound_.request(song, 0);
        act.music_request = song;
        act.music_playing = song;
        return false;
    }
    const u8 song = rom.byte(Table{0x00, 0x2259}, flow.zone * 3u + flow.act);
    sound_.request(song, 2);
    act.music_request = song;
    act.music_playing = song;
    return true;
}

} // namespace triple_trouble_gg
