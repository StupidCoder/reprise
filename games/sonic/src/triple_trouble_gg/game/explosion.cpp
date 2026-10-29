// Explosions (type 07, bank 1E, 848B-8730): what destroyed objects and
// broken blocks become; also flying debris (parameters 10 on) and the
// rising puff of parameter 6. Parameter bit 7 makes one that hurts the
// player. And the pieces of a broken wall (type 0A, 877D-884B) and
// Robotnik Winter's flakes (type 31, AEEB-AFCF), and the crumbling floor
// block of kind 0C (type 9, 8675-877C) with its falling pieces.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 85A4: the debris' speeds (X, Y) by parameter - 10.
constexpr Table debris_speeds{0x1E, 0x85A4};
// 884C: the wall pieces' tile base by zone (zone 0 act 3: 34).
constexpr Table piece_tile_bases{0x1E, 0x884C};
} // namespace

bool Game::run_explosion_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::crumble_start: { // 86BB (type 9): sound B2; the zone's tiles (34 in zone 0 act 3)
        sound_.request(0xB2, 1);
        u8 base = rom.byte(piece_tile_bases, flow.zone); // 8827
        if (flow.zone == 0 && flow.act == 2) {
            base = 0x34;
        }
        o.tile_base = o.tile_base_mirrored = base;
        if (o.param) {
            o.next_state = 0x02; // a piece
            return true;
        }
        o.next_state = 0x01;
        o.counter = 0x99;
        o.display |= 0x02;
        return true;
    }
    case Routine::crumble_snap: // 8706: at its 32-pixel cell (bytes 34-37)
        snap_to_cell(o);
        return true;
    case Routine::crumble_step: // 8731: the block's next stage (99-9C; off screen or left of the
                                // camera at once 9C)
        if ((o.display & display_flag::offscreen) ||
            o.x < std::uint16_t(camera.x - wide_left)) { // Widescreen: left of the view
            o.counter = 0x9C;
        }
        probe.block_x = std::uint16_t(o.scratch[0] | o.scratch[1] << 8);
        probe.block_y = std::uint16_t(o.scratch[2] | o.scratch[3] << 8);
        probe.cell = std::uint16_t((o.scratch[4] | o.scratch[5] << 8) - 0xC000);
        set_map_block_at(probe.cell, o.counter, probe.block_x, probe.block_y); // 6580 (0348)
        if (++o.counter == 0x9D) {
            o.type = 0xFF;
        }
        return true;
    case Routine::crumble_piece: // 86DB: a piece: off screen (or in a boss act) gone; after its
                                 // delay (parameter) falling (1/2)
        if ((o.display & display_flag::offscreen) || act.variant) {
            o.type = 0xFF;
            return true;
        }
        if (o.param) {
            --o.param;
            return true;
        }
        o.vy = std::uint16_t(o.vy + 0x80);
        move_object(o); // 5359 (0381)
        return true;
    case Routine::explosion_start: // 853C
        explosion_start(o);
        return true;
    case Routine::explosion_toss: // 85C4: up and back a little
        o.vx = 0xFDC0;
        o.vy = 0xFFE0;
        return true;
    case Routine::explosion_burn: // 85D5
        explosion_burn(o);
        return true;
    case Routine::explosion_end: // 8618
        explosion_end(o);
        return true;
    case Routine::debris_fall:             // 866C
        o.vy = std::uint16_t(o.vy + 0x14); // 551D (03AB)
        move_object(o);
        return true;
    case Routine::puff_rise: // 8653: rising faster up to F6.xx
        if (hi(o.vy) != 0xF6) {
            o.vy = std::uint16_t(o.vy - 0x20);
        }
        move_object(o);
        return true;
    case Routine::piece_start: // 8800: from where the breaker is, the parameter's direction
        piece_start(o);
        return true;
    case Routine::piece_fly: // 8857: gone off screen (or with objects running plainly)
        if ((o.display & display_flag::offscreen) || act.object_mode) {
            o.type = 0xFF;
        } else {
            move_object(o);
        }
        return true;
    case Routine::flake_start: // AF99 (type 31)
        o.tile_base = o.tile_base_mirrored = 0x64;
        if (o.param == 0xFD) {
            o.next_state = 0x02; // rising
            o.param = 0;
        } else if (o.param == 0x01) {
            o.next_state = 0x03; // falling and swaying
        } else {
            o.next_state = 0x01; // the spawner
            o.display |= 0x02;
        }
        return true;
    case Routine::flake_anchor: // AFC3: the act's partner object (slot 1) at the camera
        objects[1].x = camera.x;
        objects[1].y = camera.y;
        return true;
    case Routine::explosion_restore_block: // 85F2: the crumbled block (92) back where it was
        if (o.display & display_flag::offscreen) {
            explosion_end(o);
            return true;
        }
        probe.block_x = std::uint16_t(o.scratch[0] | o.scratch[1] << 8);
        probe.block_y = std::uint16_t(o.scratch[2] | o.scratch[3] << 8);
        probe.cell = std::uint16_t((o.scratch[4] | o.scratch[5] << 8) - 0xC000);
        set_map_block_at(probe.cell, 0x92, probe.block_x, probe.block_y); // 6580 (0348), 2D17
        explosion_end(o);
        return true;
    default:
        return false;
    }
}

// 853C: parameter 6 the puff (state 6, tile base A2); otherwise tile base
// 24, facing right; bit 7 (cleared) makes it hurt (counter FF); 4 a chain
// of explosions at its 32-pixel cell (state 4); 10 on debris (state 5);
// others state 1.
void Game::explosion_start(Object& o) {
    if (o.param == 0x06) {
        o.next_state = 0x06;
        o.tile_base = o.tile_base_mirrored = 0xA2;
        return;
    }
    o.display &= u8(~display_flag::mirrored);
    o.tile_base = o.tile_base_mirrored = 0x24;
    if (o.param & 0x80) {
        o.param &= 0x7F;
        o.counter = 0xFF;
    }
    if (o.param == 0x04) {
        // A crumbled block's chain (state 4), remembering its cell.
        o.next_state = 0x04;
        snap_to_cell(o); // 8706
        return;
    }
    if (o.param < 0x10) {
        o.next_state = 0x01;
        return;
    }
    const unsigned i = (o.param - 0x10u) * 4;
    o.vx = rom.word(debris_speeds, i);
    o.vy = rom.word(debris_speeds, i + 2);
    o.next_state = 0x05;
}

// 8706: at the 32-pixel cell it is in (Y at the cell's middle line 16),
// remembered (bytes 34-37) for drawing the block there.
void Game::snap_to_cell(Object& o) {
    o.x = std::uint16_t((o.x & 0xFF00) | (o.x & 0xE0));
    o.y = std::uint16_t((o.y & 0xFF00) | ((o.y & 0xE0) | 0x10));
    o.scratch[0] = lo(o.x);
    o.scratch[1] = hi(o.x);
    o.scratch[2] = lo(o.y);
    o.scratch[3] = hi(o.y);
}

// 8800
void Game::piece_start(Object& o) {
    o.next_state = o.param;
    const auto& breaker = objects[act.breaker - 1u]; // DBC4
    o.x = breaker.x;
    o.y = breaker.y;
    u8 base = rom.byte(piece_tile_bases, flow.zone);
    if (flow.zone == 0 && flow.act == 2) {
        base = 0x34;
    }
    o.tile_base = o.tile_base_mirrored = base;
}

// 85D5: with a line effect running it drifts; one that hurts is gone off
// screen and, on odd frame ticks, hits a player within 16 pixels (863D).
void Game::explosion_burn(Object& o) {
    if (act.line_effect) {
        move_object(o); // 5359 (0381)
        return;
    }
    if (!o.counter) {
        return;
    }
    if (o.display & display_flag::offscreen) {
        explosion_end(o);
        return;
    }
    if (!(o.frame_ticks & 1)) {
        return;
    }
    const auto near = [](std::uint16_t a, std::uint16_t b) { // 543A
        const std::uint16_t d = std::uint16_t(a - b);
        return ((d & 0x8000) ? std::uint16_t(-d) : d) < 0x10;
    };
    const auto& p = objects[0];
    if (near(o.x, p.x) && near(o.y, p.y)) { // 5424, 5430
        player.hit = 0xFF;
    }
}

// 8618: gone: parameter 0 frees its placement for good (type FE),
// parameter 2 likewise but at once (FF); others FE.
void Game::explosion_end(Object& o) {
    if (o.param == 0x00) {
        o.placement = 0;
        o.type = 0xFE;
    } else if (o.param == 0x02) {
        o.placement = 0;
        o.type = 0xFF;
    } else {
        o.type = 0xFE;
    }
}

} // namespace triple_trouble_gg
