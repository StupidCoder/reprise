#pragma once
#include <cstdint>

// The routines object scripts run (each step's routine and the commands
// that call one). The cartridge names them by address: banks 0 and 1 (mostly
// through the vectors 0300-0510) and the object type's bank; the decoded
// scripts name them by these values (routine_table.cpp has the addresses).
namespace triple_trouble_gg {

enum class Routine : std::uint16_t {
    none,             // 52C4: RET
    unknown,          // an address the reconstruction does not know yet
    move,             // 5359: position += speed
    logo_sonic_start, // 0C:893E
    sonic_start,      // 0C:97BC
    terrain,          // 713D (0384)
    enable_map,       // 4CC3 (030F)
    disable_map,      // 4CCE (0312)
    // The intro (mode 1).
    sonic_intro_wait,      // 0C:88B0
    sonic_intro_start,     // 0C:88C2
    sonic_intro_run,       // 0C:88D6
    tails_intro_start,     // 0C:950F
    tails_intro_fly,       // 0C:9530
    emerald_start,         // 1E:ADE6
    emerald_drop,          // 1E:AE86
    emerald_signal,        // 1E:AED2
    emerald_wait,          // 1E:AEA6
    emerald_fall,          // 1E:AEAA
    emerald_toss,          // 1E:ADB7
    knuckles_start,        // 1E:B462
    knuckles_intro_place,  // 1E:B311
    knuckles_intro_wait,   // 1E:B32B
    knuckles_intro_hop,    // 1E:B356
    knuckles_intro_signal, // 1E:B3BB
    knuckles_intro_glow,   // 1E:B3C1
    knuckles_intro_rise,   // 1E:B3D1
    knuckles_intro_fly,    // 1E:B3E5
    sparkle_start,         // 1E:B7EF
    sparkle_drift,         // 1E:B881
    sparkle_end,           // 1E:B898
    bumper_lit,            // 1E:B8C7
    bumper_dim,            // 1E:B8ED
    bumper_wait,           // 1E:B89D
    bumper_off,            // 1E:B8A4
    switch_start,          // 1E:BF4C (type 3F)
    switch_wait,           // 1E:BF55
    switch_release,        // 1E:BF68
    ship_intro_start,      // 17:8B6D
    ship_intro_colors,     // 17:8BC7
    ship_intro_move,       // 17:8B9D
    ship_intro_rise,       // 17:8BB3
    // The title (mode 2).
    knuckles_title_start, // 1E:B5B7
    knuckles_title_run,   // 1E:B5D4
    knuckles_title_jump,  // 1E:B601
    knuckles_title_up,    // 1E:B610
    knuckles_title_fall,  // 1E:B616
    emblem_start,         // 1E:8B11
    // The player (bank 0C).
    player_standing,          // 98E8
    player_looking_up,        // 9945
    player_crouching,         // 998C
    player_walking,           // 99B6
    player_running,           // 9A7C
    player_skidding_right,    // 9AAE
    player_skidding_left,     // 9AD9
    player_rolling,           // 9B04
    player_jumping,           // 9B40
    player_sprung,            // 9B8E
    player_air_spin,          // A509
    player_balancing,         // A165
    flight_start,             // 90E9
    card_run_start,           // 898D
    hurt_start,               // 83CC
    float_start,              // 866E
    victory_start,            // A8D2
    pipe_travel,              // BD46
    pipe_chamber,             // BD69
    pipe_choose,              // BD4D
    pipe_out,                 // BDC6
    pipe_away,                // BDBB
    pipe_done,                // 85BD
    pipe_fly,                 // BDF9
    face_right,               // A915
    victory_run,              // A91A
    run_off,                  // 84FF
    player_sprung_diagonally, // 9BAB
    player_bounced,           // 9B10
    player_float,             // A51C
    float_hold,               // A562
    player_float_short,       // A570
    player_hurt,              // 9CD6
    death_start,              // 843C
    drown_start,              // 8434
    player_dead,              // A18D
    player_drowned,           // A1D3
    card_run_second,          // 89A9
    card_run_third,           // 89B9
    card_run,                 // 89C5
    tails_fly,                // A0D7
    entry_drop,               // A590
    entry_slide,              // A5C4
    entry_jump,               // A60B
    entry_land,               // A64F
    path_right,               // 3542 (03E4)
    path_left,                // 36B3 (03EA)
    path_rise,                // 35B7 (03E7)
    animate_path,             // ADB7
    animate_path_fall,        // AD85
    path_fall,                // 9CBE
    to_falling_from_flight,   // 3B62 (03D2)
    player_charging,          // 9C52
    player_dashing,           // 9C8A
    player_falling,           // 9C9C
    animate_walking,          // AC6E
    animate_running,          // ACDA
    tails_walking,            // 92AE
    tails_running,            // 931A
    animate_spinning,         // ACF0
    spring_flag_set,          // 8190: carry = D3F0 bit 0
    vanish,                   // 50D1 (0336): type FF
    destroy,                  // 51B1 (0366): an explosion, sound B9
    destroy_if_attacked,      // 51B8 (0363)
    // The bouncing enemy (type 23, bank 1E).
    bouncer_start, // A047
    bouncer_walk,  // A0F7
    bouncer_touch, // A0FD
    bouncer_swim,  // A16F
    bouncer_face,  // A18C
    // The crawler (type 3E, bank 1E).
    board_start,              // 0C:A3CB (player state 23)
    board_frame_a,            // 0C:AF28 (player state 23)
    board_frame_b,            // 0C:AF55 (player state 23)
    board_ride,               // 0C:A3E9 (player state 23)
    spray_start,              // 1E:B982 (type 3A)
    spray_gone,               // 1E:B998 (type 3A)
    loose_board_start,        // 1E:B1B7 (type 34)
    loose_board_fall,         // 1E:B1CC (type 34)
    overhang_start_left,      // 0C:A6CE (player states 35, 36)
    overhang_left,            // 0C:A6D7 (player states 35, 36)
    overhang_start_right,     // 0C:A70A (player states 35, 36)
    overhang_right,           // 0C:A713 (player states 35, 36)
    overhang_drop_start,      // 0C:A746 (player state 3D)
    overhang_drop,            // 0C:A752 (player state 3D)
    blast_wait,               // 0C:A81E (player state 42)
    blast_start,              // 0C:A824 (player state 42)
    blast_rise,               // 0C:A832 (player state 42)
    blast_end,                // 0C:A84A (player state 42)
    tunnel_start,             // 0C:A76D (player states 3E, 3F)
    tunnel_run,               // 0C:A782 (player states 3E, 3F)
    chute_start,              // 0C:A7D9 (player states 3E, 3F)
    chute_slide,              // 0C:A800 (player states 3E, 3F)
    continue_start,           // 0C:8DC8 (player state 47)
    continue_wait,            // 0C:8DE1 (player state 47)
    continue_run,             // 0C:8E34 (player state 47)
    continue_fall,            // 0C:8DBA (player state 47)
    train_enter_start,        // 0C:BD18 (Sunset Park 3)
    train_enter,              // 0C:BD2B (Sunset Park 3)
    train_ride_start,         // 0C:BB86 (Sunset Park 3)
    train_ride,               // 0C:BB98 (Sunset Park 3)
    train_jump,               // 0C:BBAB (Sunset Park 3)
    train_fall,               // 0C:BBDC (Sunset Park 3)
    train_thrown_start,       // 0C:BCB8 (Sunset Park 3)
    train_thrown,             // 0C:BCC5 (Sunset Park 3)
    train_leave,              // 0C:BCF9 (Sunset Park 3)
    director_start,           // 1E:8E15 (type 13)
    director_wait,            // 1E:8E3C (type 13)
    director_landing,         // 1E:8E42 (type 13)
    director_ride_closer,     // 1E:8E54 (type 13)
    director_ride,            // 1E:8E5F (type 13)
    director_end,             // 1E:8EB3 (type 13)
    director_speed_up,        // 1E:8F35 (type 13)
    director_slow_down_start, // 1E:8FBC (type 13)
    director_slow_down,       // 1E:8FE1 (type 13)
    director_stop,            // 1E:904B (type 13)
    director_gap_open,        // 1E:907D (type 13)
    director_gap_close,       // 1E:9083 (type 13)
    director_redraw_0,        // 1E:9089 (type 13)
    director_redraw_1,        // 1E:90A1 (type 13)
    director_redraw_2,        // 1E:90B9 (type 13)
    director_redraw_3,        // 1E:90D1 (type 13)
    director_redraw_4,        // 1E:90E9 (type 13)
    director_redraw_5,        // 1E:9101 (type 13)
    director_redraw_6,        // 1E:9119 (type 13)
    director_redraw_7,        // 1E:9131 (type 13)
    director_station_0,       // 1E:914E (type 13)
    director_station_1,       // 1E:9166 (type 13)
    director_station_2,       // 1E:917E (type 13)
    director_station_3,       // 1E:9196 (type 13)
    roof_ring_start,          // 1E:9269 (type 15)
    roof_ring_roll,           // 1E:9296 (type 15)
    rival_start,              // 16:BC08 (type 8)
    rival_hold_camera,        // 16:BCCF (type 8)
    rival_next,               // 16:BCDB (type 8)
    rival_stride,             // 16:BD7D (type 8)
    rival_run_to,             // 16:BD9C (type 8)
    rival_run,                // 16:BDAE (type 8)
    rival_brake,              // 16:BDEF (type 8)
    rival_face_left,          // 16:BE1A (type 8)
    rival_face_right,         // 16:BE1F (type 8)
    rival_restart,            // 16:BE24 (type 8)
    rival_leave,              // 16:BE29 (type 8)
    rival_meet,               // 16:BE5F (type 8)
    rival_remove,             // 16:BEA2 (type 8)
    rival_scene_camera,       // 16:BEAF (type 8)
    rival_hop,                // 16:BF04 (type 8)
    rival_hop_on,             // 16:BF2D (type 8)
    rival_stamp,              // 16:BF4A (type 8)
    rival_art_back,           // 16:BF59 (type 8)
    rival_wait_boss,          // 16:BF65 (type 8)
    rival_drop,               // 16:BF94 (type 8)
    rival_pause,              // 16:BFAC (type 8)
    rival_to_corner,          // 16:BFB1 (type 8)
    rival_scatter,            // 16:BFBE (type 8)
    rival_shuffle,            // 16:BB8F (type 8)
    hold_player,              // 3F76 (04FE)
    lift_start,               // 17:8864 (type 4D)
    lift_wait,                // 17:8892 (type 4D)
    lift_ride,                // 17:88ED (type 4D)
    winter_boss_start,        // 17:9E84 (type 53)
    winter_boss_wait,         // 17:9EA8 (type 53)
    winter_boss_approach,     // 17:9EB7 (type 53)
    ball_start,               // 1E:B03A (type 32)
    ball_fall,                // 1E:B088 (type 32)
    ball_turn,                // 1E:B0AC (type 32)
    ball_roll,                // 1E:B0C5 (type 32)
    ball_throw,               // 1E:B004 (type 32)
    ball_fly,                 // 1E:B0EC (type 32)
    winter_boss_arrived,      // 17:9B7C (type 53)
    winter_boss_hold_player,  // 17:9DDE (type 53)
    winter_boss_free_camera,  // 17:9DF0 (type 53)
    winter_boss_done,         // 17:9E17 (type 53)
    winter_boss_gone,         // 17:9E37 (type 53)
    winter_boss_bob,          // 17:9EEC (type 53)
    winter_boss_swing,        // 17:9F2E (type 53)
    winter_boss_shake,        // 17:9F81 (type 53)
    winter_boss_count,        // 17:9FC7 (type 53)
    winter_boss_flee,         // 17:9FD9 (type 53)
    winter_boss_sink,         // 17:9FF6 (type 53)
    winter_part_left,         // 17:A02E (type 53)
    winter_part_right,        // 17:A048 (type 53)
    winter_part_core,         // 17:A062 (type 53)
    winter_part_core_off,     // 17:A07C (type 53)
    power_flight_start,       // 0C:82D4 (player state 11)
    jet_end,                  // 0C:82F8 (player state 26)
    jet_turn,                 // 0C:8306 (player state 26)
    jet_start,                // 0C:83AD (player state 26)
    jet_fly,                  // 0C:9D06 (player state 26)
    jet_button,               // 0C:9D0D (player state 26)
    jet_tick,                 // 0C:9D2B (player state 26)
    splash_start,             // 1E:AC5A (type 2F)
    splash_drop,              // 1E:ACA1 (type 2F)
    splash_gone,              // 1E:ACB4 (type 2F)
    block_item_start,         // 0F:BF77 (type 67)
    block_item_wait,          // 0F:BF84 (type 67)
    block_item_gone,          // 0F:BFAA (type 67)
    destroyer_beaten,         // 17:AAE0 (type 55)
    destroyer_drift,          // 17:AAFD (type 55)
    destroyer_start,          // 17:AB27 (type 55)
    destroyer_face_left,      // 17:AB40 (type 55)
    destroyer_face_right,     // 17:AB45 (type 55)
    destroyer_wait,           // 17:AB4A (type 55)
    destroyer_chase,          // 17:AB75 (type 55)
    destroyer_centre_camera,  // 17:ABC2 (type 55)
    destroyer_race_off,       // 17:ABC8 (type 55)
    destroyer_race,           // 17:ABEE (type 55)
    destroyer_arena,          // 17:ABFB (type 55)
    destroyer_swoop,          // 17:AC22 (type 55)
    destroyer_fly,            // 17:AC49 (type 55)
    destroyer_round_end,      // 17:AC82 (type 55)
    destroyer_to_middle,      // 17:AC9F (type 55)
    destroyer_charge_side,    // 17:ACB9 (type 55)
    destroyer_charge,         // 17:ACC8 (type 55)
    destroyer_return,         // 17:ACCF (type 55)
    destroyer_save_loop,      // 17:ACED (type 55)
    destroyer_restore_loop,   // 17:ACF4 (type 55)
    jungle_at_player,         // 17:928F (type 52)
    jungle_start,             // 17:93CB (type 52)
    jungle_wait_arm,          // 17:9423 (type 52)
    jungle_fall,              // 17:9453 (type 52)
    jungle_mark,              // 17:9478 (type 52)
    jungle_rings_gone,        // 17:9491 (type 52)
    jungle_done,              // 17:9497 (type 52)
    jungle_arrive,            // 17:94BF (type 52)
    jungle_hold,              // 17:94DE (type 52)
    jungle_shut,              // 17:94EF (type 52)
    jungle_open,              // 17:9541 (type 52)
    arm_swing,                // 17:957F (type 52)
    arm_reach,                // 17:9697 (type 52)
    arm_reach_start,          // 17:9740 (type 52)
    arm_lower,                // 17:9790 (type 52)
    arm_start,                // 17:9817 (type 52)
    arm_retract,              // 17:9843 (type 52)
    arm_retract_start,        // 17:98A0 (type 52)
    jungle_dropper_fall,      // 17:98B9 (type 52)
    jungle_dropper_start,     // 17:98CF (type 52)
    jungle_end,               // 17:9B15 (type 52)
    train_boss_start,         // 17:9111 (type 51)
    train_boss_shot,          // 17:9154 (type 51)
    train_boss_arrive,        // 17:9161 (type 51)
    train_boss_fight,         // 17:9168 (type 51)
    train_boss_ride,          // 17:916B (type 51)
    train_boss_beaten,        // 17:91D6 (type 51)
    exit_watch_start,         // 1E:9385 (type 17)
    exit_watch,               // 1E:938E (type 17)
    tidal_start,              // 17:A547 (type 54)
    tidal_wait,               // 17:A599 (type 54)
    tidal_variant,            // 17:A5C5 (type 54)
    tidal_enter,              // 17:A5CB (type 54)
    tidal_rise,               // 17:A5E8 (type 54)
    tidal_sink,               // 17:A60B (type 54)
    tidal_dance_start,        // 17:A644 (type 54)
    tidal_debris_fall,        // 17:A663 (type 54)
    tidal_debris_roll,        // 17:A6A5 (type 54)
    tidal_shard_fly,          // 17:A6D2 (type 54)
    tidal_drop_tiles,         // 17:A707 (type 54)
    tidal_drop_fall,          // 17:A70C (type 54)
    tidal_gone,               // 17:A727 (type 54)
    tidal_pause,              // 17:A72C (type 54)
    tidal_flash_bg,           // 17:A746 (type 54)
    tidal_white,              // 17:A74F (type 54)
    tidal_sink_away,          // 17:A761 (type 54)
    tidal_done,               // 17:A782 (type 54)
    tidal_move,               // 17:A79B (type 54)
    tidal_hit_check,          // 17:A79E (type 54)
    tidal_scroll,             // 17:A7DE (type 54)
    tidal_frame,              // 17:A821 (type 54)
    rock_start,               // 1E:B687 (type 36)
    rock_fall,                // 1E:B69C (type 36)
    entrance_start,           // 1E:89FE (type 0E)
    entrance_wait,            // 1E:8A1C (type 0E)
    entrance_circle,          // 1E:8A74 (type 0E)
    entrance_gone,            // 1E:8A83 (type 0E)
    count_start,              // 1E:8B5B (type 11)
    count_show,               // 1E:8B82 (type 11)
    count_drown,              // 1E:8BB0 (type 11)
    side_spring_start,        // 17:881B (type 4A)
    side_spring_wait,         // 17:8824 (type 4A)
    stage_emerald_start,      // 17:89DD (type 4E)
    stage_emerald_wait,       // 17:8A5B (type 4E)
    stage_emerald_song,       // 17:8A8B (type 4E)
    stage_emerald_won,        // 17:8A91 (type 4E)
    stage_emerald_fall,       // 17:8A99 (type 4E)
    stage_emerald_collect,    // 17:8AC7 (type 4E)
    player_peeling,           // 0C:9BC6 (player states 15, 1A)
    peel_run_start,           // 0C:8243 (player states 15, 1A)
    player_peel_run,          // 0C:9C06 (player states 15, 1A)
    sparkle_follow_start,     // 1E:806C (type 5)
    sparkle_power,            // 1E:80AA (type 5)
    sparkle_peel,             // 1E:80DF (type 5)
    sparkle_turn,             // 1E:813B (type 5)
    tails_fly_start,          // 0C:922A (Tails state 18)
    tails_fly_sound,          // 0C:9253 (Tails state 18)
    tails_hover,              // 0C:A238 (Tails state 18)
    hud_slide_start,          // 1E:8882 (type 0B)
    hud_slide,                // 1E:888B (type 0B)
    held_start,               // 0C:8E58 (player state 4A)
    held_wait,                // 0C:8E5D (player state 4A)
    held_fall,                // 0C:8E68 (player state 4A)
    prop_start,               // 1E:B737 (type 38)
    prop_wait,                // 1E:B754 (type 38)
    crumble_start,            // 1E:86BB (type 9)
    crumble_piece,            // 1E:86DB (type 9)
    crumble_snap,             // 1E:8706 (type 9)
    crumble_step,             // 1E:8731 (type 9)
    knuckles_act_wait,        // 1E:B4D7 (type 35)
    knuckles_act_enter,       // 1E:B4E5 (type 35)
    knuckles_act_run,         // 1E:B51C (type 35)
    knuckles_act_hop,         // 1E:B54A (type 35)
    knuckles_act_hop_back,    // 1E:B577 (type 35)
    knuckles_act_signal,      // 1E:B5A4 (type 35)
    fall_out_of_act,          // 0C:8EAB (player state 4B)
    victory_still,            // 0C:A953 (player state 44)
    victory_run_down,         // 0C:A95A (player state 44)
    victory_run_far,          // 0C:A993 (player state 44)
    crawler_start,            // BCA5
    crawler_link,             // BCF5
    crawler_variant,          // BC47
    crawler_home,             // BD05
    crawler_down,             // BD12
    crawler_up,               // BD1B
    crawler_left,             // BD24
    crawler_right,            // BD35
    crawler_segment,          // BE29
    crawler_sting,            // BE11
    crawler_head_off,         // BEAD
    crawler_segment_off,      // BEC6
    crawler_hold,             // BF06
    // The title card (type 14, bank 1E).
    card_start,     // 91D2
    card_frame,     // 91F7
    card_slide,     // 9203
    platform,       // 5669 (0396)
    stomp_check,    // 59DB (036F)
    platform_still, // 5677 (0399)
    platform_start, // 1E:A3D6 (type 25)
    step_start,     // 1E:AA74 (type 2C)
    step_up,        // 1E:AAAD
    step_down,      // 1E:AABB
    // The bobbing enemy that becomes a platform (type 27, bank 1E).
    bobber_start,    // A6DE
    bobber_move,     // A77A
    bobber_hold,     // A77D
    bobber_throw,    // A6AA
    bobber_piece,    // A798
    bobber_platform, // A7BE
    // The shell walker (type 28) and its shell (type 2D), bank 1E.
    walker_start, // A85A
    walker_walk,  // A892
    shell_ridden, // 9DFA
    shell_drop,   // 9E13
    shell_slide,  // 9E25
    shell_rest,   // 9E8A
    // The Great Turquoise boss (type 50, bank 17).
    boss_start,         // 8D92
    boss_wait,          // 8DB8
    boss_drive_in,      // 8DD9
    boss_left,          // 8DFF
    boss_right,         // 8E40
    boss_turn,          // 8EA1
    boss_turned,        // 8EA8
    boss_hit,           // 8EAF
    boss_hit_end,       // 8EBC
    boss_rise_start,    // 8EE0
    boss_rise,          // 8F0E
    boss_return_height, // 8F05
    boss_return,        // 8F36
    boss_fall,          // 8F8C
    boss_explode,       // 8F82
    boss_gone,          // 8FB9
    boss_song_end,      // 8FCA
    boss_release,       // 8FDB
    boss_after,         // 8FD8
    boss_flash,         // 050A (58A7)
    // The act's end sign (type 1F, bank 1E).
    sign_start,       // 9968
    sign_wait,        // 99DD
    sign_frame,       // 99E9
    sign_touch,       // 9A4B
    sign_spin,        // 9A81
    sign_result,      // 9AA3
    sign_done,        // 9B37
    sign_drop,        // 9B63
    sign_shake_left,  // 9B83
    sign_shake_right, // 9B92
    // Springs (type 21, bank 17).
    spring_start,         // A11F
    spring_home,          // A234
    spring_frame,         // A241
    spring_wait,          // A1F7
    spring_follow,        // A261
    spring_launch_weak,   // A270
    spring_launch_strong, // A274
    spring_snap,          // A287
    // Explosions (type 07, bank 1E).
    explosion_start,         // 853C
    explosion_toss,          // 85C4
    explosion_burn,          // 85D5
    explosion_end,           // 8618
    explosion_restore_block, // 85F2
    debris_fall,             // 866C
    puff_rise,               // 8653
    piece_start,             // 8800 (type 0A)
    piece_fly,               // 8857
    flake_start,             // AF99 (type 31)
    flake_anchor,            // AFC3
    // The hopper (type 33, bank 1E).
    hopper_start, // B14D
    hopper_sit,   // B156
    hopper_hop,   // B168
    // The popper (type 45, bank 17).
    popper_start, // 8417
    popper_hop,   // 8439
    popper_left,  // 8442
    popper_right, // 844F
    popper_fall,  // 845C
    popper_arc,   // 848B
    popper_touch, // 849E
    popper_drop,  // 84A4
    // The falling weight (type 47, bank 17).
    weight_start, // 8769
    weight_fall,  // 8793
    // The icicle (type 42, bank 17).
    icicle_start, // 82B1
    icicle_wait,  // 82D5
    icicle_fall,  // 82F5
    // Tails' submarine (type 40, bank 1E).
    roller_start,  // A9C2 (type 2B)
    roller_roll,   // A9D7 (type 2B)
    roller_roll_b, // A9EC (type 2B)
    roller_drop,   // AA2E (type 2B)
    shooter_start, // BB1A (type 3D)
    shooter_move,  // BB8F (type 3D)
    shot_fly,      // BBBF (type 3D)
    shot_fade,     // BBC6 (type 3D)
    shot_gone,     // BBCB (type 3D)
    sub_start,     // BFA3
    sub_wait,      // BFC2
    // The dropper (type 29, bank 10).
    dropper_start,  // BC8B
    dropper_wait,   // BCB5
    dropper_fall,   // BC90
    dropper_landed, // BCC6
    // The bomber (type 26, bank 1E).
    bomber_start,  // A513
    bomber_ready,  // A561
    bomber_patrol, // A56E
    bomber_bob,    // A584
    bomber_fly,    // A5C9
    bomber_flee,   // A5CC
    bomb_fall,     // A5D2
    bomber_pass,   // A604
    harmful_touch, // 550E (0375)
    // Item boxes (type 06, bank 1E).
    box_start,   // 828D
    box_wait,    // 82FD
    box_art_1,   // 8479
    box_art_2,   // 8482
    box_shown_1, // 8325
    box_shown_2, // 8336
    icon_start,  // 8466
    icon_end,    // 844C
    // Rings (type 1D) and sparkles (type 03), bank 1E.
    ring_start,      // 96E4
    ring_spin,       // 970E
    ring_wait,       // 9701
    ring_to_sparkle, // 988B
    sparkle_place,   // 8020
    scatter_start,   // 975C (type 1E)
    scatter_catch,   // 97EA
    scatter_bounce,  // 97F9
    // The water's objects (type 24, bank 1E).
    water_start,            // A27E
    water_at_surface,       // A321
    water_below_surface,    // A2B6
    water_above_player,     // A313
    bubble_rise,            // A2DD
    bubble_drift,           // A2E3
    bubble_end,             // A30A
    glint,                  // A357
    breathe,                // A32B
    breath_start,           // A38F
    air_start,              // BA04 (type 3B)
    air_rise,               // BACF (type 3B)
    air_sway_right,         // BAE8 (type 3B)
    air_sway_left,          // BAF1 (type 3B)
    air_drift_left,         // BAFA (type 3B)
    air_drift_back,         // BB03 (type 3B)
    air_bubble,             // BA77 (type 3B)
    machine_start,          // 0F:B7CF (type 66)
    machine_wake,           // 0F:B9ED (type 66)
    machine_to_first_form,  // 0F:BA01 (type 66)
    machine_wait_player,    // 0F:B3E9 (type 66)
    machine_first_form,     // 0F:B429 (type 66)
    machine_clear_cooldown, // 0F:B448 (type 66)
    machine_hop,            // 0F:B44D (type 66)
    machine_rest,           // 0F:B487 (type 66)
    machine_hop_again,      // 0F:B48B (type 66)
    machine_run_off,        // 0F:B4EB (type 66)
    machine_wait_arena,     // 0F:B50E (type 66)
    machine_arena,          // 0F:B53A (type 66)
    machine_art,            // 0F:B9F2 (type 66)
    machine_core_art,       // 0510 (type 66)
    machine_descend,        // 0F:BA06 (type 66)
    machine_follow,         // 0F:BA1E (type 66)
    machine_rise_start,     // 0F:B0CD (type 66)
    machine_rise,           // 0F:BA98 (type 66)
    machine_drop_start,     // 0F:BAAD (type 66)
    machine_drop,           // 0F:BAC9 (type 66)
    machine_sway_up,        // 0F:BAF2 (type 66)
    machine_sway_down,      // 0F:BAFB (type 66)
    machine_sway,           // 0F:BB04 (type 66)
    machine_open,           // 0F:BBA6 (type 66)
    machine_close,          // 0F:BB8C (type 66)
    machine_fall,           // 0F:BBBE (type 66)
    machine_drift,          // 0F:BBEB (type 66)
    machine_last_room,      // 0F:BC0E (type 66)
    machine_to_last_form,   // 0F:BC34 (type 66)
    machine_last_hits,      // 0F:BC39 (type 66)
    machine_wait_last,      // 0F:B170 (type 66)
    machine_last_fly,       // 0F:BC3E (type 66)
    machine_last_charge,    // 0F:BC7A (type 66)
    machine_place_low,      // 0F:BCBD (type 66)
    machine_place_high,     // 0F:BCCE (type 66)
    machine_flash,          // 0F:BCDF (type 66)
    machine_bolt,           // 0F:BCE9 (type 66)
    machine_trail,          // 0F:B2F7 (type 66)
    machine_strike,         // 0F:BD09 (type 66)
    machine_hold,           // 0F:B383 (type 66)
    core_start,             // 0F:BD27 (type 66)
    core_follow,            // 0F:BD2C (type 66)
    core_broken,            // 0F:BD42 (type 66)
    core_fall,              // 0F:BD4D (type 66)
    machine_shot_start,     // 0F:BD84 (type 66)
    machine_bomb_start,     // 0F:BDAF (type 66)
    machine_bomb_end,       // 0F:BDBA (type 66)
    machine_bomb_fall,      // 0F:BDC5 (type 66)
    machine_spark,          // 0F:BDE9 (type 66)
    hatch_blink,            // 0F:BDF7 (type 66)
    hatch_follow,           // 0F:BDFA (type 66)
    hatch_open,             // 0F:BE41 (type 66)
    machine_piece_start,    // 0F:BE4C (type 66)
    machine_piece,          // 0F:BE55 (type 66)
    machine_smoke,          // 0F:BE7E (type 66)
    machine_debris,         // 0F:BE99 (type 66)
    machine_part_bounce,    // 0F:B753 (type 66)
    ending_held,            // 0C:A9C4 (player state 46)
    ending_fall,            // 0C:A9EE (player state 46)
    ending_walk,            // 0C:AA00 (player state 46)
    ending_hop,             // 0C:AA39 (player state 46)
    ending_thrown,          // 0C:AA79 (player state 46)
    ending_stroll,          // 0C:AAB7 (player state 46)
    ending_jump,            // 0C:AAE6 (player state 46)
    ending_run_start,       // 0C:AB41 (player state 46)
    ending_run,             // 0C:AB1E (player state 46)
    ending_leap,            // 0C:AB56 (player state 46)
    ending_walk_back,       // 0C:ABAE (player state 46)
    ending_walk_to,         // 0C:ABDF (player state 46)
    ending_wait,            // 0C:AC0C (player state 46)
    ending_run_off,         // 0C:AC2C (player state 46)
    ending_fall_out,        // 0C:AC51 (player state 46)
    cage_start,             // 1C:B855 (type 61)
    cage_art,               // 1C:B85E (type 61)
    cage_wait,              // 1C:B872 (type 61)
    cage_fall,              // 1C:B889 (type 61)
    cage_release,           // 1C:B8AE (type 61)
    cage_fly,               // 1C:B8B4 (type 61)
    flight_title_start,     // 17:8184 (flight stage)
    flight_title_place,     // 17:819C (flight stage)
    flight_title_go,        // 17:81BB (flight stage)
    flight_title_end,       // 17:81C6 (flight stage)
    flight_letter_fall,     // 17:81E8 (flight stage)
    flight_letter_hold,     // 17:81FB (flight stage)
    flight_letter_wait,     // 17:8200 (flight stage)
    flight_letter_gather,   // 17:8212 (flight stage)
    flight_goal_hundreds,   // 17:8252 (flight stage)
    flight_goal_tens,       // 17:825E (flight stage)
    flight_goal_ones,       // 17:826E (flight stage)
    flight_letter_sound,    // 17:827A (flight stage)
    flight_letter_away,     // 17:8286 (flight stage)
    flight_at_plane,        // 1E:88F5 (flight stage)
    flight_ring_start,      // 1E:8902 (flight stage)
    flight_ring_fly,        // 1E:892A (flight stage)
    flight_ring_frame,      // 1E:8952 (flight stage)
    cloud_stop,             // 1E:9C7C (flight stage)
    cloud_faster,           // 1E:9CE2 (flight stage)
    cloud_start,            // 1E:9CE9 (flight stage)
    cloud_drift,            // 1E:9D00 (flight stage)
    cloud_place,            // 1E:9D1D-9D95 (flight stage)
    obstacle_start,         // 1E:B6C1 (flight stage)
    obstacle_frame,         // 1E:B6D8 (flight stage)
    obstacle_fly,           // 1E:B706 (flight stage)
    guardian_start,         // 17:B8BF (type 5C)
    guardian_wait,          // 17:B8D7 (type 5C)
    guardian_wake,          // 17:B8DC (type 5C)
    guardian_hop_far,       // 17:B8F8 (type 5C)
    guardian_hop_near,      // 17:B902 (type 5C)
    guardian_hop,           // 17:B966 (type 5C)
    guardian_hold,          // 17:B9EF (type 5C)
    guardian_colours,       // 17:B9F4 (type 5C)
    guardian_done,          // 17:BA1A (type 5C)
    star_start,             // 17:861B (flight stage)
    star_launch,            // 17:8637 (flight stage)
    star_faster,            // 17:8630 (flight stage)
    bright_star_start,      // 1A:BE41 (flight stage)
    bright_star_launch,     // 1A:BE58 (flight stage)
    bright_star_faster,     // 1A:BE8D (flight stage)
    bright_star_fastest,    // 1A:BE94 (flight stage)
    guardian2_start,        // 17:BAD5 (zone 9)
    guardian2_in,           // 17:BB0D (zone 9)
    guardian2_turn,         // 17:BB22 (zone 9)
    guardian2_sway,         // 17:BB28 (zone 9)
    guardian2_beaten,       // 17:BAA9 (zone 9)
    guardian2_colours,      // 17:BAB7 (zone 9)
    rider_start,            // 1C:B4DA (zone 9)
    rider_ride,             // 1C:B4EB (zone 9)
    rider_drop_start,       // 1C:B524 (zone 9)
    rider_drop,             // 1C:B535 (zone 9)
    rider_run_left_start,   // 1C:B551 (zone 9)
    rider_run_left,         // 1C:B5A4 (zone 9)
    rider_run_right_start,  // 1C:B5F5 (zone 9)
    rider_run_right,        // 1C:B60F (zone 9)
    rider_frame,            // 1C:B629 (zone 9)
    bloom_start,            // 17:87F1 (type 49)
    holder_start,           // 1E:964B (type 1B)
    holder_hold,            // 1E:96AF (type 1B)
    holder_drop_start,      // 1E:9690 (type 1B)
    holder_drop,            // 1E:9672 (type 1B)
    player_held,            // 0C:A078 (player state 12)
    sky_start,              // 1C:B9AC (credits)
    sky_wait_evening,       // 1C:BC41 (credits)
    sky_wait_dusk,          // 1C:BC58 (credits)
    sky_wait_night,         // 1C:BC6F (credits)
    sky_evening,            // 1C:B9BB (credits)
    sky_evening_horizon,    // 1C:BA68 (credits)
    sky_dusk,               // 1C:BACC (credits)
    sky_night,              // 1C:BBDD (credits)
    credits_end,            // 1C:BC86 (credits)
    credits_hold,           // 1C:BC8A (credits)
    text_start,             // 1C:BD0D (credits)
    text_line,              // 1C:BD3A (credits)
    text_done,              // 1C:BDAD (credits)
    text_score,             // 1C:BDB7 (credits)
    text_last,              // 1C:BE07 (credits)
    text_rise,              // 1C:BCE6 (credits)
    text_spread,            // 1C:BE1B (credits)
    far_plane_start,        // 1A:BABE (credits)
    scenery_start,          // 1A:BBE7 (credits)
    scenery_place,          // 1A:BBFF (credits)
    scenery_wrap,           // 1A:BC10 (credits)
    scenery_pass,           // 1A:BC74 (credits)
    scenery_stop,           // 1A:BC81 (credits)
    star_twinkle_start,     // 1A:BD52 (credits)
    star_twinkle_place,     // 1A:BD65 (credits)
    glider_start,           // 17:B2B4 (special stage guardians)
    guardian_wait_hud,      // 17:B2DF (special stage guardians)
    glider_meet,            // 17:B2EF (special stage guardians)
    glider_meet_song,       // 17:B0FA (special stage guardians)
    glider_right,           // 17:B109 (special stage guardians)
    glider_pass_start,      // 17:B2FA (special stage guardians)
    glider_left,            // 17:B120 (special stage guardians)
    glider_place,           // 17:B328 (special stage guardians)
    glider_frame,           // 17:B19D (special stage guardians)
    glider_pass1,           // 17:B3A3 (special stage guardians)
    glider_pass2,           // 17:B3B8 (special stage guardians)
    glider_pass3,           // 17:B3CF (special stage guardians)
    glider_pass4,           // 17:B3F8 (special stage guardians)
    glider_beaten,          // 17:B44F (special stage guardians)
    guardian_fly_off,       // 17:B482 (special stage guardians)
    guardian_blast_frame,   // 17:B4B3 (special stage guardians)
    knuckles_guard_start,   // 17:B59A (special stage guardians)
    knuckles_appear,        // 17:B5DD (special stage guardians)
    knuckles_run_in,        // 17:B609 (special stage guardians)
    knuckles_chase,         // 17:B625 (special stage guardians)
    knuckles_drop,          // 17:B68F (special stage guardians)
    knuckles_touch_only,    // 17:B6D3 (special stage guardians)
    knuckles_stunned,       // 17:B6D7 (special stage guardians)
    knuckles_beaten,        // 17:B736 (special stage guardians)
    knuckles_flash,         // 17:B73F (special stage guardians)
    knuckles_colours,       // 17:B755 (special stage guardians)
    knuckles_gone,          // 17:B773 (special stage guardians)
    knuckles_rise,          // 17:B7A3 (special stage guardians)
    sign_fall_away,         // 1E:9BA1 (type 1F)
    swim_start,             // 0C:A84D (player state 41)
    swim,                   // 0C:A862
    skimmer_start,          // 17:833A (types 43, 44)
    skimmer_move,           // 17:8347
    ice_block_start,        // 1E:A913 (type 2A)
    ice_block_bob,          // 1E:A946
    ice_block_broken,       // 1E:A963
    piston_start,           // 1E:959A (type 1A)
    piston_thud,            // 1E:95CC
    piston_hold,            // 1E:95D2
    piston_down,            // 1E:9614
    piston_up,              // 1E:9619
    sparkle_trail_left,     // 1E:B913 (type 39)
    sparkle_trail_right,    // 1E:B929
    craft_start,            // 17:AE09 (types 57-59)
    craft_art,              // 17:AE22 (types 57-59)
    craft_rise,             // 17:AE28 (types 57-59)
    craft_fly,              // 17:AE43 (types 57-59)
    craft_last_hop,         // 17:AE80 (types 57-59)
    craft_landed,           // 17:ADCD (types 57-59)
    craft_jump,             // 17:ADB9 (types 57-59)
    capsule_start,          // 17:AFB5 (types 57-59)
    capsule_done,           // 17:AFDD (types 57-59)
    part_start,             // 17:B03B (types 57-59)
    part_art,               // 17:B04C (types 57-59)
    part_fall_in,           // 17:B052 (types 57-59)
    part_fall,              // 17:B057 (types 57-59)
    part_down,              // 17:B06C (types 57-59)
    part_gone,              // 17:B022 (types 57-59)
    machine_part_gone,      // 0F:BF4C (type 66)
    // The player breathing a big bubble (state 25, bank 0C).
    air_breathed, // 8399
    // A shell ride (player state 16, bank 0C).
    shell_ride_start, // 8325
    shell_ride_frame, // AEB2
    shell_ride,       // BE2F
    // Meta Junglira's barrels (player state 32, bank 0C) and the sparks they
    // throw (type 39, parameters 2-4, bank 1E).
    barrel_enter,  // A667
    barrel_center, // A672
    barrel_settle, // A6C3
    barrel_creak,  // A6A2
    barrel_fire,   // A6A8
    barrel_out,    // 8740 (Tails 945A)
    spark_fall,    // 1E:B884
    // The title's sound test (mode 19): the player (state 45, bank 0C) and
    // its letters and numbers (type 5F, bank 1C).
    sound_test_place,      // 8BE2
    sound_test_go_left,    // 8C81
    sound_test_go_right,   // 8C88
    sound_test_walk_left,  // 8BEF
    sound_test_walk_right, // 8C38
    sound_test_songs,      // 8C03
    sound_test_effects,    // 8C4C
    sound_test_text_start, // 1C:B66E
    sound_test_numbers,    // 1C:B76B
    sound_test_song_digits,   // 1C:B786
    sound_test_effect_digits, // 1C:B7AF
};

// The routine at `address`, a script running in object bank `bank`
// (addresses from 8000 lie in that bank). Routine::unknown if not known.
Routine routine_at(unsigned bank, unsigned address);

} // namespace triple_trouble_gg
