// Sonic's flights: item 9 (player state 11, started by 3CFC) and item 0A
// (state 26); bank 0C, 8264-8310, 839E-83B7, 9D06-9E89. Hypothesis: the
// flying power-up and the jet board of Tidal Plant. For 300 frames
// (D534) Sonic is pushed forward the way he faces (in Atomic Destroyer act
// 3 always right), Up and Down steer him (speeds to 4, easing back when let
// go), the screen's top and bottom and the water's surface hold him. A
// button ends it (rolling), or the time runs out (script 829D: the act's
// song, falling). Not drowning meanwhile (D3CE).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
std::uint16_t jet_ticks(const Object& o) {
    return std::uint16_t(o.scratch[0] | o.scratch[1] << 8); // D534
}
} // namespace

bool Game::run_jet_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::power_flight_start: { // 82D4 (state 11): 300 frames (6000 in zone 5 act 3)
        const std::uint16_t t = (flow.zone == 5 && flow.act == 2) ? 0x1770 : 0x012C;
        o.scratch[0] = lo(t);
        o.scratch[1] = hi(t);
        o.scratch[2] = 0;
        return true;
    }
    case Routine::jet_start: // 83AD: 300 frames; no button yet (D536)
        o.scratch[0] = lo(0x012C);
        o.scratch[1] = hi(0x012C);
        o.scratch[2] = 0;
        return true;
    case Routine::jet_turn: // 8306: back to the flight (script 828D)
        jump_script(o, 0x828D);
        o.frame_ticks = 0x01;
        return true;
    case Routine::jet_tick: // 9D2B: (outside Atomic Destroyer act 3) untouchable meanwhile
        if (!(flow.zone == 5 && flow.act == 2)) {
            player.hit = 0;
            o.touch = 0;
            o.touch_sides = 0;
        }
        jet_count(o); // 9D43
        return true;
    case Routine::jet_button: // 9D0D: the first button: a turn (script 82BE)
        if ((input.pressed & 0x30) && !o.scratch[2]) {
            o.scratch[2] = 0xFF;
            jump_script(o, 0x82BE);
            o.frame_ticks = 0x01;
            return true;
        }
        jet_count(o);
        return true;
    default:
        return false;
    }
}

// 9D06: flying; a button ends it: no power-up, the act's song (21DC),
// rolling (3D6E).
Flow Game::jet_fly(Object& o) {
    player.air_count = 0;
    if (input.pressed & 0x30) {
        player.power_up_ticks = 0;
        o.power_up = 0;
        co_await play_act_music(); // 21DC (034E)
        to_rolling(o);             // 3D6E (0423)
        co_return;
    }
    jet_move(o); // 9D71
}

// 82F8: the act's song; the power-up ends at the next update.
Flow Game::jet_end(Object& o) {
    co_await play_act_music(); // 21DC (034E)
    o.power_up = 0;
    player.power_up_ticks = 0x0001;
}

// 9D43: a frame less; run out: the end (script 829D).
void Game::jet_count(Object& o) {
    const std::uint16_t left = std::uint16_t(jet_ticks(o) - 1);
    o.scratch[0] = lo(left);
    o.scratch[1] = hi(left);
    if (!left) {
        jump_script(o, 0x829D);
        o.frame_ticks = 0x01;
        return;
    }
    jet_move(o);
}

// 9D71: steered, held in, moved forward (the way he faces, the controller's
// direction forced: in zone 5 act 3 right only for this frame; elsewhere
// it stays forced); against a ceiling he is pushed down 2.
void Game::jet_move(Object& o) {
    o.display &= u8(~display_flag::hidden);
    jet_steer(o);  // 9DCD
    jet_limits(o); // 9E19
    if (flow.zone == 5 && flow.act == 2) {
        const u8 held = input.held;
        input.held = u8((held & ~0x04) | 0x08);
        player_move(o); // B05B
        input.held = held;
    } else {
        face_input(o); // B7B6
        input.held |= (o.display & display_flag::mirrored) ? 0x04 : 0x08;
        player_move(o);
    }
    if (o.contact & 0x02) {
        o.terrain &= u8(~0x02);
        o.y = std::uint16_t(o.y - 2);
        o.vy = 0;
    }
}

// 9DCD: Up: faster up by 1/4 (to 4); Down: down (to 4); neither: toward
// still by 1/8 (a still Sonic swings between 0 and -1/8).
void Game::jet_steer(Object& o) {
    if (input.held & 0x01) {
        o.vy = std::uint16_t(o.vy - 0x40);
        if ((o.vy & 0x8000) && hi(o.vy) < 0xFD) {
            o.vy = 0xFC00;
        }
        return;
    }
    if (input.held & 0x02) {
        o.vy = std::uint16_t(o.vy + 0x40);
        if (!(o.vy & 0x8000) && hi(o.vy) >= 0x03) {
            o.vy = 0x0400;
        }
        return;
    }
    o.vy = std::uint16_t(o.vy + ((o.vy & 0x8000) ? 0x0020 : 0xFFE0));
}

// 9E19: rising (in states 29, 48, 26) he stays 32 lines below the water's
// surface; (in state 11, falling, held at the surface); between screen
// lines 18 and A8 (Y on screen, byte 1C) or held at its edge.
void Game::jet_limits(Object& o) {
    std::uint16_t hl = o.vy; // as 9DCD leaves it
    const u8 n = o.next_state;
    if ((n == 0x29 || n == 0x48 || n == 0x26) && (o.vy & 0x8000)) {
        hl = std::uint16_t(o.y - 0x20);
        if (hl < act.water_level) {
            o.vy = 0; // 9E83
            return;
        }
        hl = std::uint16_t(hl - act.water_level);
    }
    if (o.state == 0x11 && !(o.vy & 0x8000) && hl && act.water_level && o.y >= act.water_level) {
        o.vy = 0;
        return;
    }
    // 9E61
    if (widescreen()) {
        // Widescreen: the presented view's top and bottom hold him.
        const int sy = int(std::int16_t(o.screen_y));
        if (sy < 0x18 - wide_top) {
            o.y = std::uint16_t(camera.y + 0x19 - wide_top);
            o.vy = 0;
        } else if (sy >= 0xA8 + wide_bottom) {
            o.y = std::uint16_t(camera.y + 0xA7 + wide_bottom);
            o.vy = 0;
        }
        return;
    }
    if (o.screen_y < 0x0018) {
        o.y = std::uint16_t(camera.y + 0x0019);
        o.vy = 0;
        return;
    }
    if (o.screen_y >= 0x00A8) {
        o.y = std::uint16_t(camera.y + 0x00A7);
        o.vy = 0;
    }
}

} // namespace triple_trouble_gg
