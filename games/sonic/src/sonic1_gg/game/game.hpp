#pragma once
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/level_data.hpp"
#include "sonic1_gg/game/body.hpp"
#include "sonic1_gg/game/object_types.hpp"
#include "sonic1_gg/game/score.hpp"
#include "sonic1_gg/game/sonic.hpp"

// The in-act game state of Sonic 1 (Game Gear) and its per-update logic.
//
// One Game is one act being played: Sonic (sonic.hpp), the camera, the 32 object
// slots and the act's mutable level data, plus groups of zone-specific state
// (water, the Sky Base sway, the boss fight, ...). Game::tick() runs one update
// of the original main loop (once per video frame, ~59.92 Hz). The frontend,
// results, game over and ending screens are driven by Session (frontend/).
//
// Where things are:
//   game.cpp            reset, the main update, palettes, the camera
//   sonic.cpp           Sonic's movement, animation, rings, damage
//   terrain.cpp         collision with the level, terrain actions
//   objects.cpp         object activation, shared helpers, the handler table
//   common_objects.cpp  monitors, the goal sign, common enemies and platforms
//   boss.cpp, bridge.cpp, jungle.cpp, labyrinth.cpp,
//   scrap_brain_sky_base.cpp, final_boss.cpp, special_stage.cpp
//                       zone-specific objects and bosses
//   rom_tables.hpp      names of the ROM data tables the objects read
//
// Hexadecimal numbers in comments such as "4B63" are Z80 addresses in the ROM's
// fixed bank 0 or in the banked area; "D415" style numbers are work-RAM
// addresses. They link the reconstruction back to the original code it was
// derived from. Behaviour is reproduced as the cartridge executes it, including
// its quirks; comments point those out where they matter.
namespace sonic1_gg {

// --- State groups of Game -----------------------------------------------------

// Palette flash of a hit boss or a special stage bumper.
struct PaletteFlash {
    int color{};    // Entry flashed: low nibble = entry, bit 7 = sprite palette
    int value{};    // The flash colour
    int ticks{};    // D2B1: remaining flash updates (also a boss's hit immunity)
    bool written{}; // The flash overwrote a colour this update
};

// Animated background tiles (Green Hill waterfalls, Jungle), D28E-D293.
struct AmbientTiles {
    int ticks{}, phase{};
    int pending{-1};  // Frame selected this update, uploaded on the next
    int uploaded{-1}; // Frame currently in the tile patterns
};

// The single queued 2x2 nametable patch (D2AB-D2AF), shared by ring collection,
// collapsing planks and the capsule; the next video interrupt releases it.
// x has bit 15 set while the slot is free.
struct TilePatch {
    int x{0xffff}, y{}, table{};
};

// Scrap Brain's electric floors: a 171-step cycle (see Game::tick).
struct ElectricFloors {
    int ticks{};
    int palette{}; // 0 off, 1 warning, 2 live
};

// Sky Base: the camera and the riding platforms share one vertical sway.
struct SkySway {
    int offset{};          // Accumulated 24-bit displacement
    int velocity{};        // Signed 16-bit, at most 2 px/update
    int acceleration{2};   // +-2, reversing past +-32 pixels
    int camera_fraction{}; // Sub-pixel part of the camera's Y
};

// Labyrinth water.
struct Water {
    int y{2048};         // Water surface in world pixels
    int scanline{};      // Surface row on screen: 0 none visible, 255 all water
    bool boss_palette{}; // The boss act's underwater colours
    std::vector<std::array<int, 3>> wave_sprites; // Surface wave sprites: tile, x, y
};

// The current boss fight.
struct BossFight {
    int arena_x{}, arena_y{}; // D275/D277: camera position of the arena
    int hits{};               // D2ED: eight hits defeat a boss, twelve the final one
    int explosion_ticks{};    // D2EE
    int defeat_phase{};       // D2EF
    bool final_barrier{};     // The final room's barrier is up
    bool orb_active{};        // A final boss orb exists
};

// The scripted escape from the final room and the good ending.
struct EndingScript {
    bool sonic_hidden{};    // Sonic is off stage (waiting to drop in)
    bool camera_scripted{}; // The camera follows the flyby, not Sonic
    bool drop{};            // Sonic is dropping in
    bool collect{};         // Good ending: Sonic walks back for the emeralds
    bool emerald_spawned{}, emerald_pose{};
    int pose_ticks{};
};

// Special stage bumpers and clock.
struct SpecialStage {
    bool time_out{};
    int bumper_ticks{}; // Round bumper cooldown and sparkle timer
    int bumper_x{}, bumper_y{};
};

// The goal sign.
struct GoalSign {
    bool started{}; // The sign is spinning: the clock stops
    int rings{};    // Rings held when the sign activated
    int outcome{};  // Face: 1 no reward, 2 extra life, 3 ten rings, 4 special stage
    int entry_speed{};
    int bonus_table{}; // ROM table of the speed bonus awarded at the sign
};

// Progress kept across restarts and carried to the next act.
struct Progress {
    unsigned emerald_acts{}; // Acts whose chaos emerald was collected
    int emerald_count{};
    std::uint64_t life_monitor_acts{}; // D306-D30B: acts whose life monitor was used
    int life_monitor_count{};          // Total life monitors; unlocks Sky Base 3's monitor
    unsigned switch_acts{};            // D318-D31B: pressure switch state per act
    bool continue_collected{};         // A continue monitor was broken
    bool special_stage_pending{};      // The goal sign's special stage face was shown
    bool ever_died{};                  // Any death in this game (affects the ending)
};

struct Game {
    explicit Game(const Level& assets);

    // =========================================================================
    // Level data
    // =========================================================================

    const Level& source; // The act as extracted; never modified.
    Level level;         // Working copy: collected rings, patched tiles, palettes.

    // =========================================================================
    // Sonic, the camera and the objects
    // =========================================================================

    Sonic sonic;
    Camera camera;
    std::array<Object, 32> objects;

    unsigned ticks{};         // D224: updates since the act started
    unsigned warmup_ticks{};  // Neutral updates run while the act fades in (1472)
    Input current_input{};    // Input as modified by terrain and scripted scenes
    int pending_camera_top{}; // Camera dead zone requested by an object this update
    int pending_camera_bottom{};
    int shared_platform_y{};      // D303: cycling platform height for the paired platform
    unsigned foreground_clouds{}; // Sky Base clouds drawn this update (at most three)
    unsigned random_seed{};       // D2D8: 16-bit random generator, kept across acts

    // =========================================================================
    // Score, rings, lives and power-ups
    // =========================================================================

    int rings{};
    int score{};
    int lives{3};
    int elapsed_ticks{};             // Act clock (special stages count down)
    int death_count{};               // Deaths in this act; chooses the goal sign's face
    unsigned next_life_score_bcd{5}; // D2F8: next extra-life score in 10,000s (BCD)

    // Rings lost this update; a presentation event for the scattered-ring
    // enhancement. Not part of the original state and not saved.
    int rings_lost_this_tick{}, ring_loss_x{}, ring_loss_y{};

    // Sparkle left by the last collected ring (D31E/D320/D322).
    int ring_sparkle_x{}, ring_sparkle_y{}, ring_sparkle_ticks{};

    // =========================================================================
    // Death, the goal and the end of the act
    // =========================================================================

    bool death_finished{}; // Death sequence over; the Session takes over (fade, game over)
    bool demo_mode{};      // Attract demo: a death ends the demo instead
    bool demo_ended{};
    int checkpoint_x{}, checkpoint_y{}; // Restart position set by a checkpoint monitor

    GoalSign goal;
    int finish_ticks{};        // Countdown to the end of the act; it completes at zero
    bool complete{};           // The act is over; Session moves to results/next act
    bool auto_run{};           // D206 bit 1: Sonic runs right on his own (after a goal)
    int music_restore_ticks{}; // Restores the level music after the emerald jingle

    // Progress that survives a restart from a checkpoint (and carries on to the
    // next act).
    Progress progress;

    // =========================================================================
    // Palette effects
    // =========================================================================

    int palette_ticks{}, palette_phase{}; // Background palette cycling (waterfalls, lights)
    int sprite_palette_kind{};            // Sprite palette source: 0 act, 1 boss, 2 goal sign
    PaletteFlash flash;
    AmbientTiles ambient;

    // =========================================================================
    // Level features
    // =========================================================================

    // Horizontally auto-scrolling acts (Bridge 2): the camera limits creep right.
    bool horizontal_scroll{};
    // D200 bit 5: new terrain is drawn as the camera moves. Off in boss arenas,
    // where it also disables the explosion bounce.
    bool terrain_streaming{true};
    TilePatch tile_patch;
    ElectricFloors electric;
    SkySway sway;
    Water water;
    // Teleporter (Scrap Brain): pose countdown and the matching table entry.
    int teleport_ticks{}, teleport_table{};
    // Linked rooms: leaving through an exit reloads another area of the act.
    bool area_transition{}, area_advance{};
    int area_target{}; // D2D4

    BossFight boss;
    EndingScript ending;

    bool special_mode{}; // Special stage physics (always curled, bumpers)
    SpecialStage special;

    // =========================================================================
    // Per-update outputs consumed by rendering and audio
    // =========================================================================

    std::vector<SoundEvent> sound_events;           // Music and effects started this update
    std::vector<std::array<int, 3>> effect_sprites; // Stars and ring sparkle: tile, x, y

    // =========================================================================
    // Widescreen enhancement (widescreen.cpp)
    // =========================================================================
    //
    // The host reports the presented view size before each update. The default
    // 160x144 is the Game Gear LCD and leaves every widescreen rule inert, so the
    // original format and the attract demos keep the original behaviour.
    int view_width{160}, view_height{144};
    // Pixels the presented view extends beyond the LCD crop on each side. Camera-
    // relative off-screen tests add these so objects never appear or vanish inside
    // the visible area.
    int wide_left{}, wide_right{}, wide_top{}, wide_bottom{};
    // Extra activation reach beyond the view: some objects draw sprites well
    // away from their position (Labyrinth's spike chain).
    static constexpr int wide_slack = 32;

    bool widescreen() const { return wide_left || wide_right || wide_top || wide_bottom; }

    // Camera-relative edges for off-screen tests: camera position plus
    // `offset`, pushed outward by the widescreen margin (zero on the LCD), so
    // that objects keep running until they leave the presented view.
    int left_edge(int offset) const { return camera.x + offset - wide_left; }

    int right_edge(int offset) const { return camera.x + offset + wide_right; }

    int top_edge(int offset) const { return camera.y + offset - wide_top; }

    int bottom_edge(int offset) const { return camera.y + offset + wide_bottom; }

    void update_wide_view();
    // Camera limits that move with the camera rather than enclose an arena:
    // auto-scrolling, or a goal sign or capsule setting camera_left = camera.x
    // every update (61F8, 736B).
    bool rolling_limits() const;
    bool wide_activation(Object&, bool original, bool reach, bool wide, bool was_active);
    void stage_object(Object&);

    // =========================================================================
    // Act lifecycle (game.cpp, objects.cpp)
    // =========================================================================

    // Clears everything to the act's initial state.
    void reset();
    // Starts the act like the cartridge's loader: zeroed body, 16 neutral updates.
    void start_act(bool demo = false);
    // Reloads the act after a death, keeping score, progress and the checkpoint.
    void restart_from_checkpoint(int remaining_lives);
    // One update of the main loop.
    void tick(Input input);

    // =========================================================================
    // The world around Sonic (game.cpp, terrain.cpp, objects.cpp)
    // =========================================================================

    // Moves a body by its velocity and resolves terrain collision.
    void integrate_body(Body& body) const;
    void update_camera();
    void update_ring_sparkle();
    void queue_effect_sprite(int tile, int x, int y);
    void add_rings(int amount);

    void add_score(int points) {
        award_points(score, lives, next_life_score_bcd, points, sound_events);
    }

    void sound(Effect effect) {
        sound_events.push_back({SoundEvent::Kind::Effect, unsigned(effect)});
    }

    void music(Music track) { sound_events.push_back({SoundEvent::Kind::Music, unsigned(track)}); }

    // =========================================================================
    // The object system (objects.cpp)
    // =========================================================================

    void reset_objects();
    void update_objects();
    // Runs the handler of the object's type (see the table in objects.cpp).
    void update_object(Object& object);

    // Helpers shared by the object handlers.
    void animate_object(Object& object, int sequence, int frames);
    bool overlaps(const Object& object, int inset_x, int inset_y) const;
    void enemy_contact(Object& object, int inset_x, int inset_y, int kill_dx = 0, int kill_dy = 0);
    void land_on_platform(const Object& object, int top, int dy);
    Object* allocate_object(int type);
    bool fire_projectile(int x, int y, int vx, int vy, int gravity);
    void emit_sprite(Object& object, int tile, int dx = 0, int dy = 0);
    void kill_object(Object& object, int offset_x = 0, int offset_y = 0);
    void spawn_explosion(const Object& parent);
    void queue_tile_patch(int x, int y, int table);
    void submit_object_mapping(Object&, int address, int x, int y);
    unsigned next_random();

    // Called once per video interrupt by the Session: the cartridge's interrupt
    // handler also advances the random generator (00D8).
    void video_interrupt() { next_random(); }

    // =========================================================================
    // Object handlers, one per type (see object_types.hpp)
    // =========================================================================

    // Common to several zones (common_objects.cpp).
    void update_monitor(Object&);
    void update_chaos_emerald(Object&);
    void update_goal_sign(Object&);
    void update_lost_rings(Object&);
    void update_explosion(Object&);
    void update_projectile(Object&);
    void update_crab(Object&);
    void update_flying_shooter(Object&);
    void update_ground_bug(Object&);
    void update_wall_shooter(Object&);
    void update_leaping_fish(Object&);
    void update_path_platform(Object&);
    void update_sinking_platform(Object&);
    void update_sliding_platform(Object&);
    void update_cycling_platform(Object&);
    void update_paired_platform(Object&);
    void update_floating_log(Object&);
    void update_launch_trigger(Object&);
    void update_flowers(Object&);
    void update_nothing(Object&);

    // Bosses and the capsule (boss.cpp); hits and defeat are shared.
    void update_green_hill_boss(Object&);
    void update_boss_contact(Object&, int mapping_offset = 0x12);
    void start_boss_fight(int arena_x, int arena_y, bool boss_palette = true);
    void lock_boss_arena(int arena_x, int arena_y);
    void use_boss_palette();
    void stop_streaming_in_arena();
    void submit_engine_flame(Object&);
    void submit_lift_jet(Object&);
    void update_capsule(Object&);
    void update_animal(Object&);

    // Bridge (bridge.cpp).
    void update_spiked_walker(Object&);
    void update_collapsing_plank(Object&);
    void update_rising_platform(Object&);
    void update_seesaw(Object&);
    void update_bridge_boss(Object&);
    void collapse_bridge(int action);

    // Jungle (jungle.cpp).
    void update_falling_platform(Object&);
    void update_boss_bomb(Object&);
    void update_jungle_boss(Object&);

    // Labyrinth (labyrinth.cpp).
    void update_pressure_switch(Object&);
    void update_swimmer(Object&);
    void update_jumping_swimmer(Object&);
    void update_path_hazard(Object&);
    void update_spike_chain(Object&);
    void update_spear(Object&);
    void update_spike_ball(Object&);
    void update_loose_spike(Object&);
    void update_water_level(Object&);
    void update_bubble_source(Object&);
    void update_small_bubble(Object&);
    void update_air_bubble(Object&);
    void update_buoyant_platform(Object&);
    void update_homing_shot(Object&);
    void update_homing_trail(Object&);
    void update_labyrinth_boss(Object&);
    void spawn_bubble(const Body&, int source_type);
    void update_air_supply();

    // Scrap Brain and Sky Base (scrap_brain_sky_base.cpp).
    void update_area_exit(Object&);
    void update_door(Object&);
    void update_electric_bolt(Object&);
    void update_electric_arc(Object&);
    void update_flame_jet(Object&);
    void update_spark_pair(Object&);
    void update_bomb_thrower(Object&);
    void update_bouncing_bomb(Object&);
    void update_crawler(Object&);
    void update_mine(Object&);
    void update_ceiling_launcher(Object&);
    void update_launcher_shot(Object&);
    void update_rotating_gun(Object&);
    void update_hidden_gun(Object&);
    void update_crusher(Object&);
    void update_cloud(Object&);
    void update_sky_platform(Object&);
    void update_scrap_brain_chase(Object&);
    void spawn_cannon_shot(const Object&, int vx, int vy, int dx, int dy);

    // The final boss and the ending (final_boss.cpp).
    void update_final_boss(Object&);
    void update_final_boss_orb(Object&);
    void update_final_boss_flame(Object&);
    void update_ending_flyby(Object&);
    void update_ending_emerald(Object&);

    // Special stages (special_stage.cpp).
    void update_special_clock();
    void special_terrain(int action);
    void update_moving_bumper(Object&);
    void update_flipper(Object&);
};

} // namespace sonic1_gg
