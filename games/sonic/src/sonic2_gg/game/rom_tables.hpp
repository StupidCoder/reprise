#pragma once
#include "sonic2_gg/data/assets.hpp"

// The cartridge tables and compressed blocks the game reads, by name.
//
// The asset pack keeps the cartridge's data under its original bank and
// address (assets.hpp), so the code reads these tables as the cartridge does;
// naming them here keeps the code readable and the link to the ROM. Tables
// read by index are named by their first entry.
namespace sonic2_gg::rom {

// --- Bank 0 -------------------------------------------------------------------
inline constexpr Table bit_reversed{0x00, 0x0100};      // each byte mirrored (mirrored tile art)
inline constexpr Table sine{0x00, 0x0330};              // 256 signed sines; cosine at +C0
inline constexpr Table shake_offsets{0x00, 0x05E0};     // screen shake: (dy, dx) pairs
inline constexpr Table underwater_colors{0x00, 0x061A}; // 64 bytes of colours below the water line
inline constexpr Table zone_select_title{0x00, 0x0A81}; // text
inline constexpr Table zone_select_lines{0x00, 0x0AA7}; // 21 lines of 26 bytes
inline constexpr Table zone_select_acts{0x00, 0x0DE3};  // (zone, act) per line
inline constexpr Table zone_select_arrow_left{0x00, 0x0E8A}; // map entries, 3 rows
inline constexpr Table zone_select_arrow_right{0x00, 0x0EAA};
inline constexpr Table act_music{0x00, 0x10CF};  // song per zone * 3 + act
inline constexpr Table big_digits{0x00, 0x1E63}; // 2x2 map entries per digit (10: blank)
inline constexpr Table tally_end{0x00, 0x1E8B};  // the tally's closing 2x2 entries
// Points (BCD, three bytes, in tens): add_score.
inline constexpr Table points_100{0x00, 0x1ECC};
inline constexpr Table points_500{0x00, 0x1ECF};
inline constexpr Table points_1000{0x00, 0x1ED2};
inline constexpr Table points_2000{0x00, 0x1EF0};
inline constexpr Table time_bonus_959{0x00, 0x1F2C};         // the bonus for exactly 9:59
inline constexpr Table time_bonus_seconds{0x00, 0x1FCC};     // under a minute: pointers by seconds
inline constexpr Table time_bonus_steps{0x00, 0x2040};       // by minutes and tens of seconds
inline constexpr Table hud_sprites{0x00, 0x227B};            // 12 x (Y, X, tile)
inline constexpr Table act_headers{0x00, 0x2338};            // per zone, per act
inline constexpr Table demos{0x00, 0x258F};                  // 8 x (zone, bank, input address)
inline constexpr Table card_streaks_x{0x00, 0x29C7};         // the title card's 16 streaks
inline constexpr Table zone_names{0x00, 0x2A03};             // the title card's text per zone
inline constexpr Table act_numbers{0x00, 0x2A11};            // the title card's big digit per act
inline constexpr std::uint16_t card_act_clear_text = 0x2C6F; // texts the title card shows
inline constexpr std::uint16_t card_zone_text = 0x2CC3;
inline constexpr std::uint16_t card_passed_text = 0x2D13;
inline constexpr Table tally_rings_label{0x00, 0x2D3F}; // map entries, 2 x 5
inline constexpr Table tally_bonus_label{0x00, 0x2D53};
inline constexpr Table tally_score_label{0x00, 0x2D67};
inline constexpr Table tally_sonic{0x00, 0x2D7B};      // 6 x 4
inline constexpr Table walk_frame_ticks{0x00, 0x3924}; // by speed
inline constexpr Table roll_frames{0x00, 0x396D};
inline constexpr Table roll_frame_ticks{0x00, 0x3981}; // by speed

// --- Bank 1 -------------------------------------------------------------------
inline constexpr Table slope_forces{0x01, 0x402E};
inline constexpr Table start_positions{0x01, 0x5286}; // per zone, per act
inline constexpr Table map_headers{0x01, 0x546A};     // per zone, per act (22 bytes each)
inline constexpr Table background_rows{0x01, 0x5860}; // row offsets of the background map
inline constexpr Table object_states{0x01, 0x5E41};   // per type: its state table
inline constexpr Table enemy_points{0x01, 0x617D};    // per enemy type: its points routine
inline constexpr std::uint16_t points_100_routine = 0x1D14;
inline constexpr Table rain_positions{0x01, 0x69EB}; // 16 x (dy, dx)
inline constexpr Table rain_columns{0x01, 0x6A76};   // where the rain starts and stops
inline constexpr Table conveyor_blocks{0x01, 0x6C05};
inline constexpr Table map_ring_quarters{0x01,
                                         0x72CB};     // per ring block: quarters; +20: replacement
inline constexpr Table zone_pictures{0x01, 0x762F};   // the title card's picture per zone
inline constexpr Table zone_card_tiles{0x01, 0x772E}; // the title card's zone tiles
inline constexpr Table object_tile_lists{0x01, 0x782C}; // object tile requests 10-1F
inline constexpr Table object_tiles_14{0x01, 0x78BF};   // requests 20 and above (bank 14)
inline constexpr Table object_tiles_07{0x01, 0x78EF};   // requests below 10 (bank 7)
inline constexpr Table level_tiles{0x01, 0x7903};       // per zone * 3 + act: 7 bytes
inline constexpr Table act_palettes{0x01, 0x7BA1};      // the fade in per zone * 3 + act

// --- Banked data ------------------------------------------------------------
inline constexpr Table card_letters{0x09, 0x8113}; // compressed tiles
inline constexpr Table card_frame{0x09, 0x8FB3};
inline constexpr Table card_corners{0x09, 0x9049};
inline constexpr Table card_digits{0x07, 0xA0E0};
inline constexpr Table fade_palettes{0x09, 0xAE45}; // 32 colours each
inline constexpr Table tube_path_y{0x09, 0xB4C5};
inline constexpr Table tube_path_x{0x09, 0xB933};
inline constexpr Table intro_tiles{0x08, 0xA832};
inline constexpr Table title_figures{0x08, 0xA9A2};
inline constexpr Table title_figures2{0x08, 0xAC32};
inline constexpr Table title_map{0x08, 0xADA2};        // 18 x 20 entries; its first row is blank
inline constexpr Table press_start_text{0x08, 0xAFFA}; // 1 x 20
inline constexpr Table intro_sky_tiles{0x13, 0xB762};
inline constexpr Table intro_cliff_tiles{0x13, 0xB9EE};
inline constexpr Table intro_figure_tiles{0x13, 0xBC1E};
inline constexpr Table game_over_tiles{0x0F, 0xB380};
inline constexpr Table continue_tiles{0x0F, 0xB570};
inline constexpr Table continue_sonic_tiles{0x0F, 0xB75A};
inline constexpr Table game_over_map{0x0F, 0xBB7E};
inline constexpr Table continue_map{0x0F, 0xBBC6};
inline constexpr Table continue_digits{0x0F, 0xBC0E}; // 2x2 entries per digit
inline constexpr Table title_tiles{0x18, 0x9600};
inline constexpr Table sega_logo_tiles{0x18, 0xB3CC};
inline constexpr Table font{0x18, 0xB76C};
inline constexpr Table sega_logo_map{0x1D, 0xB200};
inline constexpr Table credits_starts{0x1D, 0xB412}; // per letter: X and speed
inline constexpr Table credits_places{0x1D, 0xB432}; // per letter: X
inline constexpr Table credits_text{0x1D, 0xB442};   // 16 characters a line
inline constexpr Table swing_angles_above{0x1C, 0x88C0};
inline constexpr Table swing_angles_below{0x1C, 0x88CA};
inline constexpr Table moving_pole_angles_above{0x1C, 0x8CA1};
inline constexpr Table moving_pole_angles_below{0x1C, 0x8CAB};
inline constexpr Table capsule_cameras{0x1C, 0x9088}; // per zone: camera X, Y
inline constexpr Table capsule_animals{0x1C, 0x91E1};
inline constexpr Table green_hills_boss_attacks{0x1C, 0x957A};
inline constexpr Table boulder_offsets{0x1C, 0x99ED};
inline constexpr Table hopping_enemy_jump{0x1C, 0xBB2C};
inline constexpr Table placements{0x1E, 0x8135}; // per zone, per act
// Palette cycles by kind (palettes.cpp).
inline constexpr Table palette_cycle_4{0x1E, 0xAF16};
inline constexpr Table palette_cycle_2{0x1E, 0xAF28};
inline constexpr Table palette_cycle_5{0x1E, 0xAF3A};
inline constexpr Table palette_cycle_7{0x1E, 0xAF4C};
inline constexpr Table palette_cycle_6{0x1E, 0xAF5E};
inline constexpr Table palette_cycle_11{0x1E, 0xAF8E};
inline constexpr Table palette_cycle_8{0x1E, 0xAF9A};
inline constexpr Table palette_sets_9{0x1E, 0xAFD2}; // whole background palettes
inline constexpr Table palette_cycle_14{0x1E, 0xB04A};
inline constexpr Table ring_flash_a{0x1E, 0xB032}; // colours 9-14, two sets
inline constexpr Table ring_flash_b{0x1E, 0xB03E};
inline constexpr Table fireball_speeds{0x1E, 0x8EBE};
inline constexpr Table final_boss_attacks{0x1E, 0x932C}; // per hits left: a list of states
inline constexpr Table sprite_frames{0x1F, 0x8000};      // per type: its frame records
inline constexpr Table lost_ring_vx{0x1F, 0xAF32};
inline constexpr Table lost_ring_vy{0x1F, 0xAF40};
inline constexpr Table bubble_speeds{0x1F, 0xB3AD};
inline constexpr Table water_surface_x{0x1F, 0xB57F}; // ten places 16 pixels apart
inline constexpr Table monitor_items{0x1F, 0xB797};
inline constexpr Table sign_cameras{0x1F, 0xBA26}; // per zone and act: camera X, Y
inline constexpr Table sonic_tiles{0x1F, 0xBCAB};  // per frame: bank, address, chunks
inline constexpr Table sonic_tiles_mirrored{0x1F, 0xBE37};

} // namespace sonic2_gg::rom
