#pragma once

// Addresses of the ROM data tables the game logic reads: behaviour scripts,
// paths, animation sequences, sprite layouts and tile patches.
//
// The asset pack keeps these tables under their original addresses
// (Level::object_tables), so the code reads them as level.data(address) or
// level.word(address), just as the cartridge does. Naming them here keeps the
// handlers readable while preserving the link to the ROM.
//
// Conventions: a "layout" is a sprite layout (up to 3 rows of six 8x16
// sprites, 18 bytes; Object::mapping); an "animation" is a sequence of
// (frame, duration) pairs for Game::animate_object, whose frames are layouts
// 18 bytes apart starting at the matching "frames" address.
namespace sonic1_gg::rom {

// --- Sonic ------------------------------------------------------------------------
inline constexpr int sonic_animations = 0x5c5b;  // Pointer table + sequences
inline constexpr int star_offsets = 0x510a;      // Invincibility stars: (dx, dy) pairs
inline constexpr int ring_erase_patch = 0x5c53;  // Tiles replacing a collected ring
inline constexpr int teleporters = 0x58e0;       // 5 x (cell x, cell y, destination...)
inline constexpr int air_warning_rates = 0x52fa; // Chime interval masks per countdown digit

// --- Monitors, emerald, lost rings, explosion, shots -------------------------------
inline constexpr int monitor_with_icon = 0x5ea4; // The renderer adds the icon
inline constexpr int monitor_flicker = 0x5e97;
inline constexpr int emerald_layout = 0x61f1;
inline constexpr int lost_ring_frames = 0x7c53; // 3-byte entries: layout word, art frame
inline constexpr int explosion_animation = 0x69bb;
inline constexpr int explosion_frames = 0x69c2;
inline constexpr int shot_tiles = 0x6bd5;      // Two tiles, alternating
inline constexpr int boss_shot_tiles = 0x6bd7; // Bridge 3 and Labyrinth 3

// --- Goal sign ------------------------------------------------------------------------
inline constexpr int goal_frames = 0x652d;
inline constexpr int goal_idle = 0x64c2; // Frame sequences ...
inline constexpr int goal_spin = 0x64a8;
inline constexpr int goal_face_none = 0x64df;
inline constexpr int goal_face_life = 0x64c5;
inline constexpr int goal_face_rings = 0x6513;
inline constexpr int goal_face_special = 0x64f9;

// Speed bonus tables (8 bytes each: layout word, offset word, BCD points).
constexpr int goal_speed_bonus(int index) {
    return 0x65bd + index * 8;
}

// --- Common enemies -----------------------------------------------------------------
// Each scripted enemy has a looping action script, a table of animation
// sequences per action, and its frames.
inline constexpr int crab_script = 0x66d0;
inline constexpr int crab_animations = 0x66eb;
inline constexpr int crab_frames = 0x6704;
inline constexpr int flying_shooter_script = 0x6d3c;
inline constexpr int flying_shooter_animations = 0x6d47;
inline constexpr int flying_shooter_frames = 0x6d5e;
inline constexpr int ground_bug_script = 0x6eef;
inline constexpr int ground_bug_animations = 0x6f0a;
inline constexpr int ground_bug_frames = 0x6f24;
inline constexpr int wall_shooter_layout = 0x7046;
inline constexpr int fish_animation = 0x7e0b;
inline constexpr int fish_frames = 0x7e10;
inline constexpr int fish_splash_layout = 0x7e26;

// --- Platforms and scenery ----------------------------------------------------------
inline constexpr int platform_green_hill = 0x6910;
inline constexpr int platform_bridge = 0x6930;
inline constexpr int platform_other_zones = 0x6922;
inline constexpr int platform_path = 0x682e; // (dx, dy) byte pairs
inline constexpr int cycling_platform_layout = 0x7eb6;
inline constexpr int paired_platform_layout = 0x7eef;
inline constexpr int log_layout = 0x803a;
inline constexpr int log_spin_frames = 0x8031; // Layout offsets per spin step
inline constexpr int flower_phases = 0x7bc1;   // 4 bytes: even frame, odd frame, length
inline constexpr int flower_tiles = 0x7b99;    // 2x2 name table entries per frame

// --- Green Hill boss and capsule (boss.cpp) ----------------------------------------
// Each script repeats one action until that action switches to another script.
inline constexpr int gh_boss_fly_left = 0x72f0;
inline constexpr int gh_boss_fly_right = 0x72f3;
inline constexpr int gh_boss_descend_swoop_right = 0x72f6;
inline constexpr int gh_boss_descend_swoop_left = 0x72f9;
inline constexpr int gh_boss_climb = 0x72fc;
inline constexpr int gh_boss_swoop_right = 0x72ff;
inline constexpr int gh_boss_swoop_left = 0x730b;
inline constexpr int gh_boss_bob = 0x72b0;    // Signed vertical speeds
inline constexpr int gh_boss_facing = 0x7317; // Facing flag per action
inline constexpr int gh_boss_layout_right = 0x7347;
inline constexpr int gh_boss_layout_left = 0x7359;
inline constexpr int boss_hit_layouts = 0x795e; // Plus a per-boss offset (update_boss_contact)
inline constexpr int capsule_layout = 0x756d;
inline constexpr int capsule_layout_blink = 0x7591;
inline constexpr int capsule_pressed_layout = 0x757f;
inline constexpr int capsule_patches = 0x75a3; // Two alternating 12-byte patches
inline constexpr int capsule_open_patches = 0x75bb;
inline constexpr int capsule_button_heights = 0x754d;
inline constexpr int animal_bounce_green_hill = 0x7668;
inline constexpr int animal_bounce_other = 0x766d;
inline constexpr int animal_bounce_labyrinth = 0x7672;
inline constexpr int animal_bounce_frames = 0x7677;
inline constexpr int animal_ground_layouts = 0x7791; // 27 bytes per zone
inline constexpr int animal_air_layouts = 0x779f;

// --- Bridge ----------------------------------------------------------------------------
inline constexpr int walker_walk_left = 0x8389;
inline constexpr int walker_walk_right = 0x838e;
inline constexpr int walker_frames = 0x8393;
inline constexpr int plank_layout = 0x8496;
inline constexpr int plank_erase_patch = 0x84a3;
inline constexpr int rising_platform_layout = 0xb58f;
inline constexpr int seesaw_plank_tiles = 0x8845; // Tile lists, ended by a byte >= 0x80
inline constexpr int seesaw_weight_tiles = 0x8849;
inline constexpr int bridge_boss_layout = 0x866f;
inline constexpr int bridge_boss_starts = 0x8647;     // Two (x, y) word pairs
inline constexpr int bridge_boss_shots_left = 0x864f; // Offset, then 3 velocity pairs
inline constexpr int bridge_boss_shots_right = 0x865f;

// --- Jungle ----------------------------------------------------------------------------
inline constexpr int bomb_fuse_animation = 0x82d6;
inline constexpr int bomb_burst_animation = 0x82db;
inline constexpr int bomb_fuse_frames = 0x82e2;
inline constexpr int bomb_frames = 0xa384; // Shared with Scrap Brain's bombs
inline constexpr int jungle_boss_layout_left = 0x8209;
inline constexpr int jungle_boss_layout_right = 0x821b;

// --- Labyrinth ------------------------------------------------------------------------
inline constexpr int bubble_kinds = 0x9261; // Object type per random draw
inline constexpr int switch_pressed = 0xa454;
inline constexpr int switch_pressed_labyrinth = 0xa464;
inline constexpr int switch_released = 0xa45c;
inline constexpr int switch_released_labyrinth = 0xa46c;
inline constexpr int swimmer_right = 0x88c9;
inline constexpr int swimmer_left = 0x88ce;
inline constexpr int swimmer_frames = 0x88d3;
inline constexpr int hazard_path = 0x89a0; // (dx, dy) byte pairs
inline constexpr int hazard_layout = 0x8999;
inline constexpr int spike_chain_extend = 0x8bce; // Pose indices per update
inline constexpr int spike_chain_retract = 0x8bd6;
inline constexpr int spike_chain_in = 0x8bde;
inline constexpr int spike_chain_out = 0x8bdf;
inline constexpr int spike_chain_poses = 0x8be0; // 8 bytes: 3 (tile, dy), inset, height
inline constexpr int spear_left = 0x8d4a;
inline constexpr int spear_right = 0x8d52;
inline constexpr int water_level_steps = 0x8e4a; // Signed per-step motion
inline constexpr int wave_offsets = 0x8e3e;
inline constexpr int bubble_chances = 0x8ec7;
inline constexpr int bubble_source_animation = 0x8e93;
inline constexpr int bubble_source_frames = 0x8e9c;
inline constexpr int jumper_swim_right = 0x904a;
inline constexpr int jumper_swim_left = 0x904f;
inline constexpr int jumper_jump_right = 0x9054;
inline constexpr int jumper_jump_left = 0x9059;
inline constexpr int jumper_frames = 0x905e;
inline constexpr int buoyant_platform_layout = 0x91e8;
inline constexpr int homing_trail_tiles = 0x96f7;
inline constexpr int homing_shot_right = 0x968a;
inline constexpr int homing_shot_left = 0x969a;
inline constexpr int spike_ball_left = 0xb09b;
inline constexpr int spike_ball_right = 0xb0ad;
inline constexpr int spike_orbit = 0xaff7; // (x, y) offsets
inline constexpr int lab_boss_layout = 0x9495;
inline constexpr int lab_boss_stations = 0x947d; // (x, y) words per station
inline constexpr int lab_boss_arrive_y = 0x9489; // Word per station
inline constexpr int lab_boss_leave_y = 0x948f;

// --- Scrap Brain and Sky Base ---------------------------------------------------------
inline constexpr int area_exits = 0x9bae; // 5 x (cell x, cell y, area)
inline constexpr int bolt_right = 0x9c3e;
inline constexpr int bolt_left = 0x9c5c;
inline constexpr int flame_heights = 0x9d33;
inline constexpr int flame_sprites = 0x9d13; // Word per phase: 4 x (tile, dy)
inline constexpr int door_from_left_layouts = 0x9ef4;
inline constexpr int door_from_right_layouts = 0x9fb7;
inline constexpr int door_both_sides_layouts = 0xa07a;
inline constexpr int switch_door_layouts = 0xa4e3;
inline constexpr int arc_live = 0xa130;
inline constexpr int arc_warning = 0xa137;
inline constexpr int arc_frames = 0xa13c;
inline constexpr int thrower_idle = 0xa29b;
inline constexpr int thrower_throw = 0xa2a0;
inline constexpr int thrower_frames_left = 0xa2a3;
inline constexpr int thrower_frames_right = 0xa2d4;
inline constexpr int bouncing_bomb_animation = 0xa37a;
inline constexpr int bouncing_bomb_burst = 0xa37d;
inline constexpr int crawler_steps = 0xa682; // Signed dx words per step
inline constexpr int crawler_steps_reverse = 0xa732;
inline constexpr int crawler_lift = 0xa6ae;     // Vertical motion words per step
inline constexpr int crawler_segments = 0xa6da; // Segment x offsets, 22 per segment
inline constexpr int crawler_segments_reverse = 0xa75e;
inline constexpr int cloud_tiles = 0xaa29;
inline constexpr int spark_poses = 0xaac7;
inline constexpr int mine_wander_left = 0xacb7;
inline constexpr int mine_pause = 0xacbc;
inline constexpr int mine_wander_right = 0xacbf;
inline constexpr int mine_pause_again = 0xacc4;
inline constexpr int mine_primed = 0xacc7;
inline constexpr int mine_frames = 0xacd1;
inline constexpr int mine_burst_layout = 0xad19;
inline constexpr int launcher_animation = 0xadc3;
inline constexpr int launcher_frames = 0xadca;
inline constexpr int launcher_shot_layout = 0xae47;
inline constexpr int gun_sprites = 0xb1ed; // Per direction: 2 x (dx, dy, tile, -)
inline constexpr int gun_shots = 0xb22d;   // Per direction: vx, vy words, dx, dy bytes
inline constexpr int sky_platform_layout = 0xb355;
inline constexpr int propeller_sprites = 0xb362;
inline constexpr int crusher_layout = 0xb435;
inline constexpr int hidden_guns = 0xb4c0; // Per direction: shot and aiming square
inline constexpr int chase_run_animation = 0xa97d;
inline constexpr int villain_frames = 0xbb0a;
inline constexpr int lift_tiles = 0xa986;

// --- Final boss and ending --------------------------------------------------------------
inline constexpr int final_boss_poses = 0xba2d; // (dx, dy, layout word)
inline constexpr int final_boss_hop_layout = 0xbb2e;
inline constexpr int final_boss_light = 0xba42;
inline constexpr int final_boss_light_firing = 0xba4c;
inline constexpr int barrier_patches = 0xba56; // 15 x 12-byte tile patches
inline constexpr int villain_run_animation = 0xba39;
inline constexpr int villain_shaft_layout = 0xbb88;
inline constexpr int orb_animation = 0xbde5;
inline constexpr int orb_frames = 0xbdea;
inline constexpr int floor_flame_heights = 0xbc5c;
inline constexpr int floor_flame_sprites = 0xbc3c;
inline constexpr int floor_flame_positions = 0xbc6c;
inline constexpr int ending_emerald_layout = 0xbff8;
inline constexpr int flyby_layout = 0xbf28;
inline constexpr int flyby_hit_layout = 0xbf3a;

// --- Special stages ------------------------------------------------------------------
inline constexpr int bumper_bar_layout = 0x9b43;
inline constexpr int bumper_sparkle_layout = 0x550b;
inline constexpr int flipper_down = 0x9a53;
inline constexpr int flipper_middle = 0x9a65;
inline constexpr int flipper_up = 0x9a77;
inline constexpr int flipper_contour_down = 0x9973; // Surface height per x, per pose
inline constexpr int flipper_contour_middle = 0x9993;
inline constexpr int flipper_contour_up = 0x99b3;
inline constexpr int flipper_launch_x = 0x99d3; // Velocity words per x position
inline constexpr int flipper_launch_y = 0x9a13;

} // namespace sonic1_gg::rom
