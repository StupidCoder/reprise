// Mode 0D (1672): the second kind of special stage, "zone 8": the player
// flies the biplane over the sea. The view is the background moved with
// the controller (D198/D199); the plane is a picture in the name table
// (40D6); rings and other objects come in by the course (1C:9864 act 1,
// 9A7C act 2) as the flight's distance passes their marks. Taking enough
// rings (D15B: 80) wins (D180): the emerald's scene in zone 9 follows
// (1EB2); twice through the course without it ends the stage (D144 bit 7).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 40D6: the plane's pictures (bank 16, 6 by 7 entries at 3ADA): flying,
// by frame counter (bits 2-5) for Sonic (411D) and Tails (413D); won.
constexpr std::uint16_t plane_won_sonic = 0x9BFC, plane_won_tails = 0x9DA0;
constexpr std::uint16_t plane_frames_sonic = 0x411D, plane_frames_tails = 0x413D;
constexpr std::uint16_t course_act1 = 0x9864, course_act2 = 0x9A7C;
constexpr Table horizon_bands{0x00, 0x09EA};
} // namespace

// 1672
Flow Game::flight_stage() {
    di();
    flow.frame_work = 0;
    flow.zone = 0x08;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x3C);
    clock.running = 0;
    di();
    co_await start_act(); // 1327
    flight_setup();       // 1A:BA58
    flight_palettes();    // 76A7
    palette.fade_period = 2;
    co_await wait_frames(0x1E);
    flow.frame_work = 0x02;
    co_await flight_loop(); // 1DED
    act.scene[scene_byte::split] = 0;
    act.palette_cycle_off = 0xFF; // 21D6
    stop_music();                 // 2288
    if (flow.next_mode_hint == 0x03) {
        // 1727: entered from the sound test: back to the SEGA logo.
        palette.fades[0].mode = 0x40;
        palette.fades[1].mode = 0x40;
        palette.fade_period = 1;
        stop_music();
        co_await wait_frames(0x78);
        flow.mode = mode::sega_logo;
        co_return;
    }
    if (flow.flight_won) {
        co_await emerald_scene(); // 1EB2
    }
    flow.zone = flow.saved_zone;
    flow.act = flow.saved_act;
    if (flow.frame_work & 0x40) {
        palette.fades[0].mode = 0x50;
        palette.fades[1].mode = 0x50;
        palette.fade_period = 1;
        co_await wait_frames(0x78);
        flow.mode = 0x0A;
        co_return;
    }
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    palette.fade_period = 1;
    co_await wait_frames(0x78);
    flow.mode = 0x05;
}

// 1A:BA58: the view at (?, C5); the plane (slot 3, type 41) and the
// ring counter's object (slot 4, type 0C, tiles 44); no rings, 80 to
// take; act 1 the sea's object (slot 5, type 20) with the horizon at line
// 70, act 2 another (type 46, parameter FF).
void Game::flight_setup() {
    di();
    camera.scroll_y = 0xC5;
    act.scene[scene_byte::plane_x] = 0x80;
    act.scene[scene_byte::plane_y] = 0x7C;
    objects[3].type = 0x41;
    objects[4].type = 0x0C;
    objects[4].tile_base = 0x44;
    objects[4].scratch[3] = 0xFF;
    act.scene[scene_byte::counter] = 0;
    flow.flight_won = 0;
    score.ring_goal = {0x80, 0x00};
    score.rings_low = 0;
    score.rings_high = 0;
    if (!flow.act) {
        objects[5].type = 0x20;
        act.scene[scene_byte::horizon] = 0x70;
    } else {
        objects[5].type = 0x46;
        objects[5].param = 0xFF;
    }
}

// 76A7: the colours: background palette 2B (Sonic) or 2D (Tails), sprites
// 2F, one more in act 2; faded in from black.
void Game::flight_palettes() {
    const u8 a = flow.act & 0x01;
    palette.fades[0].palette = u8((flow.character == 1 ? 0x2B : 0x2D) + a);
    palette.fades[1].palette = u8(0x2F + a);
    palette.fades[0].mode = 0x80;
    palette.fades[1].mode = 0x80;
}

// 1DED: one frame per pass. While the plane (slot 3) is still in its
// first state (41: coming in) all objects are updated; then half of them
// each frame (4F9C), the steering (03:BDE2), the plane's picture (40D6),
// the power-up music's end. The phase steps every 7 frames; on two frames
// of four the course brings objects (unless DBD5); the distance grows by
// the speed; in act 1 the horizon rises on odd frames to line 40 once the
// course runs. Won (D180): sound D3; failed (D144 bit 7): sound E2, four
// frames, sound DE; then 90 frames, and the stage ends.
Flow Game::flight_loop() {
    for (;;) {
        co_await next_frame();  // 084C
        co_await check_pause(); // 20E2
        const bool arriving = objects[3].type == 0x41;
        if (arriving) {
            co_await update_objects(); // 4F3F
        } else {
            co_await update_alternate_objects(); // 4F9C
        }
        flight_steer();                // 03:BDE2
        draw_plane();                  // 40D6
        co_await update_music_timer(); // 20C7
        if (objects[3].type == 0x41) {
            continue;
        }
        if (++act.scene[scene_byte::phase_ticks] >= 0x07) {
            act.scene[scene_byte::phase_ticks] = 0;
            act.scene[scene_byte::phase] = u8((act.scene[scene_byte::phase] + 1) & 0x03);
        }
        ++flow.placement_ticks; // D141
        if ((flow.placement_ticks & 0x03) >= 0x02 && !act.flight_rings_off) {
            run_course(); // 1C:97AC
        }
        const std::uint16_t d = std::uint16_t(
            (act.scene[scene_byte::distance] | act.scene[scene_byte::distance + 1] << 8) +
            act.scene[scene_byte::speed]);
        act.scene[scene_byte::distance] = lo(d);
        act.scene[scene_byte::distance + 1] = hi(d);
        if (placements.scanned == 0x01 && !act.flight_rings_off && (video.frame_counter & 0x01) &&
            act.scene[scene_byte::horizon] >= 0x40) {
            --act.scene[scene_byte::horizon];
        }
        if (flow.flight_won) {
            sound_.request(0xD3, 1);
            break;
        }
        if (flow.frame_work & 0x80) {
            sound_.request(0xE2, 1);
            for (unsigned i = 0; i < 4; ++i) {
                co_await flight_frame(); // 1EA9
            }
            sound_.request(0xDE, 1);
            break;
        }
    }
    for (unsigned i = 0; i < 0x5A; ++i) {
        co_await flight_frame();
    }
}

// 1EA9: a frame with only the plane's picture.
Flow Game::flight_frame() {
    co_await next_frame(); // 084C
    draw_plane();          // 40D6
}

// 4F9C: half of the objects each frame: slots 1, 3, ... 21 on odd frames,
// 2, 4, ... 22 on even ones.
Flow Game::update_alternate_objects() {
    objects[0].touch_sides = 0;
    const unsigned first = (video.frame_counter & 0x01) ? 1 : 2;
    for (unsigned i = 0; i < 11; ++i) {
        co_await update_object(objects[first + 2 * i], false); // 4FC0
    }
}

// 03:BDE2: Up and Down move the view (and the plane's place) between
// scroll Y AF and 50 (wrapping at E0); Left and Right between scroll X B9
// and 5C. A hit (DBD2) makes the plane tumble for 120 frames (DBD3,
// DBD4): the view shakes by 2 every other frame (the player's bytes 1E and
// 1F count), then sound DC.
void Game::flight_steer() {
    if (act.flight_tumble) {
        if (!--act.flight_tumble_ticks) {
            act.flight_tumble = 0;
            sound_.request(0xDC, 1);
            return;
        }
        auto& p = objects[0];
        if (--p.timer) {
            return;
        }
        p.timer = 0x02;
        ++p.counter;
        camera.scroll_x = u8(camera.scroll_x + ((p.counter & 0x01) ? -2 : 2));
        return;
    }
    if (act.flight_hit) {
        act.flight_hit = 0;
        act.flight_tumble = 0x01;
        act.flight_tumble_ticks = 0x78;
        objects[0].timer = 0x02;
    }
    const u8 held = input.held;
    u8& sy = camera.scroll_y;
    u8& py = act.scene[scene_byte::plane_y];
    if (held & 0x01) {
        if (sy != 0x50) {
            ++sy;
            --py;
            if (sy == 0xE0) {
                sy = 0x00;
            }
        }
    } else if (held & 0x02) {
        if (sy != 0xAF) {
            --sy;
            ++py;
            if (sy == 0xFF) {
                sy = 0xDF;
            }
        }
    }
    u8& sx = camera.scroll_x;
    u8& px = act.scene[scene_byte::plane_x];
    if (held & 0x04) {
        if (sx != 0xB9) {
            --sx;
            --px;
        }
    } else if (held & 0x08) {
        if (sx != 0x5C) {
            ++sx;
            ++px;
        }
    }
}

// 40D6: every fourth frame the plane's picture (bank 16) into the name
// table at 3ADA (6 by 7): by the frame counter, or the won picture.
void Game::draw_plane() {
    const u8 frames = video.frame_counter;
    if ((frames & 0x03) != 0x03) {
        return;
    }
    std::uint16_t picture;
    if (flow.flight_won) {
        picture = flow.character == 1 ? plane_won_sonic : plane_won_tails;
    } else {
        const unsigned list = flow.character == 1 ? plane_frames_sonic : plane_frames_tails;
        picture = cpu_word(0x16, list + ((frames & 0x3C) >> 1));
    }
    di();
    draw_rect(0x3ADA, 0x16, picture, 0x06, 0x07); // 2C3B
    ei();
}

// 1C:97AC: the course: started (D47F 1) at its first entry; up to four
// entries whose mark (distance) is within 288 ahead come in as objects
// (type; bytes 34-35 the mark, 36-37 its place). At the course's end
// (FF) 128 calls' pause; then the distance starts over, and after the
// second time through the course is done (D47F FF): unless won, the
// stage is failed (D144 bit 7).
void Game::run_course() {
    if (placements.scanned == 0xFF) {
        return;
    }
    if (!placements.scanned) {
        placements.scanned = 0x01; // 9806
        placements.course = flow.act ? course_act2 : course_act1;
    }
    u8& pause = act.scene[5]; // DC4B
    std::uint16_t at = placements.course;
    for (unsigned n = 0; n < 4; ++n) {
        if (pause) {
            if (++pause < 0x80) {
                return;
            }
            placements.scanned = 0;
            pause = 0;
            act.scene[scene_byte::distance] = 0;
            act.scene[scene_byte::distance + 1] = 0;
            if (++act.scene[scene_byte::counter] != 0x02) {
                return;
            }
            placements.scanned = 0xFF;
            if (!flow.flight_won) {
                flow.frame_work |= 0x80;
            }
            return;
        }
        const Table e{0x1C, at};
        if (rom.byte(e, 0) == 0xFF) {
            pause = 0x01; // the position is not kept (97D1)
            return;
        }
        // 981B: the entry comes in once the distance is within 288 of its
        // mark (and a slot is free); otherwise it is tried again.
        const std::uint16_t mark = rom.word(e, 1);
        const std::uint16_t distance = std::uint16_t(act.scene[scene_byte::distance] |
                                                     act.scene[scene_byte::distance + 1] << 8);
        if (mark < distance || std::uint16_t(mark - distance) >= 0x0120) {
            continue;
        }
        Object* o = find_free_slot(); // 5142 (0360)
        if (!o) {
            continue;
        }
        o->type = rom.byte(e, 0);
        o->scratch[0] = lo(mark);
        o->scratch[1] = hi(mark);
        o->scratch[2] = rom.byte(e, 3);
        o->scratch[3] = rom.byte(e, 4);
        at = std::uint16_t(at + 5);
    }
    placements.course = at; // 97CD
}

// 1EB2: the flight won: a fade to white; zone 9 (mode 9) is loaded and
// played as an act with the picture white, palettes 26 and 1D faded in,
// the camera still, 80 rings and the player held (DBD7); then silence and
// a fade out.
Flow Game::emerald_scene() {
    palette.fades[0].mode = 0x50;
    palette.fades[1].mode = 0x50;
    palette.fade_period = 2;
    co_await wait_frames(0x78);
    di();
    flow.frame_work = 0;
    flow.mode = 0x09;
    flow.zone = 0x09;
    co_await start_act(); // 1327
    fill_white();         // 340C
    palette.fades[0] = {0x90, 0x26};
    palette.fades[1] = {0x90, 0x1D};
    palette.fade_period = 3;
    disable_map(); // 4CCE
    flow.frame_work = 0x02;
    score.rings_low = 0x80;
    score.rings_high = 0x00;
    player.in_scene = 0xFF;
    co_await act_loop(); // 1F2E
    stop_music();        // 2288
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x3C);
}

// 078D: the frame interrupt of mode 0D (and of the ending with all the
// emeralds): in act 1 with the split on (DC52) the line interrupt at the
// horizon (DC51); while the program is busy only the colours; otherwise
// the scroll (D198, D199), the sprites every other frame, the colours, and
// unless the pause the sprites built on odd frames and the fades; the
// controller, a song asked for (to DE04), the sound driver; no clock.
void Game::flight_frame_interrupt() {
    if (act.scene[scene_byte::split] == 0xFF && !flow.act) {
        // 079A: the line interrupt at the horizon (register 10), the bands
        // from the first.
        video.band_counter = act.scene[scene_byte::horizon];
        video.registers[0] |= 0x10;
        display.line_interrupt = true;
        video.water_line_mode = 0xFF;
        video.band = 0;
    }
    if (video.busy) {
        upload_colors(); // 2582 (0846)
    } else {
        display_off(); // 26E4
        display.scroll_x = camera.scroll_x;
        display.scroll_y = camera.scroll_y;
        if (!(video.frame_counter & 0x01)) {
            upload_sprites(); // 27A0
        }
        display_on();    // 26D6
        upload_colors(); // 2582
        if (!(flow.frame_work & frame_work::no_level_services)) {
            if (video.frame_counter & 0x01) {
                build_sprites(); // 2A14
            }
            run_fades(); // bank 0E, B4D3
        }
    }
    read_controller(); // 2D43
    if (act.music_request) {
        sound_.request(act.music_request, 0);
        act.music_request = 0;
    }
    sound_.tick();
    ++video.frame_done;    // D132
    ++video.frame_counter; // D12E
}

// 095A: the horizon's interrupts over the frame (the line counter as in
// band_interrupts): each sets the backdrop colour (colour 0) for the
// lines below and the count to the one after next (table 09EA: count,
// unused, colour; mode 14's in DC26), and has the colours uploaded at the
// next frame (D4F3).
void Game::horizon_interrupts() {
    unsigned reload = video.band_counter;
    unsigned counter = reload;
    for (int line = 0; line <= 192; ++line) {
        if (counter) {
            --counter;
            continue;
        }
        counter = reload;
        palette.dirty = 0x01;
        // The ending's table is in RAM (DC26: the work bytes from 40).
        auto band_byte = [&](unsigned i) {
            const unsigned at = video.band * 4u + i;
            if (flow.mode != 0x14) {
                return rom.byte(horizon_bands, at);
            }
            if (0x40 + at >= act.work.size()) {
                throw Unrecovered("the ending's bands past DC45");
            }
            return act.work[0x40 + at];
        };
        reload = band_byte(0);
        auto colors = display.changes.empty() ? display.colors : display.changes.back().colors;
        colors[0] = band_byte(2);
        colors[1] = band_byte(3);
        display.changes.push_back({line + 1, colors});
        ++video.band;
    }
    video.band_counter = u8(reload);
    // The last count (FF) is longer than a frame: no more interrupts until
    // the next frame loads the counter again (the enable bit stays set).
    if (reload > 192) {
        display.line_interrupt = false;
    }
}

} // namespace triple_trouble_gg
