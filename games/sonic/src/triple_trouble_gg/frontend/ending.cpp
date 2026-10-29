// 17F4: modes 12-14, the game's end, after Atomic Destroyer act 3.

#include "triple_trouble_gg/game/game.hpp"
#include <type_traits>

namespace triple_trouble_gg {

namespace {
constexpr u8 rival_song = 0x97, end_song = 0xA3, credits_song = 0xA2;
// 18C3: the plane's two pictures (bank 15, 6 by 7 at 3ADA).
constexpr std::uint16_t plane_picture_a = 0xBDA9, plane_picture_b = 0xBDFD;
// AFC7: the ending's horizon bands (8 of 4 bytes: count, unused, colour),
// copied to DC26 (the work bytes' 40-5F).
constexpr Table ending_bands{0x0F, 0xB007};
constexpr unsigned ending_bands_at = 0x40;
constexpr unsigned credits_frames_at = 0x3E; // DC24 (16 bits)
} // namespace

// 17F4: everything fades out; video memory and the game state cleared.
// With all six emeralds the long ending follows (1824); otherwise (1949)
// the rival's short scene in "zone 0A": its art, his song; he plays his
// part (slot 1, type 8) for 480 frames, then a song and 600 more frames
// of the objects alone; a fade and the SEGA logo again.
Flow Game::ending() {
    flow.frame_work = 0;
    video.line_split = 0;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x3C);
    di();
    co_await clear_screen(); // 273B
    clear_vram();            // 7BCF
    clear_game_state();      // 327E
    clear_map_state();       // 428B
    co_await busy(211242 + 59677 + 12074);
    if (flow.emeralds == 0x06) {
        co_await full_ending(); // 1824
        co_return;
    }
    co_await load_ending_scene(); // 7B36
    palette.fades[1] = {0x80, 0x0F};
    sound_.request(rival_song, 2);
    flow.zone = 0x0A;
    objects[1].type = 0x08;
    flow.frame_work = frame_work::build_sprites;
    for (unsigned i = 0; i < 0x01E0; ++i) {
        co_await next_frame();     // 084C
        co_await update_partner(); // 34B9
        co_await update_objects(); // 4F3F
    }
    sound_.request(end_song, 2);
    palette.fades[0] = {0x80, 0x0B};
    for (unsigned i = 0; i < 0x0258; ++i) {
        co_await next_frame();
        co_await update_objects();
    }
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x3C);
    flow.mode = mode::sega_logo;
}

// 1824: with all six emeralds: the ending's art (7BB5), a fade to white,
// colours 33 and 34 faded in, its song; its objects (0F:AFC7) and the
// credits: half the objects each frame, the plane's picture (18C3), every
// fourth frame the view's sway (190F), until the credits end (DBE1); then
// half a minute, a fade out, and the program starts again from power-on.
Flow Game::full_ending() {
    flow.act = 0;
    co_await load_full_ending_art(); // 7BB5
    palette.fades[0].mode = 0x50;
    palette.fades[1].mode = 0x50;
    co_await wait_frames(0x3C);
    palette.fade_period = 3;
    palette.fades[0] = {0x90, 0x33};
    palette.fades[1] = {0x90, 0x34};
    sound_.request(credits_song, 2);
    act.credits_running = 0x01;
    start_credits(); // 0F:AFC7
    for (;;) {
        co_await next_frame();               // 084C
        co_await update_alternate_objects(); // 4F9C
        credits_frame();                     // 18C3
        const std::uint16_t frames = std::uint16_t(credits_frames() + 1);
        act.work[credits_frames_at] = lo(frames);
        act.work[credits_frames_at + 1] = hi(frames);
        if ((video.frame_counter & 0x03) != 0x03) {
            continue;
        }
        credits_sway(); // 190F
        if (!act.credits_running) {
            break;
        }
    }
    co_await wait_frames(0x0708);
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    palette.water_fades[0].mode = 0x40;
    palette.water_fades[1].mode = 0x40;
    co_await wait_frames(0x3C);
    // JP 0000: the program starts again; power-on clears the work RAM.
    for_each_state([](auto& part) { part = std::decay_t<decltype(part)>{}; });
    co_await boot();
}

std::uint16_t Game::credits_frames() const {
    return std::uint16_t(act.work[credits_frames_at] | act.work[credits_frames_at + 1] << 8);
}

// 7BB5: 0D:B9B0 at tile 100, then the list 1A:B995 (7A58, with frame waits).
Flow Game::load_full_ending_art() {
    di();
    co_await busy(17 + 85 + load_tiles(0x2000, 0x0D, 0xB9B0));
    co_await load_tile_list_slowly(Table{0x1A, 0xB995}); // 7A58
}

// 0F:AFC7: the frame count 0; the horizon at line 70, split; the frame's
// work (D144 2); the ending's objects (types 63 (6), 69 (1), 68, 65); its
// bands into DC26.
void Game::start_credits() {
    act.work[credits_frames_at] = 0;
    act.work[credits_frames_at + 1] = 0;
    act.scene[scene_byte::horizon] = 0x70;
    act.scene[scene_byte::split] = 0xFF;
    flow.frame_work = 0x02;
    // 50E1 (0354) with IX where the tally's digits (19:BF13) left it, at
    // D154: no object; each spawn's tag (52CA of D154: F2) is written 49
    // past it, into the camera's mode (D185).
    const u8 types[][2] = {{0x63, 0x06}, {0x69, 0x01}, {0x68, 0x00}, {0x65, 0x00}};
    for (const auto& t : types) {
        for (unsigned i = 2; i < 18; ++i) {
            if (!objects[i].type) {
                objects[i].type = t[0];
                objects[i].param = t[1];
                camera.mode = 0xF2;
                break;
            }
        }
    }
    for (unsigned i = 0; i < 0x20; ++i) {
        act.work[ending_bands_at + i] = rom.byte(ending_bands, i);
    }
}

// 18C3: every 8 frames (while the act has a boss, D3E5) the horizon rises
// a line (to 2C) and the boss with it by 2; on the eighth frame of 8 the
// plane's picture (15:BDA9 / BDFD by frame counter bit 3) at 3ADA.
void Game::credits_frame() {
    const u8 frames = video.frame_counter;
    if (!(frames & 0x07) && act.boss) {
        u8& horizon = act.scene[scene_byte::horizon];
        if (horizon >= 0x2C) {
            --horizon;
            Object& b = objects[act.boss - 1u];
            b.y = std::uint16_t(b.y - 2);
        }
    }
    if ((frames & 0x07) != 0x07) {
        return;
    }
    di();
    draw_rect(0x3ADA, 0x15, (frames & 0x08) ? plane_picture_b : plane_picture_a, 0x06,
              0x07); // 2C3B
    ei();
}

// 190F: the view sways left and right (scroll X between F8 and 0, the way
// in the player's byte 0A) until frame 2600 of the credits; then it moves
// to 90.
void Game::credits_sway() {
    u8& sx = camera.scroll_x;
    u8& way = objects[0].angle;
    if (credits_frames() >= 0x2600) {
        if (sx != 0x90) {
            --sx;
        }
        return;
    }
    if (!(way & 0x01)) {
        --sx;
        if (sx < 0xF8) {
            ++way;
        }
        return;
    }
    ++sx;
    if (!sx) {
        ++way;
    }
}

// 7B36: the scene's art with interrupts off: 1A:ABF1 at tile C0, 06:BA8C
// at tile 24, and the 16 by 3 map 1F:BEF4 at 3A90, whose EI (2C73) lets
// the interrupt held meanwhile run.
Flow Game::load_ending_scene() {
    di();
    unsigned clocks = load_tiles(0x1800, 0x1A, 0xABF1);
    clocks += load_tiles(0x0480, 0x06, 0xBA8C);
    clocks += draw_rect(0x3A90, 0x1F, 0xBEF4, 0x03, 0x10);
    co_await busy(clocks);
    ei(); // 2C73
}

} // namespace triple_trouble_gg
