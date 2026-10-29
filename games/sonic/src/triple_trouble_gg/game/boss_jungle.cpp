// Meta Junglira's boss (zone 2 act 3; type 52, bank 17, 91DC-9B17).
// Parameter 0 is the machine: it waits off screen (3), arrives with the
// camera (4) and lowers its arm (parameter FF, states B, 8-C), which holds
// it; while the arm swings (8) it is shut, when the arm reaches for the
// player (9/A) it opens and can be hit (5, 7; 13 hits). Beaten (2), it
// drops, the arm draws back (C), and droppers (parameter 1, state D) fall
// at the player five times three. Then the act's song and its end.
//
// The arm is drawn from 24 pieces whose offsets (Y, X words) the boss
// writes into the shared work bytes DBE6-DC45: four groups of six, a group
// being two rows of three. Byte DC3E/DC40 (the last group's end) is where
// the arm holds the machine. Falling rocks (type 36, bank 1E, B63E-B6AE;
// the parameter + 1 its state) drop past the screen.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 98E1: per piece of the lowered arm its final Y (even bytes) and its X
// offset (odd bytes).
constexpr Table arm_rest{0x17, 0x98E1};
constexpr unsigned group_bytes = 0x18;

std::uint16_t word_at(const Object& o, unsigned i) {
    return std::uint16_t(o.scratch[i] | o.scratch[i + 1] << 8);
}

void set_word(Object& o, unsigned i, std::uint16_t v) {
    o.scratch[i] = lo(v);
    o.scratch[i + 1] = hi(v);
}
} // namespace

std::uint16_t Game::work_word(unsigned offset) const {
    return std::uint16_t(act.work[offset] | act.work[offset + 1] << 8);
}

void Game::set_work_word(unsigned offset, std::uint16_t v) {
    act.work[offset] = lo(v);
    act.work[offset + 1] = hi(v);
}

Object& Game::jungle_boss() {
    return objects[act.boss ? act.boss - 1u : 0];
}

bool Game::run_jungle_boss_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::jungle_start: // 93CB
        jungle_start(o);
        return true;
    case Routine::jungle_arrive: // 94BF: on screen: the camera to the arena, the arm (4)
        if (o.display & display_flag::offscreen) {
            return true;
        }
        o.next_state = 0x04;
        script_camera(0x04C9, 0x02CE); // 4CDA (0321)
        sound_.request(0x8A, 1);
        act.variant = u8(flow.zone + 1);
        return true;
    case Routine::jungle_hold: // 94DE: held by the arm; once it has it (34): shut (5)
        hold_by_arm(o);        // 9517
        if (o.scratch[0]) {
            o.scratch[0] = 0;
            o.next_state = 0x05;
        }
        return true;
    case Routine::jungle_shut: // 94EF: solid; a hit (rolling or jumping) opens it (7), the last
                               // beats it (2)
        hold_by_arm(o);
        solid_object(o); // 51FF (036C)
        if (!(o.touch_sides & 0x0F)) {
            return true;
        }
        bounce_off(o); // 561B (0438)
        if (!(objects[0].flags & 0x02)) {
            return true;
        }
        start_animator(0x0C); // 58A7 (050A)
        o.next_state = 0x07;
        if (!--o.status_ticks) {
            hold_by_arm(o);
            o.next_state = 0x02;
        }
        return true;
    case Routine::jungle_open: // 9541: while the arm reaches (34): hit every 16 frames at most
        hold_by_arm(o);
        if (!o.scratch[0]) {
            return true;
        }
        if (o.floor_off) {
            --o.floor_off;
            return true;
        }
        solid_object(o);
        if (!(o.touch_sides & 0x0F)) {
            return true;
        }
        bounce_off(o);
        o.floor_off = 0x10;
        if (!--o.status_ticks) {
            hold_by_arm(o);
            o.next_state = 0x02;
        }
        return true;
    case Routine::jungle_fall: // 9453: falling (1/8); off screen: the end (script 9220)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_vertically_only(o); // 5379 (045F)
        if (o.display & display_flag::offscreen) {
            jump_script(o, 0x9220);
            o.frame_ticks = 0x01;
        }
        return true;
    case Routine::jungle_mark: // 9478
        o.param = 0x01;
        return true;
    case Routine::jungle_wait_arm: // 9423: the arm drawn back (status): the droppers (script 9230),
                                   // palette 1C
        if (!o.status) {
            return true;
        }
        o.placement = 0;
        act.variant = 0;
        act.boss = 0;
        jump_script(o, 0x9230);
        o.frame_ticks = 0x01;
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = 0x1C;
        o.x = 0x0548;
        return true;
    case Routine::jungle_at_player: // 928F: the dropper just spawned at the player's X
        if (script_spawned_) {
            script_spawned_->x = objects[0].x;
        }
        return true;
    case Routine::jungle_rings_gone: // 9491
        act.rings_gone = 0xFF;
        return true;
    case Routine::jungle_done: // 9497: the act's song; the camera free within the act's right limit
        if (objects[0].state == 0x1F) {
            return true;
        }
        sound_.request(0x83, 2);
        act.extra_loader = 0xFF;
        camera.act_max_y = camera.y;
        camera.max_y = camera.y;
        camera.min_x = camera.x;
        camera.max_x = camera.act_max_x;
        release_camera(); // 4CF2 (0324)
        return true;
    case Routine::jungle_end: // 9B15 (04D4)
        boss_after();         // 3F53
        return true;
    case Routine::rock_start: // 1E:B687: tiles 34, sound CE
        o.next_state = u8(o.param + 1);
        o.tile_base = o.tile_base_mirrored = 0x34;
        sound_.request(0xCE, 1);
        return true;
    case Routine::rock_fall: // B69C: falling (1/4)
        o.vy = std::uint16_t(o.vy + 0x40);
        move_vertically_only(o); // 5379 (045F)
        return true;
    case Routine::arm_start: // 9817
        arm_start(o);
        return true;
    case Routine::arm_lower: // 9790
        arm_lower(o);
        return true;
    case Routine::arm_swing: // 957F
        arm_swing(o);
        return true;
    case Routine::arm_reach_start: // 9740
        arm_reach_start(o);
        return true;
    case Routine::arm_reach: // 9697
        arm_reach(o);
        return true;
    case Routine::arm_retract_start: // 98A0: from the last group (DC2E) back, four groups, 32
                                     // frames each
        set_word(o, 0, 0xDC2E);
        set_word(o, 2, 0);
        o.timer = 0x20;
        o.counter = 0x04;
        return true;
    case Routine::arm_retract: // 9843
        arm_retract(o);
        return true;
    case Routine::jungle_dropper_start: // 98CF: from 32 below the camera's top
        o.y = std::uint16_t(camera.y + 0x0020);
        o.counter = 0;
        return true;
    case Routine::jungle_dropper_fall: // 98B9: harmful, falling (1/4)
        harmful_touch(o);              // 550E (0375)
        o.vy = std::uint16_t(o.vy + 0x40);
        move_object(o); // 5359 (0381)
        return true;
    default:
        return false;
    }
}

// 93CB: parameter 1 a dropper (D); otherwise at X 548: parameter 0 the
// machine (at Y 368, waiting: 3; the act's boss with 13 hits, the sprites'
// palette 17, the hit flash colour), others the arm (B).
void Game::jungle_start(Object& o) {
    set_word(o, 2, 0);
    o.status = 0;
    o.display |= 0x02;
    if (o.param == 0x01) {
        o.next_state = 0x0D;
        o.display &= u8(~0x02);
        return;
    }
    o.x = 0x0548;
    if (o.param) {
        o.next_state = 0x0B;
        o.scratch[0] = 0;
        return;
    }
    o.y = 0x0368;
    o.next_state = 0x03;
    act.boss = u8(slot_number(o) + 1);
    o.status_ticks = 0x0D;
    palette.fades[1].mode |= 0x20;
    palette.fades[1].palette = 0x17;
    set_boss_flash_color(); // 5888 (050D)
}

// 9517: the machine at the arm's end (DC3E, DC40) from its origin: 26
// lower, 8 left.
void Game::hold_by_arm(Object& o) {
    o.y = std::uint16_t(work_word(0x58) + o.origin_y + 0x1A);
    o.x = std::uint16_t(work_word(0x5A) + o.origin_x - 0x08);
}

// 9817: the arm at Y 2E0, its pieces at offset 0 down, their X offsets
// from 98E2.
void Game::arm_start(Object& o) {
    o.y = 0x02E0;
    for (unsigned i = 0; i < 0x18; ++i) {
        set_work_word(4 * i, 0);
        const u8 x = rom.byte(arm_rest, 1 + 2 * i);
        act.work[4 * i + 2] = x;
        act.work[4 * i + 3] = (x & 0x80) ? 0xFF : 0x00;
    }
}

// 9790: the pieces drop 6 a frame to their places (98E1); the last in
// place (DC42 at 5E): swinging (8), the machine held (34), angle 40.
void Game::arm_lower(Object& o) {
    for (unsigned i = 0; i < 0x18; ++i) {
        const std::uint16_t y = std::uint16_t(work_word(4 * i) + 6);
        const u8 rest = rom.byte(arm_rest, 2 * i);
        set_work_word(4 * i, y > rest ? rest : y);
    }
    if (work_word(0x5C) != rom.byte(arm_rest, 0x2E)) {
        return;
    }
    o.next_state = 0x08;
    jungle_boss().scratch[0] = 0xFF;
    o.angle = 0x40;
    o.counter = 0;
}

// 966E: the steps between groups at `angle` (9911): Y plus the reach
// (36), X. The X step's high byte is read with only the address's low
// byte advanced (INC L), as the original does. The Y step is kept at D100,
// the byte the terrain probe also uses (probe.attribute).
Game::ArmSweep Game::arm_steps_at(const Object& o, u8 angle) {
    const unsigned a = 0x9911 + angle * 4u;
    ArmSweep s;
    s.dy = std::uint16_t(rom.byte(0x17, a) | rom.byte(0x17, a + 1) << 8);
    s.dy = std::uint16_t(s.dy + word_at(o, 2));
    const unsigned b = a + 2;
    const unsigned b_high = (b & 0xFF00) | ((b + 1) & 0xFF);
    s.dx = std::uint16_t(rom.byte(0x17, b) | rom.byte(0x17, b_high) << 8);
    probe.attribute = lo(s.dy);
    return s;
}

// 95F0: the next group (at work offset `group`): its Y advanced by the
// step, the rows 16 and 32 below; its X advanced, the pieces 12 and 4
// left and 4 right.
void Game::arm_group(unsigned group, ArmSweep& s) {
    s.y = std::uint16_t(s.y + s.dy);
    for (unsigned i = 0; i < 3; ++i) {
        set_work_word(group + 4 * i, std::uint16_t(s.y + 0x10));
        set_work_word(group + 0x0C + 4 * i, std::uint16_t(s.y + 0x20));
    }
    s.x = std::uint16_t(s.x + s.dx);
    for (unsigned row = 0; row < 2; ++row) {
        const unsigned at = group + 2 + row * 0x0C;
        set_work_word(at, std::uint16_t(s.x - 0x0C));
        set_work_word(at + 4, std::uint16_t(s.x - 0x04));
        set_work_word(at + 8, std::uint16_t(s.x + 0x04));
    }
}

// 957F: swinging between angles 10 and 70 (faster, 3, between 20 and 60);
// groups 1-3 follow; the machine's angle the arm's. The machine open (7):
// reaching (A); beaten (2): drawing back (C).
void Game::arm_swing(Object& o) {
    if (jungle_boss().next_state == 0x02) { // 956F
        o.next_state = 0x0C;
    }
    if (jungle_boss().next_state == 0x07) {
        o.next_state = 0x0A;
        return;
    }
    const u8 step = (o.angle >= 0x20 && o.angle < 0x60) ? 3 : 1;
    if (!(o.counter & 0x01)) {
        o.angle = u8(o.angle + step);
        if (o.angle >= 0x70) {
            ++o.counter;
        }
    } else {
        o.angle = u8(o.angle - step);
        if (o.angle < 0x10) {
            ++o.counter;
        }
    }
    ArmSweep s = arm_steps_at(o, o.angle);
    s.y = 0xFFF0;
    s.x = 0;
    for (unsigned g = 1; g <= 3; ++g) {
        arm_group(g * group_bytes, s);
    }
    jungle_boss().angle = o.angle;
}

// 9740: reaching: the reach from 0, 24 frames' pause, the target angle by
// how far right the player is (60 left of 509 down to 20 right of 551).
void Game::arm_reach_start(Object& o) {
    set_word(o, 2, 0);
    o.counter = 0x18;
    o.timer = 0;
    o.scratch[4] = 0;
    jungle_boss().scratch[0] = 0;
    u8 target = 0x60;
    std::uint16_t d = std::uint16_t(objects[0].x - 0x0509);
    if (!(objects[0].x < 0x0509)) {
        target = 0x50;
        for (const u8 t : {u8(0x40), u8(0x30), u8(0x20), u8(0x20)}) {
            if (d < 0x18) {
                break;
            }
            d = std::uint16_t(d - 0x18);
            target = t;
        }
    }
    o.scratch[0] = target;
}

// 9697: drawing in by 1 for 16 frames, a pause, then out to the target
// angle by 2 for 16 frames (the machine open meanwhile: its 34), back by 2
// to 0: swinging again (8), the machine shut (5).
void Game::arm_reach(Object& o) {
    if (jungle_boss().next_state == 0x02) { // 956F
        o.next_state = 0x0C;
    }
    if (o.timer < 0x10) {
        ++o.timer;
        set_word(o, 2, std::uint16_t(word_at(o, 2) - 1));
    } else if (o.counter) {
        --o.counter;
    } else {
        o.angle = o.scratch[0];
        if (o.scratch[4] < 0x10) {
            ++o.scratch[4];
            set_word(o, 2, std::uint16_t(word_at(o, 2) + 2));
            jungle_boss().scratch[0] = 0xFF;
        } else {
            const std::uint16_t reach = std::uint16_t(word_at(o, 2) - 2);
            set_word(o, 2, reach);
            if (!reach) {
                o.next_state = 0x08;
                jungle_boss().next_state = 0x05;
                return;
            }
        }
    }
    // 9708: group 1 at the arm's angle, groups 2-3 at the target's.
    ArmSweep s = arm_steps_at(o, o.angle);
    s.y = 0xFFF0;
    s.x = 0;
    arm_group(1 * group_bytes, s);
    const ArmSweep t = arm_steps_at(o, o.scratch[0]);
    s.dy = t.dy;
    s.dx = t.dx;
    arm_group(2 * group_bytes, s);
    arm_group(3 * group_bytes, s);
}

// 9843: drawing back: the group the pointer (34) names moves in by 4 a
// frame; every 32 frames the group before; after four: gone, the machine
// told (its status FF).
void Game::arm_retract(Object& o) {
    set_word(o, 2, std::uint16_t(word_at(o, 2) + 4));
    ArmSweep s = arm_steps_at(o, o.angle);
    s.y = 0xFFF0;
    s.x = 0;
    arm_group(unsigned(word_at(o, 0) - 0xDBE6), s);
    if (--o.timer) {
        return;
    }
    o.timer = 0x20;
    set_word(o, 0, std::uint16_t(word_at(o, 0) - group_bytes));
    set_word(o, 2, 0);
    if (--o.counter) {
        return;
    }
    o.type = 0xFF;
    jungle_boss().status = 0xFF;
}

} // namespace triple_trouble_gg
