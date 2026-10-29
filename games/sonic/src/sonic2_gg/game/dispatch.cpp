// Runs an object routine (routines.hpp).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

void Game::run_routine(Object& o, Routine routine) {
    switch (routine) {
    case Routine::none:
        return;
    case Routine::sonic_start:
        sonic_start();
        return;
    case Routine::sonic_standing:
        sonic_standing();
        return;
    case Routine::sonic_sprung:
        sonic_sprung();
        return;
    case Routine::sonic_dying:
        sonic_dying();
        return;
    case Routine::sonic_hurt:
        sonic_hurt();
        return;
    case Routine::sonic_falling:
        sonic_falling();
        return;
    case Routine::sonic_jumping:
        sonic_jumping();
        return;
    case Routine::sonic_bounced:
        sonic_bounced();
        return;
    case Routine::sonic_crouch:
        sonic_crouch();
        return;
    case Routine::sonic_look_up:
        sonic_look_up();
        return;
    case Routine::sonic_rolling:
        sonic_rolling();
        return;
    case Routine::sonic_pipe_exit:
        sonic_pipe_exit();
        return;
    case Routine::sonic_tube:
        sonic_tube();
        return;
    case Routine::sonic_pipe:
        sonic_pipe();
        return;
    case Routine::sonic_swing:
        sonic_swing();
        return;
    case Routine::sonic_in_bubble_update:
        sonic_in_bubble_update();
        return;
    case Routine::sonic_drown:
        sonic_drown();
        return;
    case Routine::sonic_glider_stand:
        sonic_glider_stand();
        return;
    case Routine::sonic_glider_run:
        sonic_glider_run();
        return;
    case Routine::sonic_glider_fly:
        sonic_glider_fly();
        return;
    case Routine::sonic_glider_climb:
        sonic_glider_climb();
        return;
    case Routine::sonic_glider_dive:
        sonic_glider_dive();
        return;
    case Routine::glider_run:
        glider_run(o);
        return;
    case Routine::sonic_act_end_run:
        sonic_act_end_run();
        return;
    case Routine::sonic_tube_frames:
        sonic_tube_frames();
        return;
    case Routine::sonic_rise_frames:
        sonic_rise_frames();
        return;
    case Routine::sonic_running:
        sonic_running();
        return;
    case Routine::sonic_floating:
        sonic_floating();
        return;
    case Routine::sonic_idle:
        sonic_idle();
        return;
    case Routine::sonic_pushed:
        sonic_pushed();
        return;
    case Routine::sonic_skid_left:
        sonic_skid(0x07);
        return;
    case Routine::sonic_skid_right:
        sonic_skid(0x08);
        return;
    case Routine::sonic_walking:
        sonic_walking();
        return;
    case Routine::sonic_launched:
        sonic_launched();
        return;
    case Routine::sonic_breathe:
        sonic_breathe();
        return;
    case Routine::sonic_balancing:
        sonic_balancing();
        return;
    case Routine::sonic_walk_animation:
        sonic_walk_animation();
        return;
    case Routine::sonic_roll_animation:
        sonic_roll_animation();
        return;
    case Routine::sonic_slide:
        sonic_slide();
        return;
    case Routine::sonic_board_cart:
        sonic_board_cart();
        return;
    case Routine::sonic_ride:
        sonic_ride();
        return;
    case Routine::sonic_riding:
        sonic_riding();
        return;
    case Routine::sonic_cart_frames:
        sonic_cart_frames();
        return;
    case Routine::flash_ring_colors:
        flash_ring_colors();
        return;
    case Routine::move_to_sonic:
        move_to_sonic(o);
        return;
    case Routine::destroy_if_attacked:
        destroy_if_attacked(o);
        return;
    case Routine::solid_object:
        solid_object(o);
        return;
    case Routine::bumper_object:
        bumper_object(o);
        return;
    case Routine::bounce_sonic_off:
        bounce_sonic_off(o);
        return;
    case Routine::hit_object_bumper:
        hit_object_bumper(o);
        return;
    case Routine::hit_object_solid:
        hit_object_solid(o);
        return;
    case Routine::push_sonic_out:
        push_sonic_out(o);
        return;
    case Routine::clear_blink:
        clear_blink(o);
        return;
    case Routine::velocity_from_angle:
        velocity_from_angle(o);
        return;
    case Routine::object_wall:
        object_wall(o);
        return;
    case Routine::face_motion:
        face_motion(o);
        return;
    case Routine::move_object:
        move_object(o);
        return;
    case Routine::touch_sonic:
        touch_sonic(o);
        return;
    case Routine::face_sonic:
        face_x(o, sonic().x);
        return;
    case Routine::explode:
        explode(o);
        return;
    case Routine::object_floor:
        object_floor(o);
        return;
    case Routine::spawn_debris:
        spawn_debris();
        return;
    case Routine::lock_camera_left:
        lock_camera_left();
        return;
    case Routine::lock_camera_right:
        lock_camera_right();
        return;
    case Routine::add_score_100:
        add_score(rom::points_100);
        return;
    case Routine::add_score_500:
        add_score(rom::points_500);
        return;
    case Routine::add_score_1000:
        add_score(rom::points_1000);
        return;
    case Routine::add_score_2000:
        add_score(rom::points_2000);
        return;
    case Routine::sparkle_start:
        sparkle_start(o);
        return;
    case Routine::debris_start:
        debris_start(o);
        return;
    case Routine::debris2_start:
        debris2_start(o);
        return;
    case Routine::shoe_image_start:
        shoe_image_start(o);
        return;
    case Routine::shoe_image_follow:
        shoe_image_follow(o);
        return;
    case Routine::star_start:
        star_start(o);
        return;
    case Routine::star_follow:
        star_follow(o);
        return;
    case Routine::debris_fall:
        debris_fall(o);
        return;
    case Routine::lost_ring_start:
        lost_ring_start(o);
        return;
    case Routine::lost_ring_move:
        lost_ring_move(o);
        return;
    case Routine::lava_ball_start:
        lava_ball_start(o);
        return;
    case Routine::lava_ball_rise:
        lava_ball_rise(o);
        return;
    case Routine::lava_ball_drop:
        lava_ball_drop(o);
        return;
    case Routine::lava_ball_wait:
        lava_ball_wait(o);
        return;
    case Routine::lava_ball_fly:
        lava_ball_fly(o);
        return;
    case Routine::explosion_repeat:
        explosion_repeat(o);
        return;
    case Routine::explosion_start:
        explosion_start(o);
        return;
    case Routine::explosion_end:
        explosion_end(o);
        return;
    case Routine::monitor_start:
        monitor_start(o);
        return;
    case Routine::monitor_appear:
        monitor_appear(o);
        return;
    case Routine::monitor_update:
        monitor_update(o);
        return;
    case Routine::sign_wait:
        sign_wait(o);
        return;
    case Routine::sign_start:
        sign_start(o);
        return;
    case Routine::sign_ready:
        sign_ready(o);
        return;
    case Routine::emerald_start:
        emerald_start(o);
        return;
    case Routine::emerald_wait:
        emerald_wait(o);
        return;
    case Routine::emerald_taken:
        emerald_taken(o);
        return;
    case Routine::sign_spin:
        sign_spin(o);
        return;
    case Routine::sign_face_short:
        sign_face_short(o);
        return;
    case Routine::sign_face_long:
        sign_face_long(o);
        return;
    case Routine::sign_finish:
        sign_finish(o);
        return;
    case Routine::rock_throw:
        rock_throw(o);
        return;
    case Routine::rock_start:
        rock_start(o);
        return;
    case Routine::rock_shake:
        rock_shake(o);
        return;
    case Routine::rock_fall:
        rock_fall(o);
        return;
    case Routine::rock_fly:
        rock_fly(o);
        return;
    case Routine::rock_wait:
        rock_wait(o);
        return;
    case Routine::intro_cloud_start:
        intro_cloud_start(o);
        return;
    case Routine::intro_cloud_move:
        intro_cloud_move(o);
        return;
    case Routine::intro_star_start:
        intro_star_start(o);
        return;
    case Routine::intro_star_move:
        intro_star_move(o);
        return;
    case Routine::intro_hill_start:
        intro_hill_start(o);
        return;
    case Routine::intro_hill_move:
        intro_hill_move(o);
        return;
    case Routine::sonic_intro_stand:
        sonic_intro_stand();
        return;
    case Routine::sonic_continue_wait:
        sonic_continue_wait();
        return;
    case Routine::sonic_face_left:
        sonic_face(true);
        return;
    case Routine::sonic_face_right:
        sonic_face(false);
        return;
    case Routine::sonic_ending_run:
        sonic_ending_run();
        return;
    case Routine::sonic_ending_arrive:
        sonic_ending_arrive();
        return;
    case Routine::sonic_ending_stop:
        sonic_ending_stop();
        return;
    case Routine::sonic_follow_remains:
        sonic_follow_remains();
        return;
    case Routine::sonic_watch_camera:
        sonic_watch_camera();
        return;
    case Routine::sonic_watch_done:
        sonic_watch_done();
        return;
    case Routine::sonic_bubble_frame_61:
        sonic_bubble_frame(0x61);
        return;
    case Routine::sonic_bubble_frame_62:
        sonic_bubble_frame(0x62);
        return;
    case Routine::air_count_start:
        air_count_start(o);
        return;
    case Routine::air_count_show:
        air_count_show(o);
        return;
    case Routine::air_count_drown:
        air_count_drown(o);
        return;
    case Routine::bubble_start:
        bubble_start(o);
        return;
    case Routine::bubble_cluster:
        bubble_cluster(o);
        return;
    case Routine::bubble_rise:
        bubble_rise(o);
        return;
    case Routine::bubble_breathe:
        bubble_breathe(o);
        return;
    case Routine::bubble_catch:
        bubble_catch(o);
        return;
    case Routine::bubble_carry:
        bubble_carry(o);
        return;
    case Routine::bubble_face:
        bubble_face(o);
        return;
    case Routine::bubble_wait:
        bubble_wait(o);
        return;
    case Routine::bubble_reset:
        bubble_reset(o);
        return;
    case Routine::bubble_from_sonic:
        bubble_from_sonic(o);
        return;
    case Routine::bubble_clear_cluster:
        bubble_clear_cluster();
        return;
    case Routine::bubble_wobble:
        bubble_wobble(o);
        return;
    case Routine::remove_self:
        o.type = object_type::removed;
        return;
    case Routine::splash_start:
        splash_start(o);
        return;
    case Routine::water_start:
        water_start(o);
        return;
    case Routine::water_rise:
        water_rise(o);
        return;
    case Routine::sonic_drop_through_190:
        sonic_drop_through(0x0190);
        return;
    case Routine::sonic_drop_through_240:
        sonic_drop_through(0x0240);
        return;
    case Routine::sonic_continue_frames:
        sonic_continue_frames();
        return;
    case Routine::sonic_continue_leave:
        sonic_continue_leave();
        return;
    case Routine::sonic_intro_run:
        sonic_intro_run();
        return;
    case Routine::fireballs_move:
        fireballs_move(o);
        return;
    case Routine::fireballs_start:
        fireballs_start(o);
        return;
    case Routine::fireballs_circle:
        fireballs_circle(o);
        return;
    case Routine::fire_start:
        fire_start(o);
        return;
    case Routine::fire_move:
        fire_move(o);
        return;
    case Routine::boss_palette_9:
        boss_palette(0x09);
        return;
    case Routine::boss_palette_10:
        boss_palette(0x0A);
        return;
    case Routine::final_boss_electric_on:
        final_boss_electric(true);
        return;
    case Routine::final_boss_electric_off:
        final_boss_electric(false);
        return;
    case Routine::clear_timer:
        o.timer = 0;
        return;
    case Routine::final_boss_lift:
        final_boss_lift(o);
        return;
    case Routine::break_block_64_above:
        break_block_at(o, 0, 0xFFC0);
        return;
    case Routine::break_block_16_above:
        break_block_at(o, 0, 0xFFF0);
        return;
    case Routine::final_boss_pause:
        final_boss_pause(o);
        return;
    case Routine::final_boss_start:
        final_boss_start(o);
        return;
    case Routine::final_boss_wait:
        final_boss_wait(o);
        return;
    case Routine::final_boss_arena:
        final_boss_arena(o);
        return;
    case Routine::final_boss_choose:
        final_boss_choose(o);
        return;
    case Routine::final_boss_shock:
        final_boss_shock(o);
        return;
    case Routine::final_boss_blink:
        final_boss_blink(o);
        return;
    case Routine::final_boss_guard:
        final_boss_guard(o);
        return;
    case Routine::final_boss_hop:
        final_boss_hop(o);
        return;
    case Routine::final_boss_run:
        final_boss_run(o);
        return;
    case Routine::final_boss_rest:
        final_boss_rest(o);
        return;
    case Routine::countdown_next:
        countdown_next(o);
        return;
    case Routine::final_boss_transform:
        final_boss_transform(o);
        return;
    case Routine::companion_signal:
        companion_signal();
        return;
    case Routine::companion_drop:
        companion_drop(o);
        return;
    case Routine::companion_fall:
        companion_fall(o);
        return;
    case Routine::companion_start:
        companion_start(o);
        return;
    case Routine::companion_close:
        companion_close(o);
        return;
    case Routine::companion_follow:
        companion_follow(o);
        return;
    case Routine::title_sonic_start:
        title_sonic_start(o);
        return;
    case Routine::title_tails_start:
        title_tails_start(o);
        return;
    case Routine::title_tails_secret:
        title_tails_secret(o);
        return;
    case Routine::intro_runner_start:
        intro_runner_start(o);
        return;
    case Routine::intro_runner_follow:
        intro_runner_follow(o);
        return;
    case Routine::intro_runner_away:
        intro_runner_away(o);
        return;
    case Routine::intro_tails_start:
        intro_tails_start(o);
        return;
    case Routine::intro_tails_follow:
        intro_tails_follow(o);
        return;
    case Routine::intro_tails_move_to_3:
        intro_tails_move(o, 0x0218, 3);
        return;
    case Routine::intro_tails_move_to_4:
        intro_tails_move(o, 0x0060, 4);
        return;
    case Routine::intro_tails_move_to_5:
        intro_tails_move(o, 0x0068, 5);
        return;
    case Routine::intro_tails_wait:
        intro_tails_wait(o);
        return;
    case Routine::intro_tails_leave:
        intro_tails_leave(o);
        return;
    case Routine::hud_slide_start:
        hud_slide_start(o);
        return;
    case Routine::hud_slide_out:
        hud_slide_out(o);
        return;
    case Routine::hud_slide_back:
        hud_slide_back(o);
        return;
    case Routine::platform_start:
        platform_start(o);
        return;
    case Routine::platform_move:
        platform_move(o);
        return;
    case Routine::platform_wait:
        platform_wait(o);
        return;
    case Routine::platform_shake:
        platform_shake(o);
        return;
    case Routine::platform_fall:
        platform_fall(o);
        return;
    case Routine::platform_swing:
        platform_swing(o);
        return;
    case Routine::platform_frame_short:
        platform_frame(o, 0x20);
        return;
    case Routine::platform_frame_long:
        platform_frame(o, 0xE0);
        return;
    case Routine::falling_block_start:
        falling_block_start(o);
        return;
    case Routine::path_platform_start:
        path_platform_start(o);
        return;
    case Routine::path_platform_ride_free:
        path_platform_ride_free(o);
        return;
    case Routine::path_platform_ride:
        path_platform_ride(o);
        return;
    case Routine::path_platform_wait:
        path_platform_wait(o);
        return;
    case Routine::shuttle_start:
        shuttle_start(o);
        return;
    case Routine::shuttle_move:
        shuttle_move(o);
        return;
    case Routine::swing_pole_start:
        swing_pole_start(o);
        return;
    case Routine::swing_pole_grab:
        swing_pole_grab(o);
        return;
    case Routine::jumper_start:
        jumper_start(o);
        return;
    case Routine::jumper_watch:
        jumper_watch(o);
        return;
    case Routine::jumper_jump:
        jumper_jump(o);
        return;
    case Routine::jumper_land:
        jumper_land(o);
        return;
    case Routine::jumper_walk:
        jumper_walk(o);
        return;
    case Routine::shaker_start:
        shaker_start(o);
        return;
    case Routine::shaker_patrol:
        shaker_patrol(o);
        return;
    case Routine::shaker_shake:
        shaker_shake(o);
        return;
    case Routine::shaker_burst:
        shaker_burst(o);
        return;
    case Routine::hopper2_start:
        hopper2_start(o);
        return;
    case Routine::hopper2_hop:
        hopper2_hop(o);
        return;
    case Routine::hopper2_turn:
        hopper2_turn(o);
        return;
    case Routine::hopper2_move:
        hopper2_move(o);
        return;
    case Routine::fireball_start:
        fireball_start(o);
        return;
    case Routine::fireball_fly:
        fireball_fly(o);
        return;
    case Routine::moving_pole_start:
        moving_pole_start(o);
        return;
    case Routine::moving_pole_update:
        moving_pole_update(o);
        return;
    case Routine::pendulum_start:
        pendulum_start(o);
        return;
    case Routine::pendulum_swing:
        pendulum_swing(o);
        return;
    case Routine::drop_block2_watch:
        drop_block2_watch(o);
        return;
    case Routine::drop_block2_shake:
        drop_block2_shake(o);
        return;
    case Routine::drop_block2_fall:
        drop_block2_fall(o);
        return;
    case Routine::drop_block2_break:
        drop_block2_break(o);
        return;
    case Routine::crawler_start:
        crawler_start(o);
        return;
    case Routine::crawler_move:
        crawler_move(o);
        return;
    case Routine::drop_block_wait:
        drop_block_wait(o);
        return;
    case Routine::drop_block_fall:
        drop_block_fall(o);
        return;
    case Routine::glider_start:
        glider_start(o);
        return;
    case Routine::glider_wait:
        glider_wait(o);
        return;
    case Routine::glider_carried:
        glider_carried(o);
        return;
    case Routine::glider_drift:
        glider_drift(o);
        return;
    case Routine::hopper_start:
        hopper_start(o);
        return;
    case Routine::hopper_watch:
        hopper_watch(o);
        return;
    case Routine::hopper_move:
        hopper_move(o);
        return;
    case Routine::hopper_charge:
        hopper_charge(o);
        return;
    case Routine::hopper_run:
        hopper_run(o);
        return;
    case Routine::hopper_approach:
        hopper_approach(o);
        return;
    case Routine::hopper_drop:
        hopper_drop(o);
        return;
    case Routine::hopper_walk:
        hopper_walk(o);
        return;
    case Routine::ball_frame_1:
        ball_frame(o, 1);
        return;
    case Routine::ball_frame_2:
        ball_frame(o, 2);
        return;
    case Routine::ball_frame_3:
        ball_frame(o, 3);
        return;
    case Routine::ball_rise:
        ball_rise(o);
        return;
    case Routine::ball_bounce:
        ball_bounce(o);
        return;
    case Routine::launcher_start:
        launcher_start(o);
        return;
    case Routine::launcher_wait:
        launcher_wait(o);
        return;
    case Routine::launcher_drop:
        launcher_drop(o);
        return;
    case Routine::shz_boss_gone:
        shz_boss_gone(o);
        return;
    case Routine::boss_add_part:
        boss_add_part();
        return;
    case Routine::shz_boss_start:
        shz_boss_start(o);
        return;
    case Routine::shz_boss_wait:
        shz_boss_wait(o);
        return;
    case Routine::shz_boss_wait_parts:
        shz_boss_wait_parts(o);
        return;
    case Routine::shz_boss_open_floor:
        shz_boss_open_floor(o);
        return;
    case Routine::shz_boss_place:
        shz_boss_place(o);
        return;
    case Routine::shz_boss_fly:
        shz_boss_fly(o);
        return;
    case Routine::shz_boss_blink:
        shz_boss_blink(o);
        return;
    case Routine::shz_flier:
        shz_flier(o);
        return;
    case Routine::minion_start:
        minion_start(o);
        return;
    case Routine::minion_rise:
        minion_rise(o);
        return;
    case Routine::minion_land:
        minion_land(o);
        return;
    case Routine::minion_wait:
        minion_wait(o);
        return;
    case Routine::minion_hop:
        minion_hop(o);
        return;
    case Routine::minion_hop_air:
        minion_hop_air(o);
        return;
    case Routine::minion_walk:
        minion_walk(o);
        return;
    case Routine::minion_jump:
        minion_jump(o);
        return;
    case Routine::trap_start:
        trap_start(o);
        return;
    case Routine::trap_wait_above:
        trap_wait_above(o);
        return;
    case Routine::trap_wait_below:
        trap_wait_below(o);
        return;
    case Routine::trap_wait_left:
        trap_wait_left(o);
        return;
    case Routine::trap_wait_right:
        trap_wait_right(o);
        return;
    case Routine::trap_fly:
        trap_fly(o);
        return;
    case Routine::fish_rest:
        fish_rest(o);
        return;
    case Routine::fish_watch:
        fish_watch(o);
        return;
    case Routine::fish_chase:
        fish_chase(o);
        return;
    case Routine::fish_return:
        fish_return(o);
        return;
    case Routine::alz_bomb_start:
        alz_bomb_start(o);
        return;
    case Routine::alz_bomb_fly:
        alz_bomb_fly(o);
        return;
    case Routine::alz_bomb_bounce:
        alz_bomb_bounce(o);
        return;
    case Routine::alz_boss_land:
        alz_boss_land(o);
        return;
    case Routine::alz_boss_leap:
        alz_boss_leap(o);
        return;
    case Routine::alz_boss_turn:
        alz_boss_turn(o);
        return;
    case Routine::alz_boss_count:
        alz_boss_count(o);
        return;
    case Routine::alz_boss_choose_jump:
        alz_boss_choose_jump(o);
        return;
    case Routine::alz_boss_to_walk:
        alz_boss_to_walk(o);
        return;
    case Routine::alz_boss_hit:
        alz_boss_hit(o);
        return;
    case Routine::alz_boss_gone:
        alz_boss_gone(o);
        return;
    case Routine::alz_boss_start:
        alz_boss_start(o);
        return;
    case Routine::alz_boss_wait:
        alz_boss_wait(o);
        return;
    case Routine::alz_boss_guard:
        alz_boss_guard(o);
        return;
    case Routine::alz_boss_jump:
        alz_boss_jump(o);
        return;
    case Routine::alz_boss_walk:
        alz_boss_walk(o);
        return;
    case Routine::alz_boss_rise:
        alz_boss_rise(o);
        return;
    case Routine::alz_boss_carry:
        alz_boss_carry(o);
        return;
    case Routine::ghz_boss_face_right:
        ghz_boss_face_right(o);
        return;
    case Routine::ghz_boss_gone:
        ghz_boss_gone(o);
        return;
    case Routine::ghz_boss_start:
        ghz_boss_start(o);
        return;
    case Routine::ghz_boss_wait:
        ghz_boss_wait(o);
        return;
    case Routine::ghz_boss_choose:
        ghz_boss_choose(o);
        return;
    case Routine::ghz_boss_hop:
        ghz_boss_hop(o);
        return;
    case Routine::ghz_boss_roll:
        ghz_boss_roll(o);
        return;
    case Routine::ghz_boss_jump_back:
        ghz_boss_jump_back(o);
        return;
    case Routine::ghz_boss_stand:
        ghz_boss_stand(o);
        return;
    case Routine::ghz_boss_guard:
        ghz_boss_guard(o);
        return;
    case Routine::ghz_boss_shake:
        ghz_boss_shake();
        return;
    case Routine::boss_bumper:
        boss_bumper(o);
        return;
    case Routine::boss_solid_hit:
        boss_solid_hit(o);
        return;
    case Routine::gmz_boss_start:
        gmz_boss_start(o);
        return;
    case Routine::gmz_boss_wait:
        gmz_boss_wait(o);
        return;
    case Routine::gmz_boss_approach:
        gmz_boss_approach(o);
        return;
    case Routine::gmz_boss_roll:
        gmz_boss_roll(o);
        return;
    case Routine::gmz_boss_stand:
        gmz_boss_stand(o);
        return;
    case Routine::gmz_boss_aim:
        gmz_boss_aim(o);
        return;
    case Routine::gmz_boss_jump:
        gmz_boss_jump(o);
        return;
    case Routine::gmz_boss_gone:
        gmz_boss_gone(o);
        return;
    case Routine::gmz_boss_turn:
        gmz_boss_turn(o);
        return;
    case Routine::gmz_boss_blink:
        gmz_boss_blink(o);
        return;
    case Routine::boulder_start:
        boulder_start(o);
        return;
    case Routine::boulder_fall:
        boulder_fall(o);
        return;
    case Routine::sez_boss_start:
        sez_boss_start(o);
        return;
    case Routine::sez_boss_gone_zone_complete:
        sez_boss_gone(o, true);
        return;
    case Routine::sez_boss_gone:
        sez_boss_gone(o, false);
        return;
    case Routine::sez_boss_wait:
        sez_boss_wait(o);
        return;
    case Routine::sez_boss_choose:
        sez_boss_choose(o);
        return;
    case Routine::sez_boss_jump:
        sez_boss_jump(o);
        return;
    case Routine::sez_boss_hop:
        sez_boss_hop(o);
        return;
    case Routine::sez_boss_dash:
        sez_boss_dash(o);
        return;
    case Routine::sez_boss_charge:
        sez_boss_charge(o);
        return;
    case Routine::sez_boss_leap:
        sez_boss_leap(o);
        return;
    case Routine::sez_boss_charge2:
        sez_boss_charge2(o);
        return;
    case Routine::sez_boss_bounce:
        sez_boss_bounce(o);
        return;
    case Routine::sez_boss_guard:
        sez_boss_guard(o);
        return;
    case Routine::capsule_descend:
        capsule_descend(o);
        return;
    case Routine::capsule_wait:
        capsule_wait(o);
        return;
    case Routine::capsule_open:
        capsule_open(o);
        return;
    case Routine::capsule_hover_down_right:
        capsule_hover(o, true, true);
        return;
    case Routine::capsule_hover_up_left:
        capsule_hover(o, false, false);
        return;
    case Routine::capsule_hover_up_right:
        capsule_hover(o, false, true);
        return;
    case Routine::capsule_hover_down_left:
        capsule_hover(o, true, false);
        return;
    case Routine::capsule_free_animal:
        capsule_free_animal(o);
        return;
    case Routine::animal_start:
        animal_start(o);
        return;
    case Routine::animal_frame_first:
        animal_frame(o, false);
        return;
    case Routine::animal_frame_second:
        animal_frame(o, true);
        return;
    case Routine::animal_fall:
        animal_fall(o);
        return;
    case Routine::animal_hop:
        animal_hop(o);
        return;
    case Routine::ugz_bomb_throw:
        ugz_bomb_throw(o);
        return;
    case Routine::ugz_bomb_fly:
        ugz_bomb_fly(o);
        return;
    case Routine::ugz_boss_wait:
        ugz_boss_wait(o);
        return;
    case Routine::ugz_boss_enter:
        ugz_boss_enter(o);
        return;
    case Routine::ugz_boss_approach:
        ugz_boss_approach(o);
        return;
    case Routine::ugz_boss_swoop:
        ugz_boss_swoop(o);
        return;
    case Routine::ugz_boss_grab:
        ugz_boss_grab(o);
        return;
    case Routine::ugz_boss_lift:
        ugz_boss_lift(o);
        return;
    case Routine::ugz_boss_carry_right:
        ugz_boss_carry_right(o);
        return;
    case Routine::ugz_boss_set_down:
        ugz_boss_set_down(o);
        return;
    case Routine::ugz_boss_to_post:
        ugz_boss_to_post(o);
        return;
    case Routine::ugz_boss_fall:
        ugz_boss_fall(o);
        return;
    case Routine::ugz_boss_fly_off:
        ugz_boss_fly_off(o);
        return;
    case Routine::ugz_target_start:
        ugz_target_start(o);
        return;
    case Routine::ugz_target_frame_1:
        ugz_target_frame(o, 1);
        return;
    case Routine::ugz_target_frame_2:
        ugz_target_frame(o, 2);
        return;
    case Routine::ugz_target_hit:
        ugz_target_hit(o);
        return;
    case Routine::ugz_target_gone:
        ugz_target_gone(o);
        return;
    case Routine::bomb_walker_start:
        bomb_walker_start(o);
        return;
    case Routine::bomb_walker_right:
        bomb_walker_right(o);
        return;
    case Routine::bomb_walker_left:
        bomb_walker_left(o);
        return;
    case Routine::bomb_walker_walk_right:
        bomb_walker_walk_right(o);
        return;
    case Routine::bomb_walker_walk_left:
        bomb_walker_walk_left(o);
        return;
    case Routine::bomb_walker_throw:
        bomb_walker_throw(o);
        return;
    case Routine::bomb_start:
        bomb_start(o);
        return;
    case Routine::bomb_fly:
        bomb_fly(o);
        return;
    case Routine::cart_start:
        cart_start(o);
        return;
    case Routine::cart_parked:
        cart_parked(o);
        return;
    case Routine::cart_roll_right:
        cart_roll(o, false);
        return;
    case Routine::cart_roll_left:
        cart_roll(o, true);
        return;
    case Routine::cart_fall:
        cart_fall(o);
        return;
    case Routine::cart_broken:
        cart_broken(o);
        return;
    case Routine::cart_gone:
        cart_gone(o);
        return;
    case Routine::pacer_start:
        pacer_start(o);
        return;
    case Routine::pacer_walk_right:
        pacer_walk(o, false);
        return;
    case Routine::pacer_walk_left:
        pacer_walk(o, true);
        return;
    case Routine::pacer_turn_left:
        pacer_turn(o, true);
        return;
    case Routine::pacer_turn_right:
        pacer_turn(o, false);
        return;
    case Routine::pacer_creep_left:
        pacer_creep(o, true);
        return;
    case Routine::pacer_creep_right:
        pacer_creep(o, false);
        return;
    case Routine::pacer_charge_right:
        pacer_charge(o, false);
        return;
    case Routine::pacer_charge_left:
        pacer_charge(o, true);
        return;
    case Routine::turret_start:
        turret_start(o);
        return;
    case Routine::turret_watch:
        turret_watch(o);
        return;
    case Routine::turret_fire:
        turret_fire(o);
        return;
    case Routine::turret_remove:
        turret_remove(o);
        return;
    case Routine::shot_start:
        shot_start(o);
        return;
    case Routine::shot_fly:
        shot_fly(o);
        return;
    }
}

} // namespace sonic2_gg
