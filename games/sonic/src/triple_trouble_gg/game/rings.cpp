// Ring objects (type 1D, bank 1E, 96D0-98AC), the rings a hit player
// scatters (type 1E, 9726-98AC) and the sparkle a taken ring leaves (type
// 03, bank 1E, 8000-805C).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 9722: the ring's frame for each phase of the HUD ring's spin (D3DD 1-4).
constexpr Table ring_frames{0x1E, 0x9722};
constexpr u8 keep_offscreen = 0x02; // display bit 1: not removed off screen (5456)
// 97CE / 97DC: a scattered ring's speeds by its number (0-6).
constexpr Table scatter_vx{0x1E, 0x97CE};
constexpr Table scatter_vy{0x1E, 0x97DC};
} // namespace

bool Game::run_ring_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::ring_start: // 96E4: centred in its 16-pixel cell
        o.tile_base = o.tile_base_mirrored = 0x10;
        o.next_state = 0x01;
        o.x = std::uint16_t(o.x + 8);
        return true;
    case Routine::ring_spin: // 970E: spinning with the HUD's ring
        o.frame = rom.byte(ring_frames, u8(animation.ring_tile - 1));
        o.frame_ticks = 0x02;
        return true;
    case Routine::ring_wait: // 9701
        if (!(o.display & display_flag::offscreen) && touches_player(o)) {
            take_ring(o); // 987E
        }
        return true;
    case Routine::ring_to_sparkle: // 988B
        ring_to_sparkle(o);
        return true;
    case Routine::scatter_start: // 975C
        scatter_start(o);
        return true;
    case Routine::scatter_catch: // 97EA: catchable (not while the player bounces off something)
        if (!(objects[0].flags & 0x40) && touches_player(o)) {
            take_ring(o); // 987E
            return true;
        }
        scatter_bounce(o);
        return true;
    case Routine::scatter_bounce: // 97F9
        scatter_bounce(o);
        return true;
    case Routine::sparkle_place: // 8020
        sparkle_place(o);
        return true;
    default:
        return false;
    }
}

// 975C: from 16 pixels above the player, thrown at its number's speeds
// (6 pixels per second more to the left with partner type 13), its bounce
// speed starting at 4 (byte 3C); untouchable at first (flags bits 6, 7).
void Game::scatter_start(Object& o) {
    o.tile_base = o.tile_base_mirrored = 0x10;
    const auto& p = objects[0];
    o.x = p.x;
    o.y = std::uint16_t(p.y - 0x10);
    o.vx = rom.word(scatter_vx, o.param * 2u);
    if (objects[1].type == 0x13) {
        o.vx = std::uint16_t(o.vx + 0xFEE0);
    }
    o.vy = rom.word(scatter_vy, o.param * 2u);
    o.origin_y = 0xFC00;
    o.next_state = 0x01;
    o.flags |= 0xC0;
    o.display |= 0x01;
}

// 97F9: falling (gravity 1/8); landing bounces it up a little less each
// time (the bounce speed rises by 1/2) until it is gone; rising into a
// solid block (16 pixels up; not blocks A4-A6) turns it down. Gone off
// screen (unless act variant), or when D3FD or DBD9 is set.
void Game::scatter_bounce(Object& o) {
    if (act.rings_gone || act.rings_gone_too) {
        o.type = 0xFF; // 9879
        return;
    }
    o.vy = std::uint16_t(o.vy + 0x20); // 551D (03AB)
    move_object(o);                    // 5359 (0381)
    if (!(o.vy & 0x8000)) {
        object_terrain(o); // 713D (0384)
        if (o.terrain & 0x02) {
            const unsigned bounce = o.origin_y + 0x80u;
            if (bounce > 0xFFFF) {
                o.type = 0xFF;
                return;
            }
            o.origin_y = std::uint16_t(bounce);
            o.vy = o.origin_y;
        }
    } else {
        probe_terrain(o, 0x0000, 0xFFF0); // 6FA6 (03FF)
        const u8 b = probe.block;
        if (b != 0xA4 && b != 0xA5 && b != 0xA6 && (probe.flags & 0x80)) {
            o.vy = std::uint16_t(-o.vy);
        }
    }
    // 986F
    if (act.variant) {
        return;
    }
    if (o.display & display_flag::offscreen) {
        o.type = 0xFF;
    }
}

// 53FD (0372): the player's centre within 12 pixels of the object's on
// both axes (543A).
bool Game::touches_player(const Object& o) const {
    const auto near = [](std::uint16_t a, std::uint16_t b) {
        const std::uint16_t d = std::uint16_t(a - b);
        return ((d & 0x8000) ? std::uint16_t(-d) : d) < 0x0C;
    };
    const auto& p = objects[0];
    return near(o.x, p.x) && near(o.y, p.y);
}

// 987E: sound C9, a ring counted (33AB through 0345), and the sparkle.
void Game::take_ring(Object& o) {
    sound_.request(0xC9, 1);
    add_ring();
    o.next_state = 0x02;
}

// 988B: the ring becomes a sparkle (type 03) where it is: a fresh script,
// no longer a placement.
void Game::ring_to_sparkle(Object& o) {
    o.type = 0x03;
    o.param = 0xFF;
    o.state = o.next_state = 0;
    o.display = 0;
    o.frame_ticks = 0;
    o.script = {};
    o.placement = 0;
}

// 8020: the sparkle; one from a map ring (parameter 0) appears at the
// ring's 16-pixel cell (D409, D40B) and stays when off screen.
void Game::sparkle_place(Object& o) {
    o.tile_base = o.tile_base_mirrored = 0;
    o.next_state = 0x01;
    if (o.param) {
        return;
    }
    o.x = std::uint16_t((probe.ring_x & 0xFF00) | u8((probe.ring_x & 0xF0) + 0x06));
    o.y = std::uint16_t(probe.ring_y & 0xFFF0);
    o.display |= keep_offscreen;
}

} // namespace triple_trouble_gg
