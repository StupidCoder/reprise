#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include <array>
#include <string>
#include <vector>

// The cockpit dashboard below the play view (640 x 37, hires, 4 bitplanes):
// the `iff-dash` picture with dash.shp parts drawn over it. Left panel: the
// weapon carried and how many (rolling digits), the spare planes (rolling);
// the oil and fuel gauges with their warning lights; in the middle the
// forward view (what lies ahead, $10417E); right panel: the score and the
// enemy planes shot down.
//
// The original redraws a part only when its value changed (`$10EE16`, a
// cache per display buffer); the needles, the warning lights and the
// rolling digits step once per drawn frame. Here the whole dashboard is
// drawn from DashboardGauges each frame.
namespace wings::game {

// `wings` tables the forward view reads.
struct DashboardTables {
    // -$6E36: the dash.shp names the game's pointer table holds, in its
    // order (`$106556` looks each up by name).
    std::vector<std::string> part_names;
    std::array<int, 12> band_distance{};   // -$6948 backwards: columns ahead per band edge
    std::array<int, 11> band_frame{};      // -$6946: the scaled frame step per band (scenery)
    std::array<int, 11> ship_frame{};      // -$693A: the same for ships
    std::vector<int> deck_band;            // -$692F: by columns to the deck's far end
    std::array<int, 16> deck_y{};          // -$68B9
    std::array<int, 16> deck_clip{};       // -$68C9
    std::array<int, 16> deck_frame{};      // -$68A9
    std::array<int, 12> digit_y{};         // -$6402: the digit strip's offset per digit
    // The enemy planes as the forward view shows them ($10D1EA builds the
    // table -$3BA2 from `wings` $11606C / $1160DC): three sizes of 56
    // pictures (left, then right), the third letter of each name the size.
    std::vector<std::string> enemy_names;
    std::array<int, 11> enemy_band{};      // -$696C: added to the picture by band (the farthest last)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(part_names, band_distance, band_frame, ship_frame, deck_band, deck_y, deck_clip, deck_frame,
           digit_y, enemy_names, enemy_band);
    }
};
DashboardTables read_dashboard_tables(const amiga::LoadedImage& wings);

// What the dashboard shows from the game.
struct DashboardInput {
    int mode{};          // the player record's +$C
    int oil{}, fuel{};   // +$12, +$E
    bool engine{};       // $117D3C: the engine running
    int weapon{};        // -$5C5A: 0 rockets, 1 bombs, 2 torpedo
    int load{};          // -$5C91 ($FF: none)
    int spare_planes{};  // -$5CA2
    long score{};        // -$5CB2
    int kills{};         // -$5C7F
    // The forward view.
    int camera{};        // -$41A2
    int altitude{};      // -$419E
    int facing{};        // -$5F72
    bool eighth_scale{}; // -$60C8 = 1
    const data::MissionMap* map{};
    bool night{};        // -$5C6E: `nightdash.shp` and the `nightdash` picture ($10653C)
    // The enemy planes up (-$5DD4): the map byte offset of their column
    // (+$2E, from the frame start), their picture (+$30) and height.
    struct Enemy {
        int column{}, picture{}, altitude{};
        int x{}, mode{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(column, picture, altitude, x, mode); }
    };
    std::vector<Enemy> enemies;
    // The ships the forward view can see on the map ($104A52): enemy slots
    // first (only active ones), the US carrier last.
    struct Ship {
        int first{}, last{};
        int overlay{};   // +$1C
        int state{};     // +$12

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(first, last, overlay, state); }
    };
    std::vector<Ship> ships;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(mode, oil, fuel, engine, weapon, load, spare_planes, score, kills, camera, altitude, facing,
           eighth_scale, night, enemies, ships);
    }
};

// The parts that move a step per drawn frame.
struct DashboardGauges {
    int oil_needle{0x3C};      // -$30A6 (start: $10EDAA)
    int fuel_needle{0x54};     // -$30A4
    int oil_blink{};           // -$30A0
    int fuel_blink{};          // -$309E
    bool oil_lit{}, fuel_lit{};
    int load_tens{}, load_units{}; // -$30D4, -$30D2: the digit strips' positions
    int load_settled{-1};      // the load the strips last came to rest on
    int spares_roll{};         // -$30D0
    int spares_settled{-1};

    // $10EDBC: the load digits jump to their places; $10EDEA: the spares.
    void set_load(int load);
    void set_spares(int spares);
    // `$10EE16`'s steps for one drawn frame; `random` stands for the beam
    // position the empty fuel needle flickers by.
    void step(const DashboardInput&, int random);

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(oil_needle, fuel_needle, oil_blink, fuel_blink, oil_lit, fuel_lit, load_tens, load_units,
           load_settled, spares_roll, spares_settled);
    }
};

IndexedCanvas draw_dashboard(const data::Assets&, const DashboardTables&, const DashboardGauges&,
                             const DashboardInput&);

} // namespace wings::game
