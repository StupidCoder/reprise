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
    int facing_timer{};   // [$3E]
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
    int fall_heading{};   // [$3E] while falling: the direction the body lies in
    int fall_count{};     // [$2A] ticks of the fall; writhing: ticks to the next sound
    bool shadowed{};      // [$58] the falling body has its shadow (or had)
};

struct Squad {
    std::vector<std::array<int, 2>> route; // waypoints in order ($85C16[n])
    std::vector<int> members;              // slots in order ($85B12[n])
    int mode{2};                           // $85BF8[n]: 0 slow, 1 normal, 2 fast
    int merged_into{-1};                   // $85C3E[n]
    int weapon{};                          // $821CC[n]: what the leader's both-button fire uses
};

// The three walk-frame counters ($94B54, once per tick before the objects):
// two step every 4 ticks, one every 2, each bouncing 0-1-2-1-0.
struct AnimationClock {
    std::array<int, 3> value{0, 0, 0};    // $81578, $8157C, $81580
    std::array<int, 3> direction{1, 1, 1}; // $85BDC, $85BDE, $85BE0
    int slow_timer{3}, fast_timer{1};      // $8157A, $8157E
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
};

// What a shadow (kind $16) follows: its body, as long as that slot still
// holds an object of the same kind.
struct Attachment {
    int owner{-1};       // [$1A]/[$6A]
    int owner_kind{};    // [$62]
};

// One row of the program's weapon table ($9E826), chosen by rank.
struct WeaponStats {
    int bullet_speed{};  // $82146
    int bullet_lifetime{}; // $82148
    int interval{};      // $8214A: frames between the squad's shots
    int spread_mask{};   // $8214C: the leader's spread
};

// The selected squad's fire control while the right button is held.
struct SquadFire {
    WeaponStats stats;
    int hold_timer{-1};  // $81DD6: ticks; when run out, the next press resets the leader
    int countdown{};     // $82140: frames to the next member's turn
    int order_index{};   // $82142: position in the firing order
    int leader_shots{};  // $82738: the leader aims every 4th shot exactly
    int lifetime_bonus{}, speed_bonus{}; // $82150, $82152: a lone veteran's bullets
};

struct World {
    MissionObjects objects;
    std::array<Walker, object_slots> walkers{};
    std::array<Squad, 3> squads{};
    int selected_squad{};
    int leader_slot{-1};          // $81554: the selected squad's leader
    AnimationClock clock;
    Camera camera;
    int pointer_x{}, pointer_y{}; // game pointer coordinates ($8CABA/$8CABC)
    bool firing{};                // $81DEC: the squad has been told to fire
    bool both_buttons{};          // $8147E
    int frame_counter{};          // $80618 (vertical blanks)
    std::array<int, 8> roster_slots{-1, -1, -1, -1, -1, -1, -1, -1}; // $80664: each recruit's soldier slot
    std::array<BulletState, object_slots> bullets{};
    std::array<Attachment, object_slots> attachments{};
    int bullets_alive{};          // $81DF2
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
    int saved_frame{};            // $817B8
    bool keep_frame{};            // $81DE4
    bool other_squad{};           // $81FCE: the update is not the selected squad's (enemies: no target)
    bool walked{};                // $8213E: the enemy stepped this tick
    int last_mode{2};             // $81F64: the last soldier's squad mode, which enemies read too
    std::mt19937 random{0x43464f44};
};

} // namespace fodder::game
