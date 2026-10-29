// Platforms (5669, through 0396): objects the player stands on, moving
// back and forth between two limits, and enemies the player may stomp
// (59DB, through 036F). Type 4D (bank 17, 884C-8988) is a lift that
// rides its distance while stood on. Type 25 (bank 1E, A3BE-A468) is the plain
// platform; type 2C (AA5C-AAC8) a still one shown pressed while ridden;
// type 27 (A616-A7F5) is an enemy that, stomped, becomes one; type 1A
// (959A-962C) a piston that stamps down and back up (Robotnik Winter).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Bits of a platform's Object::status (offset 24).
namespace platform {
inline constexpr u8 sunk = 0x01;     // sunk to the bottom (8 pixels)
inline constexpr u8 backward = 0x02; // moving up / left
inline constexpr u8 ridden = 0x04;   // the player stands on it
inline constexpr u8 turn_at_walls = 0x10;
inline constexpr u8 horizontal = 0x20;
inline constexpr u8 when_ridden = 0x40; // moves only while ridden
inline constexpr u8 sinks = 0x80;       // sinks while ridden
} // namespace platform

// Bits of Object::path (offset 25) for stompable enemies.
namespace stomp {
inline constexpr u8 survives = 0x01; // stomped, it is not destroyed (becomes a platform)
inline constexpr u8 stomped = 0x02;
inline constexpr u8 harmful = 0x04; // it cannot be stomped
} // namespace stomp

// A45E: the platforms' tile base by zone.
constexpr Table platform_tile_bases{0x1E, 0xA45E};
// AAA2: the step platforms' (type 2C) by zone.
constexpr Table step_tile_bases{0x1E, 0xAAA2};
// 95C6: the pistons' (type 1A) by zone.
constexpr Table piston_tile_bases{0x1E, 0x95C6};
constexpr u8 piston_sound = 0xB6;

bool near(std::uint16_t a, std::uint16_t b, unsigned range) { // 543A
    const std::uint16_t d = std::uint16_t(a - b);
    return ((d & 0x8000) ? std::uint16_t(-d) : d) < range;
}
} // namespace

// 5669 (0396): the platform moves (unless only while ridden and not
// ridden); while its timer runs the player cannot get on.
void Game::platform(Object& o) {
    if (!(o.status & platform::when_ridden) || (o.status & platform::ridden)) {
        move_platform(o); // 567F
    }
    if (o.timer) {
        return;
    }
    ride_platform(o); // 572E
}

// 567F: one pixel along its axis; turning at a solid block 16 pixels ahead
// (with turn_at_walls), otherwise at its limits (scratch 2-3 the low, 4-5
// the high one).
void Game::move_platform(Object& o) {
    const u8 s = o.status;
    const bool back = s & platform::backward;
    std::uint16_t& p = (s & platform::horizontal) ? o.x : o.y;
    p = std::uint16_t(back ? p - 1 : p + 1);
    if (s & platform::turn_at_walls) {
        const std::uint16_t ahead = back ? 0xFFF0 : 0x0010;
        const u8 flags = (s & platform::horizontal) ? probe_flags(o, ahead, 0)
                                                    : probe_flags(o, 0, ahead); // 709A
        if (flags & 0x80) {
            o.status ^= platform::backward;
        }
        return;
    }
    if (!back) {
        if (p >= std::uint16_t(o.scratch[4] | o.scratch[5] << 8)) {
            o.status |= platform::backward;
        }
    } else if (p < std::uint16_t(o.scratch[2] | o.scratch[3] << 8)) {
        o.status &= u8(~platform::backward);
    }
}

// 572E: the player stands on it when within 16 pixels across and its
// surface height vertically, falling (or, on a rising platform, not
// rising fast), and not in state 21 or bounced off (flags bit 6). Standing
// on it: held at its top, carried along horizontally, the platform's id
// (byte 32) in D3C4; one that sinks goes down to 8 pixels. Getting off
// (unless rising) the player falls at 4 pixels per frame.
void Game::ride_platform(Object& o) {
    o.touch_sides = 0;
    auto& p = objects[0];
    bool on = false;
    if (!(o.display & display_flag::offscreen)) {
        if (p.state == 0x21 || p.next_state == 0x21) {
            return;
        }
    }
    if (!(o.display & display_flag::offscreen) && !(p.vy & 0x8000) && !(p.flags & 0x40)) {
        if (player.carried && player.carried != o.power_up) {
            rise_platform(o); // 5835
            return;
        }
        on = near(o.y, p.y, o.surface) && near(o.x, p.x, 0x10); // 5430, 5424
    }
    if (!on) { // 5797
        if (player.carried != o.power_up) {
            return;
        }
        if (!(p.vy & 0x8000)) {
            p.vy = 0x0400;
        }
        player.carried = 0;
        p.touch_sides &= u8(~0x20);
        p.contact &= u8(~0x02);
        rise_platform(o);
        return;
    }
    // 57BC
    o.touch_sides = 0xFF;
    player.carried = o.power_up;
    p.touch = 0x25;
    o.status |= platform::ridden;
    p.touch_sides |= 0x20;
    p.vy = 0;
    p.y = std::uint16_t(o.y - (o.surface - 2));
    if (o.status & platform::horizontal) {
        p.x = std::uint16_t((o.status & platform::backward) ? p.x - 1 : p.x + 1);
    }
    // 580E
    if (!(o.status & platform::sinks)) {
        return;
    }
    if (o.status & platform::sunk) {
        rise_platform(o, false);
        return;
    }
    if (o.scratch[0] == 0x08) {
        o.status |= platform::sunk;
        return;
    }
    ++o.scratch[0];
    ++o.y;
}

// 5835 / 5839: a sinking platform rises back a pixel per frame.
void Game::rise_platform(Object& o, bool reset) {
    if (reset) {
        o.status &= u8(~platform::sunk);
    }
    if (!(o.status & platform::sinks) || !o.scratch[0]) {
        return;
    }
    --o.scratch[0];
    --o.y;
}

// 59DB (036F): an enemy the player may stomp: overlapping its frame box,
// a player with power-up 5, or one jumping down onto it (bounced up at 3
// pixels per frame), defeats it: destroyed (51CB) or, if it survives,
// marked stomped. Otherwise (or a harmful one) the player is hit and
// bounced up at 2.
void Game::stomp_check(Object& o) {
    if (o.path & stomp::stomped) {
        return;
    }
    auto& p = objects[0];
    if (p.flags & 0x40) {
        return;
    }
    if (!near(o.x, p.x, o.frame_width) || !near(o.y, p.y, o.frame_height)) {
        return;
    }
    if (p.power_up != 0x05) {
        if (!(p.flags & 0x02) || (o.path & stomp::harmful)) {
            p.vy = 0xFE00; // 5A2F
            player.hit = 0xFF;
            return;
        }
        if (!(p.vy & 0x8000)) {
            p.vy = 0xFD00;
            p.flags |= 0x03;
        }
    }
    // 5A21
    if (!(o.path & stomp::survives)) {
        sound_.request(0xCE, 1); // 51CB
        become_explosion(o);
        return;
    }
    o.path |= stomp::stomped;
}

void Game::set_lift_travel(Object& o, std::uint16_t v) {
    o.scratch[0] = lo(v);
    o.scratch[1] = hi(v);
}

// 88D4: back to its place (origin Y) with its whole distance to go.
void Game::reset_lift(Object& o) {
    o.scratch[0] = o.scratch[2];
    o.scratch[1] = o.scratch[3];
    o.y = o.origin_y;
}

// 8892: waiting (seen only on screen): the player, not on another
// platform, not rolling (18), not rising, within 14 across and 10 up or
// down boards it (state 2).
void Game::lift_wait(Object& o) {
    o.display &= u8(~0x02);
    const auto& p = objects[0];
    if (player.carried || p.state == 0x18 || (p.vy & 0x8000)) {
        return;
    }
    if (!near(o.x, p.x, 0x14) || !near(o.y, p.y, 0x10)) { // 543A (037E)
        return;
    }
    o.next_state = 0x02;
    player.carried = o.power_up;
    reset_lift(o);
}

// 88ED: ridden: the player kept on its top (touch 4D, bit 5), it moves a
// pixel per frame (up; down with a parameter) until its distance is used
// up. The player leaving (rising, off it by 12 across or 6 up or down, or
// on another platform) sends it back (state 1).
void Game::lift_ride(Object& o) {
    o.display |= 0x02;
    auto& p = objects[0];
    if (player.carried == o.power_up) {
        if (!(p.vy & 0x8000) && near(o.x, p.x, 0x12) && near(o.y, p.y, 0x06)) {
            player.carried = o.power_up;
            p.touch = 0x4D;
            p.touch_sides |= 0x20;
            p.y = o.y;
            const std::uint16_t left = std::uint16_t(o.scratch[0] | o.scratch[1] << 8);
            if (!left) {
                return;
            }
            set_lift_travel(o, std::uint16_t(left - 1));
            o.y = std::uint16_t(o.param ? o.y + 1 : o.y - 1);
            return;
        }
        player.carried = 0; // 896A
    }
    o.next_state = 0x01; // 896E
    reset_lift(o);
}

bool Game::run_platform_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::ice_block_start: // 1E:A913 (type 2A): a platform bobbing 60 above and below its
                                   // place (bytes 36-39)
        o.next_state = 0x01;
        {
            const std::uint16_t low = std::uint16_t(o.y + 0x60), high = std::uint16_t(o.y - 0x60);
            o.scratch[4] = lo(low);
            o.scratch[5] = hi(low);
            o.scratch[2] = lo(high);
            o.scratch[3] = hi(high);
        }
        o.power_up = u8(slot_number(o) + 1); // 52CA (035A)
        o.status = 0;
        o.surface = 0x10;
        return true;
    case Routine::ice_block_bob: // A946: a rolling player's touch breaks it (2, sound CE)
        move_object(o);          // 5359 (0381)
        touch_test(o);           // 58AC (0369)
        if ((o.touch_sides & 0x0F) && (objects[0].flags & 0x02)) {
            o.next_state = 0x02;
            sound_.request(0xCE, 1);
        }
        return true;
    case Routine::ice_block_broken: // A963
        platform(o);                // 5669 (0396)
        return true;
    // Type 1A's script: the sound, 6 frames down at 4 pixels, 32 still;
    // the sound, 6 frames up, 32 still; only rising and still is it solid.
    case Routine::piston_start: // 1E:959A: 2 lower; the zone's tiles
        o.next_state = 0x01;
        o.flags |= 0x80;
        o.display |= 0x02;
        o.y = std::uint16_t(o.y + 2);
        o.tile_base = o.tile_base_mirrored = rom.byte(piston_tile_bases, flow.zone);
        return true;
    case Routine::piston_thud: // 95CC
        sound_.request(piston_sound, 1);
        return true;
    case Routine::piston_hold: // 95D2
        piston_hold(o);
        return true;
    case Routine::piston_down: // 9614
        o.y = std::uint16_t(o.y + 4);
        return true;
    case Routine::piston_up: // 9619
        piston_hold(o);
        o.y = std::uint16_t(o.y - 4);
        return true;
    case Routine::lift_start: { // 17:8864: kept off screen; the distance (display byte as placed,
                                // minus 2, twice) in 34 and 36
        o.display |= 0x02;
        o.next_state = 0x01;
        const std::uint16_t distance = std::uint16_t(u8(o.display - 2) * 2u);
        set_lift_travel(o, distance);
        o.scratch[2] = o.scratch[0];
        o.scratch[3] = o.scratch[1];
        o.display = 0;
        o.power_up = u8(slot_number(o) + 1); // 52CA (035A)
        return true;
    }
    case Routine::lift_wait: // 8892
        lift_wait(o);
        return true;
    case Routine::lift_ride: // 88ED
        lift_ride(o);
        return true;
    default:
        break;
    }
    switch (r) {
    case Routine::platform:
        platform(o);
        return true;
    case Routine::stomp_check:
        stomp_check(o);
        return true;
    case Routine::platform_still: // 5677 (0399): only ridden, never moving
        if (!o.timer) {
            ride_platform(o); // 572E
        }
        return true;
    case Routine::platform_start: // A3D6
        platform_start(o);
        return true;
    case Routine::step_start: // AA74: a still platform (surface 10) with the zone's tile base
        o.next_state = 0x01;
        o.surface = 0x10;
        o.power_up = u8(slot_number(o) + 1); // 52CA (035A)
        o.scratch[0] = o.scratch[1] = 0;
        o.timer = 0;
        o.tile_base = o.tile_base_mirrored = rom.byte(step_tile_bases, flow.zone);
        return true;
    case Routine::step_up: // AAAD: ridden, it shows pressed (state 2)
        ride_platform(o);  // 572E (0450)
        if (player.carried == o.power_up) {
            ++o.next_state;
        }
        return true;
    case Routine::step_down: // AABB: left, back to state 1
        ride_platform(o);
        if (player.carried != o.power_up) {
            --o.next_state;
        }
        return true;
    case Routine::bobber_start: // A6DE
        bobber_start(o);
        return true;
    case Routine::bobber_throw: // A6AA (a script call): the pieces' spawns, of type 27 in zone 1
                                // (A66B), else 18 (A67F)
        jump_script(o, flow.zone == 1 ? 0xA66B : 0xA67F);
        o.frame_ticks = 0x01;
        return true;
    case Routine::bobber_move: // A77A
        move_object(o);
        [[fallthrough]];
    case Routine::bobber_hold: // A77D: the same without the move
        if (o.display & display_flag::offscreen) {
            return true;
        }
        if (video.frame_counter & 1) {
            return true;
        }
        if (!(o.path & stomp::stomped)) {
            stomp_check(o); // 59DB (036F)
            return true;
        }
        o.next_state = 0x03;
        sound_.request(0xCE, 1);
        return true;
    case Routine::bobber_piece: // A798
        if (o.display & display_flag::offscreen) {
            o.type = 0xFF;
            return true;
        }
        o.vy = std::uint16_t(o.vy + 0x10);
        move_object(o);
        if (!(video.frame_counter & 1)) {
            stomp_check(o);
        }
        return true;
    case Routine::bobber_platform: // A7BE
        bobber_platform(o);
        return true;
    default:
        return false;
    }
}

// 95D2: while the player is not rising: gone once more than 320 pixels
// away across; otherwise solid (51FF), and a player on top is crushed
// (D3C1, DBD0 50 as 37B5); one pressing against a side stops.
void Game::piston_hold(Object& o) {
    auto& p = objects[0];
    if (p.vy & 0x8000) {
        return;
    }
    if (!touches_x(o, p, 0x0140)) { // 5424 (0378)
        o.type = 0xFF;
        return;
    }
    solid_object(o); // 51FF (036C)
    const u8 sides = o.touch_sides;
    if (sides & 0x01) { // on top
        player.hit = 0xFF;
        player.touch_kind = 0x50;
        return;
    }
    if (sides & 0x04) { // at its right, moving left
        if (p.vx & 0x8000) {
            p.vx = 0;
        }
        return;
    }
    if ((sides & 0x08) && !(p.vx & 0x8000)) { // at its left, moving right
        p.vx = 0;
    }
}

// A3D6: the zone's tile base, its id (byte 32), surface 10. The
// parameter's high nibble is its status (platform bits); a low nibble n
// picks state n + 1 (2: still). A moving one ranges by its placement's
// byte 4 (50 if 0) about its Y (X when horizontal, which starts moving
// left), unless it turns at walls.
void Game::platform_start(Object& o) {
    o.tile_base = o.tile_base_mirrored = rom.byte(platform_tile_bases, flow.zone);
    o.power_up = u8(slot_number(o) + 1); // 52CA (035A)
    o.surface = 0x10;
    o.next_state = 0x01;
    o.scratch[0] = o.scratch[1] = 0;
    o.timer = 0;
    o.status = o.param & 0xF0;
    if (o.param & 0x0F) {
        o.next_state = u8((o.param & 0x0F) + 1);
        return;
    }
    const u8 s = o.status;
    if (!(s & platform::turn_at_walls)) {
        const std::uint16_t range = o.display ? o.display : 0x50;
        const std::uint16_t centre = (s & platform::horizontal) ? o.x : o.y;
        const std::uint16_t high = std::uint16_t(centre + range),
                            low = std::uint16_t(centre - range);
        o.scratch[4] = lo(high);
        o.scratch[5] = hi(high);
        o.scratch[2] = lo(low);
        o.scratch[3] = hi(low);
    }
    if (s & platform::horizontal) {
        o.status = u8((o.status | platform::backward) & ~platform::ridden);
    }
    o.display = 0;
}

// A6DE: its id (slot number + 1) in byte 32. Parameters 1 and 2 are pieces
// thrown right and left (state 2, harmful). Otherwise a survivor of a
// stomp: the parameter is its platform status (A0: a platform from the
// start, state 3; else state 1); its surface 0E; its range (placement
// byte 4) about its Y (X when horizontal) sets the limits.
void Game::bobber_start(Object& o) {
    o.power_up = u8(slot_number(o) + 1); // 52CA (035A)
    if (o.param == 0x01 || o.param == 0x02) {
        o.vx = o.param == 0x01 ? 0x0100 : 0xFF00;
        o.vy = 0xFE00;
        o.path |= stomp::harmful;
        o.next_state = 0x02;
        return;
    }
    o.path |= stomp::survives;
    o.status = o.param;
    o.surface = 0x0E;
    const std::uint16_t range = o.display;
    const std::uint16_t centre = (o.status & platform::horizontal) ? o.x : o.y;
    const std::uint16_t high = std::uint16_t(centre + range), low = std::uint16_t(centre - range);
    o.scratch[4] = lo(high);
    o.scratch[5] = hi(high);
    o.scratch[2] = lo(low);
    o.scratch[3] = hi(low);
    // A72A tests bit 5 of B, which holds the object loop's count of slots
    // left (below 20 here): never set. (Hypothesis from the call path.)
    o.scratch[0] = o.scratch[1] = 0;
    o.counter = o.timer = 0;
    o.display = 0;
    o.next_state = o.param == 0xA0 ? 0x03 : 0x01;
}

// A7BE: a platform; when the player gets on it (once per ride) he stands
// at its X in state 30. After he leaves, 16 frames pass before he can get
// on again.
void Game::bobber_platform(Object& o) {
    if (o.timer) {
        --o.timer;
    }
    platform(o); // 5669 (0396)
    if (player.carried == o.power_up) {
        if (o.counter & 1) {
            return;
        }
        o.counter = 0x01;
        auto& p = objects[0];
        p.x = o.x;
        p.next_state = 0x30; // 3ED6 (03CC)
        p.flags &= u8(~0x03);
        return;
    }
    if (o.counter & 1) {
        o.timer = 0x10;
    }
    o.counter = 0;
}

} // namespace triple_trouble_gg
