// The objects of the ending with all emeralds (frontend/ending.cpp: 1824),
// timed by the credits' frame count (DC24).
//
// Type 63 (bank 1C, B8D8-BC98), the sky: from frame 1800 sixteen steps of
// its colours towards the evening's (every 5 frames), from 2500 sixteen
// towards the dusk's (every 10), from 2880 the horizon's bands eight
// steps, then sixteen towards night; the stars (type 6A, 1-12); from 5000
// (then 224 frames) the credits end (DBE1 0). The targets are the code's
// own (B9BB-BC40): palette 1's colours 1-14, colour 0 with the horizon's
// eight bands (DC28, DC2C, ... DC44).
// Type 65 (bank 1C, BC99-BE3F), the credits' text (1C:BE40, 9 bytes a
// line: eight letters, A = tile 0 in steps of 2, then the X step; FF after
// the last): a line every 120 frames as a sprite frame built in the work
// bytes (layout DBE6, from 1C:BD8D; tiles DC06); then the score (D14C)
// for a second; the last line rises to Y 60 and spreads.
// Type 68 (bank 1A, BAAC-BAFB), the plane in the distance: the act's
// "boss" the horizon lifts (18C3).
// Type 69 (bank 1A, BAFC-BC97), scenery passing to the left: parameter 1
// sends the others (7, 6, 4, 5, 9, 10; 20 times) until frame 2580.
// Type 6A (bank 1A, BC98-BDD0), twelve twinkling stars.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr Table credits_text{0x1C, 0xBE40};
constexpr Table credits_layout{0x1C, 0xBD8D};
constexpr unsigned text_tiles = 0x20; // DC06 in the work bytes
constexpr unsigned frames_at = 0x3E;  // DC24 (16 bits) in the work bytes
constexpr unsigned bands_at = 0x40;   // DC26: the horizon's bands

// B9BB-BC40: the sky's targets.
constexpr std::uint16_t evening[14] = {0x006F, 0x0801, 0x008F, 0x001B, 0x0A31, 0x0001, 0x0DDF,
                                       0x012F, 0x0008, 0x0224, 0x099B, 0x0005, 0x059F, 0x0557};
constexpr std::uint16_t dusk[14] = {0x005F, 0x0804, 0x007F, 0x0017, 0x0A34, 0x0003, 0x0AAF,
                                    0x012D, 0x0005, 0x0114, 0x077B, 0x0002, 0x059E, 0x0447};
constexpr std::uint16_t evening_horizon[8] = {0x049F, 0x066B, 0x075A, 0x0849,
                                              0x0938, 0x0A27, 0x0B16, 0x0C05};
constexpr std::uint16_t dusk_horizon[8] = {0x009F, 0x006F, 0x005F, 0x004F,
                                           0x003F, 0x002F, 0x001F, 0x000E};
constexpr std::uint16_t night_horizon[8] = {0x0000, 0x0634, 0x0523, 0x0412,
                                            0x0301, 0x0200, 0x0100, 0x0000};

// The script positions the sky moves to at its times, and the times.
struct SkyStep {
    std::uint16_t from, script;
};

constexpr SkyStep sky_steps[] = {{0x1800, 0xB8F4}, {0x2500, 0xB90B}, {0x2880, 0xB922}};
} // namespace

bool Game::run_credits_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::sky_start: // 1C:B9AC: state by parameter; tiles 24
        o.next_state = o.param;
        o.tile_base = 0x24;
        o.display |= 0x02;
        return true;
    case Routine::sky_wait_evening: // BC41
        sky_wait(o, sky_steps[0].from, sky_steps[0].script);
        return true;
    case Routine::sky_wait_dusk: // BC58
        sky_wait(o, sky_steps[1].from, sky_steps[1].script);
        return true;
    case Routine::sky_wait_night: // BC6F
        sky_wait(o, sky_steps[2].from, sky_steps[2].script);
        return true;
    case Routine::sky_evening: // B9BB: then the horizon too (BA68)
        step_colours(evening);
        step_horizon(evening_horizon);
        return true;
    case Routine::sky_evening_horizon: // BA68
        step_horizon(evening_horizon);
        return true;
    case Routine::sky_dusk: // BACC
        step_colours(dusk);
        step_horizon(dusk_horizon);
        return true;
    case Routine::sky_night: // BBDD
        step_horizon(night_horizon);
        return true;
    case Routine::credits_end: // BC86: the credits end; then as BC8A
        act.credits_running = 0;
        [[fallthrough]];
    case Routine::credits_hold: // BC8A: the count held at 5000
        if (credits_frames() >= 0x5000) {
            act.work[frames_at] = 0x00;
            act.work[frames_at + 1] = 0x50;
        }
        return true;
    case Routine::text_start: // BD0D: tiles 44; the text from its start; at (0, A8)
        o.display |= 0x02;
        o.next_state = 0x01;
        o.tile_base = 0x44;
        o.scratch[0] = lo(credits_text.address);
        o.scratch[1] = hi(credits_text.address);
        o.counter = 0;
        o.timer = 0;
        text_place(o, 0xA8);
        return true;
    case Routine::text_line: // BD3A
        text_line(o);
        return true;
    case Routine::text_done: // BDAD: after the last line the score (2)
        if (o.timer) {
            o.next_state = 0x02;
        }
        return true;
    case Routine::text_score: // BDB7: "?" and the score's six digits as tiles
        text_score();
        return true;
    case Routine::text_last: // BE07: the next line at (0, B8)
        text_line(o);
        text_place(o, 0xB8);
        return true;
    case Routine::text_rise: // BCE6: up faster by 1/4 a frame, to Y 60
        o.vy = std::uint16_t(o.vy - 0x40);
        move_object(o); // 5359 (0381)
        if (o.y < 0x0060) {
            o.y = 0x0060;
        }
        return true;
    case Routine::text_spread: { // BE1B: the second letter 2 left, the fourth 2 right (DBEE, DBF6)
        auto word = [&](unsigned i) -> std::uint16_t {
            return std::uint16_t(act.work[i] | act.work[i + 1] << 8);
        };
        auto put = [&](unsigned i, std::uint16_t v) {
            act.work[i] = lo(v);
            act.work[i + 1] = hi(v);
        };
        put(0x08, std::uint16_t(word(0x08) - 2));
        put(0x10, std::uint16_t(word(0x10) + 2));
        return true;
    }
    case Routine::far_plane_start: // 1A:BABE: tiles 8E; at (C8, E5) going left by 1/16; the act's
                                   // boss
        o.tile_base = 0x8E;
        o.display |= 0x02;
        o.next_state = 0x01;
        o.x = std::uint16_t((o.x & 0xFF00) | 0xC8);
        o.y = std::uint16_t((o.y & 0xFF00) | 0xE5);
        o.vx = 0xFFF0;
        act.boss = u8(slot_number(o) + 1);
        return true;
    case Routine::scenery_start: // BBE7: tiles 30; state by parameter; 1 (the sender) kept off
                                 // screen
        o.tile_base = 0x30;
        o.next_state = o.param;
        if (o.next_state == 0x01) {
            o.display |= 0x02;
        } else {
            o.display &= u8(~0x02);
        }
        return true;
    case Routine::scenery_place: { // BBFF-BC63: from X E0 at its height and speed (by state)

        struct Place {
            u8 y;
            std::uint16_t vx;
        };

        static constexpr Place places[] = {{0x5C, 0xFFE0}, {0x5C, 0xFFE0}, {0x74, 0xFE70},
                                           {0x74, 0xFE70}, {0x68, 0xFED0}, {0x63, 0xFF10},
                                           {0x63, 0xFF10}, {0x68, 0xFEC0}, {0x72, 0xFEA0}};
        const Place& p = places[o.state - 2];
        o.x = std::uint16_t((o.x & 0xFF00) | 0xE0);
        o.y = std::uint16_t((o.y & 0xFF00) | p.y);
        o.vx = p.vx;
        return true;
    }
    case Routine::scenery_wrap: // BC10: left of X 20 back to E0
        if (lo(o.x) < 0x20) {
            o.x = std::uint16_t((o.x & 0xFF00) | 0xE0);
            return true;
        }
        move_horizontally_only(o); // 539A (0462)
        return true;
    case Routine::scenery_pass: // BC74: gone left of X 30
        if (lo(o.x) >= 0x30) {
            move_horizontally_only(o);
            return true;
        }
        o.type = 0xFF;
        return true;
    case Routine::scenery_stop: // BC81: from frame 2580 no more (BB7B)
        if (credits_frames() >= 0x2580) {
            jump_script(o, 0xBB7B);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::star_twinkle_start: // BD52: state by parameter; tiles 9E; blinking
        o.next_state = o.param;
        o.tile_base = 0x9E;
        o.display |= 0x02 | display_flag::flicker;
        return true;
    case Routine::star_twinkle_place: { // BD65-BDC8: its place by state
        static constexpr u8 places[][2] = {{0x60, 0x30}, {0x64, 0x40}, {0x61, 0x33}, {0x66, 0x50},
                                           {0x5C, 0x54}, {0x57, 0x52}, {0x88, 0x48}, {0x8C, 0x50},
                                           {0x81, 0x4D}, {0x84, 0x56}, {0xB0, 0x44}, {0x90, 0x50}};
        o.x = std::uint16_t((o.x & 0xFF00) | places[o.state - 1][0]);
        o.y = std::uint16_t((o.y & 0xFF00) | places[o.state - 1][1]);
        return true;
    }
    default:
        return false;
    }
}

// BC41, BC58, BC6F: from the step's frame on, its part of the script.
void Game::sky_wait(Object& o, std::uint16_t from, std::uint16_t script) {
    if (credits_frames() < from) {
        return;
    }
    jump_script(o, script);
    o.frame_ticks = 0x01;
}

// Palette 1's colours 1-14 (D4B5-D4CF) a step towards `targets`; the
// colours are uploaded at the next frame (D4F3).
void Game::step_colours(const std::uint16_t* targets) {
    for (unsigned i = 0; i < 14; ++i) {
        u8* c = &palette.colors[2 + 2 * i];
        const std::uint16_t v =
            step_color(std::uint16_t(c[0] | c[1] << 8), targets[i]); // 25DC (04DD)
        c[0] = lo(v);
        c[1] = hi(v);
    }
    palette.dirty = 0xFF;
}

// Colour 0 (D4B3) a step towards the first target, the horizon's first
// band the same (DC28); the other bands (DC2C, ... DC44) towards the rest.
void Game::step_horizon(const std::uint16_t* targets) {
    const std::uint16_t c0 =
        step_color(std::uint16_t(palette.colors[0] | palette.colors[1] << 8), targets[0]);
    palette.colors[0] = lo(c0);
    palette.colors[1] = hi(c0);
    act.work[bands_at + 2] = lo(c0);
    act.work[bands_at + 3] = hi(c0);
    for (unsigned i = 1; i < 8; ++i) {
        u8* c = &act.work[bands_at + 4 * i + 2];
        const std::uint16_t v = step_color(std::uint16_t(c[0] | c[1] << 8), targets[i]);
        c[0] = lo(v);
        c[1] = hi(v);
    }
}

// BD29
void Game::text_place(Object& o, u8 y) {
    o.x = 0;
    o.y = y;
}

// BD3A: at (0, A8); the line's letters as tiles (DC06), its X step, FF
// after it: the last (byte 1F); the layout (DBE6); the next line.
void Game::text_line(Object& o) {
    text_place(o, 0xA8);
    const Table line{0x1C, std::uint16_t(o.scratch[0] | o.scratch[1] << 8)};
    for (unsigned i = 0; i < 8; ++i) {
        act.work[text_tiles + i] = u8((rom.byte(line, i) - 0x41) * 2);
    }
    o.x = std::uint16_t(o.x + rom.byte(line, 8));
    if (rom.byte(line, 9) == 0xFF) {
        o.timer = 0xFF;
    }
    for (unsigned i = 0; i < 0x20; ++i) {
        act.work[i] = rom.byte(credits_layout, i);
    }
    ++o.counter;
    const std::uint16_t next = std::uint16_t(line.address + 9);
    o.scratch[0] = lo(next);
    o.scratch[1] = hi(next);
}

// BDB7: "?" (3E) and the six digits of the score (D14E first), tiles
// (digit + 1B) * 2, into DC06.
void Game::text_score() {
    act.work[text_tiles] = u8(0x3E * 2);
    for (unsigned i = 0; i < 3; ++i) {
        const u8 b = score.points[2 - i];
        act.work[text_tiles + 1 + 2 * i] = u8(((b >> 4) + 0x1B) * 2);
        act.work[text_tiles + 2 + 2 * i] = u8(((b & 0x0F) + 0x1B) * 2);
    }
}

} // namespace triple_trouble_gg
