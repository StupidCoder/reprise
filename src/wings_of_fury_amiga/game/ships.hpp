#pragma once
#include "amiga/hunk.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include <array>
#include <string>
#include <vector>

// The enemy ships: a destroyer, a battleship, a cruise ship and a carrier,
// each placed by its marker column ($102DE8: the first of each kind on the
// map). A ship takes some torpedo hits (then sinks, `$101CAE`: its value
// and a message at step 10); its anti-aircraft guns (`$104C3E`) fire like
// the islands' and rockets knock them out; planes parked on it take off as
// lone fighters while the player is near (`$101510`).
namespace wings::game {

struct ShipKind {
    int marker{};          // the column's scenery index
    int slot{};            // in FlightWorld::ships ($105710 order)
    int deck{}, value{}, hits{};
    int sink_timer{};      // +$16 as the scan sets it (-1: left as it was)
    int first_from{}, span{}; // first = marker offset + first_from, last = first + span
    int overlay{};         // +$1C: the library's first scenery index ($103252)
    std::vector<int> gun_x;      // $10350E: the guns' x from the ship's
    std::vector<int> gun_lift;   // (the cruise ship's second and third: 5)
    std::vector<int> plane_counts; // by bonus row: (parked, limit) pairs ($10252C)
    std::vector<int> plane_template; // low, high, then (flag, x, altitude, facing) per plane

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(marker, slot, deck, value, hits, sink_timer, first_from, span, overlay, gun_x, gun_lift,
           plane_counts, plane_template);
    }
};

struct ShipTables {
    std::array<ShipKind, 4> kinds{};
    std::array<std::string, 4> names{}; // -$7456: by value $1194, $1770, $9C4, other
    std::string sunk;                    // -$7526: ". . . %s sunk . . . BONUS %ld points . . ."

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(kinds, names, sunk); }
};
ShipTables read_ship_tables(const amiga::LoadedImage& wings);

} // namespace wings::game
