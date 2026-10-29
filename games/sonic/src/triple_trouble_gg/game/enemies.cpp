// Enemies: the bouncing enemy of type 23 (bank 1E, 9F37-A1A3), whose top
// is a spring, and the bomber of type 26 (bank 1E, A469-A615), which
// bobs along, drops a bomb (itself a type 26) when above the player and
// flies off, and the dropper of type 29 (bank 10, BC52-BCCB), which
// falls when the player passes below.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

bool Game::run_enemy_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::skimmer_start: // 17:833A (types 43, 44): off to the left at 4
        o.next_state = 0x01;
        o.vx = 0xFC00;
        return true;
    case Routine::skimmer_move: { // 8347: slowing by 1/128; at a wall (709A, 8 up) back at 4 (sound
                                  // B5); destroyed when attacked
        std::uint16_t back;
        if (!(o.vx & 0x8000)) {
            o.vx = std::uint16_t(o.vx - 2);
            back = 0xFC00;
        } else {
            o.vx = std::uint16_t(o.vx + 2);
            back = 0x0400;
        }
        if (probe_flags(o, 0, 0xFFF8) & 0x80) { // 709A (0387)
            o.vx = back;
            sound_.request(0xB5, 1);
        }
        move_horizontally_only(o); // 539A (0462)
        touch_test(o);             // 58AC (0369)
        destroy_if_attacked(o);    // 51B8 (0363)
        return true;
    }
    case Routine::bouncer_start: // A047
        bouncer_start(o);
        return true;
    case Routine::bouncer_walk: // A0F7
        move_object(o);
        object_terrain(o); // 713D (0384)
        bouncer_touch(o);
        return true;
    case Routine::bouncer_touch: // A0FD
        bouncer_touch(o);
        return true;
    case Routine::bouncer_swim: // A16F: kept 16 pixels below the water's surface
        move_object(o);
        object_terrain(o);
        if (below_water(o, 0x10)) {
            o.y = std::uint16_t(act.water_level + 0x10);
        }
        bouncer_touch(o);
        return true;
    case Routine::bouncer_face: // A18C: toward the player (state 2 left, 3 right)
        o.next_state = objects[0].x >= o.x ? 0x03 : 0x02;
        return true;
    case Routine::hopper_start: // B14D (type 33): hopping first (state 2)
        o.next_state = 0x02;
        o.param = 0;
        return true;
    case Routine::hopper_sit: // B156
        hopper_touch(o);
        return true;
    case Routine::hopper_hop: // B168: falling (gravity 1/8) until it lands (state 1)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        if (o.vy & 0x8000) {
            touch_test(o);
            destroy_if_attacked(o);
            return true;
        }
        object_terrain(o);
        if (o.terrain & 0x02) {
            o.next_state = 0x01;
            return true;
        }
        hopper_touch(o);
        return true;
    case Routine::popper_start: // 17:8417 (type 45): parameter FF pops up (state 2), FE drops (3)
        o.next_state = 0x01;
        if (o.param) {
            o.next_state = o.param == 0xFE ? 0x03 : 0x02;
            o.param = 0;
            o.tile_base = 0x90;
            o.tile_base_mirrored = 0x94;
        }
        return true;
    case Routine::popper_hop: // 8439
        o.vy = 0xFE00;
        return true;
    case Routine::popper_left: // 8442
        o.display &= u8(~display_flag::mirrored);
        o.vx = 0xFF80;
        return true;
    case Routine::popper_right: // 844F
        o.display |= display_flag::mirrored;
        o.vx = 0x0080;
        return true;
    case Routine::popper_fall: // 845C
        popper_fall(o);
        return true;
    case Routine::popper_arc: // 848B: gravity 1/8, moving, touchable
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        touch_test(o);
        destroy_if_attacked(o);
        return true;
    case Routine::popper_touch: // 849E
        touch_test(o);
        destroy_if_attacked(o);
        return true;
    case Routine::popper_drop: // 84A4: straight down until it lands (state 1)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_vertically_only(o); // 5379 (045F)
        object_terrain(o);
        if (o.terrain & 0x02) {
            o.next_state = 0x01;
            return true;
        }
        touch_test(o);
        destroy_if_attacked(o);
        return true;
    case Routine::weight_start: // 17:8769 (type 47): falls at 4 from 16 higher, sound D9
        o.next_state = 0x01;
        o.tile_base = o.tile_base_mirrored = 0xA0;
        o.vy = 0x0400;
        o.y = std::uint16_t(o.y - 0x10);
        sound_.request(0xD9, 1);
        return true;
    case Routine::weight_fall: // 8793: harmful (odd frames) until it lands (state 2)
        move_object(o);
        if (video.frame_counter & 1) {
            harmful_touch(o); // 550E (0375)
            return true;
        }
        object_terrain(o);
        if (o.terrain & 0x02) {
            o.next_state = 0x02;
        }
        return true;
    case Routine::icicle_start: // 17:82B1 (type 42): parameter nonzero falls fast
        o.next_state = 0x01;
        if (o.param) {
            o.param = 0;
            o.scratch[0] = 0x01;
            return true;
        }
        o.y = std::uint16_t(o.y + 0x0A);
        return true;
    case Routine::icicle_wait: // 82D5: the player below and within 32 across: it falls (sound DF)
        if (o.y >= objects[0].y) {
            return true;
        }
        if (touches_x(o, objects[0], 0x20)) { // 5424 (0378)
            o.next_state = 0x02;
            sound_.request(0xDF, 1);
        }
        return true;
    case Routine::icicle_fall: { // 82F5: speeding up (1/32 to 2, fast 1/8 to 4); shatters on
                                 // landing
        const std::uint16_t step = o.scratch[0] ? 0x0020 : 0x0008;
        const u8 cap = o.scratch[0] ? 0x04 : 0x02;
        const std::uint16_t v = std::uint16_t(o.vy + step);
        if (hi(v) != cap) {
            o.vy = v;
        }
        move_object(o);
        harmful_touch(o); // 550E (0375)
        object_terrain(o);
        if (o.terrain & 0x02) {
            sound_.request(0xCE, 1); // 51CB (047A)
            become_explosion(o);
        }
        return true;
    }
    case Routine::roller_start: // 1E:A9C2 (type 2B): parameter 0 drops first (state 3)
        o.flags |= 0x80;
        o.next_state = o.param ? 0x01 : 0x03;
        return true;
    case Routine::roller_roll: // A9D7: status bit 6 picks the other frames (state 2)
        roller_roll(o);
        if (o.status & 0x40) {
            o.next_state = 0x02;
        } else {
            roller_break(o);
        }
        return true;
    case Routine::roller_roll_b: // A9EC
        roller_roll(o);
        if (!(o.status & 0x40)) {
            o.next_state = 0x01;
        } else {
            roller_break(o);
        }
        return true;
    case Routine::roller_drop: // AA2E: rolls (state 1) once it has been in the air and lands
        move_object(o);
        object_terrain(o); // 713D (0384)
        if (!(o.terrain & 0x02)) {
            o.counter = 0xFF;
        } else if (o.counter) {
            o.next_state = 0x01;
        }
        return true;
    case Routine::shooter_start: // 1E:BB1A (type 3D)
        shooter_start(o);
        return true;
    case Routine::shooter_move: // BB8F
        shooter_move(o);
        return true;
    case Routine::shot_fly:        // BBBF
        move_horizontally_only(o); // 539A (0462)
        harmful_touch(o);          // 550E (0375)
        return true;
    case Routine::shot_fade: // BBC6
        o.display |= display_flag::flicker;
        return true;
    case Routine::shot_gone: // BBCB
        o.type = 0xFF;
        return true;
    case Routine::sub_start: // 1E:BFA3 (type 40): Tails' submarine; gone when Sonic plays
        o.next_state = 0x01;
        o.flags |= 0x80;
        o.tile_base = o.tile_base_mirrored = 0xA4;
        o.display &= u8(~display_flag::mirrored);
        if (objects[0].type == 0x01) {
            o.type = 0xFF;
        }
        return true;
    case Routine::sub_wait: // BFC2: Tails landing on it boards (state 29, 3F29)
        sub_wait(o);
        return true;
    case Routine::dropper_start: // BC8B
        o.next_state = 0x01;
        return true;
    case Routine::dropper_wait: // BCB5: within 16 pixels across it drops (state 2)
        if (touches_x(o, objects[0], 0x10)) {
            o.next_state = 0x02;
            return true;
        }
        move_object(o);
        touch_test(o);          // 58AC (0369)
        destroy_if_attacked(o); // 51B8 (0363)
        return true;
    case Routine::dropper_fall: // BC90: falling (up to 4 pixels per frame) until it lands (state 3)
        object_terrain(o);      // 713D (0384)
        if (o.terrain & 0x02) {
            o.next_state = 0x03;
            return true;
        }
        if (hi(o.vy) < 0x04) {
            o.vy = std::uint16_t(o.vy + 0x10);
        }
        move_object(o);
        touch_test(o);
        destroy_if_attacked(o);
        return true;
    case Routine::dropper_landed: // BCC6
        touch_test(o);
        destroy_if_attacked(o);
        return true;
    case Routine::bomber_start: // A513
        bomber_start(o);
        return true;
    case Routine::bomber_ready: // A561: bobbing starts (16 frames, downward)
        o.next_state = 0x02;
        o.timer = 0x10;
        o.counter = 0;
        return true;
    case Routine::bomber_patrol: // A56E
        bomber_patrol(o);
        return true;
    case Routine::bomber_bob: // A584
        bomber_bob(o);
        return true;
    case Routine::bomber_fly: // A5C9
        move_object(o);
        touch_test(o);          // 58AC (0369)
        destroy_if_attacked(o); // 51B8 (0363)
        return true;
    case Routine::bomber_flee: // A5CC
        touch_test(o);
        destroy_if_attacked(o);
        return true;
    case Routine::bomb_fall: // A5D2
        bomb_fall(o);
        return true;
    case Routine::bomber_pass: // A604: gone near the left edge of its 256-pixel page
        if (lo(o.x) < 0x1E) {
            o.type = 0xFF;
            return true;
        }
        move_object(o);
        harmful_touch(o); // 550E (0375)
        return true;
    default:
        return false;
    }
}

// A047: the parameter (kept in byte 26 as the kind) picks the script:
// 0 walking (1), 6 (6), others swimming (2); FE-FB enter at the screen's
// right (FE, FD) or left edge (FC, FB) at Y 26E or 1AE (4, 5). The player
// does not note touching it (flags bit 7).
void Game::bouncer_start(Object& o) {
    o.flags |= 0x80;
    const u8 kind = o.param;
    o.status_ticks = kind;
    o.param = 0;
    if (kind == 0x06) {
        o.next_state = 0x06;
        return;
    }
    if (kind >= 0xFB && kind <= 0xFE) {
        const bool right = kind == 0xFE || kind == 0xFD;
        // Widescreen: beyond the presented view's edge.
        o.x = std::uint16_t(camera.x + (right ? 0x00E0 + wide_right : 0x0030 - wide_left));
        o.y = (kind == 0xFE || kind == 0xFC) ? 0x026E : 0x01AE;
        o.next_state = right ? 0x04 : 0x05;
        o.tile_base = o.tile_base_mirrored = 0x36;
        o.status_ticks = 0;
        return;
    }
    o.next_state = kind ? 0x02 : 0x01;
}

// A0FD: in zone 0's act 3 (variant 0) it is destroyed at once (51CB).
// Touched from above (the player at least 4 pixels above its origin,
// falling, not already sprung) it springs the player up at 6 (6.5 for
// kind FC) pixels per frame; from elsewhere a rolling player (or one with
// power-up 5) destroys it, otherwise the player is hit.
void Game::bouncer_touch(Object& o) {
    if (flow.zone == 0 && flow.act == 2 && !act.variant) {
        throw Unrecovered("bouncer in zone 0 act 3 (047A)");
    }
    auto& p = objects[0];
    if (p.state == 0x11) {
        return;
    }
    touch_test(o); // 58AC (0369)
    if (!(o.touch_sides & 0x0F)) {
        return;
    }
    if (std::uint16_t(o.y - 4) >= p.y) {
        if (p.next_state == 0x0B || (p.vy & 0x8000)) {
            return;
        }
        player.spring_flag = 0xFF;
        spring_player(o, o.status_ticks == 0xFC ? 0xF980 : 0xFA00); // 5186 (03B1)
        return;
    }
    if ((p.flags & 0x02) || p.power_up == 0x05) {
        destroy_if_attacked(o); // 51B8 (0363)
        return;
    }
    player.hit = 0xFF;
}

// A513: parameter 0 a bomber (state 1, left at 1 pixel per frame; in
// zone 1 at 1000, 19E), FE one passing fast (state 6), 7 one circling
// (state 7), others a bomb (state 5).
void Game::bomber_start(Object& o) {
    const u8 kind = o.param;
    o.param = 0;
    if (kind == 0x07) {
        o.next_state = 0x07;
        return;
    }
    if (kind == 0xFE) {
        o.next_state = 0x06;
        o.tile_base = o.tile_base_mirrored = 0x62;
        return;
    }
    if (kind) {
        o.next_state = 0x05;
        return;
    }
    o.next_state = 0x01;
    o.vx = 0xFF00;
    if (flow.zone != 1) {
        return;
    }
    o.x = 0x1000;
    o.y = 0x019E;
    o.tile_base = o.tile_base_mirrored = 0x62;
}

// A56E: moving; on screen within 8 pixels of the player's X it attacks
// (state 3); otherwise it bobs (A584).
void Game::bomber_patrol(Object& o) {
    move_object(o);
    if (o.display & display_flag::offscreen) {
        return;
    }
    const std::uint16_t d = std::uint16_t(o.x - objects[0].x);
    if (((d & 0x8000) ? std::uint16_t(-d) : d) < 0x08) { // 5424 (0378)
        o.next_state = 0x03;
        return;
    }
    bomber_bob(o);
}

// A584: touched from below it hits the player; it bobs, its vertical speed
// changing by 1/16 pixel per frame, turning every 32 frames; a rolling
// player destroys it (51B8).
void Game::bomber_bob(Object& o) {
    touch_test(o); // 58AC (0369)
    if (o.touch_sides & 0x02) {
        player.hit = 0xFF;
    }
    if (o.state != 0x07) {
        o.vy = std::uint16_t(o.vy + ((o.counter & 1) ? 0xFFF0 : 0x0010));
        if (!--o.timer) {
            o.timer = 0x20;
            ++o.counter;
        }
    }
    destroy_if_attacked(o); // 51B8 (0363)
}

// A5D2: the bomb falls (gravity 1/8 pixel on every other frame) and is
// gone off screen; on the other frames, landing destroys it (51B1).
// Touching it hits the player.
void Game::bomb_fall(Object& o) {
    if (o.display & display_flag::offscreen) {
        o.type = 0xFF;
        return;
    }
    move_object(o);
    if (++o.timer & 1) {
        o.vy = std::uint16_t(o.vy + 0x20);
        harmful_touch(o);
        return;
    }
    object_terrain(o); // 713D (0384)
    if (o.terrain & 0x02) {
        destroy_object(o); // 51B1 (0366)
        return;
    }
    harmful_touch(o);
}

// 550E (0375): touching it hits the player.
void Game::harmful_touch(Object& o) {
    touch_test(o);
    if (o.touch_sides & 0x0F) {
        player.hit = 0xFF;
    }
}

// B156: touchable (and destroyed by a rolling player) unless the player
// stands on blocks A7-A9.
void Game::hopper_touch(Object& o) {
    const u8 b = terrain_state.block;
    if (b == 0xA7 || b == 0xA8 || b == 0xA9) {
        return;
    }
    touch_test(o);          // 58AC (0369)
    destroy_if_attacked(o); // 51B8 (0363)
}

// 17:845C: falling; landing it hops on the other way (scripts 83C5 /
// 839D); then as 848B.
void Game::popper_fall(Object& o) {
    if (!(o.vy & 0x8000)) {
        object_terrain(o); // 713D (0384)
        if (o.terrain & 0x02) {
            jump_script(o, (o.vx & 0x8000) ? 0x83C5 : 0x839D);
            o.frame_ticks = 0x01;
        }
    }
    o.vy = std::uint16_t(o.vy + 0x20);
    move_object(o);
    touch_test(o);
    destroy_if_attacked(o);
}

// 1E:BFC2: Tails (not in state 41, not rising) landing on the submarine
// sits in it: placed in it, state 29 with its song (3F29); the object goes.
// A9D7 / A9EC: the roller (type 2B) rolls at 1 and falls at 4. Touched
// by Sonic on his board or by a rolling player it breaks (AA27); other
// touches (and power-up 5) hurt.
void Game::roller_roll(Object& o) {
    touch_test(o); // 58AC (0369)
    if (o.touch_sides & 0x0F) {
        const auto& p = objects[0];
        if (p.power_up != 0x05 && (p.state == 0x23 || (p.flags & 0x02))) {
            o.param = 0;
            sound_.request(0xCE, 1); // 51CB (047A)
            become_explosion(o);
        } else {
            player.hit = 0xFF;
        }
    }
    // Even once broken it moves and picks its state this update (the
    // explosion's state then comes from the roller's status bit 6).
    move_object(o);
    object_terrain(o); // 713D (0384)
}

// AA49: on even frames, a block flagged solid (bit 7) at (-8, -8) breaks
// it.
void Game::roller_break(Object& o) {
    if (video.frame_counter & 1) {
        return;
    }
    if (probe_flags(o, 0xFFF8, 0xFFF8) & 0x80) { // 709A (0387)
        sound_.request(0xCE, 1);                 // 51CB (047A)
        become_explosion(o);
    }
}

// BB1A: the shooter (type 3D) rides up and down (parameter 0: frame 3,
// state 1; 1: frame 4, state 2; both 96 updates up, 96 down); its shots are
// parameters 2 (left) and 3 (right) at 1 pixel per update (state 3),
// flickering after 180 updates and gone after 196.
void Game::shooter_start(Object& o) {
    o.tile_base = 0x86;
    switch (o.param) {
    case 0x02:
        o.vx = 0xFF00;
        o.next_state = 0x03;
        return;
    case 0x03:
        o.vx = 0x0100;
        o.next_state = 0x03;
        return;
    case 0x01:
        o.next_state = 0x02;
        return;
    default:
        o.next_state = 0x01;
    }
}

// BB8F: moving; level with the player (within 8 lines) it fires a shot
// (type 3D, parameter + 2) and waits 60 updates.
void Game::shooter_move(Object& o) {
    move_vertically_only(o); // 5379 (045F)
    if (o.scratch[0]) {
        --o.scratch[0];
        return;
    }
    const std::uint16_t d = std::uint16_t(o.y - objects[0].y); // 5430 (037B)
    if (((d & 0x8000) ? std::uint16_t(-d) : d) >= 0x08) {
        return;
    }
    spawn_object_at(o, 0x3D, u8(o.param + 2), o.x, o.y); // 510A (0357)
    o.scratch[0] = 0x3C;
}

void Game::sub_wait(Object& o) {
    auto& p = objects[0];
    if (p.type == 0x01 || p.state == 0x41 || (p.vy & 0x8000)) {
        return;
    }
    touch_test(o); // 58AC (0369)
    if (!(o.touch_sides & 0x01)) {
        return;
    }
    p.y = std::uint16_t(p.y + 0x0C);
    p.x = o.x;
    // 3F29 (047D)
    p.next_state = 0x29;
    p.vx = 0;
    p.vy = 0;
    sound_.request(0x94, 2);
    act.music_playing = 0x94;
    o.placement = 0;
    o.type = 0xFF;
}

// 5424 (0378): the objects' X within `range` of each other.
bool Game::touches_x(const Object& a, const Object& b, unsigned range) const {
    const std::uint16_t d = std::uint16_t(a.x - b.x);
    return ((d & 0x8000) ? std::uint16_t(-d) : d) < range;
}

// 5607 (03AE): the object more than `depth` pixels below the water's
// surface.
bool Game::below_water(const Object& o, unsigned depth) const {
    if (!act.water_level) {
        return false;
    }
    return std::uint16_t(act.water_level + depth) < o.y;
}

// 5186 (03B1): the player is sprung up at `vy` by `spring` (3DB3), then
// leaves a state 26 (5854).
void Game::spring_player(Object& spring, std::uint16_t vy) {
    auto& p = objects[0];
    p.touch_sides = 0;
    launch_player(spring, vy); // 3DB3
    p.status &= u8(~0x04);
    if (p.state == 0x23) {
        return;
    }
    leave_special_state(p); // 5854
}

// 3DB3: the player up at `vy` (sound B5): in the air, no longer on the
// floor; sprung (state 0B) unless in state 16 or 23, when the launching
// object's status bit 2 is cleared too (3DDF runs with it as its object).
void Game::launch_player(Object& source, std::uint16_t vy) {
    auto& p = objects[0];
    p.vy = vy;
    p.flags = u8((p.flags | 0x01) & ~0x02);
    p.terrain &= u8(~0x02);
    p.contact &= u8(~0x02);
    p.touch_sides &= u8(~0x02);
    sound_.request(0xB5, 1);
    if (p.state == 0x16 || p.state == 0x23) {
        return;
    }
    p.next_state = 0x0B;
    source.status &= u8(~0x04);
}

// 5854: springs end states 23 and 26: the power-up timer to 1 and falling
// (state 23: 3B6F, in zone 3 with a puff (type 34); 26: 3B47).
void Game::leave_special_state(Object& p) {
    if (p.state == 0x23) {
        player.power_up_ticks = 0x0001;
        if (flow.zone == 3) {
            spawn_object_at(p, 0x34, 0x00, p.x, p.y); // 510A
        }
        to_falling_state(p); // 3B6F
        return;
    }
    if (p.state == 0x26) {
        player.power_up_ticks = 0x0001;
        to_falling(p); // 3B47
    }
}

} // namespace triple_trouble_gg
