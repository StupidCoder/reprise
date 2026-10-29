// Atomic Destroyer act 3's end scene (bank 17, AD09-B073), started by the
// beaten last machine (type 66, state 19).
//
// Type 57, the craft: it rises from the floor of the last room (AC0, 2AE)
// to 24E, freeing the camera, and hops off to the right along the floor,
// keeping the player 48 to its left (past X AC5 he is in state 46); from X
// C7E it hops once more, lands (its block becomes 94, sound C5), drops a
// part (type 59) from above, jumps and leaves a capsule (58) where it
// stood.
// Type 58: opens (an emerald of type 4E and eight pieces of type 07) and
// marks the scene done (DC49).
// Type 59: falls in to Y 248, ends the act's boss variant (D3E4) and,
// after a while, falls away.
// Type 61 (bank 1C, B7C8-B8D7; placed at 1050, 230): what the player
// frees: its art (1A:B989, frame waits), palette 1B; when he lands back
// (DC47) it bursts (eight pieces, sound C1), falls to 24E, after about
// eight seconds lets him go (DC48) and hops off to the right.
// The player (state 46, bank 0C A9C4-AC6D; Tails' scripts at 96xx): held
// facing right until he lands, then he walks right (to 3) to X C88, hops,
// is thrown back (sound C1) onto the floor (26E), strolls to CD0, jumps
// (sound A1) onto the craft's level (24E), runs (the work byte DBE6 FF)
// to 1020, leaps back past 1040, lands (DC47 FF, DC48 0), walks back to
// 103A, waits for DC48, runs off to the right past 10D0 (the camera
// stops) and falls; below Y 29E the act ends (D144 bit 4).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 7D1B requests: the craft's, the capsule's and the part's art.
constexpr u8 craft_art = 0x1A, capsule_art = 0x1B, part_art = 0x1C;

// The player's script positions: Sonic's, Tails'.
struct Beat {
    std::uint16_t sonic, tails;
};

constexpr Beat beat_walking{0x8CA7, 0x9664}, beat_hopping{0x8CB1, 0x966E},
    beat_thrown{0x8CCF, 0x9689};
constexpr Beat beat_landed{0x8CE6, 0x969A}, beat_jumping{0x8CFA, 0x96AE},
    beat_running{0x8D17, 0x96C5};
constexpr Beat beat_leaping{0x8D29, 0x96D7}, beat_resting{0x8D43, 0x96EB},
    beat_walking_back{0x8D4B, 0x96F3};
constexpr Beat beat_waiting{0x8D59, 0x9701}, beat_running_off{0x8D68, 0x9711},
    beat_falling{0x8D7C, 0x9725};
} // namespace

// The next beat of the player's end scene, from the next update.
void Game::ending_beat(Object& p, std::uint16_t sonic, std::uint16_t tails) {
    jump_script(p, p.type == 0x01 ? sonic : tails);
    p.frame_ticks = 0x01;
}

bool Game::run_ending_player_routine(Object& p, Routine r) {
    switch (r) {
    case Routine::ending_held: // A9C4: facing right; landed: walking (1, falling 2)
        ending_held(p);
        return true;
    case Routine::ending_fall: // A9EE
        p.vy = std::uint16_t(p.vy + 0x20);
        move_object(p);
        object_terrain(p);
        ending_held(p);
        return true;
    case Routine::ending_walk: // AA00: up to 3 by 1/16; at X C88 a hop
        if (p.vx <= 0x0300) {
            p.vx = std::uint16_t(p.vx + 0x10);
        }
        move_object(p);
        object_terrain(p);
        if (p.x >= 0x0C88) {
            ending_beat(p, beat_hopping.sonic, beat_hopping.tails);
        }
        return true;
    case Routine::ending_hop: // AA39: slowing by 1/32, falling (1/16); coming down past Y 212:
                              // thrown
        if (p.vx) {
            p.vx = std::uint16_t(p.vx - 0x08);
        }
        p.vy = std::uint16_t(p.vy + 0x10);
        move_object(p);
        if (!(p.vy & 0x8000) && p.y > 0x0212) {
            ending_beat(p, beat_thrown.sonic, beat_thrown.tails);
        }
        return true;
    case Routine::ending_thrown: // AA79: back at 2, falling to the floor (26E)
        p.vx = 0xFE00;
        p.vy = std::uint16_t(p.vy + 0x10);
        move_object(p);
        if (!(p.vy & 0x8000) && p.y > 0x026E) {
            p.y = 0x026E;
            ending_beat(p, beat_landed.sonic, beat_landed.tails);
        }
        return true;
    case Routine::ending_stroll: // AAB7: right at 1 to X CD0
        p.vx = 0x0100;
        p.vy = 0;
        move_object(p);
        if (p.x > 0x0CD0) {
            ending_beat(p, beat_jumping.sonic, beat_jumping.tails);
        }
        return true;
    case Routine::ending_jump: // AAE6: falling (1/16) onto Y 24E
        p.vy = std::uint16_t(p.vy + 0x10);
        move_object(p);
        if (!(p.vy & 0x8000) && p.y > 0x024E) {
            p.y = 0x024E;
            ending_beat(p, beat_running.sonic, beat_running.tails);
        }
        return true;
    case Routine::ending_run_start: // AB41: DBE6 FF; right at 2; the camera aimed as usual
        act.work[0] = 0xFF;
        p.vy = 0;
        p.vx = 0x0200;
        aim_camera_at_player(); // 98CE
        return true;
    case Routine::ending_run: // AB1E: to X 1020, then a leap
        move_object(p);
        if (p.x > 0x1020) {
            ending_beat(p, beat_leaping.sonic, beat_leaping.tails);
        }
        return true;
    case Routine::ending_leap: // AB56
        if (p.x >= 0x1040) {
            p.vx = 0xFE80;
        }
        p.vy = std::uint16_t(p.vy + 0x10);
        move_object(p);
        if (p.vy & 0x8000) {
            return true;
        }
        if (p.y >= 0x024E) {
            ending_beat(p, beat_resting.sonic, beat_resting.tails);
            act.scene[scene_byte::landed] = 0xFF;
            act.scene[scene_byte::go] = 0;
            return true;
        }
        if (!p.scratch[0]) {
            p.vx = 0xFF80;
        }
        return true;
    case Routine::ending_walk_back: // ABAE: right at 1 to X 103A (bytes 3A-3B)
        ending_beat(p, beat_walking_back.sonic, beat_walking_back.tails);
        p.origin_x = 0x103A;
        p.vy = 0;
        p.vx = 0x0100;
        return true;
    case Routine::ending_walk_to: // ABDF
        move_object(p);
        if (p.x < p.origin_x) {
            return true;
        }
        p.x = p.origin_x;
        ending_beat(p, beat_waiting.sonic, beat_waiting.tails);
        p.scratch[2] = 0;
        p.scratch[3] = 0;
        return true;
    case Routine::ending_wait: // AC0C: until DC48
        if (!act.scene[scene_byte::go]) {
            return true;
        }
        ending_beat(p, beat_running_off.sonic, beat_running_off.tails);
        p.scratch[2] = 0;
        p.scratch[3] = 0;
        return true;
    case Routine::ending_run_off: // AC2C: past X 10D0 the camera stops; falling
        move_object(p);
        if (p.x < 0x10D0) {
            return true;
        }
        ending_beat(p, beat_falling.sonic, beat_falling.tails);
        disable_map(); // 4CCE (0312)
        return true;
    case Routine::ending_fall_out: // AC51: below Y 29E the act ends
        p.vy = std::uint16_t(p.vy + 0x10);
        move_object(p);
        if (p.y >= 0x029E) {
            flow.frame_work |= 0x10;
        }
        return true;
    default:
        return false;
    }
}

// A9C4
void Game::ending_held(Object& p) {
    p.display &= u8(~display_flag::mirrored);
    if (!(p.terrain & 0x02)) {
        return;
    }
    p.vy = 0x0200;
    p.vx = 0x0100;
    ending_beat(p, beat_walking.sonic, beat_walking.tails);
}

bool Game::run_destroyer_end_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::craft_start: // AE09: at (AC0, 2AE)
        o.next_state = 0x01;
        o.display |= 0x02;
        o.x = 0x0AC0;
        o.y = 0x02AE;
        return true;
    case Routine::craft_art: // AE22, AEC3
        object_art.request = craft_art;
        return true;
    case Routine::craft_rise: // AE28: up to Y 24E: off (3), the camera free to the act's end
        move_object(o);       // 5359 (0381)
        if (o.y >= 0x024E) {
            return true;
        }
        o.next_state = 0x03;
        camera.max_x = camera.act_max_x;
        return true;
    case Routine::craft_fly: // AE43
        craft_hold_player(o);
        if (o.display & display_flag::offscreen) {
            return true;
        }
        if (o.terrain & 0x02) {
            // AE6B: along the floor; from X C7E the last hop (4)
            move_horizontally_only(o); // 539A (0462)
            if (o.x >= 0x0C7E) {
                o.next_state = 0x04;
            }
            return true;
        }
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        object_terrain(o); // 713D (0384)
        return true;
    case Routine::craft_last_hop: // AE80: falling (1/8); landed: 5
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        object_terrain(o);
        if (o.terrain & 0x02) {
            o.next_state = 0x05;
        }
        return true;
    case Routine::craft_landed: // ADCD: 8 lower; the block 4 below it becomes 94 (kept in bytes
                                // 34-37), sound C5
        o.y = std::uint16_t(o.y + 0x08);
        probe_terrain(o, 0x0000, 0x0004); // 6FA6 (03FF)
        probe.block_x = probe.x;
        o.scratch[0] = lo(probe.x);
        o.scratch[1] = hi(probe.x);
        probe.block_y = probe.y;
        o.scratch[2] = lo(probe.y);
        o.scratch[3] = hi(probe.y);
        set_map_block_at(probe.cell, 0x94, probe.block_x, probe.block_y); // 6580 (0348)
        sound_.request(0xC5, 1);
        return true;
    case Routine::craft_jump: // ADB9, AFC9, B027: moving, falling (1/4)
        move_object(o);
        o.vy = std::uint16_t(o.vy + 0x40);
        return true;
    case Routine::cage_start: // B855
        o.next_state = 0x01;
        o.display |= 0x02;
        return true;
    case Routine::cage_wait: // B872: the player landed: it bursts (3), still
        if (!act.scene[scene_byte::landed]) {
            return true;
        }
        o.next_state = 0x03;
        o.vx = 0;
        o.vy = 0;
        return true;
    case Routine::cage_fall: // B889: falling (1/16) to Y 24E (4)
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o);
        if (o.y >= 0x024E) {
            o.next_state = 0x04;
        }
        return true;
    case Routine::cage_release: // B8AE
        act.scene[scene_byte::go] = 0xFF;
        return true;
    case Routine::cage_fly: // B8B4: falling (3/32) and speeding right (1/32)
        o.vy = std::uint16_t(o.vy + 0x18);
        o.vx = std::uint16_t(o.vx + 0x08);
        move_object(o);
        return true;
    case Routine::capsule_start: // AFB5: the act's boss (D3E5)
        o.next_state = 0x01;
        o.display |= 0x02;
        object_art.request = capsule_art;
        act.boss = u8(slot_number(o) + 1);
        return true;
    case Routine::capsule_done: // AFDD
        act.scene[scene_byte::done] = 0xFF;
        return true;
    case Routine::part_start: // B03B: tiles 6E
        o.display |= 0x02;
        o.next_state = 0x01;
        o.tile_base = 0x6E;
        o.tile_base_mirrored = 0x6E;
        return true;
    case Routine::part_art: // B04C
        object_art.request = part_art;
        return true;
    case Routine::part_fall_in: // B052
        o.next_state = 0x02;
        return true;
    case Routine::part_fall: // B057: down to Y 248 (3)
        move_object(o);
        if (o.y >= 0x0248) {
            o.next_state = 0x03;
        }
        return true;
    case Routine::part_down: // B06C: the act's variant over; the work byte DBE6 cleared
        act.variant = 0;
        act.work[0] = 0;
        return true;
    case Routine::part_gone: // B022
        o.type = 0xFF;
        return true;
    default:
        return false;
    }
}

// B85E (state 1): its art (7A25: list 1A:B989, with frame waits), the
// sprites' palette 1B; then waiting (2) for the player.
Flow Game::cage_art(Object& o) {
    co_await load_tile_list_slowly(Table{0x1A, 0xB989}); // 7A58
    o.next_state = 0x02;
    palette.fades[1].mode |= 0x20;
    palette.fades[1].palette = 0x1B;
    act.scene[scene_byte::landed] = 0;
}

// AF15, AEFE: past X AC5 the player is in state 46; he is kept at least 48
// to the craft's left.
void Game::craft_hold_player(Object& o) {
    auto& p = objects[0];
    if (p.x > 0x0AC5) {
        p.next_state = 0x46;
    }
    const std::uint16_t limit = std::uint16_t(o.x - 0x30);
    if (p.x >= limit) {
        p.x = limit;
    }
}

} // namespace triple_trouble_gg
