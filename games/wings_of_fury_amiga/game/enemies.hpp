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

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(state, mode, side, hit, oil, hit_timer, order, attack_timer, firing, facing, turn, turn_wait,
           loops, speed, target_speed, x, fall_speed, target_altitude, altitude, distance, flash, picture,
           turn_delay);
    }
};

// An enemy airfield (list -$5D04, $14 bytes; `cama` columns between a $114
// and a $115 marker column, $102C84): parked fighters take off one at a
// time while the player is near.
struct Airfield {
    int left{}, right{};   // +0, +2: the markers' x (the first and the second on the map)
    int limit{};           // +4: no take-off while this many fighters are up
    int parked{};          // +6
    int takeoff_x{};       // +8: the plane taking off (0: none)
    int speed{};           // +$C: its speed x 8
    int direction{};       // +$E: the second marker's: $114 -1 (to the left), $115 +1
    int shown_parked{}, shown_takeoff{}; // +$10, +$12: the frame start's copies ($101008)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(left, right, limit, parked, takeoff_x, speed, direction, shown_parked, shown_takeoff);
    }
};

struct Enemies {
    std::array<EnemyPlane, 4> planes{}; // -$5DD4
    std::array<Airfield, 4> airfields{};
    int launch_cooldown{};     // -$3CB6: ticks before another take-off
    int deck_roll{};           // -$3CB4: the Japanese carrier's rolling plane's speed x 16
    int fighters_out{};        // -$5E28: lone fighters in the air
    std::vector<int> wrecks;   // -$5E24 (-$5E26 counts): burnt-out planes on the islands (x, sign = facing)
    int attacker_step{};       // -$3198: how many attackers ahead were spaced out this tick
    int skid_counter{};        // -$4368: a falling plane skidding on the water loses height every second tick
    bool invulnerable{};       // -$408C: the debug mode key $64 toggles it ($10D184): fighters ($10DE36) and flak ($104F96) do not hurt

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(planes, airfields, launch_cooldown, deck_roll, fighters_out, wrecks, attacker_step, skid_counter,
           invulnerable);
    }
};

// `wings` data the enemy planes read.
struct EnemyTables {
    int min_speed{}, max_speed{};        // $115EA6, $115EA8
    std::array<int, 6> climb_step{};     // $11614C: altitude step by distance to the target / 20
    std::array<std::string, 56> names{}; // $115EAC, $115F1C: japplane.shp by picture (left, then right)
    std::array<std::string, 56> eighth_names{}; // $115F8C, $115FFC: 8thscale.shp
    std::array<std::string, 4> flash_names{};   // $114520 + $40: fc10, fd10, fc1a, fd1a
    std::array<std::string, 2> parked_names{};  // $114520 + $50: jp21 (taking off to the left), jp22
    std::vector<int> airfield_bytes;            // -$7B0A: per bonus row 4 bytes (parked, limit for two airfields)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(min_speed, max_speed, climb_step, names, eighth_names, flash_names, parked_names, airfield_bytes);
    }
};
EnemyTables read_enemy_tables(const amiga::LoadedImage& wings);

// What a frame draws of them ($100DA6): wrecks, planes and gun flashes.
struct EnemySprite {
    const data::Shape* shape{};
    int x{}, y{};       // screen place of the shape's origin
    bool xor_drawn{};   // the gun flash

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(x, y, xor_drawn); }
};
struct EnemyView {
    int camera{};       // -$60CE
    int ground_line{};  // -$60CC
    bool eighth_scale{};
    int margin{};       // widescreen columns on each side (IndexedCanvas)

    // Whether a picture at screen x is drawn: -$80..$1C0, and as far beyond
    // a widescreen view's sides. `original` keeps to the original's view.
    bool shown(int x, bool original = false) const {
        const int m = original ? 0 : margin;
        return x >= -0x80 - m && x <= 0x1C0 + m;
    }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(camera, ground_line, eighth_scale); }
};
// $102C84: the map's airfields (the $114/$115 column pairs) with the
// parked planes and the limit for this rank and mission (`row`: the bonus
// table's row).
void find_airfields(Enemies&, const data::MissionMap&, const EnemyTables&, int row);
// $103A18 (after the islands' guns): the parked planes and the one taking off.
std::vector<EnemySprite> airfield_sprites(const Enemies&, const EnemyTables&, const data::Assets&, const EnemyView&);
// $100DA6 steps the firing planes' gun flashes as it draws them (in the
// original's view): a rule of the game, run once per tick before the
// pictures are taken.
void step_gun_flashes(Enemies&, const EnemyTables&, const data::Assets&, const EnemyView&);
// The wrecks, the enemy planes and their gun flashes.
std::vector<EnemySprite> enemy_sprites(const Enemies&, const EnemyTables&, const data::Assets&, const EnemyView&);
void draw_enemies(IndexedCanvas&, const std::vector<EnemySprite>&);

} // namespace wings::game
