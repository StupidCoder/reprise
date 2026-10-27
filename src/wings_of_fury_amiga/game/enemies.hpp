#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The enemy planes (`wings`' compiled-C module $10D18C..$10E8B8, run each
// tick by $10E7D6): up to four, sent by the timer in the player's record
// ($10BC02) $1800 behind or ahead of the plane. A lone fighter (mode 1)
// closes in and attacks (mode 2: its guns hit the player); a formation's
// planes (mode 4) are torpedo bombers that go for the carrier, drop their
// torpedo low near it and leave (mode $10). Shot down by the player's guns
// ($10B682) they fall smoking (state 4); in the water they sink, on an
// island they burn out (state $10) and leave a wreck.
namespace wings::game {

struct EnemyPlane {
    int state{};         // +0: 0 none, 2 flying, 4 falling, $10 burning on an island
    int mode{};          // +2: 1 closing in, 2 attacking, 4 torpedo run, $10 leaving; | 8 turning round
    int side{};          // +4: 1 behind the player, 3 ahead (both the same way), 2 / 4 coming the other way
    int hit{};           // +6: the player's guns hit it this tick
    int oil{};           // +8: $F0; smokes below $80, down below $60
    int hit_timer{};     // +$A: hits to the next damage
    int order{};         // +$C: its place among the planes behind the player (1 nearest)
    int attack_timer{};  // +$10
    bool firing{};       // +$12: its guns firing
    int facing{};        // +$14
    int turn{};          // +$16: 0, or the step of a turn round (0..$19, facing flips at $E)
    int turn_wait{};     // +$18: ticks to the next turn round
    int loops{};         // +$1A: turns round made while chasing
    int speed{};         // +$1C (x 100 per tick); burning: frames to the next smoke step
    int target_speed{};  // +$1E; burning: the smoke's size
    int x{};             // +$20
    int fall_speed{};    // +$22: falling (never reset: the slot keeps it)
    int target_altitude{}; // +$24
    int altitude{};      // +$26
    int distance{};      // +$28: from the player
    int flash{};         // +$2C: the gun flash's frame counter (drawing)
    int picture{};       // +$30: japplane.shp table entry (+$1C facing right)
    int turn_delay{};    // +$32: a turn steps every third call
};

struct Enemies {
    std::array<EnemyPlane, 4> planes{}; // -$5DD4
    int fighters_out{};        // -$5E28: lone fighters in the air
    std::vector<int> wrecks;   // -$5E24 (-$5E26 counts): burnt-out planes on the islands (x, sign = facing)
    int attacker_step{};       // -$3198: how many attackers ahead were spaced out this tick
    int skid_counter{};        // -$4368: a falling plane skidding on the water loses height every second tick
    bool invulnerable{};       // -$408C: a debugging key's switch (enemy guns do not hurt)
};

// `wings` data the enemy planes read.
struct EnemyTables {
    int min_speed{}, max_speed{};        // $115EA6, $115EA8
    std::array<int, 6> climb_step{};     // $11614C: altitude step by distance to the target / 20
    std::array<std::string, 56> names{}; // $115EAC, $115F1C: japplane.shp by picture (left, then right)
    std::array<std::string, 56> eighth_names{}; // $115F8C, $115FFC: 8thscale.shp
    std::array<std::string, 4> flash_names{};   // $114520 + $40: fc10, fd10, fc1a, fd1a
};
EnemyTables read_enemy_tables(const amiga::LoadedImage& wings);

// What a frame draws of them ($100DA6): wrecks, planes and gun flashes.
struct EnemySprite {
    const data::Shape* shape{};
    int x{}, y{};       // screen place of the shape's origin
    bool xor_drawn{};   // the gun flash
};
struct EnemyView {
    int camera{};       // -$60CE
    int ground_line{};  // -$60CC
    bool eighth_scale{};
};
// Steps the gun flashes' counters as the original's drawing does.
std::vector<EnemySprite> enemy_sprites(Enemies&, const EnemyTables&, const data::Assets&, const EnemyView&);
void draw_enemies(IndexedCanvas&, const std::vector<EnemySprite>&);

} // namespace wings::game
