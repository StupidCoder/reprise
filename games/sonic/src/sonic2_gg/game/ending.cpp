// The ending (zone 7; act 1 with all six emeralds): Sonic runs on the spot
// while the credits fly in letter by letter (bank 1D, B2F0/B2FC); then
// the scene with 5D, and the game is over. Also object 5D in the last
// boss scene (the boss's remains, parameter 1).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 0686 (D293 bit 7)
Flow Game::ending_state() {
    co_await init_act();  // 2206
    direct_start = false; // a level selector start: from here on, original timing
    play_act_music();     // 10B8
    fade_in_act();        // 7578
    enable_map();         // 4A0F
    play_act_music();
    co_await ending_scene(); // 06BD
    palette.cycle1[0] = 0;
    palette.cycle2[0] = 0;
    start_fade_out(); // 12FC
    co_await wait_frames(60);
    score.continues = 0;
    score.emeralds = 0;
    flow.game_flags &= u8(
        ~(game_flag::scene | game_flag::playing | game_flag::zone_complete | game_flag::life_lost));
}

// 06BD: Sonic runs (state 2A) with 5D in slot 8; the credits start after
// 300 frames; the scene ends when 5D reaches its state 6.
Flow Game::ending_scene() {
    camera.mode |= camera_mode::hold_vertical;
    camera.offset_y = 0x66;
    objects[8].type = object_type::companion;
    sonic().type = object_type::sonic;
    sonic().next_state = 0x2A;
    clock.running = 0;
    palette.cycle1[0] = 0x0D;
    credits_clear();          // B2F0
    ending.letters[0] = 0x2C; // 012C
    ending.letters[1] = 0x01;
    do {
        if (main_loop_hook) {
            main_loop_hook(); // the harness's snapshot point (05CA)
        }
        co_await wait_frame(); // 05C5
        game_frame();          // 09BB
        credits_tick();        // B2FC
    } while (objects[8].state != 0x06);
}

// --- the credits (bank 1D) --------------------------------------------------
// Sixteen sprites (30-3F): two rows of eight letters at Y 8A and 99.

// B2F0: the letters off screen.
void Game::credits_clear() {
    for (unsigned i = 0; i < 0x10; ++i) {
        sprites.table[0x30 + i].y = 0xE0;
    }
}

namespace {
unsigned letters_word(const EndingState& ending, unsigned i) {
    return ending.letters[i * 3] | (ending.letters[i * 3 + 1] << 8);
}
} // namespace

// B2FC: once a frame.
void Game::credits_tick() {
    switch (ending.phase) {
    case 0: { // a pause (the word in D44A)
        const unsigned t = (letters_word(ending, 0) - 1) & 0xFFFF;
        ending.letters[0] = u8(t);
        ending.letters[1] = u8(t >> 8);
        if (!t) {
            ending.phase = 1;
        }
        return;
    }
    case 1:
        credits_next_line(); // B323
        return;
    case 2:
        credits_move(); // B39F
        return;
    case 3: { // B400: shown for three seconds
        const unsigned t = (letters_word(ending, 0) - 1) & 0xFFFF;
        if (!t) {
            ending.phase = 1;
            return;
        }
        ending.letters[0] = u8(t);
        ending.letters[1] = u8(t >> 8);
        return;
    }
    case 4:
        credits_clear();
        return;
    default:
        return;
    }
}

// B323: the next line of the text (16 characters, table B442; FF ends the
// credits): tiles 12 + 2 per letter from A, comma 46, hyphen 48, others
// (the space) 4A; each letter starts at its X from B412 (a speed in the
// high byte) and flies to its place from B432.
void Game::credits_next_line() {
    for (unsigned i = 0; i < 8; ++i) {
        sprites.table[0x30 + i].y = 0x8A;
        sprites.table[0x38 + i].y = 0x99;
    }
    const auto text = rom::credits_text + ending.line * 16u;
    for (unsigned i = 0; i < 0x10; ++i) {
        sprites.table[0x30 + i].x = 0;
        const u8 ch = rom.byte(text, i);
        if (ch == 0xFF) {
            ending.phase = 4; // B399
            return;
        }
        u8 tile = u8(u8(u8(ch - 0x41) * 2) + 0x12);
        if (tile >= 0x45) {
            tile = tile == 0xE8 ? 0x46 : tile == 0xEA ? 0x48 : 0x4A;
        }
        sprites.table[0x30 + i].tile = tile;
    }
    for (unsigned i = 0; i < 0x10; ++i) {
        ending.letters[i * 3] = rom.byte(rom::credits_starts, i * 2);
        ending.letters[i * 3 + 1] = rom.byte(rom::credits_starts, i * 2 + 1);
        ending.letters[i * 3 + 2] = rom.byte(rom::credits_places, i);
    }
    ++ending.line;
    ending.phase = 2;
}

// B39F: each letter moves by its speed (high byte), which drops by 20/256
// per frame; once the speed is negative it stops at its place. When all
// are there, the line stays for 180 frames (3).
void Game::credits_move() {
    bool moving = false;
    for (unsigned i = 0; i < 0x10; ++i) {
        u8* l = &ending.letters[i * 3];
        u8& x = sprites.table[0x30 + i].x;
        if ((l[1] & 0x80) && l[2] >= x) {
            x = l[2];
            continue;
        }
        // B3E5
        x = u8(x + l[1]);
        const unsigned w = ((l[0] | (l[1] << 8)) + 0xFFE0u) & 0xFFFF;
        l[0] = u8(w);
        l[1] = u8(w >> 8);
        moving = true;
    }
    if (moving) {
        return;
    }
    ending.phase = 3;
    ending.letters[0] = 0xB4;
    ending.letters[1] = 0x00;
}

// --- Sonic in the ending ------------------------------------------------------

// A965: running on the spot: he moves, and his X wraps within 90-28F.
void Game::sonic_ending_move() {
    Object& o = sonic();
    move_object(o); // 0257
    unsigned x = (unsigned(o.x) + 0xFF70u) & 0xFFFF;
    x = (x & 0x01FF) + 0x0090;
    o.x = x & 0xFFFF;
}

// A925 (states 29-2B): when the credits are over, the last part (2C;
// 5D to state A).
void Game::sonic_ending_run() {
    sonic_ending_move();
    if (ending.phase != 0x04) {
        return;
    }
    sonic().next_state = 0x2C;
    objects[8].next_state = 0x0A;
}

// A938 (state 2C): until the camera is at X 8 (5D to state 2).
void Game::sonic_ending_arrive() {
    sonic_ending_move();
    if (camera.x != 0x0008) {
        return;
    }
    sonic().next_state = 0x2D;
    objects[8].next_state = 0x02;
}

// A951 (state 2D): the camera stops there (5D to state 3).
void Game::sonic_ending_stop() {
    script_camera(camera.x, 0x0008); // 02F6
    sonic().next_state = 0x2E;
    objects[8].next_state = 0x03;
}

// A91B / A920: facing left / right.
void Game::sonic_face(bool left) {
    if (left) {
        sonic().display |= display_flag::mirrored;
    } else {
        sonic().display &= u8(~display_flag::mirrored);
    }
}

// --- 5D --------------------------------------------------------------------------

// 958F (state 0): parameter 1 (the last boss's remains) waits 224 frames
// (7); otherwise, in the ending, it follows Sonic (1), seen only with all
// six emeralds.
void Game::companion_start(Object& o) {
    if (o.param) {
        o.next_state = 7;
        o.timer = 0xE0;
        boss_palette(0x09); // 915E
        return;
    }
    if ((score.emeralds & 0x3F) != 0x3F) {
        o.display |= display_flag::hidden;
    }
    o.display |= display_flag::mirrored | display_flag::keep_on_screen;
    o.next_state = 1;
    o.y = sonic().y;
    o.tile_base_mirrored = 0x1C;
    o.tile_base = 0x4A;
    o.x = (unsigned(sonic().x) + 0xFFE0u) & 0xFFFF;
}

// 95E9 (state 1): behind Sonic, drifting back and forth (IX+0A/0B as a
// word, high byte first: 0-28 pixels).
void Game::companion_follow(Object& o) {
    unsigned d = (o.angle << 8) | o.speed;
    d = (d + ((o.timer & 0x01) ? 0x0008u : 0xFFF8u)) & 0xFFFF;
    if (d & 0x8000) {
        d = 0;
        o.timer |= 0x01;
    } else if (d > 0x2800) {
        d = 0x2800;
        o.timer &= u8(~0x01);
    }
    o.angle = u8(d >> 8);
    o.speed = u8(d);
    o.x = (unsigned(sonic().x) + 0xFFE0u - o.angle) & 0xFFFF;
    move_object(o);
}

// 95D9 (state A): just behind Sonic.
void Game::companion_close(Object& o) {
    o.x = (unsigned(sonic().x) + 0xFFE0u) & 0xFFFF;
    move_object(o);
}

// 950E (state 3): palette cycle E and a sound.
void Game::companion_signal() {
    palette.cycle1[0] = 0x0E;
    sound_.request(sound::ending);
}

// 9519 (state 3, end): drops in from above the screen at X 88 (4).
void Game::companion_drop(Object& o) {
    o.y = 0xFFE0;
    o.x = 0x0088;
    o.display &= u8(~(display_flag::mirrored | display_flag::hidden));
    o.next_state = 4;
}

// 9555 (state 4): falls (gravity 40) to Y A0 and bounces until slow (5).
void Game::companion_fall(Object& o) {
    fall(o, 0x0040, 0x0600);
    move_object(o);
    if (o.y & 0x8000) {
        return;
    }
    if (o.y < 0x00A0) {
        return;
    }
    const unsigned vy = o.vy;
    if (vy & 0x8000) {
        return;
    }
    if (vy > 0x0080) {
        o.vy = (0x0080u - vy) & 0xFFFF;
        return;
    }
    o.next_state = 5;
}

} // namespace sonic2_gg
