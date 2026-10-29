// Routine addresses of the object scripts (routines.hpp): the link from the
// cartridge's scripts to the reconstructed routines. Addresses below 8000
// are in banks 0 and 1 (Sonic's routines through the vectors 0200-0310 and
// shared helpers); the others are in the object's bank (1C, 1E or 1F).

#include "sonic2_gg/game/routines.hpp"
#include <stdexcept>
#include <string>

namespace sonic2_gg {

namespace {

struct Entry {
    std::uint8_t bank;
    std::uint16_t address;
    Routine routine;
};

constexpr Entry entries[] = {
    {0x00, 0x0266, Routine::sonic_start},            // 2F72
    {0x00, 0x0269, Routine::sonic_standing},         // 31A2
    {0x00, 0x0284, Routine::sonic_sprung},           // 3407
    {0x00, 0x0287, Routine::sonic_dying},            // 34CB
    {0x00, 0x028A, Routine::sonic_hurt},             // 3470
    {0x00, 0x028D, Routine::sonic_falling},          // 343E
    {0x00, 0x029F, Routine::sonic_jumping},          // 33CD
    {0x00, 0x02A5, Routine::sonic_bounced},          // 3426
    {0x00, 0x02A8, Routine::sonic_crouch},           // 324A
    {0x00, 0x02AB, Routine::sonic_look_up},          // 3228
    {0x00, 0x02B1, Routine::sonic_rolling},          // 338B
    {0x00, 0x02B7, Routine::sonic_pipe_exit},        // 3675
    {0x00, 0x02B4, Routine::sonic_tube},             // 3598
    {0x00, 0x02AE, Routine::sonic_pipe},             // 43DC
    {0x00, 0x02A2, Routine::sonic_swing},            // 4290
    {0x00, 0x02E7, Routine::sonic_in_bubble_update}, // 347B
    {0x00, 0x0308, Routine::sonic_drown},            // 34D7
    {0x00, 0x0296, Routine::sonic_glider_stand},     // 40A6
    {0x00, 0x029C, Routine::sonic_glider_run},       // 40CF
    {0x00, 0x0293, Routine::sonic_glider_fly},       // 40F8
    {0x00, 0x0299, Routine::sonic_glider_climb},     // 41D5
    {0x00, 0x0290, Routine::sonic_glider_dive},      // 421E
    {0x00, 0x027E, Routine::glider_run},             // 404F
    {0x00, 0x02BD, Routine::sonic_act_end_run},      // 34F8
    {0x00, 0x026F, Routine::sonic_tube_frames},      // 39F9
    {0x00, 0x0272, Routine::sonic_rise_frames},      // 39AC
    {0x00, 0x02BA, Routine::sonic_running},          // 3320
    {0x00, 0x02C0, Routine::sonic_floating},         // 344F
    {0x00, 0x02C3, Routine::sonic_idle},             // 3205
    {0x00, 0x02C9, Routine::sonic_pushed},           // 3764
    {0x00, 0x02CC, Routine::sonic_skid_left},        // 3345
    {0x00, 0x02CF, Routine::sonic_skid_right},       // 3368
    {0x00, 0x02D8, Routine::sonic_walking},          // 3266
    {0x00, 0x02E1, Routine::sonic_launched},         // 339A
    {0x00, 0x02EA, Routine::sonic_breathe},          // 3495
    {0x00, 0x02F0, Routine::sonic_balancing},        // 31DB
    {0x00, 0x02F9, Routine::sonic_walk_animation},   // 38FF
    {0x00, 0x02FC, Routine::sonic_roll_animation},   // 3934
    {0x00, 0x02FF, Routine::sonic_slide},            // 349A
    {0x00, 0x022A, Routine::none},                   // 6370: RET
    {0x00, 0x02D2, Routine::sonic_board_cart},       // 4717
    {0x00, 0x0281, Routine::sonic_ride},             // 4712
    {0x00, 0x02D5, Routine::sonic_riding},           // 4741
    {0x00, 0x02DB, Routine::sonic_cart_frames},      // 47BB
    {0x00, 0x02DE, Routine::flash_ring_colors},      // 7E0F
    {0x00, 0x0209, Routine::move_to_sonic},          // 6127
    {0x00, 0x020C, Routine::destroy_if_attacked},    // 613C
    {0x00, 0x0218, Routine::solid_object},           // 620F
    {0x00, 0x021B, Routine::bumper_object},          // 6215
    {0x00, 0x021E, Routine::bounce_sonic_off},       // 621B
    {0x00, 0x0221, Routine::hit_object_bumper},      // 6269
    {0x00, 0x0224, Routine::hit_object_solid},       // 6280
    {0x00, 0x0227, Routine::push_sonic_out},         // 62B1
    {0x00, 0x022D, Routine::clear_blink},            // 6371
    {0x00, 0x0236, Routine::velocity_from_angle},    // 6394
    {0x00, 0x0239, Routine::object_wall},            // 6404
    {0x00, 0x023C, Routine::face_motion},            // 643B
    {0x00, 0x0257, Routine::move_object},            // 66C3
    {0x00, 0x025A, Routine::touch_sonic},            // 6704
    {0x00, 0x023F, Routine::face_sonic},             // 6452
    {0x00, 0x025D, Routine::explode},                // 614C
    {0x00, 0x0260, Routine::object_floor},           // 7490
    {0x00, 0x02E4, Routine::spawn_debris},           // 710C
    {0x00, 0x0302, Routine::lock_camera_left},       // 4A3F
    {0x00, 0x0305, Routine::lock_camera_right},      // 4A4F
    {0x00, 0x0317, Routine::add_score_100},          // 1D14: 100 points
    {0x00, 0x031A, Routine::add_score_500},          // 1D1A: 500
    {0x00, 0x031D, Routine::add_score_1000},         // 1D20: 1000
    {0x00, 0x0320, Routine::add_score_2000},         // 1D26
    {0x1F, 0xAAEF, Routine::sparkle_start},
    {0x1F, 0xAB12, Routine::none},
    {0x1F, 0xAB26, Routine::debris_start},
    {0x1F, 0xABF2, Routine::debris2_start},
    {0x1F, 0xAC7A, Routine::shoe_image_start},
    {0x1F, 0xACA5, Routine::shoe_image_follow},
    {0x1F, 0xAE06, Routine::star_start},
    {0x1F, 0xAE19, Routine::star_follow},
    {0x1F, 0xABCA, Routine::debris_fall},
    {0x1F, 0xAEE1, Routine::lost_ring_start},
    {0x1F, 0xAF4E, Routine::lost_ring_move},
    {0x1F, 0xAFB0, Routine::none},
    {0x1F, 0xB5D0, Routine::lava_ball_start},
    {0x1F, 0xB5FF, Routine::lava_ball_rise},
    {0x1F, 0xB616, Routine::lava_ball_drop},
    {0x1F, 0xB63B, Routine::lava_ball_wait},
    {0x1F, 0xB645, Routine::lava_ball_fly},
    {0x1F, 0xB6A2, Routine::explosion_repeat},
    {0x1F, 0xB6BC, Routine::explosion_start},
    {0x1F, 0xB6D1, Routine::none},
    {0x1F, 0xB6D2, Routine::explosion_end},
    {0x1F, 0xB71F, Routine::monitor_start},
    {0x1F, 0xB728, Routine::monitor_appear},
    {0x1F, 0xB733, Routine::monitor_update},
    {0x1F, 0xB8DA, Routine::sign_wait},
    {0x1F, 0xB8F9, Routine::sign_start},
    {0x1F, 0xB8FE, Routine::sign_ready},
    {0x1F, 0xB7D1, Routine::emerald_start},
    {0x1F, 0xB80C, Routine::emerald_wait},
    {0x1F, 0xB854, Routine::emerald_taken},
    {0x1F, 0xB91A, Routine::sign_spin},
    {0x1F, 0xB9DA, Routine::sign_face_short},
    {0x1F, 0xB9E5, Routine::sign_face_long},
    {0x1F, 0xB9F0, Routine::sign_finish},
    {0x1F, 0xBA7A, Routine::rock_throw},
    {0x1F, 0xBADF, Routine::rock_start},
    {0x1F, 0xBAEB, Routine::rock_shake},
    {0x1F, 0xBAEF, Routine::rock_fall},
    {0x1F, 0xBB24, Routine::rock_fly},
    {0x1F, 0xBB45, Routine::rock_wait},
    {0x1F, 0xBBBA, Routine::intro_cloud_start},
    {0x1F, 0xBBDD, Routine::intro_cloud_move},
    {0x1F, 0xBC06, Routine::intro_star_start},
    {0x1F, 0xBC29, Routine::intro_star_move},
    {0x1F, 0xBC55, Routine::intro_hill_start},
    {0x1F, 0xBC72, Routine::intro_hill_move},
    {0x1F, 0xA706, Routine::sonic_intro_stand},
    {0x1F, 0xAA94, Routine::sonic_continue_wait},
    {0x1F, 0xA91B, Routine::sonic_face_left},
    {0x1F, 0xA920, Routine::sonic_face_right},
    {0x1F, 0xA925, Routine::sonic_ending_run},
    {0x1F, 0xA938, Routine::sonic_ending_arrive},
    {0x1F, 0xA951, Routine::sonic_ending_stop},
    {0x1F, 0xA9F8, Routine::sonic_follow_remains},
    {0x1F, 0xA9E8, Routine::sonic_watch_camera},
    {0x1F, 0xA9F2, Routine::sonic_watch_done},
    {0x1F, 0xA84B, Routine::sonic_bubble_frame_61},
    {0x1F, 0xA852, Routine::sonic_bubble_frame_62},
    {0x1F, 0xAFEB, Routine::air_count_start},
    {0x1F, 0xB012, Routine::air_count_show},
    {0x1F, 0xB040, Routine::air_count_drown},
    {0x1F, 0xB164, Routine::bubble_start},
    {0x1F, 0xB17C, Routine::bubble_cluster},
    {0x1F, 0xB183, Routine::bubble_rise},
    {0x1F, 0xB187, Routine::bubble_breathe},
    {0x1F, 0xB1A9, Routine::bubble_catch},
    {0x1F, 0xB1ED, Routine::bubble_carry},
    {0x1F, 0xB29B, Routine::bubble_face},
    {0x1F, 0xB2B2, Routine::bubble_wait},
    {0x1F, 0xB2CA, Routine::bubble_reset},
    {0x1F, 0xB2E3, Routine::bubble_from_sonic},
    {0x1F, 0xB2FA, Routine::bubble_clear_cluster},
    {0x1F, 0xB30F, Routine::bubble_rise},
    {0x1F, 0xB316, Routine::bubble_wobble},
    {0x1F, 0xB40B, Routine::remove_self},
    {0x1F, 0xB43A, Routine::splash_start},
    {0x1F, 0xB474, Routine::none},
    {0x1F, 0xB485, Routine::water_start},
    {0x1F, 0xB4AC, Routine::water_rise},
    {0x1F, 0xAA26, Routine::sonic_drop_through_190},
    {0x1F, 0xAA3E, Routine::sonic_drop_through_240},
    {0x1F, 0xAA7E, Routine::sonic_continue_frames},
    {0x1F, 0xAAB8, Routine::sonic_continue_leave},
    {0x1F, 0xA729, Routine::sonic_intro_run},
    {0x1E, 0x8E7A, Routine::fireballs_move},
    {0x1E, 0x8E8C, Routine::fireballs_start},
    {0x1E, 0x8EDE, Routine::fireballs_circle},
    {0x1E, 0x9043, Routine::fire_start},
    {0x1E, 0x9049, Routine::fire_move},
    {0x1E, 0x905B, Routine::remove_self},
    {0x1E, 0x915E, Routine::boss_palette_9},
    {0x1E, 0x9164, Routine::boss_palette_10},
    {0x1E, 0x916A, Routine::final_boss_electric_on},
    {0x1E, 0x9170, Routine::final_boss_electric_off},
    {0x1E, 0x91F2, Routine::clear_timer},
    {0x1E, 0x91F7, Routine::final_boss_lift},
    {0x1E, 0x9211, Routine::break_block_64_above},
    {0x1E, 0x921B, Routine::break_block_16_above},
    {0x1E, 0x9288, Routine::final_boss_pause},
    {0x1E, 0x92AD, Routine::final_boss_start},
    {0x1E, 0x92C3, Routine::final_boss_wait},
    {0x1E, 0x92DB, Routine::final_boss_arena},
    {0x1E, 0x930B, Routine::final_boss_choose},
    {0x1E, 0x935A, Routine::final_boss_shock},
    {0x1E, 0x936E, Routine::final_boss_blink},
    {0x1E, 0x937E, Routine::final_boss_guard},
    {0x1E, 0x93A4, Routine::final_boss_hop},
    {0x1E, 0x93F6, Routine::final_boss_run},
    {0x1E, 0x9435, Routine::final_boss_rest},
    {0x1E, 0x9462, Routine::countdown_next},
    {0x1E, 0x946E, Routine::final_boss_transform},
    {0x1E, 0x950E, Routine::companion_signal},
    {0x1E, 0x9519, Routine::companion_drop},
    {0x1E, 0x9555, Routine::companion_fall},
    {0x1E, 0x958F, Routine::companion_start},
    {0x1E, 0x95D9, Routine::companion_close},
    {0x1E, 0x95E9, Routine::companion_follow},
    {0x1E, 0x8DC5, Routine::title_sonic_start},
    {0x1E, 0x8DE0, Routine::none},
    {0x1E, 0x8E21, Routine::title_tails_start},
    {0x1E, 0x8E3C, Routine::none},
    {0x1E, 0x8E3D, Routine::title_tails_secret},
    {0x1E, 0x96BB, Routine::intro_runner_start},
    {0x1E, 0x971B, Routine::intro_runner_follow},
    {0x1E, 0x973C, Routine::intro_runner_away},
    {0x1E, 0x9767, Routine::intro_tails_start},
    {0x1E, 0x97DB, Routine::intro_tails_follow},
    {0x1E, 0x97FD, Routine::intro_tails_move_to_3},
    {0x1E, 0x9815, Routine::intro_tails_move_to_4},
    {0x1E, 0x982C, Routine::intro_tails_move_to_5},
    {0x1E, 0x9843, Routine::intro_tails_wait},
    {0x1E, 0x9857, Routine::intro_tails_leave},
    {0x1F, 0xAE45, Routine::hud_slide_start},
    {0x1F, 0xAE4E, Routine::hud_slide_out},
    {0x1F, 0xAE7C, Routine::hud_slide_back},
    {0x1C, 0x8067, Routine::platform_start},
    {0x1C, 0x8080, Routine::platform_move},
    {0x1C, 0x80F6, Routine::platform_wait},
    {0x1C, 0x8104, Routine::platform_shake},
    {0x1C, 0x8108, Routine::platform_fall},
    {0x1C, 0x8123, Routine::platform_swing},
    {0x1C, 0x81E7, Routine::platform_frame_short},
    {0x1C, 0x81EE, Routine::platform_frame_long},
    {0x1C, 0x8EC4, Routine::falling_block_start},
    {0x1C, 0x835C, Routine::path_platform_start},
    {0x1C, 0x8374, Routine::path_platform_ride_free},
    {0x1C, 0x8389, Routine::path_platform_ride},
    {0x1C, 0x838D, Routine::path_platform_wait},
    {0x1C, 0x83D9, Routine::shuttle_start},
    {0x1C, 0x83ED, Routine::shuttle_move},
    {0x1C, 0x8823, Routine::swing_pole_start},
    {0x1C, 0x8828, Routine::swing_pole_grab},
    {0x1C, 0xB768, Routine::jumper_start},
    {0x1C, 0xB773, Routine::jumper_watch},
    {0x1C, 0xB7B3, Routine::jumper_jump},
    {0x1C, 0xB812, Routine::jumper_land},
    {0x1C, 0xB846, Routine::jumper_walk},
    {0x1C, 0xB941, Routine::shaker_start},
    {0x1C, 0xB958, Routine::shaker_patrol},
    {0x1C, 0xB996, Routine::shaker_shake},
    {0x1C, 0xB9BD, Routine::shaker_burst},
    {0x1C, 0xB9F6, Routine::hopper2_start},
    {0x1C, 0xBA19, Routine::hopper2_hop},
    {0x1C, 0xBA72, Routine::hopper2_turn},
    {0x1C, 0xBAA5, Routine::hopper2_move},
    {0x1C, 0xBB3E, Routine::fireball_start},
    {0x1C, 0xBB96, Routine::fireball_fly},
    {0x1C, 0x8B7A, Routine::moving_pole_start},
    {0x1C, 0x8B83, Routine::moving_pole_update},
    {0x1C, 0x8CC9, Routine::pendulum_start},
    {0x1C, 0x8CDB, Routine::pendulum_swing},
    {0x1C, 0x8DF8, Routine::drop_block2_watch},
    {0x1C, 0x8E5D, Routine::drop_block2_shake},
    {0x1C, 0x8E66, Routine::drop_block2_fall},
    {0x1C, 0x8E89, Routine::drop_block2_break},
    {0x1C, 0xB6C5, Routine::crawler_start},
    {0x1C, 0xB6E5, Routine::crawler_move},
    {0x1C, 0x8622, Routine::drop_block_wait},
    {0x1C, 0x8633, Routine::drop_block_fall},
    {0x1C, 0x8798, Routine::glider_start},
    {0x1C, 0x879D, Routine::glider_wait},
    {0x1C, 0x87BD, Routine::glider_carried},
    {0x1C, 0x87F9, Routine::glider_drift},
    {0x1C, 0xAD2F, Routine::hopper_start},
    {0x1C, 0xAD45, Routine::hopper_watch},
    {0x1C, 0xAD72, Routine::hopper_move},
    {0x1C, 0xAD86, Routine::hopper_charge},
    {0x1C, 0xADC2, Routine::hopper_move},
    {0x1C, 0xADD6, Routine::hopper_run},
    {0x1C, 0xAE09, Routine::hopper_approach},
    {0x1C, 0xAE3A, Routine::hopper_drop},
    {0x1C, 0xAE84, Routine::hopper_walk},
    {0x1C, 0x9A5E, Routine::ball_frame_1},
    {0x1C, 0x9A65, Routine::ball_frame_2},
    {0x1C, 0x9A6C, Routine::ball_frame_3},
    {0x1C, 0x9A7F, Routine::ball_rise},
    {0x1C, 0x9A95, Routine::ball_bounce},
    {0x1C, 0x9B1B, Routine::launcher_start},
    {0x1C, 0x9B2F, Routine::launcher_wait},
    {0x1C, 0x9B47, Routine::launcher_drop},
    {0x1C, 0x9D62, Routine::shz_boss_gone},
    {0x1C, 0x9D6A, Routine::boss_add_part},
    {0x1C, 0x9D98, Routine::shz_boss_start},
    {0x1C, 0x9DB9, Routine::shz_boss_wait},
    {0x1C, 0x9DD8, Routine::shz_boss_wait_parts},
    {0x1C, 0x9DF0, Routine::shz_boss_open_floor},
    {0x1C, 0x9E03, Routine::shz_boss_place},
    {0x1C, 0x9E16, Routine::shz_boss_fly},
    {0x1C, 0x9E59, Routine::shz_boss_blink},
    {0x1C, 0x9EA6, Routine::shz_flier},
    {0x1C, 0xB4EB, Routine::minion_start},
    {0x1C, 0xB4F0, Routine::minion_rise},
    {0x1C, 0xB510, Routine::minion_land},
    {0x1C, 0xB558, Routine::minion_wait},
    {0x1C, 0xB568, Routine::minion_hop},
    {0x1C, 0xB59F, Routine::minion_hop_air},
    {0x1C, 0xB5EF, Routine::minion_wait},
    {0x1C, 0xB5FF, Routine::minion_walk},
    {0x1C, 0xB61B, Routine::minion_jump},
    {0x1C, 0x86B3, Routine::trap_start},
    {0x1C, 0x86BC, Routine::trap_wait_above},
    {0x1C, 0x86ED, Routine::trap_wait_below},
    {0x1C, 0x8702, Routine::trap_wait_left},
    {0x1C, 0x8733, Routine::trap_wait_right},
    {0x1C, 0x8750, Routine::trap_fly},
    {0x1C, 0xAEDE, Routine::fish_rest},
    {0x1C, 0xAEEC, Routine::fish_watch},
    {0x1C, 0xAF41, Routine::fish_chase},
    {0x1C, 0xAF88, Routine::fish_return},
    {0x1C, 0x9F11, Routine::alz_bomb_start},
    {0x1C, 0x9F36, Routine::alz_bomb_fly},
    {0x1C, 0x9FC6, Routine::alz_bomb_bounce},
    {0x1C, 0xA169, Routine::alz_boss_land},
    {0x1C, 0xA177, Routine::alz_boss_leap},
    {0x1C, 0xA18B, Routine::alz_boss_turn},
    {0x1C, 0xA1AD, Routine::alz_boss_count},
    {0x1C, 0xA1B1, Routine::alz_boss_choose_jump},
    {0x1C, 0xA1BF, Routine::alz_boss_to_walk},
    {0x1C, 0xA1D0, Routine::alz_boss_hit},
    {0x1C, 0xA1E1, Routine::alz_boss_gone},
    {0x1C, 0xA1EF, Routine::alz_boss_start},
    {0x1C, 0xA202, Routine::alz_boss_wait},
    {0x1C, 0xA229, Routine::alz_boss_guard},
    {0x1C, 0xA23C, Routine::alz_boss_jump},
    {0x1C, 0xA26A, Routine::alz_boss_walk},
    {0x1C, 0xA2F0, Routine::alz_boss_rise},
    {0x1C, 0xA313, Routine::alz_boss_carry},
    {0x1C, 0x9407, Routine::ghz_boss_face_right},
    {0x1C, 0x9521, Routine::ghz_boss_gone},
    {0x1C, 0x952F, Routine::ghz_boss_start},
    {0x1C, 0x9539, Routine::ghz_boss_wait},
    {0x1C, 0x9560, Routine::ghz_boss_choose},
    {0x1C, 0x958A, Routine::ghz_boss_hop},
    {0x1C, 0x95C9, Routine::ghz_boss_roll},
    {0x1C, 0x962F, Routine::ghz_boss_jump_back},
    {0x1C, 0x9695, Routine::ghz_boss_stand},
    {0x1C, 0x96B1, Routine::ghz_boss_guard},
    {0x1C, 0x973F, Routine::ghz_boss_shake},
    {0x1C, 0xAC29, Routine::boss_bumper},
    {0x1C, 0xAC6C, Routine::boss_solid_hit},
    {0x1C, 0x97E4, Routine::gmz_boss_start},
    {0x1C, 0x97EE, Routine::gmz_boss_wait},
    {0x1C, 0x9816, Routine::gmz_boss_approach},
    {0x1C, 0x984E, Routine::gmz_boss_roll},
    {0x1C, 0x98F8, Routine::gmz_boss_stand},
    {0x1C, 0x9917, Routine::gmz_boss_aim},
    {0x1C, 0x9940, Routine::gmz_boss_jump},
    {0x1C, 0x995F, Routine::gmz_boss_gone},
    {0x1C, 0x996D, Routine::gmz_boss_turn},
    {0x1C, 0x9983, Routine::gmz_boss_blink},
    {0x1C, 0x99BD, Routine::boulder_start},
    {0x1C, 0x99F9, Routine::boulder_fall},
    {0x1C, 0xA838, Routine::sez_boss_start},
    {0x1C, 0xA9DD, Routine::sez_boss_gone_zone_complete},
    {0x1C, 0xA9E2, Routine::sez_boss_gone},
    {0x1C, 0xA9E7, Routine::sez_boss_wait},
    {0x1C, 0xAA1F, Routine::sez_boss_choose},
    {0x1C, 0xAA9D, Routine::sez_boss_jump},
    {0x1C, 0xAAC9, Routine::sez_boss_hop},
    {0x1C, 0xAAF5, Routine::sez_boss_dash},
    {0x1C, 0xAB21, Routine::sez_boss_jump},
    {0x1C, 0xAB4D, Routine::sez_boss_charge},
    {0x1C, 0xAB64, Routine::sez_boss_leap},
    {0x1C, 0xABA7, Routine::sez_boss_charge2},
    {0x1C, 0xABB3, Routine::sez_boss_bounce},
    {0x1C, 0xAC91, Routine::sez_boss_guard},
    {0x1C, 0x8FFB, Routine::capsule_descend},
    {0x1C, 0x903B, Routine::capsule_wait},
    {0x1C, 0x90A4, Routine::capsule_open},
    {0x1C, 0x90DC, Routine::capsule_hover_down_right},
    {0x1C, 0x90EC, Routine::capsule_hover_up_left},
    {0x1C, 0x90FC, Routine::capsule_hover_up_right},
    {0x1C, 0x910C, Routine::capsule_hover_down_left},
    {0x1C, 0x918E, Routine::capsule_free_animal},
    {0x1C, 0x9273, Routine::animal_start},
    {0x1C, 0x9278, Routine::animal_frame_first},
    {0x1C, 0x9286, Routine::animal_frame_second},
    {0x1C, 0x9295, Routine::animal_fall},
    {0x1C, 0x92C0, Routine::animal_hop},
    {0x1C, 0xA37C, Routine::ugz_bomb_throw},
    {0x1C, 0xA3A9, Routine::ugz_bomb_fly},
    {0x1C, 0xA47B, Routine::ugz_boss_wait},
    {0x1C, 0xA513, Routine::ugz_boss_enter},
    {0x1C, 0xA52E, Routine::ugz_boss_approach},
    {0x1C, 0xA55F, Routine::ugz_boss_swoop},
    {0x1C, 0xA585, Routine::ugz_boss_grab},
    {0x1C, 0xA5B5, Routine::ugz_boss_lift},
    {0x1C, 0xA5FD, Routine::ugz_boss_carry_right},
    {0x1C, 0xA639, Routine::ugz_boss_set_down},
    {0x1C, 0xA67A, Routine::ugz_boss_to_post},
    {0x1C, 0xA6AA, Routine::ugz_boss_fall},
    {0x1C, 0xA6F1, Routine::ugz_boss_fly_off},
    {0x1C, 0xA7B7, Routine::ugz_target_start},
    {0x1C, 0xA7C5, Routine::ugz_target_frame_1},
    {0x1C, 0xA7CC, Routine::ugz_target_frame_2},
    {0x1C, 0xA7D9, Routine::ugz_target_hit},
    {0x1C, 0xA7EB, Routine::ugz_target_gone},
    {0x1C, 0x850C, Routine::bomb_walker_start},
    {0x1C, 0x8515, Routine::bomb_walker_right},
    {0x1C, 0x852F, Routine::bomb_walker_left},
    {0x1C, 0x8546, Routine::bomb_walker_walk_right},
    {0x1C, 0x8572, Routine::bomb_walker_walk_left},
    {0x1C, 0x858E, Routine::bomb_walker_throw},
    {0x1C, 0x85B1, Routine::bomb_start},
    {0x1C, 0x85D1, Routine::bomb_fly},
    {0x1C, 0x8952, Routine::cart_start},
    {0x1C, 0x8957, Routine::cart_parked},
    {0x1C, 0x8976, Routine::cart_roll_right},
    {0x1C, 0x89B9, Routine::cart_roll_left},
    {0x1C, 0x8A10, Routine::cart_fall},
    {0x1C, 0x8A95, Routine::cart_broken},
    {0x1C, 0x8AAA, Routine::cart_gone},
    {0x1C, 0xB0F5, Routine::pacer_start},
    {0x1C, 0xB0FF, Routine::pacer_walk_right},
    {0x1C, 0xB15F, Routine::pacer_walk_left},
    {0x1C, 0xB1C0, Routine::pacer_turn_left},
    {0x1C, 0xB203, Routine::pacer_turn_right},
    {0x1C, 0xB246, Routine::pacer_creep_left},
    {0x1C, 0xB26A, Routine::pacer_creep_right},
    {0x1C, 0xB28E, Routine::pacer_charge_right},
    {0x1C, 0xB2CE, Routine::pacer_charge_left},
    {0x1C, 0xB402, Routine::turret_start},
    {0x1C, 0xB410, Routine::turret_watch},
    {0x1C, 0xB423, Routine::none},
    {0x1C, 0xB424, Routine::turret_fire},
    {0x1C, 0xB430, Routine::none},
    {0x1C, 0xB431, Routine::turret_remove},
    {0x1C, 0xBDAB, Routine::shot_start},
    {0x1C, 0xBDC1, Routine::shot_fly},
    {0x1C, 0xBDD9, Routine::shot_fly}, // the same code again (state 2)
};

} // namespace

Routine routine_at(unsigned bank, unsigned address) {
    if (address < 0x8000) {
        bank = 0;
    }
    for (const auto& e : entries) {
        if (e.bank == bank && e.address == address) {
            return e.routine;
        }
    }
    throw std::runtime_error("Object routine not yet reconstructed: bank " + std::to_string(bank) +
                             ", address " + std::to_string(address));
}

} // namespace sonic2_gg
