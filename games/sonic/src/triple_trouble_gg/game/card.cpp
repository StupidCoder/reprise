// The title card's object (type 14, bank 1E, 91AE-9244): the act's name
// (frame act + 1) slides in from the right, overshoots the stop at X B8,
// is pushed back and settles.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

bool Game::run_card_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::card_start: // 91D2: at X 100, Y 67 (low byte), left at 4
        o.next_state = 0x01;
        o.tile_base = o.tile_base_mirrored = 0x10;
        o.x = 0x0100;
        o.y = std::uint16_t((o.y & 0xFF00) | 0x67);
        o.vx = 0xFC00;
        o.counter = 0;
        return true;
    case Routine::card_frame: // 91F7
        o.frame = u8(flow.act + 1);
        o.frame_ticks = 0x20;
        return true;
    case Routine::card_slide: // 9203
        card_slide(o);
        return true;
    default:
        return false;
    }
}

// 9203: after the push back (counter set) the speed drops by 1/4 pixel per
// frame. Moving left past X B8: the first time pushed back right at 3,
// the second time it stops (state 2).
void Game::card_slide(Object& o) {
    if (o.counter) {
        o.vx = std::uint16_t(o.vx - 0x40);
    }
    move_object(o); // 5359 (0381)
    if (!(o.vx & 0x8000) || o.x >= 0x00B8) {
        return;
    }
    if (!o.counter) {
        o.vx = 0x0300;
        o.counter = 0xFF;
        return;
    }
    ++o.next_state;
}

} // namespace triple_trouble_gg
