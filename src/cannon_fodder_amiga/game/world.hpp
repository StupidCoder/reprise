#pragma once
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include <array>
#include <cstdint>
#include <random>
#include <vector>

// The state of a mission in play (.analysis/cannon_fodder_amiga/game.md
// "Game logic"): the objects, what the soldiers and bullets keep beyond
// their drawing fields, the squads and the squad's fire control. The rules
// that change it are in walking.hpp and combat.hpp.
namespace fodder::game {

// What a soldier needs beyond the drawing fields of GameObject; offsets of
// the original's object record in brackets.
struct Walker {
    int heading{};        // [$10] direction of travel, sine units 0-$1FE
    int speed{};          // [$36] 16.16 step = sine/4 x speed
    int facing{};         // [$3C] 0-$E (8 facings)
    int facing_timer{};   // [$3E]; also the falling body's heading (the original keeps
                          // both in this word: a soldier who lands from a fall walks on
                          // with that heading as his turn timer)
    int waypoint_x{}, waypoint_y{}; // [$26], [$28]
    int route_index{};    // [$40] byte offset into the squad's route (4 per waypoint)
    bool took_waypoint{}; // [$42]
    int idle{};           // [$43] byte
    int wait{};           // [$44] byte: ticks before heading again
    int pause{};          // [$45] byte
    int fire_delay{};     // [$4A] 0 waits for the squad's turn, -1 fires, n counts down
    int stopped{};        // [$4C] byte
    bool wading{};        // [$4F] in water
    int slow_target{};    // [$50] hidden lines aimed at by slow terrain
    int animation_state{}; // [$54] byte: 2 firing, 1 throwing, 3 (open)
    int animation_step{};  // [$55] byte
    int squeeze{};        // [$56] byte
    int animation_timer{}; // [$57] byte: ticks of the firing pose
    bool aim_override{};  // [$5A] face the aim point instead of the pointer
    int entering{};       // [$5B] byte
    int terrain{};        // [$60] byte: attribute under the soldier
    bool sinking{};       // [$61]
    int state{};          // [$38] 0 = normal
    int aim_x{}, aim_y{}; // [$2E], [$30]
    int step_counter{};   // [$3A]
    int carried{};        // [$6E] byte: in a vehicle
    int skill{};          // [$62] enemies: speed, fire rate, bullet range and speed
    int target{};         // [$5E] enemies: index into the recruits' lists, < 0 follows the leader
    int building{-1};     // [$70] enemies: the building (kind $48) they came out of
    int roster_index{-1}; // [$46] the recruit's roster entry
    int roster_weapon{};  // roster entry word 6: 2 or 3 change the walk frames
    int rank{};           // roster entry byte 2
    int recruit{};        // roster entry word 0: the recruit's name
    // Set by a hit (combat.cpp) and used while dying (walking.cpp)
    int state_timer{};    // [$12]
    int hit_timer{};      // [$64] byte: $FF when hit
    bool hit_from_behind{}; // [$59] (the original's test can never hold; always cleared)
    int shot_by_roster{}; // [$5E] enemies: the shooter's roster entry
    int shot_by_mark{};   // [$5D] byte
    std::int32_t vertical_speed{}; // [$1A] 16.16, up
    int height_fraction{}; // [$20] below GameObject::height
    int spin{-1};         // [$3A] while falling: the body's turning direction, -1 none; writhing: ticks
    int fall_count{};     // [$2A] ticks of the fall; writhing: ticks to the next sound
    bool shadowed{};      // [$58] the falling body has its shadow (or had)
    int hits{};           // [$74] byte: big blasts taken by a sturdy building; a hostage's tent count
    int idle_frame{};      // [$32] a hostage's frame standing still
    // Vehicles (vehicles.cpp)
    int vehicle_type{};   // [$6F] byte: 0, 1, 9 turrets; 2, 3, 10 ground vehicles; 4 tanks; 5-8 helicopters
    bool is_vehicle{};    // [$65] byte
    int rest_x{}, rest_y{}; // [$46], [$48] an enemy helicopter's home
    int patrol{};         // [$62] an enemy helicopter's patrol count
    bool fire_held{};     // [$57] a vehicle's shot waits for a free slot
    bool fire_request{};  // [$54] byte: fire now
    bool barrel{};        // [$5B] the gun's other barrel next
    bool gun_toggle{};    // [$55] the gun fires every second press
    int sink{};           // [$56] byte: sinking into a river bank
    bool land{};          // [$6E] a helicopter told to land; a turret entered
    bool crashing{};      // a helicopter going down (the original marks +$26 with $7171)
    int boarding{-1};     // [$66] a soldier's vehicle to get into
    int vehicle{-1};      // [$6A] the vehicle a soldier sits in
    int homing_target{-1}; // [$1A] a homing missile's target
    std::int32_t thrust{}; // [$6A] a missile's acceleration (16.16)
    bool wall_check{};    // [$32] a missile explodes on walls
    int flags75{};        // [$75] byte

    template <class A> void persist(A& a) {
        a(heading, speed, facing, facing_timer, waypoint_x, waypoint_y, route_index, took_waypoint, idle, wait, pause,
          fire_delay, stopped, wading, slow_target, animation_state, animation_step, squeeze, animation_timer,
          aim_override, entering, terrain, sinking, state, aim_x, aim_y, step_counter, carried, skill, target, building,
          roster_index, roster_weapon, rank, recruit, state_timer, hit_timer, hit_from_behind, shot_by_roster,
          shot_by_mark, vertical_speed, height_fraction, spin, fall_count, shadowed, hits, idle_frame, vehicle_type,
          is_vehicle, rest_x, rest_y, patrol, fire_held, fire_request, barrel, gun_toggle, sink, land, crashing,
          boarding, vehicle, homing_target, thrust, wall_check, flags75);
    }
};

struct Squad {
    std::vector<std::array<int, 2>> route; // waypoints in order ($85C16[n])
    std::vector<int> members;              // slots in order ($85B12[n])
    int mode{2};                           // $85BF8[n]: 0 slow, 1 normal, 2 fast
    int merged_into{-1};                   // $85C3E[n]
    int route_end{};                       // $81DD8[n]: where the next click's waypoint goes
    int weapon{};                          // $821CC[n]: what the leader's both-button fire uses (1 grenade, 3 rocket)
    int grenades{}, rockets{};             // $81F4C[n], $81F52[n]
    bool ammunition_changed{};             // $821C0[n] / $821C6[n]: the panel redraws the counts

    template <class A> void persist(A& a) {
        a(route, members, mode, merged_into, route_end, weapon, grenades, rockets, ammunition_changed);
    }
};

// The three walk-frame counters ($94B54, once per tick before the objects):
// two step every 4 ticks, one every 2, each bouncing 0-1-2-1-0.
struct AnimationClock {
    std::array<int, 3> value{0, 0, 0};    // $81578, $8157C, $81580
    std::array<int, 3> direction{1, 1, 1}; // $85BDC, $85BDE, $85BE0
    int slow_timer{3}, fast_timer{1};      // $8157A, $8157E
    template <class A> void persist(A& a) { a(value, direction, slow_timer, fast_timer); }
    void tick();
};

// A bullet's own record beyond GameObject and Walker (the heading and the
// speed, which it shares with walkers, and its aim point stay there).
struct BulletState {
    int owner{-1};       // [$1A] slot of the shooter
    int muzzle_ticks{};  // [$2A]
    int offset_x{}, offset_y{}; // [$14], [$16] from the shooter while the muzzle shows
    int lifetime{};      // [$12]
    int age{};           // [$64] byte
    int flown{};         // [$3A]
    int cruise_speed{};  // [$4A]
    int spread{};        // [$50]
    bool spread_done{};  // [$59]
    int first_heading{-1}; // [$34]
    bool past_target{};  // [$44]
    int ending{};        // [$43] byte: 0 flying, 1 hit, $FF spent
    int owner_roster{};  // [$5E]
    int owner_mark{};    // [$5D] byte

    template <class A> void persist(A& a) {
        a(owner, muzzle_ticks, offset_x, offset_y, lifetime, age, flown, cruise_speed, spread, spread_done,
          first_heading, past_target, ending, owner_roster, owner_mark);
    }
};

// What a shadow (kind $16) follows: its body, as long as that slot still
// holds an object of the same kind.
struct Attachment {
    int owner{-1};       // [$1A]/[$6A]
    int owner_kind{};    // [$62]
    template <class A> void persist(A& a) { a(owner, owner_kind); }
};

// One row of the program's weapon table ($9E826), chosen by rank.
struct WeaponStats {
    int bullet_speed{};  // $82146
    int bullet_lifetime{}; // $82148
    int interval{};      // $8214A: frames between the squad's shots
    int spread_mask{};   // $8214C: the leader's spread
    template <class A> void persist(A& a) { a(bullet_speed, bullet_lifetime, interval, spread_mask); }
};

// The selected squad's fire control while the right button is held.
struct SquadFire {
    WeaponStats stats;
    int hold_timer{-1};  // $81DD6: ticks; when run out, the next press resets the leader
    int countdown{};     // $82140: frames to the next member's turn
    int order_index{};   // $82142: position in the firing order
    int leader_shots{};  // $82738: the leader aims every 4th shot exactly
    int lifetime_bonus{}, speed_bonus{}; // $82150, $82152: a lone veteran's bullets
    template <class A> void persist(A& a) {
        a(stats, hold_timer, countdown, order_index, leader_shots, lifetime_bonus, speed_bonus);
    }
};

// A sound the rules ask for ($A3A30: an effect pattern of the landscape's
// song and a priority); the sound board plays it (sound/board.hpp).
struct SoundRequest {
    int effect{}, priority{}, slot{};
    template <class A> void persist(A& a) { a(effect, priority, slot); }
};

struct World {
    MissionObjects objects;
    std::array<Walker, object_slots> walkers{};
    std::array<Squad, 3> squads{};
    int selected_squad{};
    int leader_slot{-1};          // $81554: the selected squad's leader
    int driven_vehicle{-1};       // $82798: the vehicle the selected squad sits in
    AnimationClock clock;
    Camera camera;
    int pointer_x{}, pointer_y{}; // game pointer coordinates ($8CABA/$8CABC)
    bool firing{};                // $81DEC: the squad has been told to fire
    bool both_buttons{};          // $8147E
    int frame_counter{};          // $80618 (vertical blanks)
    int tick_counter{};           // $8061A (mission ticks)
    bool right_held{};            // $81478: the right button is down
    bool left_held{};             // $81476: the left button is down
    bool vehicle_click_latched{}; // $81482: a click on the own vehicle was taken (until the button is let go)
    std::array<int, 8> roster_slots{-1, -1, -1, -1, -1, -1, -1, -1}; // $80664: each recruit's soldier slot
    std::array<BulletState, object_slots> bullets{};
    std::array<Attachment, object_slots> attachments{};
    int bullets_alive{};          // $81DF2
    int hit_flag{};               // $81F4A: the last hit test against objects: 1 solid, -1 hurt, 0 none
    SquadFire fire;
    // The original keeps these in globals that carry over from one
    // soldier's update to the next; a leftover changes the next soldier's
    // frame, so they are kept here rather than per update.
    int phase{};                  // $80626: the mission phase (0 = map 1)
    int casualties{};             // $80D1C
    int enemies_killed{};         // $80D1E
    std::array<int, 8> kills{};   // roster entry +$A: each recruit's kills
    // The squads' recruits as the original lists them each tick ($9DC18,
    // $85B26: 3 lists of up to 8 slots and a -1 end, read as one array of
    // 30 by the enemies; -1 elsewhere).
    std::array<int, 30> recruit_lists{};
    std::vector<std::array<int, 2>> fallen; // $80CE8 / $80A0E: each dead recruit's name and rank
    // Explosives
    std::array<int, 2> grenades_in_air{}, rockets_in_air{}; // $81DF0, $81DF4 by side
    std::array<std::array<int, 2>, 128> blast_points{}; // $82498: ring of points for map damage
    int blast_write{}, blast_read{};   // $826A8, $826AC
    int blast_count{};                 // (entries waiting; the original ends the ring with -1)
    bool map_damage_drawing{};         // $82494: an on-screen tile change is being drawn
    int landscape{};                   // $8278C: 0-4, which tiles only big blasts change
    Camera snapshot;                   // $81506/$8150A: the camera as last drawn
    int drawn_objects{};               // $82736: objects in the last draw list (effects stop at 20)
    bool jitter_next_effect{};         // $827DA: the next smoke or splash is placed at random
    std::array<int, 3> route_window{}; // $81DDE[n]: ticks in which a click adds to the route (the marker hides)
    std::array<int, 2> last_click{};   // $826EE/$826F0: the last walk order's target (x 0: none)
    int jolt{};                        // $817C0 = 9 after an explosion on screen (shake: $817C6-$817CE, open)
    // The mission
    enum class Outcome { playing, won, lost };
    Outcome outcome{Outcome::playing}; // $81DCC won, $81DCA lost
    bool failed_objective{};           // $81DD2: lost by an objective (a civilian or hostage killed, …)
    int end_count{};                   // $81DCE: ticks of the end left (0 not started, -1 run out)
    std::array<bool, 8> objectives{};  // $82A08…$82A16: the phase's objectives 1-8 (program $A4D8E)
    bool civilian_home{};              // $82A1C (objective 8)
    int civilian_house{-1};            // $82A18: the civilians' house ($5A)
    int rescue_tent{-1};               // $82B96: the hostages' rescue tent ($49)
    bool hostage_held{};               // $8287A: a hostage is held (guarded or following)
    int markers{}, markers_taken{};    // $82A28, $82A2A: the last mission's places (kind $63) and those taken this tick
    int last_marker_recruit{-1};       // $82A70
    int time_left{100};                // $82A76: the last phase's count on the panel
    int time_step{};                   // $82A74: ticks to the next count (40)
    int doom_count{};                  // $82A78
    bool panel_dirty{};                // $821B8 = 0: the panel's names and ranks are drawn again
    int buildings_left{};              // $81DD0
    int skill_min{}, skill_max{}, skill_next{}, skill_step{1}; // $8062A, $8062C, $8062E, $80630
    int spawns{};                      // $80650: every 16th raises skill_max (to 30)
    int saved_frame{};            // $817B8
    bool keep_frame{};            // $81DE4
    int target_distance{};        // $81FD8: the last enemy's distance to its target
    bool civilians_angry{};       // $82860: a civilian was killed
    bool other_squad{};           // $81FCE: the update is not the selected squad's (enemies: no target)
    bool walked{};                // $8213E: the enemy stepped this tick
    int last_mode{2};             // $81F64: the last soldier's squad mode, which enemies read too
    // Vehicles
    int skill_average{};          // $80628: (skill_min + skill_max) / 2 at the phase's start
    std::array<int, 2> launch_offset{}; // $82716/$82718: where a missile starts from the launcher
    bool homing_ready{};          // $82A80: the turret has a target for a homing missile
    int homing_target{-1};        // (a5) the turret's target
    int target_height{};          // $82A90
    bool enemy_helicopter{};      // $82702: the helicopter routine runs for the enemy's
    int rescue_x{-1}, rescue_y{}; // $82A6C/$82A6E: where the last mission's helicopters fly to (-1 none yet)
    int target_distance2{};       // $82704: an enemy vehicle's distance to its recruit
    std::array<int, 3> boarding_count{}; // $82A02: ticks until the squad is put in its vehicle
    // The panel's split: recruits picked by roster place (+9 bit 0) and how
    // the grenades and rockets are shared ($85B0A, $85B0C: 0 all go, 1
    // half, 2 none).
    std::array<bool, 8> marked{};
    int grenade_split{1}, rocket_split{1};
    struct Engine {
        bool locked{};            // $82A00: the driven vehicle's engine has the channel
        int volume{}, effect{}, param{}; // $829EE, $829F0, $829F2
        template <class A> void persist(A& a) { a(locked, volume, effect, param); }
    } engine;                     // the loudest engine this tick ($A3B60)
    std::vector<SoundRequest> sounds; // this tick's, in order
    std::mt19937 random{0x43464f44};

    // Every field, for the quick saves (game/module.cpp).
    template <class A> void persist(A& a) {
        a(objects, walkers, squads, selected_squad, leader_slot, driven_vehicle, clock, camera, pointer_x, pointer_y,
          firing, both_buttons, frame_counter, tick_counter, right_held, left_held, vehicle_click_latched, roster_slots,
          bullets, attachments, bullets_alive, hit_flag, fire, phase, casualties, enemies_killed, kills, recruit_lists,
          fallen, grenades_in_air, rockets_in_air, blast_points, blast_write, blast_read, blast_count,
          map_damage_drawing, landscape, snapshot, drawn_objects, jitter_next_effect, route_window, last_click, jolt);
        a(outcome, failed_objective, end_count, objectives, civilian_home, civilian_house, rescue_tent, hostage_held,
          markers, markers_taken, last_marker_recruit, time_left, time_step, doom_count, panel_dirty, buildings_left,
          skill_min, skill_max, skill_next, skill_step, spawns, saved_frame, keep_frame, target_distance,
          civilians_angry, other_squad, walked, last_mode);
        a(skill_average, launch_offset, homing_ready, homing_target, target_height, enemy_helicopter, rescue_x,
          rescue_y, target_distance2, boarding_count, marked, grenade_split, rocket_split, engine, sounds, random);
    }
};

// $81DD2: a phase objective has failed (a civilian or hostage killed, …).
inline void fail_objective(World& world) {
    world.outcome = World::Outcome::lost;
    world.failed_objective = true;
}

inline void request_sound(World& world, int effect, int priority, int slot) {
    world.sounds.push_back({effect, priority, slot});
}

} // namespace fodder::game
