// Springs (type 21, bank 17, A0B1-A291): spring objects, and the
// animation of a spring in the map (parameters FF, FE: 642A). A spring
// launches a player who lands on it (states 2: weak, 3: strong). The side
// spring (type 4A, bank 17, 87F7-884B) throws a player who runs into it
// back the other way at 4 (3DFF / 3E0C).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// A186: the tile base by zone (zone 0 act 3: 66, A191).
constexpr Table spring_tile_bases{0x17, 0xA186};
} // namespace

// The spring's own bytes.
//   scratch[0] (34): the state a landing player triggers (2 weak, 3 strong)
//   scratch[1] (35): half its width
//   scratch[2-3] (36): the launch speed
//   scratch[4] (38): nonzero: shown compressed while waiting
//   scratch[5] (39): nonzero: it follows the player's X when launching

bool Game::run_spring_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::side_spring_start: // 17:881B
        o.next_state = 0x01;
        o.flags |= 0x80;
        return true;
    case Routine::side_spring_wait: { // 8824: (not for a player with flags bit 5) solid; touched on
                                      // a side: bounced (state 2)
        if (objects[0].flags & 0x20) {
            return true;
        }
        solid_object(o); // 51FF (036C)
        const u8 t = o.touch_sides;
        if (t & 0x01) {
            destroy_if_attacked(o); // 51B8 (0363)
            return true;
        }
        if (!(t & 0x0C)) {
            return true;
        }
        o.next_state = 0x02;
        wall_spring(o, (t & 0x04) ? 0x0400 : 0xFC00); // 3DFF (04A7) / 3E0C (04A4)
        return true;
    }
    case Routine::spring_start: // A11F
        spring_start(o);
        return true;
    case Routine::spring_home: // A234
        o.x = o.origin_x;
        return true;
    case Routine::spring_frame: // A241
        o.frame_ticks = 0xE0;
        o.frame = !o.scratch[4] ? 0x00 : o.scratch[0] == 0x02 ? 0x02 : 0x01;
        return true;
    case Routine::spring_wait: // A1F7
        spring_wait(o);
        return true;
    case Routine::spring_follow: // A261
        if (o.scratch[5]) {
            o.x = objects[0].x;
        }
        return true;
    case Routine::spring_launch_weak:   // A270
    case Routine::spring_launch_strong: // A274
        player.spring_flag = r == Routine::spring_launch_weak ? 0x00 : 0xFF;
        spring_player(o, std::uint16_t(o.scratch[2] | o.scratch[3] << 8)); // 5186 (03B1)
        return true;
    case Routine::spring_snap: // A287: the map spring's block centre
        o.x = std::uint16_t((o.x & 0xFF00) | (o.x & 0xE0) | 0x12);
        return true;
    default:
        return false;
    }
}

// A11F: the zone's tile base; a map spring's animation (FF: state 4, FE:
// 5); otherwise a spring (state 1): bit 7 of the parameter strong (state
// 3, F9.00) or weak (state 2, FC.80); the rest picks its size and kind
// (table A1A6).
void Game::spring_start(Object& o) {
    o.next_state = 0x01;
    o.scratch[4] = o.scratch[5] = 0;
    u8 base = rom.byte(spring_tile_bases, flow.zone);
    if (flow.zone == 0 && flow.act == 2) {
        base = 0x66; // A191
    }
    o.tile_base = o.tile_base_mirrored = base;
    if (o.param == 0xFF) {
        o.next_state = 0x04;
        return;
    }
    if (o.param == 0xFE) {
        o.next_state = 0x05;
        return;
    }
    std::uint16_t speed = 0xFC80;
    o.scratch[0] = 0x02;
    if (o.param & 0x80) {
        o.scratch[0] = 0x03;
        speed = 0xF900;
    }
    o.param &= 0x7F;
    o.scratch[1] = 0x10;
    switch (o.param) { // A1A6
    case 0:
        break;
    case 1:
        o.scratch[5] = 0xFF;
        break;
    case 2:
        o.scratch[1] = 0x20;
        o.scratch[5] = 0xFF;
        break;
    case 3:
        o.scratch[1] = 0x40;
        o.scratch[5] = 0xFF;
        break;
    case 4:
        o.scratch[4] = 0xFF;
        break;
    case 5:
        speed = 0xFA00;
        break;
    case 6:
        speed = 0xFC00;
        break;
    default:
        throw Unrecovered("spring kind past table A1A6");
    }
    o.scratch[2] = lo(speed);
    o.scratch[3] = hi(speed);
}

// A1F7: on odd frames, a falling player (not in states 11, 43, without
// status bit 5 and flags bit 6) within 16 pixels vertically and its half
// width horizontally sets it off.
void Game::spring_wait(Object& o) {
    const auto& p = objects[0];
    if (p.state == 0x11 || p.state == 0x43) {
        return;
    }
    if ((p.status & 0x20) || (o.display & display_flag::offscreen) || (p.flags & 0x40)) {
        return;
    }
    if (p.vy & 0x8000) {
        return;
    }
    if (!(video.frame_counter & 1)) {
        return;
    }
    const auto near = [](std::uint16_t a, std::uint16_t b, unsigned range) { // 543A
        const std::uint16_t d = std::uint16_t(a - b);
        return ((d & 0x8000) ? std::uint16_t(-d) : d) < range;
    };
    if (!near(o.y, p.y, 0x10) || !near(o.x, p.x, o.scratch[1])) { // 5430, 5424
        return;
    }
    o.next_state = o.scratch[0];
}

} // namespace triple_trouble_gg
