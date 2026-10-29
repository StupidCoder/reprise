// Routines named by object scripts (routines.hpp), and the player's checks
// that run after its routine every frame.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

// Runs a script routine; the carry flag it leaves (routine_carry_) is what
// the script command 8 (5D97) tests. Routines that wait for a frame run
// as coroutines; the rest at once (run_routine_now).
Flow Game::run_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::sign_result:
        routine_carry_ = false;
        co_await sign_result(o); // 1E:9AA3
        co_return;
    case Routine::rival_scene_camera:
        routine_carry_ = false;
        co_await rival_scene_camera(o);
        co_return;
    case Routine::stage_emerald_start:
        routine_carry_ = false;
        co_await stage_emerald_start(o);
        co_return;
    case Routine::flight_title_end:
        routine_carry_ = false;
        co_await flight_title_end(o);
        co_return;
    case Routine::glider_meet:
        routine_carry_ = false;
        co_await glider_meet(o);
        co_return;
    case Routine::knuckles_appear:
        routine_carry_ = false;
        co_await knuckles_appear(o);
        co_return;
    case Routine::guardian_fly_off:
        routine_carry_ = false;
        co_await guardian_fly_off(o);
        co_return;
    case Routine::knuckles_gone:
        routine_carry_ = false;
        co_await knuckles_gone(o);
        co_return;
    case Routine::sign_fall_away:
        routine_carry_ = false;
        co_await sign_fall_away(o);
        co_return;
    case Routine::cage_art:
        routine_carry_ = false;
        co_await cage_art(o);
        co_return;
    case Routine::machine_art:
        routine_carry_ = false;
        co_await machine_art(o);
        co_return;
    case Routine::machine_core_art:
        routine_carry_ = false;
        co_await machine_core_art(o);
        co_return;
    case Routine::sparkle_power:
        routine_carry_ = false;
        co_await sparkle_power(o);
        co_return;
    case Routine::jet_fly:
        routine_carry_ = false;
        co_await jet_fly(o);
        co_return;
    case Routine::jet_end:
        routine_carry_ = false;
        co_await jet_end(o);
        co_return;
    case Routine::train_ride:
        routine_carry_ = false;
        co_await train_ride(o);
        co_return;
    case Routine::train_jump:
        routine_carry_ = false;
        co_await train_jump(o);
        co_return;
    case Routine::train_fall:
        routine_carry_ = false;
        co_await train_fall(o);
        co_return;
    case Routine::train_thrown:
        routine_carry_ = false;
        co_await train_thrown(o);
        co_return;
    default:
        routine_carry_ = run_routine_now(o, r);
        co_return;
    }
}

bool Game::run_routine_now(Object& o, Routine r) {
    if (run_player_routine(o, r) || run_box_routine(o, r) || run_enemy_routine(o, r) ||
        run_explosion_routine(o, r) || run_spring_routine(o, r) || run_platform_routine(o, r) ||
        run_card_routine(o, r) || run_crawler_routine(o, r) || run_board_routine(o, r) ||
        run_train_routine(o, r) || run_rival_routine(o, r) || run_winter_boss_routine(o, r) ||
        run_jet_routine(o, r) || run_destroyer_boss_routine(o, r) ||
        run_final_machine_routine(o, r) || run_destroyer_end_routine(o, r) ||
        run_ending_player_routine(o, r) || run_flight_routine(o, r) || run_credits_routine(o, r) ||
        run_special_guardian_routine(o, r) || run_jungle_boss_routine(o, r) ||
        run_tidal_boss_routine(o, r) || run_special_routine(o, r) || run_sparkle_routine(o, r) ||
        run_sign_routine(o, r) || run_boss_routine(o, r) || run_shell_routine(o, r) ||
        run_ring_routine(o, r) || run_water_routine(o, r) || run_barrel_routine(o, r) ||
        run_sound_test_routine(o, r)) {
        return false;
    }
    switch (r) {
    case Routine::spring_flag_set:
        return player.spring_flag & 0x01; // 8190
    case Routine::none:
        return false;
    case Routine::vanish:
        o.type = 0xFF; // 50D1
        return false;
    case Routine::destroy_if_attacked:
        destroy_if_attacked(o);
        return false;
    case Routine::harmful_touch:
        harmful_touch(o);
        return false;
    case Routine::move:
        move_object(o);
        return false;
    case Routine::logo_sonic_start:
        logo_sonic_start(o);
        return false;
    case Routine::sonic_start:
        sonic_start(o);
        return false;
    case Routine::terrain:
        object_terrain(o);
        return false;
    case Routine::enable_map:
        enable_map();
        return false;
    case Routine::disable_map:
        disable_map();
        return false;
    case Routine::sonic_intro_wait:
        sonic_intro_wait(o);
        return false;
    case Routine::sonic_intro_start:
        sonic_intro_start(o);
        return false;
    case Routine::sonic_intro_run:
        sonic_intro_run(o);
        return false;
    case Routine::tails_intro_start:
        tails_intro_start(o);
        return false;
    case Routine::tails_intro_fly:
        tails_intro_fly(o);
        return false;
    case Routine::emerald_start:
        emerald_start(o);
        return false;
    case Routine::emerald_drop:
        emerald_drop(o);
        return false;
    case Routine::emerald_signal:
        objects[9].scratch[0] = 0xFF; // D774: Knuckles (slot 9) starts
        return false;
    case Routine::emerald_wait:
        emerald_taken(o);
        return false;
    case Routine::emerald_fall:
        emerald_fall(o);
        return false;
    case Routine::emerald_toss:
        emerald_toss(o);
        return false;
    case Routine::knuckles_start:
        knuckles_start(o);
        return false;
    case Routine::knuckles_intro_place:
        knuckles_intro_place(o);
        return false;
    case Routine::knuckles_intro_wait:
        knuckles_intro_wait(o);
        return false;
    case Routine::knuckles_intro_hop:
        knuckles_intro_hop(o);
        return false;
    case Routine::knuckles_intro_signal:
        objects[0].scratch[0] = 0xFF; // D534: Sonic starts running
        return false;
    case Routine::knuckles_intro_glow:
        palette.fade_period = 7;
        palette.fades[0] = {0x80, 0x27};
        return false;
    case Routine::knuckles_intro_rise:
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        return false;
    case Routine::knuckles_intro_fly:
        knuckles_intro_fly(o);
        return false;
    case Routine::sparkle_start:
        sparkle_start(o);
        return false;
    case Routine::sparkle_trail_left: // 1E:B913 (type 39, parameter FD): tiles D4, drifting left at
                                      // 1
    case Routine::sparkle_trail_right: // 1E:B929 (parameter FC): the same to the right
        o.tile_base = o.tile_base_mirrored = 0xD4;
        o.vx = r == Routine::sparkle_trail_left ? 0xFF00 : 0x0100;
        o.vy = 0;
        o.display &= u8(~display_flag::mirrored);
        return false;
    case Routine::sparkle_drift:
        move_object(o);
        return false;
    case Routine::sparkle_end:
        o.type = 0xFF;
        return false;
    case Routine::bumper_lit:
        bumper_light(o, 0x86);
        return false;
    case Routine::bumper_dim:
        bumper_light(o, 0x87);
        return false;
    case Routine::bumper_wait:
        if (o.display & display_flag::offscreen) {
            bumper_light(o, 0x00);
        }
        return false;
    case Routine::bumper_off:
        bumper_light(o, 0x00);
        return false;
    case Routine::switch_start:
        o.next_state = 0x01;
        o.display |= 0x02;
        return false;
    case Routine::switch_wait:
        switch_wait(o);
        return false;
    case Routine::switch_release:
        switch_release(o);
        return false;
    case Routine::ship_intro_start:
        ship_intro_start(o);
        return false;
    case Routine::ship_intro_colors:
        palette.fades[1].mode |= 0x20; // set at once
        palette.fades[1].palette = 0x28;
        return false;
    case Routine::ship_intro_move:
        move_object(o);
        if (o.x >= 0x0510) {
            o.next_state = 0x02;
        }
        return false;
    case Routine::knuckles_title_start:
        knuckles_title_start(o);
        return false;
    case Routine::knuckles_title_run:
        knuckles_title_run(o);
        return false;
    case Routine::knuckles_title_jump:
        knuckles_title_jump(o);
        return false;
    case Routine::knuckles_title_up:
        animation.animators[0].type = 0x0B; // B610: "PRESS START" blinks; the title takes input
        return false;
    case Routine::knuckles_title_fall:
        knuckles_title_fall(o);
        return false;
    case Routine::emblem_start:
        emblem_start(o);
        return false;
    case Routine::ship_intro_rise:
        o.vy = std::uint16_t(o.vy - 0x20);
        move_object(o);
        return false;
    case Routine::unknown:
    default:
        break;
    }
    throw Unrecovered("routine " + std::to_string(int(r)));
}

// 0C:893E: on the SEGA logo Sonic starts at the right (X F0, Y 70) facing
// left, running 4 pixels per frame. (Only the positions' low bytes are
// set; the logo clears the rest before.)
void Game::logo_sonic_start(Object& o) {
    o.display |= display_flag::mirrored;
    auto& p = objects[0];
    p.vx = 0xFC00;
    p.x = std::uint16_t((p.x & 0xFF00) | 0xF0);
    p.y = std::uint16_t((p.y & 0xFF00) | 0x70);
}

// 0C:97BC: the player's first state: chosen by what is running.
void Game::sonic_start(Object& o) {
    if (flow.mode == 0x0D) {
        throw Unrecovered("player start in mode 0D (9838)");
    }
    if (flow.mode == mode::intro) {
        sonic_intro_place(o); // 985D
        return;
    }
    if (flow.zone == 0x10) {
        o.next_state = 0x3A; // 9833: the SEGA logo's run
        return;
    }
    player_start_in_act(o); // 97CF
}

// 3484-3497: after the player's routine: water (0C:BA5D), harm (37B5),
// falling off the act (0C:AFDE) and, every other frame, the water surface
// (0C:AF82).
Flow Game::player_frame_checks() {
    auto& p = objects[0];
    player_air(p);              // BA5D
    co_await player_contact(p); // 37B5
    // AFDE
    const u8 m = flow.mode;
    if (m != 0x00 && m != 0x01 && m != 0x02 && m != 0x11) {
        co_await player_fall_check(p);
    }
    if (!(video.frame_counter & 1)) {
        water_surface(p); // AF82
    }
}

// 0C:AFDE: when the clock ran out, or the player is below the camera's Y
// + D0 (with act variant 3 or in zone 3's act 3: 120 lines below the
// screen's top), he dies (03DE); in zone 6 the act ends instead.
Flow Game::player_fall_check(Object& p) {
    if (act.time_over & 0x80) {
        co_await kill_player(p); // 03DE (3942)
        co_return;
    }
    bool fell;
    if (flow.zone >= 6) {
        // AFFC: zones 6 on: below the act's lowest camera point + D0.
        fell = std::uint16_t(camera.act_max_y + 0xD0) < p.y;
    } else if (p.screen_y & 0x8000) {
        co_return;
    } else if (act.variant == 0x03 || (flow.zone == 3 && flow.act == 2)) {
        fell = p.screen_y >= 0x0120; // B03B
    } else {
        fell = std::uint16_t(camera.y + 0xD0) < p.y;
    }
    if (!fell) {
        co_return;
    }
    // B04B
    if (flow.zone == 6) {
        flow.frame_work |= 0x04;
        co_return;
    }
    co_await kill_player(p);
}

// 0C:AF82: entering the water (16 pixels below the player's centre):
// flags bit 2, a splash (type 24) outside zone 2, sound BD, top speed 3.
// Zone 4 act 3 is all water.
void Game::water_surface(Object& p) {
    if (flow.zone == 4 && flow.act == 2) {
        p.flags |= 0x04;
        return;
    }
    if (!act.water_level) {
        p.flags &= u8(~0x04);
        return;
    }
    const std::uint16_t y = std::uint16_t(p.y - 0x10);
    if (y & 0x8000) {
        return;
    }
    if (y < act.water_level) {
        p.flags &= u8(~0x04);
        return;
    }
    if (p.flags & 0x04) {
        return;
    }
    p.flags |= 0x04;
    if (flow.zone != 2) {
        spawn_object_at(p, 0x24, 0x00, p.x, p.y); // 0357
        sound_.request(0xBD, 1);
    }
    player.top_speed = 0x0300;
}

// 0C:BA5D: under water a bubble every 120 frames and, after 8 (14) of
// them, the drowning count (type 11); above it, skimming the water when
// running into it fast enough (BB1E).
void Game::player_air(Object& p) {
    const u8 s = p.state;
    if (s == 0x41 || s == 0x29 || s == 0x48 || s == 0x26) {
        return;
    }
    if (!(flow.zone == 4 && flow.act == 2)) {
        if (!act.water_level) {
            return;
        }
        if (!(p.flags & 0x04)) {
            skim_water(p); // BB1E
            return;
        }
    }
    // BA87
    player.top_speed = 0x0400;
    if (s == 0x29 || s == 0x48 || s == 0x41) {
        return;
    }
    if (flow.zone == 0 && flow.act == 2) {
        player.air_count = 0;
        return;
    }
    if (++player.air_ticks < 0x78) {
        return;
    }
    player.air_ticks = 0;
    if (flow.zone != 2) {
        spawn_object_at(p, 0x24, 0x01, p.x, p.y); // BADC: a bubble
    }
    ++player.air_count;
    const u8 limit = (act.variant == 0x05 || act.variant == 0xFF) ? 0x0E : 0x08;
    if (player.air_count == limit) {
        spawn_object(p, 0x11, 0x00); // 0354: the drowning count
    }
}

// 0C:BB1E: above the water, running at 4 pixels per frame or more into it
// (falling, flags bit 1): bounced up (FD.00) with a splash (type 24, 2) and
// sound B6; from state 10 rolling (status bit 4: gravity once).
void Game::skim_water(Object& p) {
    if (p.state == 0x21) {
        return;
    }
    player.air_count = 0;
    if (act.variant) {
        return;
    }
    if (flow.zone == 2) {
        return;
    }
    if (p.vy & 0x8000) {
        return;
    }
    if (!(p.flags & 0x02)) {
        return;
    }
    if (p.y & 0x8000) {
        return;
    }
    if (std::uint16_t(act.water_level - 4) >= p.y) {
        return;
    }
    if (high_speed(p.vx) < 4) {
        return;
    }
    p.vy = 0xFD00;
    spawn_object_at(p, 0x24, 0x02, p.x, p.y);
    sound_.request(0xB6, 1);
    if (p.state == 0x10) {
        p.next_state = 0x09;
        p.status |= 0x10;
    }
}

} // namespace triple_trouble_gg
