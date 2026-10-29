// Item boxes (type 06, bank 1E, 8215-8487): a box shows its item with one
// of two art slots while on screen; broken by a rolling or jumping player
// (or one with power-up 5) it gives the item and sends up its icon. And
// the solid item block of type 67 (bank 0F, BF51-BFB2): broken by a
// rolling or jumping player it gives item 9 (Sonic) or 0F (Tails). And
// the special stage's entrance (type 0E, bank 1E, 89A2-8A87) that item 1
// brings: it circles, trailing sparkles (parameter FF), and touched takes
// the player into the special stage, noting where to come back.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 82DD: the item by the placement's parameter, for Sonic and for Tails.
constexpr Table box_items{0x1E, 0x82DD};
constexpr u8 keep_offscreen = 0x02; // display bit 1: not removed off screen (5456)
constexpr u8 ten_rings = 0x08;
} // namespace

bool Game::run_box_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::holder_start: { // 1E:964B (type 1B, item 2's holder): tiles by zone (table 9667);
                                  // held state (1)
        o.display |= 0x02;
        o.next_state = 0x01;
        const Table tiles{0x1E, std::uint16_t(0x9667 + flow.zone)};
        o.tile_base = o.tile_base_mirrored = rom.byte(tiles, 0);
        // 519B (03A2) adds HL to the player's Y: here still the table entry's address.
        follow_player(o, std::uint16_t(0x9667 + flow.zone));
        return true;
    }
    case Routine::holder_hold: // 96AF: once the player is no longer held (state 12): let go (2);
                               // item 2 ends next update
        if (objects[0].next_state == 0x12) {
            return true;
        }
        player.holder = 0;
        o.next_state = 0x02;
        if (objects[0].power_up == 0x02) {
            player.power_up_ticks = 0x0001;
            objects[0].power_up = 0;
        }
        return true;
    case Routine::holder_drop_start: // 9690: shown, falling at 2
        player.holder = 0;
        o.display &= u8(~(0x02 | display_flag::offscreen));
        o.vx = 0;
        o.vy = 0x0200;
        return true;
    case Routine::player_held: // 0C:A078 (player state 12)
        player_held(o);
        return true;
    case Routine::holder_drop: // 9672: falling (1/4); off screen gone
        if (o.display & display_flag::offscreen) {
            o.type = 0xFF;
            return true;
        }
        o.vy = std::uint16_t(o.vy + 0x40);
        move_object(o); // 5359 (0381)
        return true;
    case Routine::entrance_start: // 1E:89FE: tiles 0; parameter 0 the entrance (1), others its
                                  // sparkles (2); speed 4
        o.flags |= 0x80;
        o.tile_base = o.tile_base_mirrored = 0;
        o.next_state = o.param ? 0x02 : 0x01;
        o.speed = 0x40;
        return true;
    case Routine::entrance_circle: // 8A74: around by an eighth of a turn's 32nd
        o.angle = u8(o.angle + 0x08);
        velocity_from_angle(o); // 52E8 (032D)
        move_object(o);         // 5359 (0381)
        return true;
    case Routine::entrance_wait: // 8A1C
        touch_test(o);           // 58AC (0369)
        if (!(o.touch_sides & 0x0F)) {
            o.angle = u8(o.angle + 0x08);
            velocity_from_angle(o);
            move_object(o);
            return true;
        }
        enter_special_stage();
        return true;
    case Routine::entrance_gone: // 8A83
        o.type = 0xFF;
        return true;
    case Routine::block_item_start: // 0F:BF77: tiles E2
        o.next_state = 0x01;
        o.tile_base = 0xE2;
        o.flags |= 0x80;
        return true;
    case Routine::block_item_wait: // BF84
        solid_object(o);           // 51FF (036C)
        if ((o.touch_sides & 0x0F) && (objects[0].flags & 0x02)) {
            player.power_up_pending = objects[0].type == 0x01 ? 0x09 : 0x0F;
            destroy_object(o); // 51B1 (0366)
        }
        return true;
    case Routine::block_item_gone: // BFAA
        o.type = 0xFE;
        o.param = 0;
        return true;
    case Routine::box_start: // 828D
        box_start(o);
        return true;
    case Routine::box_wait: // 82FD
        box_wait(o);
        return true;
    case Routine::box_art_1: // 8479: the item's art list into slot 1
        object_art.queued = u8(o.scratch[0] + 0x2F);
        return true;
    case Routine::box_art_2: // 8482: into slot 2
        object_art.queued_next = u8(o.scratch[0] + 0x3F);
        return true;
    case Routine::box_shown_1: // 8325
        box_shown(o, 0);
        return true;
    case Routine::box_shown_2: // 8336
        box_shown(o, 1);
        return true;
    case Routine::destroy: // 0366 (51B1)
        destroy_object(o);
        return true;
    case Routine::icon_start: // 8466: up at 4 pixels per frame
        o.vx = 0;
        o.vy = 0xFC00;
        return true;
    case Routine::icon_end: // 844C
        icon_end(o);
        return true;
    default:
        return false;
    }
}

// 828D: a box takes its item (parameter 1 gives ten rings with D17E 5 or
// on a special entry) and waits for an art slot; parameters FF and FE are
// the icons the broken box spawns (states 6, 7: slot 1, 2).
void Game::box_start(Object& o) {
    if (o.param & 0x80) {
        o.next_state = u8(5 - o.param);
        o.display |= keep_offscreen;
        return;
    }
    if (o.param == 0x01 && (flow.emeralds == 0x05 || flow.special_entry)) {
        o.param = 0x08;
    }
    o.scratch[0] =
        rom.byte(box_items, o.param * 2u + (objects[0].type == 0x01 ? 0 : 1)); // the item
    o.param = 0x03;
    o.next_state = 0x01;
    o.tile_base = 0x44;
    o.scratch[1] = 0; // falling
}

// 82FD: on screen, the first free art slot (states 2, 3).
void Game::box_wait(Object& o) {
    if (o.display & display_flag::offscreen) {
        return;
    }
    const u8 slot = u8(slot_number(o) + 1);
    if (!object_art.box_owner[0]) {
        object_art.box_owner[0] = slot;
        o.next_state = 0x02;
        return;
    }
    if (object_art.box_owner[1]) {
        return;
    }
    object_art.box_owner[1] = slot;
    o.next_state = 0x03;
}

// 8325 / 8336 / 8347: off screen the box gives its art slot back. A box
// with nothing below falls (checked every 8 frames) until it lands. Unless
// the player rises, it is solid (51FF); a player pushing against its sides
// stops; one rolling, jumping (from above), in state 48 or 29 breaks it.
void Game::box_shown(Object& o, unsigned art) {
    if (o.display & display_flag::offscreen) {
        object_art.box_owner[art] = 0;
        o.next_state = 0x01;
        return;
    }
    if (o.scratch[1]) {
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        if (nothing_below(o)) {
            return;
        }
        o.scratch[1] = 0;
        o.vy = 0;
        object_terrain(o); // 713D (0384)
        return;
    }
    if ((++o.counter & 0x07) == 0x07 && nothing_below(o)) {
        o.scratch[1] = 0xFF;
    }
    auto& p = objects[0];
    if (p.vy & 0x8000) {
        return;
    }
    solid_object(o); // 51FF (036C)
    const u8 sides = o.touch_sides;
    if (!(sides & 0x0F)) {
        return;
    }
    if (sides & 0x0C) {
        // The pushing frames: Sonic's 60-62, Tails' 56-57.
        const u8 f = p.frame;
        if (p.type == 0x01 ? (f == 0x60 || f == 0x61 || f == 0x62) : (f == 0x56 || f == 0x57)) {
            p.vx = 0;
        }
    }
    if (p.state != 0x48 && p.state != 0x29) {
        if (sides & 0x02) {
            // 83DC: struck from below: an airborne player is knocked down
            // (2; 1/2 under water) and falls (3B62); the box stays.
            if (!(p.terrain & 0x02)) {
                p.vy = (p.flags & 0x04) ? 0x0080 : 0x0200;
                to_falling_from_flight(p); // 3B62 (03D2)
            }
            return;
        }
        if (!(p.flags & 0x20) && !(p.flags & 0x02)) {
            return;
        }
        if (sides & 0x01) {
            const u8 n = p.next_state;
            if (n == 0x0F || n == 0x10 || n == 0x15 || n == 0x1A) {
                return;
            }
        }
    }
    break_box(o); // 840F
}

// 840F: the item: ten rings at once; power-ups (and 1, 3 always) through
// D3E3 unless another power-up (other than 5, 0D) is running. The box
// spawns its icon (states 4, 5).
void Game::break_box(Object& o) {
    const u8 item = o.scratch[0];
    if (item == ten_rings) {
        box_broken(o);
        add_ten_rings(); // 0432 (3396)
        return;
    }
    if (item != 0x01 && item != 0x03) {
        auto& power = objects[0].power_up;
        if (power == 0x05 || power == 0x0D) {
            power = 0;
        } else if (power) {
            box_broken(o);
            return;
        }
    }
    player.power_up_pending = item;
    box_broken(o);
}

// 843C
void Game::box_broken(Object& o) {
    o.next_state = u8(o.state + 2);
    o.placement = 0;
}

// 844C: the icon is gone (type FE) and frees its art slot.
void Game::icon_end(Object& o) {
    o.type = 0xFE;
    o.param = 0;
    object_art.box_owner[o.state == 0x06 ? 0 : 1] = 0;
}

// 8A26: into the special stage (D17A; the act's loop ends, D144 bit 5),
// sound E1; the player untouchable; where to come back: his place, the
// camera's, the clock (D172-D17D).
void Game::enter_special_stage() {
    flow.special_entry = 0xFF;
    flow.frame_work |= 0x20;
    sound_.request(0xE1, 1);
    player.hit = 0;
    auto& p = objects[0];
    p.touch_sides = 0;
    p.touch = 0;
    p.contact = 0;
    p.flags |= 0xC0;
    flow.special_return = {p.x, p.y};
    flow.special_camera = {camera.x, camera.y};
    flow.checkpoint_time = {score.seconds, score.minutes, score.clock_extra};
}

// 0C:A078 (player state 12): held by item 2's holder. Without it he falls
// (3B6F). He steers and moves as usual; the holder keeps 16 below him (519B).
// A button lets go (B891: the holder falls) and jumps; landing he bounces
// up at 6 (sound B7).
void Game::player_held(Object& p) {
    if (!player.holder) {
        to_falling_state(p); // 3B6F (0483)
        return;
    }
    face_input(p);  // B7B6
    player_move(p); // B05B
    if (!player.holder) {
        to_falling_state(p);
        return;
    }
    follow_player(objects[player.holder - 1u], 0x0010); // A0C8: 519B (03A2)
    if (input.pressed & 0x30) {
        objects[player.holder - 1u].next_state = 0x02; // B891
        player.holder = 0;
        p.power_up = 0;
        p.flags &= u8(~0x01);
        to_jumping(p); // 3AB8 (03BA)
        return;
    }
    if (!(p.contact & 0x02)) {
        return;
    }
    p.terrain &= u8(~0x02);
    p.flags |= 0x01;
    p.vy = 0xFA00;
    p.y = std::uint16_t(p.y - 1);
    sound_.request(0xB7, 1);
}

} // namespace triple_trouble_gg
