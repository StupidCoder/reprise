#pragma once

// Object type numbers.
//
// Every act starts with a placement list of (type, cell X, cell Y) records that
// fill object slots 1..31; enemies also allocate free slots for projectiles,
// explosions and similar short-lived objects. The type number selects the update
// routine (Game::update_object), the activation distances (Level::active_bounds)
// and, implicitly, the art. The names below describe what each type does in this
// game; they are not taken from any external source.
namespace sonic1_gg::object_type {

inline constexpr int none = 255; // An empty slot.

// --- Monitors and collectables (objects.cpp) ------------------------------------
inline constexpr int ring_monitor = 1;          // +10 rings.
inline constexpr int shoes_monitor = 2;         // Speed shoes for 240 x 4 updates.
inline constexpr int life_monitor = 3;          // Extra life, once per act.
inline constexpr int shield_monitor = 4;        // Absorbs one hit.
inline constexpr int invincibility_monitor = 5; // 240 x 4 updates of invincibility.
inline constexpr int checkpoint_monitor = 0x51; // Sets the restart position.
inline constexpr int continue_monitor = 0x52;   // Awards a continue at the act's end.
inline constexpr int chaos_emerald = 6;
inline constexpr int goal_sign = 7;
inline constexpr int lost_rings = 0x55; // The rings scattered by a hit.

// --- Common enemies and effects (objects.cpp, boss.cpp) ----------------------------
inline constexpr int crab = 8;                  // Walks, pauses and fires two arcing shots.
inline constexpr int explosion = 10;            // Destroyed enemy; also bounces a falling Sonic.
inline constexpr int projectile = 13;           // Shared enemy/boss shot, damaging only.
inline constexpr int flying_shooter = 14;       // Flies in from the right, fires downward.
inline constexpr int ground_bug = 16;           // Crawls left and right along the terrain.
inline constexpr int wall_shooter = 17;         // Stationary; appears, fires left, hides.
inline constexpr int leaping_fish = 38;         // Jumps straight up out of the water.
inline constexpr int freed_animal_pausing = 35; // Released from the capsule.
inline constexpr int freed_animal_bouncing = 36;
inline constexpr int capsule = 37; // The animal capsule after each zone's boss.

// --- Platforms and scenery (objects.cpp) ------------------------------------------
inline constexpr int path_platform = 9;     // Follows a table-driven 2D path.
inline constexpr int sinking_platform = 11; // Sinks half a block while ridden.
inline constexpr int sliding_platform = 15; // Slides 160 px back and forth.
inline constexpr int cycling_platform = 39; // Rises and snaps back to its origin.
inline constexpr int paired_platform = 40;  // Follows the cycling platform's height.
inline constexpr int floating_log = 41;     // Drifts with the speed of its rider.
inline constexpr int launch_trigger = 75;   // Invisible; throws a grounded Sonic up.
inline constexpr int flowers = 0x50;        // Animates background flower tiles.

// --- Bosses ---------------------------------------------------------------------------
inline constexpr int green_hill_boss = 18;
inline constexpr int jungle_boss = 44;
inline constexpr int bridge_boss = 72;
inline constexpr int labyrinth_boss = 73;
inline constexpr int scrap_brain_chase = 34; // The pursuit and lift ending Scrap Brain 3.
inline constexpr int final_boss = 74;
inline constexpr int final_boss_orb = 71;   // The homing orb fired in the final room.
inline constexpr int final_boss_flame = 86; // Floor flames of the final room.

// --- Bridge (bridge.cpp) -----------------------------------------------------------
inline constexpr int spiked_walker = 45;    // Spiked back (always hurts), soft front.
inline constexpr int collapsing_plank = 46; // A bridge plank that drops after a delay.
inline constexpr int rising_platform = 59;  // Accelerates up and down.
inline constexpr int seesaw = 78;

// --- Jungle (jungle.cpp) ------------------------------------------------------------
inline constexpr int falling_platform = 12; // Falls a moment after being touched.
inline constexpr int boss_bomb = 43;        // Dropped by the Jungle boss.

// --- Labyrinth (labyrinth.cpp) --------------------------------------------------
inline constexpr int pressure_switch = 29;
inline constexpr int air_bubble = 32;   // Large bubble that refills Sonic's air.
inline constexpr int homing_trail = 42; // Smoke left behind by the homing shot.
inline constexpr int homing_shot = 47;  // Labyrinth boss shot that steers toward Sonic.
inline constexpr int spike_ball = 53;   // Ball with four orbiting spikes.
inline constexpr int loose_spike = 54;  // A spike shed by the spike ball.
inline constexpr int swimmer = 60;      // Slow fish, emits bubbles.
inline constexpr int path_hazard = 61;  // Damaging object on a looping path.
inline constexpr int spike_chain = 62;  // Extending/retracting spikes on a timer.
inline constexpr int spear = 63;        // Shoots out of a wall periodically.
inline constexpr int water_level = 64;  // Raises and lowers the water surface.
inline constexpr int bubble_source = 65;
inline constexpr int small_bubble = 66;
inline constexpr int unused_67 = 67;        // Its update routine is a bare RET.
inline constexpr int jumping_swimmer = 68;  // Jumps at a nearby Sonic.
inline constexpr int buoyant_platform = 69; // Floats up once stood on.

// --- Scrap Brain and Sky Base (scrap_brain_sky_base.cpp) -------------------------
inline constexpr int area_exit = 19;       // Invisible sensor leading to a linked room.
inline constexpr int electric_right = 20;  // Electric bolt travelling right.
inline constexpr int electric_left = 21;   // Electric bolt travelling left.
inline constexpr int flame_jet = 22;       // Vertical burst of fire.
inline constexpr int door_from_left = 23;  // Opens when Sonic approaches from the left.
inline constexpr int door_from_right = 24; // Opens when Sonic approaches from the right.
inline constexpr int door_both_sides = 25; // Opens from either side.
inline constexpr int electric_arc = 26;    // Timed arc between two poles.
inline constexpr int bomb_thrower = 27;
inline constexpr int bouncing_bomb = 28;
inline constexpr int switch_door = 30; // Opened by a pressure switch.
inline constexpr int crawler = 31;     // Two-ended crawler with a spiked end.
inline constexpr int cloud = 48;       // Foreground cloud in Sky Base.
inline constexpr int spark_pair = 49;
inline constexpr int mine = 50; // Wanders; bursts when Sonic is near.
inline constexpr int ceiling_launcher = 51;
inline constexpr int launcher_shot = 52;
inline constexpr int rotating_gun = 55;
inline constexpr int sky_platform = 56; // Bobs with Sky Base's swaying camera.
inline constexpr int crusher = 57;      // Pushes horizontally toward Sonic.
inline constexpr int hidden_gun = 58;   // Fires when Sonic is in its line.

// --- Special stages (special_stage.cpp) ------------------------------------------
inline constexpr int moving_bumper = 33;
inline constexpr int flipper = 76;

// --- Ending (final_boss.cpp) --------------------------------------------------------
inline constexpr int ending_flyby = 83;   // Final escape: the boss flies past.
inline constexpr int ending_emerald = 84; // The emeralds rising in the good ending.

// Types >= this value are never activated by the object scan.
inline constexpr int first_inactive = 0x57;

} // namespace sonic1_gg::object_type
