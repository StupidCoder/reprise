#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include <array>
#include <cstdint>
#include <random>

// The short-lived pictures a crash leaves, and the carrier's deck crew:
//
// - the object table (-$6350, 15 slots): the player's rockets, bombs and
//   torpedoes in flight (moved by the tick, flight.cpp) and blasts. A blast
//   plays world.shp `exp0`..`exp5` (fire turning into smoke) on land and
//   ships, `spl0`..`spl5` on water, one frame per drawn frame ($1006BE); a
//   weapon becomes a blast where it hits.
// - splashes: the list -$40CE (20 entries) of water splashes (`spl5`..`spl1`)
//   and, over an island, dust (`ric0`) ($1052AC, drawn by $1052F8).
// - smoke: the puff list -$40A6 (40 entries; $105460, drawn by $100EE0),
//   `smk5` shrinking to `smk0` as it drifts up and to the right.
// - the deck crew (`fgy*`), whose two figures swing their flags between the
//   poses the deck officer's signal asks for ($103BCE).
//
// The three effects are drawn after the whole view, on top of everything:
// smoke, then splashes, then blasts.
namespace wings::game {

// The game's random numbers come from two sources: the C library's rand()
// ($111E24, 15 bits; only the wreck's blasts use it) and the video beam
// position ($1103BE, 16 bits of hardware noise, everything else). The
// rebuild draws both from a standard generator; only how each value is used
// (its range and mask) follows the original.
struct CRandom {
    std::mt19937 engine{0x57494E47u};
    int next() { return int(engine() & 0x7FFF); }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(engine); }
};
struct BeamRandom {
    std::mt19937 engine{0x46555259u};
    int next() { return int(engine() & 0xFFFF); }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(engine); }
};

// A slot of the object table. Positions and speeds are 16.16 fixed point;
// the game rewrites only the whole part of some of them, so a slot keeps its
// fractions from one use to the next.
struct FlyingObject {
    enum class Kind : std::uint8_t { none, blast, rocket, bomb, torpedo };
    Kind kind{};                          // +$20 (8: blast) and +$22 (0 rocket, 1 bomb, 2 torpedo)
    // What the drawing sees: the kind and the place at the frame start
    // ($100FBA copies +$20, +0 and +4 into +$C, +8 and +$A).
    Kind listed{};
    int shown_x{}, shown_y{};
    std::int32_t x{}, y{};                // +0, +4: y is the height above the ground line - $B
    std::int32_t dx{}, dy{};              // +$E, +$12
    std::int32_t thrust_x{}, thrust_y{};  // +$16, +$1A: a rocket's acceleration
    int picture{};    // +$1E: weapon picture step (bomb tumbling 0..11, rocket angle); $A: torpedo running
    int surface{};    // +$1F: blasts: what was hit (0 water: splash frames); torpedo: its facing byte
    int frame{};      // +$21: a blast's frame: 1 when started, 2..7 drawn, 8 ends it
    int ignition{};   // +$24: ticks before a rocket's motor lights
    int angle{};      // +$26: a rocket's launch angle (-$5BFA / 2)
    int speed{};      // +$28: the plane's speed at launch
    int weapon{1};    // +$22: 0 rocket, 1 bomb (and blasts the spawner starts), 2 torpedo
    int run{};        // +$12 once running: a torpedo's ticks left
    int wake_frame{}; // +$1A once in the water: the draw counter at the last wake splash

    bool active() const { return kind != Kind::none; }
    int pixel_x() const { return std::int16_t(x >> 16); }
    int pixel_y() const { return std::int16_t(y >> 16); }
    void set_pixel_x(int v) { x = std::int32_t(std::uint32_t(std::uint16_t(v)) << 16 | std::uint16_t(x)); }
    void set_pixel_y(int v) { y = std::int32_t(std::uint32_t(std::uint16_t(v)) << 16 | std::uint16_t(y)); }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(kind, listed, shown_x, shown_y, x, y, dx, dy, thrust_x, thrust_y, picture, surface, frame,
           ignition, angle, speed, weapon, run, wake_frame);
    }
};

struct Splash {
    int x{};          // +0: world x
    int left{};       // +2: frames left (6 water, 4 dust); drawn as it counts down
    bool dust{};      // +3 = 2: over an island
    int shown{};      // the frame drawn this frame (0: none)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(x, left, dust, shown); }
};

struct Puff {
    std::int32_t x{}, y{};    // +0, +4: 16.16 world position (y up)
    std::int32_t dx{}, dy{};  // +8, +$C: 1.0 .. 2.0 per frame
    int size{};               // +$10: 6 .. 1 (`smk5`..`smk0`), 0 free
    int count{};              // +$12: frames until it shrinks (7 per size)
    int shown_x{}, shown_y{}, shown_size{}; // where it was drawn this frame

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(x, y, dx, dy, size, count, shown_x, shown_y, shown_size); }
};

struct Effects {
    std::array<FlyingObject, 15> objects{};  // -$6350
    // -$5A6A: an enemy torpedo bomber's torpedo, outside the table but
    // flown, set off and drawn by the same code.
    FlyingObject enemy_torpedo;
    // Every object the flying code steps: the table, then the enemy torpedo.
    template <typename F> void each_object(F&& f) {
        for (auto& o : objects) f(o);
        f(enemy_torpedo);
    }
    template <typename F> void each_object(F&& f) const {
        for (const auto& o : objects) f(o);
        f(enemy_torpedo);
    }
    std::array<Splash, 20> splashes{};
    std::array<Puff, 40> puffs{};
    // The victory ceremony's fireworks (*-$4098, 20 x $12 bytes): after a
    // promotion, sparks rise from beside the carrier.
    struct Spark {
        std::int32_t x{}, y{};   // +0, +4: 16.16 world place (y up from the ground line)
        std::int32_t dx{}, dy{}; // +8, +$C: 1.0 .. 3.0 a tick, up and to the right
        int kind{};              // +$10: 0..2 (scenery entries 12..14)
        bool active{};           // +$11

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, dx, dy, kind, active); }
    };
    std::array<Spark, 20> sparks{};

    // $100820: a blast in the column at `column`, `height` above the ground
    // (sinking wrecks: 0); ignored when every slot is busy. (A blast on land
    // also starts the explosion sample, -$7F92: sound is not rebuilt.)
    bool start_blast(int column, int height, bool water) { return start_blast_at(column * 8, height, water); }
    // The same at a pixel x (the spawner's x is the map byte offset x 4).
    bool start_blast_at(int x, int height, bool water);
    // $1052AC: a splash at world x; `surface` is the map's under x ($1050C8).
    void start_splash(int x, int surface);
    // $101AE2: the player's torpedoes (and the enemy's, any weapon in its
    // slot) listed within `reach` of x go off.
    void set_off_torpedoes(int x, int reach);
    // $10CAE0: a puff of smoke of `size` at world (x, y), nudged 0..7 to the
    // right and 6 up; $105460 gives it a drift of 1..2 right and up.
    void start_smoke(int x, int y, int size, BeamRandom&);
    // $105460: a puff at a 16.16 place (y at least 16).
    void start_puff(std::int32_t x, std::int32_t y, int size, BeamRandom&);
    // $101C5E (each tick while promoted): the sparks rise; one at height
    // $AA or more is done.
    void step_sparks();
    // $10557C (each frame while promoted, in the normal view): a spark that
    // is done starts again $74 left of the carrier at height $38, with a
    // random colour and speed.
    void start_sparks(int carrier_x, BeamRandom&);
    // $100FBA (frame start): what the drawing will see.
    void frame_start();
    // What the drawers change as they draw one frame ($100EE0, $1052F8,
    // $1006BE): puffs drift and shrink, splashes count down, blasts step
    // their frame (ending at 8).
    void advance();
    void clear() { *this = Effects{}; }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(objects, enemy_torpedo, splashes, puffs, sparks); }
};

// The view the effects are drawn into ($105174's inputs).
struct EffectView {
    int camera{};
    int ground_line{};   // -$60CC ($4B8 in the 1/8 view)
    bool eighth_scale{};
    int smoke_clip_bottom{-1}; // $100EE0: at the deck while the elevator moves

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(camera, ground_line, eighth_scale, smoke_clip_bottom); }
};
// $10557C: the fireworks (scenery entries 12..14), over the effects.
void draw_sparks(IndexedCanvas&, const data::Assets&, const Effects&, const EffectView&);
// A scenery-table entry drawn at a world place (the islands' soldiers and guns).
struct WorldSprite {
    int entry{}, x{}, y{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(entry, x, y); }
};
// Smoke, then `between` (the soldiers, $103EEE), then splashes and blasts.
void draw_effects(IndexedCanvas&, const data::Assets&, const Effects&, const EffectView&,
                  const std::vector<WorldSprite>& between = {});
// $105174 for scenery entries (the 1/8-scale table in the 1/8 view).
void draw_world_sprite(IndexedCanvas&, const data::Assets&, const EffectView&, const WorldSprite&);

// The deck crew's two flag figures ($103BCE): pose a (`fgy0`..`fgy7`) and
// pose b (`fgy8`..`fgyf`) each walk a step per drawn frame toward a target,
// which swings between the two ends the signal's table entry gives.
struct CrewTables {
    std::array<std::array<int, 2>, 6> a{}, b{}; // -$59A2, -$598A (by signal)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(a, b); }
};
CrewTables read_crew_tables(const amiga::LoadedImage& wings);

struct DeckCrew {
    int pose_a{}, pose_b{};       // -$59A8, -$59A4
    int target_a{}, target_b{};   // -$59AA, -$59A6
    int phase{};                  // -$5972: the signal the targets were taken for
    // Once per drawn frame in which a crew column is on screen.
    void step(const CrewTables&, int signal);

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(pose_a, pose_b, target_a, target_b, phase); }
};

} // namespace wings::game
