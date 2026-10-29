// The crawler (type 3E, bank 1E, BBD0-BF33): a head (parameter 0) swaying
// left and right with three segments (parameters 1-3) trailing behind it:
// the head's X history (DBE6, newest first) places them at 15, 31 and 46
// frames back, each bobbing by its own counter in the head. A rolling
// player hits the head (or a segment whose turn it is) and the whole
// crawler flies apart.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

// DBE6: the trail, 48 words.
std::uint16_t Game::trail_x(unsigned i) const {
    return std::uint16_t(act.work[2 * i] | act.work[2 * i + 1] << 8);
}

void Game::set_trail_x(unsigned i, std::uint16_t x) {
    act.work[2 * i] = lo(x);
    act.work[2 * i + 1] = hi(x);
}

// The head's own bytes: scratch[0-2] (34-36) the segments' bob (0-8),
// counter (1E) bits 0-2 their directions, timer (1F) the frames (its low 2
// bits say which segment may be touched), scratch[4] (38) hit, scratch[5]
// (39) beaten; status_ticks (26) FF: the faster variant (state 6).
// A segment's scratch[0-1] (34-35) hold the head's address.

bool Game::run_crawler_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::crawler_start: // BCA5
        crawler_start(o);
        return true;
    case Routine::crawler_link: // BCF5: the segment just spawned (IY) notes the head
        if (script_spawned_) {
            const std::uint16_t a = object_address(o);
            script_spawned_->scratch[0] = lo(a);
            script_spawned_->scratch[1] = hi(a);
        }
        return true;
    case Routine::crawler_variant: // BC47
        if (o.status_ticks) {
            o.next_state = 0x06;
        }
        return true;
    case Routine::crawler_home: // BD05
        o.x = o.origin_x;
        return true;
    case Routine::crawler_down: // BD12
        o.vy = 0x0040;
        return true;
    case Routine::crawler_up: // BD1B
        o.vy = 0xFFC0;
        return true;
    case Routine::crawler_left: // BD24: speeding up left to 2
        if (hi(o.vx) != 0xFE) {
            o.vx = std::uint16_t(o.vx - 0x10);
        }
        crawler_head(o);
        return true;
    case Routine::crawler_right: // BD35: and right to 1
        if (hi(o.vx) != 0x01) {
            o.vx = std::uint16_t(o.vx + 0x10);
        }
        crawler_head(o);
        return true;
    case Routine::crawler_segment: // BE29
        crawler_segment(o, false);
        return true;
    case Routine::crawler_sting: // BE11: the segment's sting hurts
        crawler_segment(o, true);
        return true;
    case Routine::crawler_head_off: // BEAD: the head flies off left at 3
        o.display &= u8(~0x02);
        o.placement = 0;
        o.vx = 0xFD00;
        o.vy = 0;
        return true;
    case Routine::crawler_segment_off: // BEC6: down, up or right at 3 by its number
        o.display &= u8(~0x02);
        o.vx = o.param == 3 ? 0x0300 : 0;
        o.vy = o.param == 1 ? 0x0300 : o.param == 2 ? 0xFD00 : 0;
        return true;
    case Routine::crawler_hold: // BF06
        o.frame_ticks = 0xE0;
        return true;
    default:
        return false;
    }
}

// BCA5: kept when off screen; parameter FF is the faster head. The head
// (state 1) fills the trail with its X; a segment follows (state 3).
void Game::crawler_start(Object& o) {
    o.status_ticks = 0;
    o.display |= 0x02;
    if (o.param == 0xFF) {
        o.param = 0;
        o.status_ticks = 0xFF;
    }
    if (o.param) {
        o.next_state = 0x03;
        return;
    }
    o.next_state = 0x01;
    o.counter = 0;
    o.scratch[0] = 0x00;
    o.scratch[1] = 0x04;
    o.scratch[2] = 0x08;
    o.scratch[4] = 0;
    o.scratch[5] = 0;
    for (unsigned i = 0; i < trail_length; ++i) {
        set_trail_x(i, o.x);
    }
}

// BD4A: moving; hit by its segments' report or touched by a rolling
// player (or one with power-up 5) it is beaten (BE03); on odd frames the
// segments' bobs step (each 0-8 and back); the trail takes its X; gone
// when far from the player (BF0B).
void Game::crawler_head(Object& o) {
    move_object(o); // 5359 (0381)
    touch_test(o);  // 58AC (0369)
    const bool beaten =
        o.scratch[4] ||
        ((o.touch_sides & 0x0F) && (objects[0].power_up == 0x05 || (objects[0].flags & 0x02)));
    if (beaten) {
        sound_.request(0xCE, 1); // BE03
        o.next_state = 0x04;
        o.scratch[5] = 0x01;
        return;
    }
    ++o.timer;
    if (video.frame_counter & 1) {
        for (unsigned k = 0; k < 3; ++k) {
            u8& v = o.scratch[k];
            const u8 bit = u8(1 << k);
            if (!(o.counter & bit)) {
                if (v < 0x08) {
                    ++v;
                } else {
                    o.counter |= bit;
                    --v;
                }
            } else if (v) {
                --v;
            } else {
                o.counter &= u8(~bit);
                ++v;
            }
        }
    }
    // BDEC: the trail moves on a place (94 bytes moved two up: the last
    // word's high byte, DC45, keeps the X it was filled with); the newest is
    // the head's X.
    for (unsigned j = 2 * trail_length - 2; j >= 2; --j) {
        act.work[j] = act.work[j - 2];
    }
    set_trail_x(0, o.x);
    crawler_range(o); // BF0B
}

// BE29 / BE11: a segment: when the head's frame count picks it, a
// rolling player's touch (or power-up 5) beats the head (BE29), or its
// sting hurts (BE11, 550E); a beaten head breaks it off (state 5). Its
// place: the trail's X 15, 31 or 46 frames back, the head's Y plus its bob
// minus 4.
void Game::crawler_segment(Object& o, bool sting) {
    Object& head = object_at(std::uint16_t(o.scratch[0] | o.scratch[1] << 8));
    if ((head.timer & 0x03) == o.param) {
        if (sting) {
            harmful_touch(o); // 550E (0375)
        } else {
            touch_test(o); // 58AC (0369)
            if ((o.touch_sides & 0x0F) &&
                (objects[0].power_up == 0x05 || (objects[0].flags & 0x02))) {
                head.scratch[4] = 0x01;
            }
        }
    }
    if (head.scratch[5]) { // BEA3
        o.next_state = 0x05;
    }
    const u8 bob = head.scratch[o.param - 1];
    o.y = std::uint16_t(head.y + std::uint16_t(std::int16_t(std::int8_t(u8(bob - 4)))));
    const unsigned back = o.param == 1 ? 0x0F : o.param == 2 ? 0x1F : 0x2E;
    o.x = trail_x(back);
    crawler_range(o); // BF0B
}

// BF0B: more than 256 lines or 464 pixels from the player it is gone
// (type FE when from a placement, else FF).
void Game::crawler_range(Object& o) {
    const auto near = [](std::uint16_t a, std::uint16_t b, unsigned range) { // 543A
        const std::uint16_t d = std::uint16_t(a - b);
        return ((d & 0x8000) ? std::uint16_t(-d) : d) < range;
    };
    const auto& p = objects[0];
    if (near(o.y, p.y, 0x0100) && near(o.x, p.x, 0x01D0)) {
        return;
    }
    o.type = o.placement ? 0xFE : 0xFF;
    o.state = 0;
}

} // namespace triple_trouble_gg
