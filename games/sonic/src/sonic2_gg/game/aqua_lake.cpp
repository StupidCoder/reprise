// Aqua Lake Zone's water: the air countdown (09), bubbles (0A), the splash
// (0B), the rising water of act 2 (0C) with its line interrupt, and
// Sonic's states in a big bubble (24), breathing (25) and drowning (28).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
constexpr u8 sonic_drowned = 0x1F, sonic_drowning = 0x28, sonic_climbing = 0x21;
constexpr u8 sonic_in_bubble = 0x24, sonic_breathing = 0x25;

// Sonic states in which bubbles do nothing to him.
bool takes_bubbles(u8 state) {
    return state != sonic_drowned && state != sonic_drowning && state != sonic_climbing;
}
} // namespace

// --- 09: the air countdown -------------------------------------------------

// AFEB: a digit above Sonic.
void Game::air_count_start(Object& o) {
    o.x = sonic().x;
    o.y = (sonic().y + 0xFFDEu) & 0xFFFF;
    o.tile_base = 0x0E;
    o.tile_base_mirrored = 0x0E;
    o.next_state = 1;
    o.display |= display_flag::keep_on_screen;
}

// B012: counts 5 to 0 (the script, two seconds per digit, blinking); gone
// once Sonic has air again.
void Game::air_count_show(Object& o) {
    o.x = sonic().x;
    o.y = (sonic().y + 0xFFD8u) & 0xFFFF;
    o.display &= u8(~display_flag::hidden);
    if (o.frame_ticks & 0x04) {
        o.display |= display_flag::hidden;
    }
    if (!water.air_count) {
        o.type = object_type::removed;
    }
}

// B040: out of air: Sonic drowns (state 28) and the camera stops going
// down.
void Game::air_count_drown(Object& o) {
    Object& s = sonic();
    s.next_state = sonic_drowning;
    s.flags |= 0x01;
    s.display = 0;
    s.vy = 0;
    player.input_force = 0;
    s.terrain &= u8(~0x02);
    sound_.request(sound::life_lost);
    camera.max_y = camera.y;
    o.type = object_type::removed;
}

// --- 0A: bubbles -----------------------------------------------------------
// The parameter picks the kind: 0 a bubble spring (state 1, sending a
// cluster of six small bubbles drawn from D3CE), 1 a breathable bubble (3),
// 2 a big bubble that carries Sonic (5), 3 Sonic's own bubbles (7).

// B164
void Game::bubble_start(Object& o) {
    o.next_state = u8(o.param * 2 + 1);
    o.flags |= 0x80;
    if (o.next_state != 7) {
        return;
    }
    o.display = u8((o.display | display_flag::keep_on_screen) & ~display_flag::offscreen);
}

// B316: rises, wobbling 1/4 pixel left and right every 16 frames.
void Game::bubble_wobble(Object& o) {
    ++o.counter;
    o.vx = (o.counter & 0x10) ? 0xFFC0 : 0x0040;
    move_object(o);
}

// B3DD: gone when more than 512 pixels below the camera or above it, at
// the surface (act 2: the rising water; otherwise where the profile 30
// pixels up is empty or solid).
void Game::bubble_check_surface(Object& o) {
    // Widescreen margins (zero on the LCD) keep it until it leaves the view.
    const unsigned top = (camera.y - unsigned(wide_top)) & 0xFFFF;
    const unsigned dy = (unsigned(o.y) - top) & 0xFFFF;
    if (unsigned(o.y) < top || (dy >> 8) >= 0x02) {
        o.type = object_type::removed;
        return;
    }
    if (flow.act == 1) {
        if (((unsigned(water.rise_y) + 0x0010u) & 0xFFFF) < unsigned(o.y)) {
            return;
        }
        o.type = object_type::removed;
        return;
    }
    const u8 type = probe_type(o, 0, 0xFFD0); // 0263
    if (type == 0x00 || type == 0x81) {
        o.type = object_type::removed;
    }
}

// B30F
void Game::bubble_rise(Object& o) {
    bubble_wobble(o);
    bubble_check_surface(o);
}

// B17C (state 1): the cluster of small bubbles spreads (B32F) as it rises.
void Game::bubble_cluster(Object& o) {
    bubble_spread();
    bubble_wobble(o);
}

// B32F: moves the six small bubbles of a cluster. Each has two 24-bit
// offsets (the run-time layout's words and fractions) that
// grow by a pair of speeds from table B3AD, picked by the clock's tens of
// seconds; the first also by its seconds.
void Game::bubble_spread() {
    const u8 seconds = clock.seconds;
    for (unsigned b = 6, k = 0; b >= 1; --b, ++k) {
        const auto entry = rom::bubble_speeds + u8(u8(((seconds >> 4) & 0x03) * 2 + b - 1) * 4);
        const unsigned first = (rom.word(entry) + 0xFFF4u + u8((seconds & 0x0F) + b)) & 0xFFFF;
        const unsigned second = rom.word(entry, 2);
        // 24-bit offsets: the word and its fraction, plus a signed speed.
        auto add24 = [](std::uint16_t& whole, u8& fraction, unsigned delta) {
            unsigned v = (whole << 8) | fraction;
            v = (v + delta + ((delta & 0x8000) ? 0xFF0000u : 0)) & 0xFFFFFF;
            whole = std::uint16_t(v >> 8);
            fraction = u8(v);
        };
        auto& offset = sprites.run_layout[k];
        add24(offset.dx, offset.dx_fraction, first);
        add24(offset.dy, offset.dy_fraction, second);
    }
}

// B2B2: a random-looking wait (by slot and seconds) before the next frame.
void Game::bubble_wait(Object& o) {
    const u8 slot2 = u8(slot_number(o) * 2);
    o.frame_ticks = u8(((clock.seconds + slot2) & 0x3F) + 0x40);
    o.frame = 0;
}

// B2CA: back at the origin.
void Game::bubble_reset(Object& o) {
    o.x = o.origin_x;
    o.y = o.origin_y;
}

// B2E3: the origin becomes Sonic's mouth.
void Game::bubble_from_sonic(Object& o) {
    o.origin_x = sonic().x;
    o.origin_y = (sonic().y + 0xFFE8u) & 0xFFFF;
}

// B29B: Sonic's bubbles face alternately each second.
void Game::bubble_face(Object& o) {
    o.tile_base = 0xCE;
    o.tile_base_mirrored = 0xCE;
    if (clock.seconds & 0x01) {
        o.display &= u8(~display_flag::mirrored);
    } else {
        o.display |= display_flag::mirrored;
    }
}

// B2FA: the cluster's offsets start at 0.
void Game::bubble_clear_cluster() {
    sprites.run_layout = {};
}

// B187 (state 4): a breathable bubble: touched, Sonic breathes (25) and
// it pops (9).
void Game::bubble_breathe(Object& o) {
    bubble_rise(o);
    touch_sonic(o);
    if (!(o.touch_sides & 0x0F) || !takes_bubbles(sonic().state)) {
        return;
    }
    o.next_state = 9;
    sonic().next_state = sonic_breathing;
}

// B1A9 (state 6): a big bubble takes Sonic in (state 24; it becomes A).
void Game::bubble_catch(Object& o) {
    bubble_rise(o);
    touch_sonic(o);
    if (!(o.touch_sides & 0x0F) || !takes_bubbles(sonic().state)) {
        return;
    }
    Object& s = sonic();
    o.x = s.x;
    o.y = s.y;
    s.next_state = sonic_in_bubble;
    s.touch_sides = 0;
    s.flags = 0;
    o.next_state = 0x0A;
    o.vy = 0xFF00;
}

// B24E: steering the big bubble: left/right move it 1 pixel per frame;
// up rises faster (1), down slower (0.19), otherwise 0.5.
void Game::bubble_steer(Object& o) {
    const u8 held = input.held[0];
    if (held & 0x0C) {
        o.vx = (held & 0x08) ? 0x0100 : 0xFF00;
        const unsigned vy = o.vy;
        o.vy = 0;
        move_object(o);
        o.vy = vy;
    }
    if (held & 0x01) {
        o.vy = 0xFF00;
    } else if (held & 0x02) {
        o.vy = 0xFFD0;
    } else {
        o.vy = 0xFF80;
    }
}

// B1ED (state A): carrying Sonic; it bursts against solid ground above or
// beside it, at the surface, or when Sonic leaves state 24.
void Game::bubble_carry(Object& o) {
    bubble_rise(o);
    bubble_steer(o);
    Object& s = sonic();
    s.x = o.x;
    s.y = o.y;
    bool burst = o.type != object_type::bubble;
    if (!burst) {
        burst = probe_type(o, 0, 0xFFE0) == 0x81 || probe_type(o, 0xFFF0, 0xFFF0) == 0x81 ||
                probe_type(o, 0x0010, 0xFFF0) == 0x81;
    }
    if (!burst) {
        if (s.state == sonic_in_bubble) {
            return;
        }
        o.next_state = 5;
        return;
    }
    // B23E
    s.next_state = 0x0E;
    s.vy = 0x0080;
    o.next_state = 5;
}

// --- 0B: a splash ----------------------------------------------------------

// B43A: at Sonic, on the 32-pixel row above his feet (act 2: at the
// rising water's surface).
void Game::splash_start(Object& o) {
    o.x = sonic().x;
    o.y = ((sonic().y + 0xFFF0u) & 0xFFE0) & 0xFFFF;
    if (flow.act == 1) {
        o.y = (unsigned(water.rise_y) + 0xFFE8u) & 0xFFFF;
    }
    o.tile_base = 0xCC;
    o.next_state = 1;
    o.display |= 0x01;
}

// --- 0C: the rising water (act 2) ------------------------------------------

// B485: the surface sprite at Y E0; the line interrupt on.
void Game::water_start(Object& o) {
    o.x = camera.x;
    o.y = 0x00E0;
    o.tile_base = 0xC6;
    o.display |= display_flag::keep_on_screen;
    video.line_interrupt = 0xFF;
    o.next_state = 1;
    display.line_interrupt = true; // 0323 (1320): register 0 = 36
}

// B4AC (state 1): the water rises half a pixel per frame up to Y 30.
void Game::water_rise(Object& o) {
    water_surface_x(o); // B55B
    water_line();       // B4E0
    ++o.counter;
    if (!(o.counter & 0x01)) {
        return;
    }
    o.y = o.y - 1u;
    water.rise_y = o.y;
    if (o.y != 0x0030) {
        return;
    }
    water_off();                    // B527
    display.line_interrupt = false; // 0326 (1327): register 0 = 26
    o.counter = 0;
    o.type = object_type::removed;
}

// B55B: the surface sprite steps along ten places 16 pixels apart.
void Game::water_surface_x(Object& o) {
    u8& step = o.timer;
    if (++step >= 0x0A) {
        step = 0;
    }
    o.x = (camera.x + rom.word(rom::water_surface_x, step * 2u)) & 0xFFFF;
}

// B4E0: with the surface on screen the line interrupt splits the picture
// at it (normal palettes 12/08); with the camera under water the whole
// screen uses the underwater palettes (31/33); far below the screen, off.
void Game::water_line() {
    const unsigned top = (camera.y + 0x0020u) & 0xFFFF;
    const unsigned surface = water.rise_y;
    if (top >= surface) {
        water_off();
        return;
    }
    if ((surface - top) >> 8) {
        // B541
        video.line_interrupt = 0;
        video.line_interrupt_line = 0xFF;
        set_fade_palettes(0x12, 0x08);
        return;
    }
    if (camera.y >= surface) {
        return;
    }
    video.line_interrupt_line = u8(surface - camera.y);
    video.line_interrupt = 0xFF;
    set_fade_palettes(0x12, 0x08);
}

// B527
void Game::water_off() {
    video.line_interrupt = 0;
    video.line_interrupt_line = 0xFF;
    set_fade_palettes(0x31, 0x33);
}

// B516 / B530: both palettes switched (fade bit 5) to these.
void Game::set_fade_palettes(u8 background, u8 sprites) {
    palette.fades[0] |= 0x20;
    palette.fades[1] = background;
    palette.fades[2] |= 0x20;
    palette.fades[3] = sprites;
}

// --- Sonic ---------------------------------------------------------------

// 347B (state 24): in a big bubble: he only collects rings, faces the
// input and touches objects; his air is full.
void Game::sonic_in_bubble_update() {
    Object& o = sonic();
    o.display &= u8(~display_flag::hidden);
    collect_map_items(); // 723C
    combine_contacts();  // 67FC
    face_input();        // 318D
    touch_objects();     // 3773
    water.air_count = 0;
    camera.mode |= camera_mode::hold_vertical;
}

// A84B / A852: his frames in the bubble, 12 ticks each; 8 holding up, 16
// holding down.
void Game::sonic_bubble_frame(u8 frame) {
    Object& o = sonic();
    o.frame = frame;
    const u8 held = input.held[0] & 0x03;
    u8 ticks = 0x0C;
    if (held) {
        ticks = (held & 0x01) ? 0x08 : 0x10;
    }
    o.frame_ticks = ticks;
}

// 34D7 (state 28): drowned: sinks at half a pixel per frame; once low on
// the screen (Y D8) the life is lost and he drops fast.
void Game::sonic_drown() {
    Object& o = sonic();
    o.display &= u8(~display_flag::hidden);
    camera.mode |= camera_mode::hold_vertical;
    unsigned vy = 0x0080;
    if (lo(o.screen_y) >= 0xD8) {
        flow.game_flags |= game_flag::life_lost;
        vy = 0x1000;
    }
    o.vy = vy;
    move_vertically(); // 3B66
}

} // namespace sonic2_gg

namespace sonic2_gg {

namespace {
// |a - b| when below 256, otherwise -1.
int near_distance(unsigned a, unsigned b) {
    const unsigned d = a >= b ? a - b : b - a;
    return (d >> 8) ? -1 : int(d & 0xFF);
}
} // namespace

// --- 26: a trap that shoots out when Sonic lines up with it ---------------
// Parameter n: state 2n+1 waits (for Sonic above, below, left or right),
// state 2n+2 flies with the script's velocity.

// 86B3
void Game::trap_start(Object& o) {
    o.next_state = u8(o.param * 2 + 1);
}

// 8748: off it goes.
void Game::trap_fire(Object& o) {
    o.next_state = u8(o.state + 1);
}

// 86BC (states 1, 3): Sonic above it, within 48 pixels horizontally.
void Game::trap_wait_above(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (sonic().y >= o.y) {
        return;
    }
    const int d = near_distance(sonic().x, o.x);
    if (d >= 0 && d < 0x30) {
        trap_fire(o);
    }
}

// 86ED (state 5): Sonic below it.
void Game::trap_wait_below(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (sonic().y < o.y) {
        return;
    }
    const int d = near_distance(sonic().x, o.x);
    if (d >= 0 && d < 0x30) {
        trap_fire(o);
    }
}

// 8702 (state 7): Sonic to its left, within 48 pixels vertically.
void Game::trap_wait_left(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (sonic().x >= o.x) {
        return;
    }
    const int d = near_distance(sonic().y, o.y);
    if (d >= 0 && d < 0x30) {
        trap_fire(o);
    }
}

// 8733 (state 9): Sonic to its right.
void Game::trap_wait_right(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    if (sonic().x < o.x) {
        return;
    }
    const int d = near_distance(sonic().y, o.y);
    if (d >= 0 && d < 0x30) {
        trap_fire(o);
    }
}

// 8750 (even states): flying; hurts Sonic; waits as FE off screen.
void Game::trap_fly(Object& o) {
    move_object(o);
    touch_sonic(o);
    if (o.touch_sides & 0x0F) {
        player.hit_request = 0xFF;
    }
    if (o.display & display_flag::offscreen) {
        o.type = object_type::waiting;
    }
}

// --- 33: a fish that chases Sonic ------------------------------------------

// AEDE (state 1, first frame): resting, facing Sonic.
void Game::fish_rest(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    touch_sonic(o);
    face_x(o, sonic().x);
    destroy_if_attacked(o); // 020C
}

// AEEC (state 1, second frame): Sonic within |dx| + |dy| < 128 starts a
// chase of 80 frames (state 2).
void Game::fish_watch(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    touch_sonic(o);
    face_x(o, sonic().x);
    const int dx = near_distance(sonic().x, o.x);
    const int dy = near_distance(sonic().y, o.y);
    if (dx >= 0 && dy >= 0 && dx + dy < 0x80) {
        o.next_state = 2;
        o.timer = 0x50;
    }
    destroy_if_attacked(o);
}

// AF41 (state 2): swims at Sonic (0.75 pixels per frame each way) until a
// wall, touching him, or the time is up; then home (state 3).
void Game::fish_chase(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    move_toward_x(o, sonic().x, 0, 0x00C0); // 0248
    move_toward_y(o, sonic().y, 0, 0x00C0); // 024B
    touch_sonic(o);
    object_wall(o); // 0239
    bool home = o.terrain & 0x0C;
    if (!home) {
        move_object(o);
        object_floor(o); // 0260
        face_x(o, sonic().x);
        home = (o.touch_sides & 0x0F) || !--o.timer;
    }
    if (home) {
        o.next_state = 3;
    }
    destroy_if_attacked(o);
}

// AF88 (state 3): back to its origin at 1.5 pixels per frame (not moving
// in frame 4), then rests again.
void Game::fish_return(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    object_floor(o);
    face_x(o, sonic().x);
    if (o.frame != 0x04) {
        move_object(o);
    }
    touch_sonic(o);
    move_toward_x(o, o.origin_x, 0, 0x0180);
    move_toward_y(o, o.origin_y, 0, 0x0180);
    if (!o.vx && !o.vy) {
        o.next_state = 1;
    }
    destroy_if_attacked(o);
}

} // namespace sonic2_gg
