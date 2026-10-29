// Meta Junglira's barrels (blocks of kind 18, 6054): a player landing on
// one near its middle (8-17 pixels into its 32) gets state 32 (Sonic's
// script 8691, Tails' 93B1, bank 0C). He is untouchable (byte 03 bit 6),
// slides a pixel per frame to the middle, sinks in at 2 for 10 frames,
// sits 27 pixels below where he landed, creaks (sound BA) for 20 more,
// then is fired out at 9 (sound C4) with six sparks (type 39, parameters
// 2-4: up at 8, 4 and 2, falling at 1/32) and leaves rolling (3D99).
// Observed in the original: trace rp_barrel (a test setup over the first
// barrels) and the random player's run through act 1.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// The scripts' part after the slide (Sonic, Tails).
constexpr unsigned sonic_sink = 0x86A9, tails_sink = 0x93C3;
} // namespace

bool Game::run_barrel_routine(Object& o, Routine r) {
    auto& p = objects[0];
    switch (r) {
    case Routine::barrel_enter: // A667: untouchable; the landing height kept (D53C)
        o.flags |= 0x40;
        p.origin_y = p.y;
        return true;
    case Routine::barrel_center: // A672
        barrel_center(o);
        return true;
    case Routine::barrel_settle: // A6C3: 27 below the landing height
        p.y = std::uint16_t(p.origin_y + 0x1B);
        return true;
    case Routine::barrel_creak: // A6A2
        sound_.request(0xBA, 1);
        return true;
    case Routine::barrel_fire: // A6A8
        sound_.request(0xC4, 1);
        return true;
    case Routine::barrel_out: // 8740 / 945A
        o.flags &= u8(~0x40);
        to_bounced_roll(o); // 3D99 (03BD)
        return true;
    case Routine::spark_fall: // 1E:B884: gravity 1/32
        o.vy = std::uint16_t(o.vy + 0x08);
        move_object(o); // 5359 (0381)
        return true;
    default:
        return false;
    }
}

// A672: a pixel a frame toward the barrel's middle (16 into its 32; the
// low byte of X only, which never carries there), then the sinking part.
void Game::barrel_center(Object& o) {
    const u8 x = o.x & 0x1F;
    if (x == 0x10) {
        jump_script(o, o.type == 0x01 ? sonic_sink : tails_sink);
        o.frame_ticks = 0x01;
        return;
    }
    o.x = std::uint16_t((o.x & 0xFF00) | u8(lo(o.x) + (x < 0x10 ? 1 : -1)));
}

} // namespace triple_trouble_gg
