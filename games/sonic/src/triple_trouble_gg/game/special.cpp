// The special stages' emerald (type 4E, bank 17, 8989-8AD9). It glitters
// in the emerald's colours (table 8A37, six bytes per emerald won so far;
// in zone 5 the third set) at the stage's end; touched it is won (D17E),
// the camera turns to it, its song plays and the stage ends with the win
// (D144 bits 6 and 4). Parameter 1 is a falling emerald that lands at Y
// 264 and is collected with sound D5.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr Table emerald_colors{0x17, 0x8A37};
constexpr u8 emerald_song = 0xA0;
} // namespace

bool Game::run_special_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::stage_emerald_wait: // 8A5B: touched: an emerald more, the camera to it (8A70),
                                      // state 2
        touch_test(o);                // 58AC (0369)
        if (!(o.touch_sides & 0x0F)) {
            return true;
        }
        ++flow.emeralds;
        o.next_state = 0x02;
        script_camera(std::uint16_t(o.x - 0x80), std::uint16_t(o.y - 0x58)); // 4CDA (0321)
        return true;
    case Routine::stage_emerald_song: // 8A8B
        sound_.request(emerald_song, 2);
        return true;
    case Routine::stage_emerald_won: // 8A91: the stage ends, won
        flow.frame_work |= 0x50;
        return true;
    case Routine::stage_emerald_fall: // 8A99: falling (1/4) to Y 264, then waiting there (4)
        o.vy = std::uint16_t(o.vy + 0x40);
        move_object(o); // 5359 (0381)
        if (o.y >= 0x0264) {
            o.y = 0x0264;
            o.next_state = 0x04;
        }
        return true;
    case Routine::stage_emerald_collect: // 8AC7: touched: sound D5, gone
        touch_test(o);
        if (o.touch_sides & 0x0F) {
            sound_.request(0xD5, 1);
            o.type = 0xFF;
        }
        return true;
    default:
        return false;
    }
}

// 89DD: facing right, flickering, kept off screen; sprite colour 7 white
// (FF 0F), colours 13-15 the emerald's; its art loaded with frame waits:
// 09:BEBC at tile AC (79C0), or (parameter 1: state 3, tiles E8) the list
// 1A:B98F (7A2C).
Flow Game::stage_emerald_start(Object& o) {
    o.display = u8((o.display & ~display_flag::mirrored) | display_flag::flicker | 0x02);
    o.flags |= 0x80;
    palette.colors[0x2E] = 0xFF;
    palette.colors[0x2F] = 0x0F;
    const unsigned set = flow.zone == 5 ? 2u : flow.emeralds;
    for (unsigned i = 0; i < 6; ++i) {
        palette.colors[0x3A + i] = rom.byte(emerald_colors, set * 6u + i);
    }
    palette.dirty = 0xFF;
    o.next_state = 0x01;
    if (o.param != 0x01) {
        video.loading = 0xFF; // 79C0 (0501)
        co_await load_tiles_slowly(0x1580, 0x09, 0xBEBC);
        video.loading = 0;
        co_return;
    }
    o.next_state = 0x03;
    o.tile_base = 0xE8;
    co_await load_tile_list_slowly(Table{0x1A, 0xB98F}); // 7A2C (04DA)
}

} // namespace triple_trouble_gg
