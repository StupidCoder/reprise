// The water's objects (type 24, bank 1E, A1A4-A3BD): the splash where the
// player enters the water, the bubbles the player breathes out, and the
// row of glints along the surface. The parameter picks the script (state
// = parameter + 1). And the air bubbles (type 3B, B99D-BB0B): a vent
// (parameter 0) sending up big bubbles the player can breathe from. And
// the drowning count over the player's head (type 11, 8B1F-8BEE: 5 down
// to 0, sound C0 each, blinking; gone once he breathes; at its end he
// drowns, state 28), and the splash of a sinking block (type 2F, AC06-ACB8; hypothesis: mud, in
// zone 2): parameter 0 throws four drops (1, 2) at the player's X.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// A2C2: the tile base by zone (zone 4 act 3: CC, zone 0 act 3: 54).
constexpr Table water_tile_bases{0x1E, 0xA2C2};
// A37B: the glints' X offsets from the camera (10 of them).
constexpr Table glint_offsets{0x1E, 0xA37B};
constexpr u8 keep_offscreen = 0x02; // display bit 1: not removed off screen (5456)
} // namespace

bool Game::run_water_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::water_start: // A27E
        water_start(o);
        return true;
    case Routine::water_at_surface: // A321
        o.y = act.water_level;
        return true;
    case Routine::water_below_surface: // A2B6
        o.y = std::uint16_t(act.water_level + 2);
        return true;
    case Routine::water_above_player: // A313
        o.y = std::uint16_t(objects[0].y - 0x10);
        return true;
    case Routine::bubble_rise:             // A2DD: rising faster and faster
        o.vy = std::uint16_t(o.vy - 0x0A); // 551D (03AB)
        bubble_drift(o);
        return true;
    case Routine::bubble_drift: // A2E3
        bubble_drift(o);
        return true;
    case Routine::bubble_end: // A30A
        o.type = 0xFF;
        o.param = 0;
        return true;
    case Routine::glint: // A357
        glint(o);
        return true;
    case Routine::breathe: // A32B
        breathe(o);
        return true;
    case Routine::breath_start: // A38F
        breath_start(o);
        return true;
    case Routine::count_start: // 8B5B: 34 above the player; tiles 2E; kept off screen
        o.x = objects[0].x;
        o.y = std::uint16_t(objects[0].y - 0x22);
        o.tile_base = o.tile_base_mirrored = 0x2E;
        o.next_state = 0x01;
        o.display |= 0x02;
        return true;
    case Routine::count_show: // 8B82: 40 above him, blinking (frame ticks bit 2); breathed (D3CE
                              // 0): gone
        o.x = objects[0].x;
        o.y = std::uint16_t(objects[0].y - 0x28);
        o.display &= u8(~display_flag::hidden);
        if (o.frame_ticks & 0x04) {
            o.display |= display_flag::hidden;
        }
        if (!player.air_count) {
            o.type = 0xFF;
        }
        return true;
    case Routine::count_drown: { // 8BB0: drowned (28): still, shown no more, the drowning song; the
                                 // camera stays
        auto& p = objects[0];
        if (p.state == 0x28 || p.state == 0x1F) {
            return true;
        }
        p.next_state = 0x28;
        p.flags |= 0x01;
        p.display = 0;
        p.vy = 0;
        player.input_force = 0;
        p.terrain &= u8(~0x02);
        sound_.request(0x8D, 2);
        camera.max_y = camera.y;
        o.type = 0xFF;
        return true;
    }
    case Routine::splash_start: // AC5A: at the player's X; tiles 96 (C2 in act 3)
        o.x = objects[0].x;
        o.display &= u8(~display_flag::mirrored);
        o.tile_base = flow.act == 2 ? 0xC2 : 0x96;
        if (!o.param) {
            o.next_state = 0x01;
            return true;
        }
        // AC81: a drop: up at 1, sideways at 0.625 the way the player faces.
        o.next_state = 0x02;
        o.vx = (objects[0].display & display_flag::mirrored) ? 0x00A0 : 0xFF60;
        o.vy = 0xFF00;
        return true;
    case Routine::splash_drop: // ACA1: falling (1/16)
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o); // 5359 (0381)
        return true;
    case Routine::splash_gone: // ACB4
        o.type = 0xFF;
        return true;
    case Routine::air_start: // BA04
        air_start(o);
        return true;
    case Routine::air_rise: // BACF: rising faster up to 3
        if (hi(o.vy) != 0xFD) {
            o.vy = std::uint16_t(o.vy - 0x02);
        }
        move_object(o);
        return true;
    case Routine::air_sway_right: // BAE8
        o.vx = 0x0050;
        return true;
    case Routine::air_sway_left: // BAF1
        o.vx = 0xFFB0;
        return true;
    case Routine::air_drift_left: // BAFA
        o.vx = 0xFFD0;
        return true;
    case Routine::air_drift_back: // BB03
        o.vx = 0xFF30;
        return true;
    case Routine::air_bubble: // BA77
        air_bubble(o);
        return true;
    default:
        return false;
    }
}

// A27E: the zone's tile base; the script by the parameter. Parameter 5,
// the glints, lie at the surface and stay when off screen.
void Game::water_start(Object& o) {
    u8 base = rom.byte(water_tile_bases, flow.zone);
    if (flow.zone == 4 && flow.act == 2) {
        base = 0xCC;
    }
    if (flow.zone == 0 && flow.act == 2) {
        base = 0x54; // A2C8
    }
    o.tile_base = o.tile_base_mirrored = base;
    o.next_state = u8(o.param + 1);
    if (o.next_state != 6) {
        return;
    }
    o.display |= keep_offscreen;
    o.y = std::uint16_t(act.water_level + 2);
}

// A2E3: moving; on odd frames (not with act variant 5) gone at the
// surface (16 pixels below it), or at once without water.
void Game::bubble_drift(Object& o) {
    move_object(o); // 5359 (0381)
    if (act.variant == 0x05) {
        return;
    }
    if (!(video.frame_counter & 1)) {
        return;
    }
    if (act.water_level && std::uint16_t(act.water_level + 0x10) < o.y) {
        return;
    }
    o.type = 0xFF; // A3B9
}

// A357: each update the next of the 10 glint positions across the screen.
void Game::glint(Object& o) {
    if (++o.timer >= 0x0A) {
        o.timer = 0;
    }
    const unsigned offset = rom.word(glint_offsets, o.timer * 2u);
    if (widescreen()) {
        // Widescreen: the same positions spread across the presented view
        // (the table's span 38-C8 lies across the LCD's 30-CF).
        const int width = 160 + wide_left + wide_right;
        o.x = std::uint16_t(camera.x + 0x30 - wide_left + (int(offset) - 0x30) * width / 160);
        return;
    }
    o.x = std::uint16_t(camera.x + offset);
}

// A32B: while the player is in state 26, 48 or 29 a breath (type 24,
// parameter 6) at its mouth, alternately 5 and 8 pixels below its centre.
void Game::breathe(Object& o) {
    const auto& p = objects[0];
    if (p.state != 0x26 && p.state != 0x48 && p.state != 0x29) {
        return;
    }
    ++o.counter;
    const std::uint16_t dy = (o.counter & 1) ? 0x0008 : 0x0005;
    spawn_object_at(o, 0x24, 0x06, p.x, std::uint16_t(p.y + dy)); // 510A (0357)
}

// BA04: the zone's tile base (76; act 3 BC), not noted by the player's
// touch; parameter 0 the vent (state 1), 1 a big bubble that can be
// breathed from (FF: state 41), 10 a big bubble from the screen's bottom
// (state 3); others a big bubble (state 2), rising at first at 1/2.
void Game::air_start(Object& o) {
    o.display &= u8(~display_flag::mirrored);
    o.tile_base = o.tile_base_mirrored = flow.act == 2 ? 0xBC : 0x76;
    o.flags |= 0x80;
    const u8 p = o.param;
    o.param = 0;
    if (p == 0x10) {
        o.next_state = 0x03;
        o.x = std::uint16_t(camera.x + 0x90);
        o.y = std::uint16_t(camera.y + 0xA0);
        return;
    }
    if (!p) {
        o.next_state = 0x01;
        return;
    }
    o.next_state = 0x02;
    o.vy = 0xFF80;
    if (p == 0x01) {
        o.param = 0xFF;
    }
}

// BA77: a big bubble bursts above the water (48 over the surface); touched
// by the player (not in states 29, 26, 48, 34, 40, 25) it is breathed in:
// state 25 (41 for one from parameter 1; D41B cleared).
void Game::air_bubble(Object& o) {
    if (std::uint16_t(o.y - 0x30) < act.water_level) {
        o.type = 0xFF;
        return;
    }
    const u8 s = objects[0].state;
    if (s == 0x29 || s == 0x26 || s == 0x48 || s == 0x34 || s == 0x40 || s == 0x25) {
        rise_air(o);
        return;
    }
    touch_test(o); // 58AC (0369)
    if (!(o.touch_sides & 0x0F)) {
        rise_air(o);
        return;
    }
    o.type = 0xFF;
    if (o.param == 0xFF) {
        objects[0].next_state = 0x41;
        return;
    }
    objects[0].next_state = 0x25;
    terrain_state.pipe_block = 0;
}

// BACF
void Game::rise_air(Object& o) {
    if (hi(o.vy) != 0xFD) {
        o.vy = std::uint16_t(o.vy - 0x02);
    }
    move_object(o);
}

// A38F: a breath drifts away from the player's face (1.5 pixels per frame
// against the way it faces); 12 higher in state 26.
void Game::breath_start(Object& o) {
    const auto& p = objects[0];
    o.vx = (p.display & display_flag::mirrored) ? 0x0180 : 0xFE80;
    if (p.state == 0x26) {
        o.y = std::uint16_t(o.y - 0x0C);
    }
}

} // namespace triple_trouble_gg
